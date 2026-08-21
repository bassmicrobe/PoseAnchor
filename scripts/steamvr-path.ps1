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
        if (Test-Path -LiteralPath $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw 'SteamVR vrpathreg.exe was not found. Add a custom Steam library path to this script if needed.'
}
