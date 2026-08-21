[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$SkipBuild,

    # SHA-1 thumbprint of an Authenticode code-signing certificate in the current
    # user's or machine's certificate store. When set, the driver DLL is signed
    # before packaging and the setup executable after compilation.
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$CertificateThumbprint,

    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

# CMakeLists.txt is the single source of truth for the version: the driver DLL
# stamps its log line from it, so the installer must match it too.
$cmakeLists = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(PoseAnchor VERSION (\d+\.\d+\.\d+)') {
    throw 'Could not read the project version from CMakeLists.txt'
}
$Version = $Matches[1]
$packageRoot = Join-Path $projectRoot 'build\pose_anchor'
$outputRoot = Join-Path $projectRoot 'dist'
$installerScript = Join-Path $projectRoot 'installer\PoseAnchor.iss'

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "PoseAnchor build failed: $LASTEXITCODE" }
}

$requiredFiles = @(
    'driver.vrdrivermanifest',
    'bin\win64\driver_pose_anchor.dll',
    'resources\settings\default.vrsettings',
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'licenses\OpenVR-LICENSE.txt',
    'licenses\MinHook-LICENSE.txt',
    'licenses\Inno-Setup-LICENSE.txt',
    'licenses\OpenVR-SpaceCalibrator-LICENSE.txt'
)
foreach ($relativePath in $requiredFiles) {
    $fullPath = Join-Path $packageRoot $relativePath
    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
        throw "Installer input is missing: $fullPath"
    }
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $kitsRoot) {
        $candidate = Get-ChildItem -Path $kitsRoot -Directory |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }
    throw 'signtool.exe was not found. Install the Windows 10/11 SDK signing tools.'
}

function Invoke-CodeSign([string]$Path) {
    & $script:signTool sign /sha1 $CertificateThumbprint /fd SHA256 `
        /tr $TimestampUrl /td SHA256 $Path
    if ($LASTEXITCODE -ne 0) { throw "signtool failed for ${Path}: $LASTEXITCODE" }
}

if ($CertificateThumbprint) {
    $script:signTool = Find-SignTool
    # Sign the DLL before Inno Setup packages it, so the installed driver binary
    # carries a signature too, not just the setup executable.
    Invoke-CodeSign (Join-Path $packageRoot 'bin\win64\driver_pose_anchor.dll')
}

$isccCandidates = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
    Select-Object -Unique
$iscc = $isccCandidates | Select-Object -First 1
if (-not $iscc) {
    throw 'Inno Setup 6 was not found. Install JRSoftware.InnoSetup, then rerun this build script.'
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$versionQuad = "$Version.0"
& $iscc "/DPackageDir=$packageRoot" "/DOutputDir=$outputRoot" `
    "/DAppVersion=$Version" "/DAppVersionQuad=$versionQuad" $installerScript
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed: $LASTEXITCODE" }

$installer = Join-Path $outputRoot "PoseAnchor-Setup-$Version.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Inno Setup reported success but the installer is missing: $installer"
}

# Sign before hashing so the published checksum matches the signed file.
if ($CertificateThumbprint) {
    Invoke-CodeSign $installer
}

$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = "$installer.sha256"
Set-Content -LiteralPath $checksumFile -Encoding Ascii `
    -Value "$hash  $(Split-Path -Leaf $installer)"

$signature = Get-AuthenticodeSignature -LiteralPath $installer
Write-Host "Installer build succeeded: $installer"
Write-Host "SHA-256: $hash"
Write-Host "Authenticode: $($signature.Status)"
if ($signature.Status -ne 'Valid') {
    Write-Warning 'The installer is unsigned. Rerun with -CertificateThumbprint <sha1> to sign it before public distribution.'
}
