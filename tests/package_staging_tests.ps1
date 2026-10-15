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

# An incremental Release build must restore Release bytes after Debug packaging.
# No SteamVR registration or installed files are touched by this test.
foreach ($configuration in @('Release', 'Debug', 'Release')) {
    & $CMake --build $BuildRoot --config $configuration --target pose_anchor_package --parallel
    if ($LASTEXITCODE -ne 0) { throw "Package build failed: $configuration" }

    $packageRoot = Join-Path $BuildRoot 'pose_anchor'
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
}
Write-Host 'Package staging regression passed: Release -> Debug -> Release.'
