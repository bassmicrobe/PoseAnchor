[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$DriverPath,
    [switch]$AllWithName
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
    throw 'Exit SteamVR before unregistering the driver.'
}
$vrPathReg = Find-SteamVrPathReg
if ($AllWithName) {
    if ($PSCmdlet.ShouldProcess('pose_anchor', 'Unregister every SteamVR driver with this name')) {
        & $vrPathReg removedriverswithname pose_anchor
        if ($LASTEXITCODE -ne 0) { throw "vrpathreg removedriverswithname failed: $LASTEXITCODE" }
        Write-Host 'Unregistered every PoseAnchor path.'
    }
    return
}
# vrpathreg removedriver only edits openvrpaths.vrpath; the folder itself does not
# need to exist. A deleted build tree must still be unregisterable, or SteamVR keeps
# loading a stale driver path forever.
$resolvedDriver = if (Test-Path -LiteralPath $DriverPath) {
    (Resolve-Path -LiteralPath $DriverPath).Path
} else {
    Write-Warning "Driver folder not found on disk: $DriverPath. Removing the stale SteamVR registration anyway (use -AllWithName if this path does not match the registered one)."
    $PSCmdlet.GetUnresolvedProviderPathFromPSPath($DriverPath)
}
if ($PSCmdlet.ShouldProcess($resolvedDriver, 'Unregister the SteamVR external driver')) {
    & $vrPathReg removedriver $resolvedDriver
    if ($LASTEXITCODE -ne 0) { throw "vrpathreg removedriver failed: $LASTEXITCODE. If this path is not the registered one, run scripts\uninstall.ps1 -AllWithName to remove every pose_anchor registration." }
    Write-Host "Unregistered PoseAnchor: $resolvedDriver"
}
