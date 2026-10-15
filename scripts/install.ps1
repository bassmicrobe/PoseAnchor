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
Assert-SteamVrRegistrationWritable
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
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedDriver $required) -PathType Leaf)) {
        throw "The driver package is incomplete: $required"
    }
}
$vrPathReg = Find-SteamVrPathReg
$registration = Get-PoseAnchorDriverRegistration $vrPathReg
if ($registration.ExitCode -eq 0) {
    $existingPath = $registration.Path
    if (-not (Test-SteamVrDriverPathEqual $existingPath $resolvedDriver)) {
        throw "A different PoseAnchor path is already registered: $existingPath. Unregister it first with scripts\uninstall.ps1 -AllWithName."
    }
    Write-Host "PoseAnchor is already registered at: $existingPath"
    return
}
if ($registration.ExitCode -eq 2) {
    throw 'PoseAnchor is registered more than once. Run scripts\uninstall.ps1 -AllWithName first.'
}
if ($PSCmdlet.ShouldProcess($resolvedDriver, 'Register as a SteamVR external driver')) {
    # Confirmation can remain open while the user starts SteamVR.
    Assert-SteamVrRegistrationWritable
    & $vrPathReg adddriver $resolvedDriver
    if ($LASTEXITCODE -ne 0) { throw "vrpathreg adddriver failed: $LASTEXITCODE" }
    Assert-PoseAnchorDriverRegistration -VrPathReg $vrPathReg -DriverPath $resolvedDriver
    Write-Host "Registered PoseAnchor: $resolvedDriver"
}
