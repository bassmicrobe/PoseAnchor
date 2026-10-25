[CmdletBinding()]
param(
    [string]$BuildRoot = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build'),
    [string]$CMake = 'cmake'
)

$ErrorActionPreference = 'Stop'
$BuildRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$cmakeLists = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(PoseAnchor VERSION (\d+\.\d+\.\d+)') {
    throw 'Could not read the expected project version'
}
$expectedVersion = $Matches[1]
$projectRoot = Split-Path -Parent $PSScriptRoot
$packageDocuments = @{
    'README.md' = 'PACKAGE_README.md'
    'MANUAL.md' = 'MANUAL.md'
    'LICENSE' = 'LICENSE'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'PoseAnchor.ico' = 'src\status\PoseAnchorStatus.ico'
}
foreach ($name in @('README.md', 'poseanchor-icon.png', 'poseanchor-icon.svg',
    'poseanchor-icon-cyan.svg', 'poseanchor-logo-horizontal-ink.svg',
    'poseanchor-logo-horizontal-white.svg', 'poseanchor-logo-stacked-ink.svg',
    'poseanchor-logo-stacked-white.svg', 'poseanchor-symbol-ink.svg', 'poseanchor-symbol-white.svg')) {
    $relativePath = 'assets\branding\' + $name
    $packageDocuments[$relativePath] = $relativePath
}

# The folder can also be distributed without the installer's explicit [Files]
# allow-list. Refuse stale debug symbols, test DLLs, logs, and hidden files rather
# than silently redistributing files left by an older build. Do not delete them:
# the developer must inspect and move any unexpected artifacts out of the package.
function Assert-PackageInventory([string]$PackageRoot) {
    $expectedFiles = @(
        'build-info.json',
        'driver.vrdrivermanifest',
        'bin\win64\driver_pose_anchor.dll',
        'PoseAnchorStatus.exe',
        'PoseAnchor.ico',
        'resources\settings\default.vrsettings',
        'README.md',
        'MANUAL.md',
        'LICENSE',
        'THIRD_PARTY_NOTICES.md',
        'licenses\OpenVR-LICENSE.txt',
        'licenses\MinHook-LICENSE.txt',
        'licenses\Inno-Setup-LICENSE.txt',
        'licenses\OpenVR-SpaceCalibrator-LICENSE.txt',
        'scripts\install.ps1',
        'scripts\uninstall.ps1',
        'scripts\steamvr-path.ps1'
    )
    $expectedFiles += @($packageDocuments.Keys | Where-Object { $_.StartsWith('assets\branding\') })
    $expectedDirectories = @('bin', 'bin\win64', 'resources', 'resources\settings', 'licenses', 'scripts', 'assets', 'assets\branding')
    $root = Get-Item -LiteralPath $PackageRoot -Force
    if (-not $root.PSIsContainer -or
        ($root.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'The staged package must be a normal directory, not a link.'
    }
    $rootPrefix = $root.FullName.TrimEnd('\') + '\'
    $actualFiles = @()
    foreach ($entry in Get-ChildItem -LiteralPath $root.FullName -Recurse -Force) {
        $relativePath = $entry.FullName.Substring($rootPrefix.Length)
        if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "The package must not contain linked files or directories: $relativePath"
        }
        if ($entry.PSIsContainer) {
            if ($expectedDirectories -cnotcontains $relativePath) {
                throw "Unexpected package directory: $relativePath. Inspect and move it out before distributing."
            }
        } else {
            if ($expectedFiles -cnotcontains $relativePath) {
                throw "Unexpected package file: $relativePath. Inspect and move it out before distributing."
            }
            $actualFiles += $relativePath
        }
    }
    foreach ($relativePath in $expectedFiles) {
        if ($actualFiles -cnotcontains $relativePath) {
            throw "Missing package file: $relativePath"
        }
    }
}

# An incremental Release build must restore Release bytes after Debug packaging.
# No SteamVR registration or installed files are touched by this test.
foreach ($configuration in @('Release', 'Debug', 'Release')) {
    & $CMake --build $BuildRoot --config $configuration --target pose_anchor_package --parallel
    if ($LASTEXITCODE -ne 0) { throw "Package build failed: $configuration" }

    $packageRoot = Join-Path $BuildRoot 'pose_anchor'
    Assert-PackageInventory $packageRoot
    $configRoot = Join-Path $BuildRoot "$configuration\pose_anchor"
    $info = Get-Content -LiteralPath (Join-Path $packageRoot 'build-info.json') -Raw |
        ConvertFrom-Json
    if ($info.project -cne 'PoseAnchor' -or $info.version -cne $expectedVersion -or
        $info.configuration -cne $configuration) {
        throw "Incorrect package build metadata for $configuration"
    }
    foreach ($relativePath in @('PoseAnchorStatus.exe', 'bin\win64\driver_pose_anchor.dll')) {
        $expected = (Get-FileHash -LiteralPath (Join-Path $configRoot $relativePath)).Hash
        $actual = (Get-FileHash -LiteralPath (Join-Path $packageRoot $relativePath)).Hash
        if ($expected -cne $actual) {
            throw "Wrong configuration staged: $configuration / $relativePath"
        }
    }
    foreach ($relativePath in $packageDocuments.Keys) {
        $expected = (Get-FileHash -LiteralPath (Join-Path $projectRoot $packageDocuments[$relativePath])).Hash
        $actual = (Get-FileHash -LiteralPath (Join-Path $packageRoot $relativePath)).Hash
        if ($expected -cne $actual) {
            throw "Missing or stale package document: $configuration / $relativePath"
        }
    }
}
Write-Host 'Package staging regression passed: Release -> Debug -> Release.'
