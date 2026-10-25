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
Assert-True ($installerSource -match '(?m)^Uninstallable=yes\s*$') `
    'Every distributable installer must include the Inno uninstaller.'
Assert-True ($installerSource.Contains('Name: "{group}\{cm:UninstallProgram,PoseAnchor}"; Filename: "{uninstallexe}"')) `
    'Users need a Start menu entry for the bundled uninstaller.'
# Inno tracks the installed files itself. Recursive/wildcard cleanup could
# delete files added by the user, SteamVR settings, or another driver copy.
Assert-True ($installerSource -notmatch '(?im)^\[UninstallDelete\]\s*$') `
    'Uninstall must rely on recorded installed files, not an extra deletion list.'
Assert-True ($installerSource -notmatch '(?i)\bDelTree\s*\(') `
    'The installer must not recursively erase an installation or shared settings directory.'
$manualPath = Join-Path $projectRoot 'MANUAL.md'
Assert-True (Test-Path -LiteralPath $manualPath -PathType Leaf) 'The user manual is missing.'
Assert-True ($installerSource.Contains('Source: "{#PackageDir}\MANUAL.md"; DestDir: "{app}"; Flags: ignoreversion')) `
    'The installer must include the user manual.'
Assert-True ($installerSource.Contains('Name: "{group}\{cm:ManualShortcut}"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\MANUAL.md"""')) `
    'The user manual needs a Start menu shortcut independent of Markdown file associations.'
$installerBuildSource = Get-Content -LiteralPath (Join-Path $projectRoot 'scripts\build-installer.ps1') -Raw -Encoding UTF8
Assert-True ($installerBuildSource -match '(?s)\$requiredFiles = @\(.*?''MANUAL\.md''.*?\)') `
    'Installer packaging must reject a staged package without the user manual.'
Assert-True ($installerBuildSource -match '(?s)foreach \(\$relativePath in \$packageDocuments.Keys\).*?Get-FileHash.*?if \(\$sourceHash -cne \$stagedHash\).*?throw "Stale package document:') `
    'SkipBuild must reject stale package documentation and notices.'

# Experimental-release wording is part of the distribution contract. Read UTF-8
# explicitly and use Unicode escapes so Windows PowerShell 5.1 can run this test
# regardless of the script's own encoding or the host ANSI code page.
$cmakeSource = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw -Encoding UTF8
foreach ($relativePath in @('README.md', 'PACKAGE_README.md')) {
    $document = Get-Content -LiteralPath (Join-Path $projectRoot $relativePath) -Raw -Encoding UTF8
    Assert-True ($document.Contains('prefers-color-scheme: dark') -and
        $document.Contains('prefers-color-scheme: light') -and $document.Contains('alt="PoseAnchor"')) `
        "README branding must support both themes and accessible fallback text: $relativePath"
    foreach ($asset in @('poseanchor-logo-horizontal-ink.svg', 'poseanchor-logo-horizontal-white.svg')) {
        Assert-True ($document.Contains('assets/branding/' + $asset) -and
            (Test-Path -LiteralPath (Join-Path $projectRoot ('assets\branding\' + $asset)) -PathType Leaf)) `
            "README logo reference must resolve locally: $asset"
    }
}
Assert-True ($cmakeSource.Contains('"${CMAKE_SOURCE_DIR}/assets/branding"') -and
    $cmakeSource.Contains('"${POSE_ANCHOR_PACKAGE_DIR}/assets/branding"')) `
    'Packaged README logos must be staged alongside the documentation.'
Assert-True ($installerSource.Contains('Source: "{#PackageDir}\assets\branding\*.svg"; DestDir: "{app}\assets\branding"; Flags: ignoreversion')) `
    'Installed README logo references must resolve offline.'
Assert-True ($installerSource.Contains('IconFilename: "{app}\PoseAnchor.ico"') -and
    $installerSource.Contains('Source: "{#PackageDir}\PoseAnchor.ico"; DestDir: "{app}"; Flags: ignoreversion')) `
    'The uninstall shortcut icon must be installed and tracked by Inno.'
Assert-True ($installerBuildSource.Contains("'PoseAnchor.ico' = 'src\status\PoseAnchorStatus.ico'") -and
    $installerBuildSource.Contains("'assets\branding\' +")) `
    'SkipBuild must reject stale app icons and README branding assets.'
$iconBytes = [IO.File]::ReadAllBytes((Join-Path $projectRoot 'src\status\PoseAnchorStatus.ico'))
Assert-True ($iconBytes.Length -ge 150 -and [BitConverter]::ToUInt16($iconBytes, 0) -eq 0 -and
    [BitConverter]::ToUInt16($iconBytes, 2) -eq 1 -and [BitConverter]::ToUInt16($iconBytes, 4) -eq 9) `
    'Windows branding must be a valid nine-frame ICO.'
$iconSizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
for ($frame = 0; $frame -lt $iconSizes.Count; $frame++) {
    $offset = 6 + 16 * $frame
    $dimension = $iconSizes[$frame] % 256
    Assert-True ($iconBytes[$offset] -eq $dimension -and $iconBytes[$offset + 1] -eq $dimension) `
        "Missing Windows icon size: $($iconSizes[$frame])"
}
foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $projectRoot 'assets\branding') -Filter '*.svg' -File) {
    [xml]$svg = Get-Content -LiteralPath $asset.FullName -Raw -Encoding UTF8
    Assert-True ($svg.DocumentElement.LocalName -eq 'svg') "Invalid logo SVG: $($asset.Name)"
    Assert-True ($svg.SelectNodes('//*[local-name()="script" or local-name()="foreignObject" or local-name()="image" or local-name()="text"]').Count -eq 0) `
        "Branding must be self-contained vector paths without scripts, linked images or font dependencies: $($asset.Name)"
}
$projectVersion = [regex]::Match($cmakeSource, 'project\(PoseAnchor VERSION (\d+\.\d+\.\d+)').Groups[1].Value
Assert-True ($projectVersion -ne '') 'The distribution version must come from CMake.'
Assert-True ($installerSource.Contains('#define AppVersion "' + $projectVersion + '"') -and
    $installerSource.Contains('#define AppVersionQuad "' + $projectVersion + '.0"')) `
    'Direct ISCC builds must use the current project version too.'
Assert-True ($installerSource.Contains('AppVerName={#AppName} {#AppVersion} (experimental)') -and
    $installerSource.Contains('UninstallDisplayName=PoseAnchor {#AppVersion} (experimental)')) `
    'Setup and Windows Apps must identify the build as experimental.'
foreach ($relativePath in @('README.md', 'PACKAGE_README.md', 'MANUAL.md')) {
    $document = Get-Content -LiteralPath (Join-Path $projectRoot $relativePath) -Raw -Encoding UTF8
    Assert-True ($document -match '(?m)^# PoseAnchor ' + [regex]::Escape($projectVersion) + '\b') `
        "The current version must be visible in $relativePath."
    Assert-True ($document.Contains('controlled ON/OFF') -and $document.Contains('can worsen motion')) `
        "English effectiveness and false-correction warnings are required in $relativePath."
    Assert-True ($document -match '\u672a\u5b9f\u8a3c' -and $document -match '\u60aa\u5316') `
        "Japanese effectiveness and false-correction warnings are required in $relativePath."
}
$japaneseInfo = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\info-before-ja.txt') -Raw -Encoding UTF8
$englishInfo = Get-Content -LiteralPath (Join-Path $projectRoot 'installer\info-before-en.txt') -Raw -Encoding UTF8
Assert-True ($japaneseInfo -match '\u672a\u5b9f\u8a3c' -and $japaneseInfo -match '\u60aa\u5316' -and
    $japaneseInfo.Contains('ON/OFF')) 'Japanese Setup must explain unproven benefit and false-correction risk.'
Assert-True ($englishInfo.Contains('controlled ON/OFF') -and $englishInfo.Contains('can worsen motion')) `
    'English Setup must explain unproven benefit and false-correction risk.'
foreach ($relativePath in @('README.md', 'PACKAGE_README.md')) {
    $document = Get-Content -LiteralPath (Join-Path $projectRoot $relativePath) -Raw -Encoding UTF8
    foreach ($match in [regex]::Matches($document, 'PoseAnchor-Setup-(\d+\.\d+\.\d+)\.exe')) {
        Assert-True ($match.Groups[1].Value -ceq $projectVersion) `
            "Installer examples must not point at an older build in $relativePath."
    }
}
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

$unregister = Get-InstallerRoutine 'RemoveInstalledDriver'
Assert-True ($unregister -match 'AppPath := CleanPath\(ExpandConstant\(''\{app\}''\)\);' -and
    $unregister -match 'RunVrPathReg\(''removedriver '' \+ AddQuotes\(AppPath\)') `
    'Uninstall may unregister only the exact installed application path.'
Assert-True ($unregister -notmatch '(?i)removedriverswithname' -and
    ([regex]::Matches($unregister, "RunVrPathReg\('removedriver '")).Count -eq 1) `
    'Uninstall must not remove other PoseAnchor registrations by driver name.'
Assert-True ($unregister -match "(?s)if not TryRegistrationPathPresent\(AppPath, Present\) then\s+Exit;\s+LateUninstallBlockMessage := ProcessBlockMessage\(\s*'SteamVRRunningUninstall', 'StatusRunningUninstall'\);\s*if LateUninstallBlockMessage <> '' then\s+Exit;\s*if not Present then\s+begin\s+Result := True;\s+Exit;\s+end;\s+Result := RunVrPathReg\('removedriver ' \+ AddQuotes\(AppPath\)") `
    'Uninstall must verify presence, recheck processes, and return without writing if this path is already absent.'
Assert-True ($unregister -notmatch '(?i)\b(?:DeleteFile|RemoveDir|DelTree)\s*\(') `
    'Unregistration must not directly delete application files or shared SteamVR data.'
Assert-True ($unregister -match '(?s)if not LocateSteamVR then\s+begin.*?if RegistrationFileDefinitelyMissing\(OpenVrPathsFile\) then\s+begin.*?Result := True;\s+Exit;' -and
    $unregister -notmatch 'FileExists\(OpenVrPathsFile\)') `
    'Missing SteamVR may authorize removal only when the registration file is definitely absent.'
$missingRegistry = Get-InstallerRoutine 'RegistrationFileDefinitelyMissing'
Assert-True ($missingRegistry -match '(?s)Attributes := GetFileAttributesW\(Path\);\s+if Attributes <> \$FFFFFFFF then\s+Exit;.*?ErrorCode := DLLGetLastError;' -and
    $missingRegistry -match 'Result := \(ErrorCode = 2\) or \(ErrorCode = 3\);' -and
    $missingRegistry -notmatch 'Result := True;') `
    'Only Win32 file/path-not-found may prove registry absence; unreadable files must block removal.'
$registryCommand = Get-InstallerRoutine 'RunVrPathReg'
Assert-True ($registryCommand -match '(?s)begin\s+Result := False;.*?try\s+Result := ExecAndCaptureOutput.*?except\s+Log\(.*?GetExceptionMessage\);\s+Exit;\s+end;') `
    'Registration command launch/capture exceptions must return an unverified result.'
Assert-True ($registryCommand -match '(?s)if Output\.Error then\s+begin.*?Result := False;\s+end;') `
    'Incomplete vrpathreg output must never count as a verified registration state.'
$singlePath = Get-InstallerRoutine 'TrySingleRegistrationPath'
Assert-True ($singlePath -match '(?s)Count := 0;.*?if Trim\(Lines\[I\]\) <> '''' then\s+begin\s+Count := Count \+ 1;\s+if Count > 1 then\s+Exit;' -and
    $singlePath -match 'if Count = 1 then\s+Result := TryNormalizeRegistrationPath\(Candidate, Path\);') `
    'A successful registration query must contain exactly one normalized path, not its first line.'
$normalizePath = Get-InstallerRoutine 'TryNormalizeRegistrationPath'
Assert-True ($normalizePath.Contains("StringChangeEx(Value, '/', '\', True);") -and
    $normalizePath -match 'if \(not DrivePath\) and \(not NetworkPath\) then\s+Exit;' -and
    $normalizePath -match 'Path := RemoveBackslashUnlessRoot\(ExpandFileName\(Value\)\);') `
    'Registration comparison must reject relative paths and normalize slash/dot aliases.'
$samePath = Get-InstallerRoutine 'SamePath'
Assert-True ($samePath -match '(?s)TryNormalizeRegistrationPath\(Left, NormalizedLeft\) and\s+TryNormalizeRegistrationPath\(Right, NormalizedRight\) and\s+\(CompareText\(NormalizedLeft, NormalizedRight\) = 0\)') `
    'Both registration paths must normalize successfully before comparison.'
Assert-True ($installerSource -notmatch '\bFirstNonEmptyLine\b') `
    'Registration safety checks must not ignore additional nonempty query lines.'
foreach ($name in @('TryRegistrationPathPresent', 'PrepareRegistrationPlan', 'AddInstalledDriver', 'RemoveInstalledDriver')) {
    $routine = Get-InstallerRoutine $name
    Assert-True ($routine.Contains('TrySingleRegistrationPath(Output.StdOut, ExistingPath)')) `
        "A single-path registration query must validate all output in $name."
}
foreach ($name in @('RemoveInstalledDriver', 'InitializeUninstall', 'RunVrPathReg')) {
    $routine = Get-InstallerRoutine $name
    Assert-True ($routine -match "(?s)if GetEnv\('VR_PATHREG_OVERRIDE'\) <> '' then\s+begin.*?Exit;\s+end;") `
        "A path-registry override must stop the operation in $name."
}
$overrideIndex = $unregister.IndexOf("if GetEnv('VR_PATHREG_OVERRIDE')", [StringComparison]::Ordinal)
$unregisterIndex = $unregister.IndexOf("'removedriver '", [StringComparison]::Ordinal)
Assert-True ($overrideIndex -ge 0 -and $unregisterIndex -gt $overrideIndex) `
    'The registration override guard must run before uninstall mutates registrations.'
Assert-True ($unregister -match "(?s)LateUninstallBlockMessage := ProcessBlockMessage\(\s*'SteamVRRunningUninstall', 'StatusRunningUninstall'\);\s*if LateUninstallBlockMessage <> '' then\s+Exit;") `
    'A running or uninspectable process must stop the unregister operation.'
$initializeUninstall = Get-InstallerRoutine 'InitializeUninstall'
Assert-True ($initializeUninstall -match "(?s)if GetEnv\('VR_PATHREG_OVERRIDE'\) <> '' then\s+begin.*?Result := False;\s+Exit;\s+end;" -and
    $initializeUninstall -match "(?s)BlockMessage := ProcessBlockMessage\('SteamVRRunningUninstall', 'StatusRunningUninstall'\);\s*if BlockMessage <> '' then\s+begin.*?Result := False;") `
    'Uninstaller startup must reject unsafe environment and process states before opening removal.'
$uninstallStep = Get-InstallerRoutine 'CurUninstallStepChanged'
# Ignore explanatory line comments, but not any executable Pascal statement,
# when checking that the error message is followed directly by the common abort.
$uninstallControlFlow = [regex]::Replace($uninstallStep, '(?m)//[^\r\n]*', '')
Assert-True ($uninstallStep -match '(?s)if \(CurUninstallStep = usUninstall\).*?if not RemoveInstalledDriver then\s+begin') `
    'Unregistration must succeed before Inno starts removing the installed files.'
Assert-True ($uninstallStep -notmatch '\b(?:MB_YESNO|IDYES|mbConfirmation)\b') `
    'Unregistration failure must not offer an unsafe continue-anyway override.'
Assert-True ($uninstallControlFlow -match "(?s)if not RemoveInstalledDriver then\s+begin.*?CustomMessage\('UnregistrationFailed'\).*?mbError,\s*MB_OK,\s*IDOK\);\s*Abort;\s*end;") `
    'Every failed unregistration must show an error and unconditionally abort file removal.'
Assert-True (([regex]::Matches($uninstallStep, '\bAbort;')).Count -eq 1) `
    'Failed unregistration should converge on one unconditional abort, including silent uninstall.'

$englishMessages = @{}
$japaneseMessages = @{}
foreach ($match in [regex]::Matches($installerSource, '(?m)^(english|japanese)\.(\w+)=(.*)$')) {
    $messages = if ($match.Groups[1].Value -eq 'english') { $englishMessages } else { $japaneseMessages }
    $messages[$match.Groups[2].Value] = $match.Groups[3].Value
}
foreach ($messages in @($englishMessages, $japaneseMessages)) {
    $shortcut = $messages['ManualShortcut'].Trim() + '.lnk'
    Assert-True ($installerSource.Contains('Type: files; Name: "{group}\' + $shortcut + '"')) `
        "Installer language changes must not leave a stale manual shortcut: $shortcut"
}
Assert-True ($englishMessages.Count -eq $japaneseMessages.Count) 'Installer language message counts differ.'
foreach ($name in $englishMessages.Keys) {
    Assert-True ($japaneseMessages.ContainsKey($name)) "Japanese installer message missing: $name"
    $englishArgs = @([regex]::Matches($englishMessages[$name], '%\d+') | ForEach-Object { $_.Value }) -join ','
    $japaneseArgs = @([regex]::Matches($japaneseMessages[$name], '%\d+') | ForEach-Object { $_.Value }) -join ','
    Assert-True ($englishArgs -ceq $japaneseArgs) "Installer message arguments differ: $name"
}

Write-Host "Installer helper regression checks passed: $script:checks"
