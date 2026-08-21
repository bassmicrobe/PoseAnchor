[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$DriverPath
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'steamvr-path.ps1')

$projectOrPackageRoot = Split-Path -Parent $PSScriptRoot
if (-not $DriverPath) {
    $DriverPath = if (Test-Path -LiteralPath (Join-Path $projectOrPackageRoot 'driver.vrdrivermanifest')) {
        $projectOrPackageRoot
    } else {
        Join-Path $projectOrPackageRoot 'build\pose_anchor'
    }
}
if (Get-Process vrserver -ErrorAction SilentlyContinue) {
    throw 'Exit SteamVR before installing the driver.'
}
$resolvedDriver = (Resolve-Path -LiteralPath $DriverPath).Path
foreach ($required in @(
    'driver.vrdrivermanifest',
    'bin\win64\driver_pose_anchor.dll',
    'resources\settings\default.vrsettings',
    'README.md',
    'LICENSE',
    'THIRD_PARTY_NOTICES.md',
    'licenses\OpenVR-LICENSE.txt',
    'licenses\MinHook-LICENSE.txt',
    'licenses\OpenVR-SpaceCalibrator-LICENSE.txt',
    'scripts\uninstall.ps1'
)) {
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedDriver $required))) {
        throw "The driver package is incomplete: $required"
    }
}
$vrPathReg = Find-SteamVrPathReg
$existingOutput = @(& $vrPathReg finddriver pose_anchor 2>&1)
$findExitCode = $LASTEXITCODE
if ($findExitCode -eq 0) {
    $existingPath = [string]($existingOutput | Where-Object { $_ } | Select-Object -Last 1)
    $existingFullPath = [IO.Path]::GetFullPath($existingPath).TrimEnd('\')
    $requestedFullPath = [IO.Path]::GetFullPath($resolvedDriver).TrimEnd('\')
    if (-not $existingFullPath.Equals($requestedFullPath, [StringComparison]::OrdinalIgnoreCase)) {
        throw "A different PoseAnchor path is already registered: $existingPath. Unregister it first with scripts\uninstall.ps1 -AllWithName."
    }
    Write-Host "PoseAnchor is already registered at: $existingPath"
    return
}
if ($findExitCode -eq 2) {
    throw 'PoseAnchor is registered more than once. Run scripts\uninstall.ps1 -AllWithName first.'
}
if ($findExitCode -ne 1) {
    throw "vrpathreg finddriver failed: $findExitCode"
}
if ($PSCmdlet.ShouldProcess($resolvedDriver, 'Register as a SteamVR external driver')) {
    & $vrPathReg adddriver $resolvedDriver
    if ($LASTEXITCODE -ne 0) { throw "vrpathreg adddriver failed: $LASTEXITCODE" }
    Write-Host "Registered PoseAnchor: $resolvedDriver"
}
