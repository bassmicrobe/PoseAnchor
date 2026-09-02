[CmdletBinding()]
param(
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Configuration = 'Release',

    [switch]$SkipBuild,

    # SHA-1 thumbprint of an Authenticode code-signing certificate in the current
    # user's or machine's certificate store. When set, packaged executable files
    # are signed before Inno Setup signs Setup and its embedded uninstaller.
    [ValidatePattern('^[0-9A-Fa-f]{40}$')]
    [string]$CertificateThumbprint,

    [ValidatePattern('^https?://[^\s"]+$')]
    [string]$TimestampUrl = 'http://timestamp.digicert.com',

    # Defaults to dist. A separate directory is useful for release-candidate
    # validation without replacing a previously published installer.
    [string]$OutputDirectory
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
$outputRoot = if ($OutputDirectory) {
    $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
} else {
    Join-Path $projectRoot 'dist'
}
$installerScript = Join-Path $projectRoot 'installer\PoseAnchor.iss'

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
    if ($LASTEXITCODE -ne 0) { throw "PoseAnchor build failed: $LASTEXITCODE" }
}

$requiredFiles = @(
    'driver.vrdrivermanifest',
    'bin\win64\driver_pose_anchor.dll',
    'PoseAnchorStatus.exe',
    'PoseAnchor.ico',
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
    # Sign every installed PE before Inno Setup packages it. Setup and the
    # embedded uninstaller are signed by Inno itself below.
    foreach ($relativePath in @(
        'bin\win64\driver_pose_anchor.dll',
        'PoseAnchorStatus.exe'
    )) {
        $path = Join-Path $packageRoot $relativePath
        Invoke-CodeSign $path
        $signature = Get-AuthenticodeSignature -LiteralPath $path
        if ($signature.Status -ne 'Valid') {
            throw "Signing completed but the signature is not valid: $path ($($signature.Status))"
        }
    }
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
$isccArguments = @(
    "/DPackageDir=$packageRoot"
    "/DOutputDir=$outputRoot"
    "/DAppVersion=$Version"
    "/DAppVersionQuad=$versionQuad"
)
if ($CertificateThumbprint) {
    # Inno Setup must invoke signtool itself so it can sign both Setup and the
    # embedded uninstaller. $q and $f are Inno SignTool substitutions; the
    # unique tool name prevents included scripts from reusing a generic command.
    $signCommand = '$q{0}$q sign /sha1 {1} /fd SHA256 /tr $q{2}$q /td SHA256 $f' -f `
        $script:signTool, $CertificateThumbprint, $TimestampUrl
    $isccArguments += '/DEnableSigning=1'
    # Inno Setup 6.7.x uses the short /S<name>=<command> form.
    $isccArguments += "/Sposeanchorsign=$signCommand"
}
$isccArguments += $installerScript
& $iscc @isccArguments
if ($LASTEXITCODE -ne 0) { throw "Inno Setup compilation failed: $LASTEXITCODE" }

$installer = Join-Path $outputRoot "PoseAnchor-Setup-$Version.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Inno Setup reported success but the installer is missing: $installer"
}

$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = "$installer.sha256"
Set-Content -LiteralPath $checksumFile -Encoding Ascii `
    -Value "$hash  $(Split-Path -Leaf $installer)"

$signature = Get-AuthenticodeSignature -LiteralPath $installer
Write-Host "Installer build succeeded: $installer"
Write-Host "SHA-256: $hash"
Write-Host "Authenticode: $($signature.Status)"
if ($CertificateThumbprint -and $signature.Status -ne 'Valid') {
    throw "Signing was requested, but the completed installer signature is not valid: $($signature.Status)"
}
if (-not $CertificateThumbprint -and $signature.Status -ne 'Valid') {
    Write-Warning 'The installer is unsigned. Rerun with -CertificateThumbprint <sha1> to sign it before public distribution.'
}
