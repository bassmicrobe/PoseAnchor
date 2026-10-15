[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $projectRoot 'scripts\steamvr-path.ps1')
$script:checks = 0

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Assert-Throws([scriptblock]$Action, [string]$Expected) {
    $message = $null
    try { & $Action } catch { $message = $_.Exception.Message }
    Assert-True ($null -ne $message -and $message -like "*$Expected*") `
        "Expected an error containing '$Expected'; actual: '$message'"
}

# This fake is called through the same invocation path as vrpathreg. It only
# returns stdout and an exit code; it cannot access SteamVR or its registry.
function Invoke-FakeVrPathReg {
    param([string]$Command, [string]$DriverName)
    if ($Command -ne 'finddriver' -or $DriverName -ne 'pose_anchor') {
        throw 'Unexpected command in the registration query test.'
    }
    if ($null -eq $script:fakeExitCode) {
        $global:LASTEXITCODE = $null
    } else {
        # A real, harmless native exit also catches accidental shadowing of
        # LASTEXITCODE, which a function-only mock would otherwise miss.
        & $env:ComSpec /d /c "exit /b $script:fakeExitCode"
    }
    $script:fakeOutput
}

# SteamVR may be running on the developer machine. Only the process inspection
# is mocked; no process is started/stopped and no live registration is touched.
function Get-Process {
    param([string]$Name, [string]$ErrorAction)
    if ($Name -ne 'vrserver') { throw 'Unexpected process check.' }
    if ($script:fakeSteamVrRunning) { [PSCustomObject]@{ ProcessName = 'vrserver' } }
}

$driverPath = 'C:\PoseAnchor test\driver'
$script:fakeExitCode = 0
$script:fakeOutput = @('  "C:\PoseAnchor test\driver"  ')
$registration = Get-PoseAnchorDriverRegistration 'Invoke-FakeVrPathReg'
Assert-True ($registration.Path -ceq $driverPath) 'A single quoted path was not normalized.'
Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath
Assert-True (Test-SteamVrDriverPathEqual 'c:/PoseAnchor test/driver/' $driverPath) `
    'Equivalent Windows paths did not compare equal.'
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath -Absent
} 'Could not confirm'
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -Absent -AllWithName
} 'Could not confirm'

# Removing only this copy must not be confused with unregistering another copy.
$script:fakeOutput = @('C:\Other PoseAnchor\driver')
Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath -Absent
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath
} 'Could not confirm'

$script:fakeExitCode = 1
$script:fakeOutput = @()
Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath -Absent
Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -Absent -AllWithName
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath
} 'Could not confirm'

# Duplicate output is not documented to enumerate every matching path. Even
# when it omits this path, absence must not be reported as proven.
$script:fakeExitCode = 2
$script:fakeOutput = @('C:\Other PoseAnchor\driver', 'C:\Another PoseAnchor\driver')
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath -Absent
} 'Could not confirm'
Assert-Throws {
    Assert-PoseAnchorDriverRegistration -VrPathReg 'Invoke-FakeVrPathReg' -DriverPath $driverPath
} 'Could not confirm'

foreach ($code in @(-1, 3, $null)) {
    $script:fakeExitCode = $code
    Assert-Throws { Get-PoseAnchorDriverRegistration 'Invoke-FakeVrPathReg' } 'finddriver failed'
    Assert-True ($ErrorActionPreference -eq 'Stop') 'Query changed the caller error policy.'
}
$script:fakeExitCode = 0
$script:fakeOutput = @()
Assert-Throws { Get-PoseAnchorDriverRegistration 'Invoke-FakeVrPathReg' } 'ambiguous'
$script:fakeOutput = @($driverPath, 'C:\Another\driver')
Assert-Throws { Get-PoseAnchorDriverRegistration 'Invoke-FakeVrPathReg' } 'ambiguous'

$savedOverride = $env:VR_PATHREG_OVERRIDE
try {
    $env:VR_PATHREG_OVERRIDE = 'C:\fake\override.vrpath'
    $script:fakeSteamVrRunning = $false
    Assert-Throws { Assert-SteamVrRegistrationWritable } 'VR_PATHREG_OVERRIDE'
    $env:VR_PATHREG_OVERRIDE = $null
    $script:fakeSteamVrRunning = $true
    Assert-Throws { Assert-SteamVrRegistrationWritable } 'Exit SteamVR'
    $script:fakeSteamVrRunning = $false
    Assert-SteamVrRegistrationWritable
} finally {
    $env:VR_PATHREG_OVERRIDE = $savedOverride
}

# The Inno runtime is not executed here: these are source-contract checks for
# its safety guards and localized messages. ISCC compilation additionally checks
# Pascal syntax; real install/uninstall behavior still needs a disposable host.
$installerSource = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\PoseAnchor.iss') -Raw -Encoding UTF8
function Get-InstallerRoutine([string]$Name) {
    $pattern = '(?ms)^(?:function|procedure) ' + [regex]::Escape($Name) +
        '(?:\(|:|;).*?(?=^(?:function|procedure) |\z)'
    $match = [regex]::Match($installerSource, $pattern)
    Assert-True $match.Success "Installer routine not found: $Name"
    $match.Value
}

$processCheck = Get-InstallerRoutine 'TryIsImageRunning'
Assert-True ($processCheck -match 'var Running: Boolean' -and
    $processCheck -match '(?s)Result := False;\s+Running := False;.*?ExecAndCaptureOutput') `
    'Process inspection must begin unverified, separately from the running flag.'
Assert-True ($processCheck -match '(?s)if ResultCode <> 0 then\s+begin.*?Exit;\s+end;\s+Result := True;') `
    'Process inspection failure must never be treated as a successful check.'
Assert-True ($processCheck -match '(?s)try\s+if not ExecAndCaptureOutput.*?except\s+Log\(.*?GetExceptionMessage\);\s+Exit;\s+end;') `
    'Process capture exceptions must report an unverified inspection instead of escaping the guard.'
Assert-True ($processCheck -match '(?s)if Output\.Error then\s+begin.*?Exit;\s+end;.*?Result := True;') `
    'Incomplete tasklist output must not be treated as evidence that a process stopped.'
$blockCheck = Get-InstallerRoutine 'ProcessBlockMessage'
Assert-True (([regex]::Matches($blockCheck, "CustomMessage\('ProcessInspectionFailed'\)")).Count -eq 2 -and
    ([regex]::Matches($blockCheck, 'else if Running then')).Count -eq 2) `
    'Both process checks must distinguish inspection failure from a running process.'
foreach ($entry in @(
    @{ Name = 'PrepareToInstall'; Before = 'PrepareRegistrationPlan' },
    @{ Name = 'AddInstalledDriver'; Before = "'removedriver '" },
    @{ Name = 'RemoveInstalledDriver'; Before = "'removedriver '" },
    @{ Name = 'InitializeUninstall'; Before = 'SuppressibleMsgBox(BlockMessage' }
)) {
    $routine = Get-InstallerRoutine $entry.Name
    $guardIndex = $routine.IndexOf('ProcessBlockMessage(', [StringComparison]::Ordinal)
    $operationIndex = $routine.IndexOf($entry.Before, [StringComparison]::Ordinal)
    Assert-True ($guardIndex -ge 0 -and $operationIndex -gt $guardIndex) `
        "Process safety guard must precede the operation in $($entry.Name)."
}

$englishMessages = @{}
$japaneseMessages = @{}
foreach ($match in [regex]::Matches($installerSource, '(?m)^(english|japanese)\.(\w+)=(.*)$')) {
    $messages = if ($match.Groups[1].Value -eq 'english') { $englishMessages } else { $japaneseMessages }
    $messages[$match.Groups[2].Value] = $match.Groups[3].Value
}
Assert-True ($englishMessages.Count -eq $japaneseMessages.Count) 'Installer language message counts differ.'
foreach ($name in $englishMessages.Keys) {
    Assert-True ($japaneseMessages.ContainsKey($name)) "Japanese installer message missing: $name"
    $englishArgs = @([regex]::Matches($englishMessages[$name], '%\d+') | ForEach-Object { $_.Value }) -join ','
    $japaneseArgs = @([regex]::Matches($japaneseMessages[$name], '%\d+') | ForEach-Object { $_.Value }) -join ','
    Assert-True ($englishArgs -ceq $japaneseArgs) "Installer message arguments differ: $name"
}

Write-Host "Installer helper regression checks passed: $script:checks"
