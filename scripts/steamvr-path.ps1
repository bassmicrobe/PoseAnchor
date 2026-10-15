function Assert-SteamVrRegistrationWritable {
    # The GUI's backup/rollback and diagnostics use the standard registry. Keep
    # developer helpers on that same registry instead of silently editing an
    # environment-selected second copy.
    if ($env:VR_PATHREG_OVERRIDE) {
        throw 'VR_PATHREG_OVERRIDE is set. Remove that override and start a new session before changing the PoseAnchor registration.'
    }
    if (Get-Process vrserver -ErrorAction SilentlyContinue) {
        throw 'Exit SteamVR before changing the driver registration.'
    }
}

function Get-PoseAnchorDriverRegistration([string]$VrPathReg) {
    # Windows PowerShell 5.1 can turn redirected native stderr into a terminating
    # NativeCommandError. Keep stdout separate and restore the caller's policy
    # even if the tool fails to launch. A missing exit code is never success.
    $previousErrorActionPreference = $ErrorActionPreference
    # Native commands update the global automatic variable even when a local
    # variable of the same name exists; do not shadow it in this function.
    $global:LASTEXITCODE = $null
    try {
        $ErrorActionPreference = 'Continue'
        $output = @(& $VrPathReg finddriver pose_anchor 2>$null |
            ForEach-Object { "$_".Trim() } | Where-Object { $_ })
        $exitCode = $global:LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($null -eq $exitCode -or $exitCode -notin @(0, 1, 2)) {
        throw "vrpathreg finddriver failed: $exitCode"
    }
    if ($exitCode -eq 0 -and $output.Count -ne 1) {
        throw 'vrpathreg returned an ambiguous PoseAnchor path.'
    }
    [PSCustomObject]@{
        ExitCode = $exitCode
        Path = if ($exitCode -eq 0) { $output[0].Trim('"') } else { $null }
    }
}

function Test-SteamVrDriverPathEqual([string]$Left, [string]$Right) {
    if ([string]::IsNullOrWhiteSpace($Left) -or
        [string]::IsNullOrWhiteSpace($Right)) { return $false }
    $leftFullPath = [IO.Path]::GetFullPath($Left).TrimEnd('\', '/')
    $rightFullPath = [IO.Path]::GetFullPath($Right).TrimEnd('\', '/')
    return $leftFullPath.Equals($rightFullPath, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-PoseAnchorDriverRegistration {
    param(
        [string]$VrPathReg,
        [string]$DriverPath,
        [switch]$Absent,
        [switch]$AllWithName
    )
    $registration = Get-PoseAnchorDriverRegistration $VrPathReg
    if ($Absent) {
        if ($registration.ExitCode -eq 1) { return }
        if (-not $AllWithName -and $registration.ExitCode -eq 0 -and
            -not (Test-SteamVrDriverPathEqual $registration.Path $DriverPath)) { return }
        throw 'Could not confirm that the requested PoseAnchor registration was removed. Keep the driver files and inspect the SteamVR registration before retrying.'
    }
    if ($registration.ExitCode -ne 0 -or
        -not (Test-SteamVrDriverPathEqual $registration.Path $DriverPath)) {
        throw 'Could not confirm the new PoseAnchor registration. Keep SteamVR closed and inspect its registration before retrying.'
    }
}

function Find-SteamVrPathReg {
    $candidates = [System.Collections.Generic.List[string]]::new()
    foreach ($uninstallPath in @(
        'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820',
        'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820',
        'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820'
    )) {
        if (Test-Path $uninstallPath) {
            $installLocation = (Get-ItemProperty -Path $uninstallPath).InstallLocation
            if ($installLocation) {
                $candidates.Add((Join-Path $installLocation 'bin\win64\vrpathreg.exe'))
            }
        }
    }
    foreach ($registryPath in @('HKCU:\Software\Valve\Steam', 'HKLM:\Software\WOW6432Node\Valve\Steam')) {
        if (Test-Path $registryPath) {
            $properties = Get-ItemProperty -Path $registryPath
            foreach ($name in @('SteamPath', 'InstallPath')) {
                if ($properties.$name) {
                    $candidates.Add((Join-Path $properties.$name 'steamapps\common\SteamVR\bin\win64\vrpathreg.exe'))
                }
            }
        }
    }
    if (${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'Steam\steamapps\common\SteamVR\bin\win64\vrpathreg.exe'))
    }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'SteamVR vrpathreg.exe was not found. Add a custom Steam library path to this script if needed.'
}
