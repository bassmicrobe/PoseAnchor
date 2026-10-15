#requires -Version 5.1
[CmdletBinding()]
param(
    # Compile the isolated fixture without running its installer/uninstaller.
    [switch]$CompileOnly
)

# Optional Windows/Inno integration test. This intentionally is not a normal
# CTest dependency: it runs an inert installer and uninstaller, never PoseAnchor
# or SteamVR. All generated files are retained below a new ignored build folder.
$ErrorActionPreference = 'Stop'
if ($env:OS -ne 'Windows_NT') { throw 'This test requires Windows.' }
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
$fixtureId = [Guid]::NewGuid().ToString('N')
$testRoot = [IO.Path]::GetFullPath((Join-Path $buildRoot "uninstall-safety-$fixtureId"))
$sourcePath = Join-Path $projectRoot 'installer\PoseAnchor.iss'
$script:checks = 0
$utf8 = [Text.UTF8Encoding]::new($true)

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
    $script:checks++
}

function Assert-NoReparseAncestor([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Fixture paths must not traverse a reparse point: $current"
            }
        }
        $parent = Split-Path -Parent $current
        if (-not $parent -or $parent -eq $current) { break }
        $current = $parent
    }
}

function Assert-FixturePath([string]$Path) {
    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($testRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path escaped the fresh fixture directory: $fullPath"
    }
    Assert-NoReparseAncestor $fullPath
}

function Quote-NativeArgument([string]$Value) {
    if ($Value -match '["\r\n]' -or $Value.EndsWith('\')) {
        throw 'Unexpected characters or trailing slash in a fixture process argument.'
    }
    '"' + $Value + '"'
}

function Invoke-FixtureProcess {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$LogPrefix
    )
    Assert-FixturePath $LogPrefix
    # Only the compiler and executables generated beneath this test's root can
    # run. No executable discovery, SteamVR tool, or installed app is invoked.
    $fullExecutable = [IO.Path]::GetFullPath($Executable)
    if (-not $fullExecutable.Equals($script:iscc, [StringComparison]::OrdinalIgnoreCase)) {
        Assert-FixturePath $fullExecutable
    }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $fullExecutable
    $start.Arguments = ($Arguments | ForEach-Object { Quote-NativeArgument $_ }) -join ' '
    $start.WorkingDirectory = $testRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    # Clear only the child's inherited override so the real event's override
    # guard cannot accidentally substitute for the scenario under test.
    $start.EnvironmentVariables['VR_PATHREG_OVERRIDE'] = ''
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw "Could not launch fixture process: $fullExecutable" }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(30000)) {
            # Only this process and its children belong to the fixture. Never
            # terminate an application by image name or another installer's PID.
            $stop = [Diagnostics.ProcessStartInfo]::new()
            $stop.FileName = Join-Path $env:SystemRoot 'System32\taskkill.exe'
            $stop.Arguments = "/PID $($process.Id) /T /F"
            $stop.UseShellExecute = $false
            $stop.CreateNoWindow = $true
            $stop.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
            $stopProcess = [Diagnostics.Process]::Start($stop)
            try { $null = $stopProcess.WaitForExit(5000) } finally { $stopProcess.Dispose() }
            throw "Fixture process exceeded 30 seconds; its own process tree was stopped. Retained: $testRoot"
        }
        if (-not [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($stdout, $stderr), 5000)) {
            throw "Fixture process output did not finish. Retained: $testRoot"
        }
        [IO.File]::WriteAllText("$LogPrefix.stdout.txt", $stdout.Result, $utf8)
        [IO.File]::WriteAllText("$LogPrefix.stderr.txt", $stderr.Result, $utf8)
        $process.ExitCode
    } finally {
        $process.Dispose()
    }
}

# Guard every directory before generating or running anything. The unique root
# may not already exist, must be a direct build child, and must be git-ignored.
Assert-True ([IO.Path]::IsPathRooted($testRoot) -and
    (Split-Path -Parent $testRoot) -eq $buildRoot) 'Unexpected fixture root.'
Assert-NoReparseAncestor $testRoot
Assert-True (-not (Test-Path -LiteralPath $testRoot)) 'Fixture root already exists.'
& git -C $projectRoot check-ignore --quiet -- "build/uninstall-safety-$fixtureId/payload.txt"
Assert-True ($LASTEXITCODE -eq 0) 'Fixture directory must be ignored by Git.'

$isccCandidates = @(
    (Get-Command ISCC.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe')
) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }
$script:iscc = $isccCandidates | Select-Object -First 1
if (-not $script:iscc) { throw 'Inno Setup 6 was not found; the optional runtime test was not run.' }
$script:iscc = [IO.Path]::GetFullPath($script:iscc)
$installerSource = Get-Content -LiteralPath $sourcePath -Raw -Encoding UTF8

function Get-SourceRoutine([string]$Name) {
    $pattern = '(?ms)^(?:function|procedure) ' + [regex]::Escape($Name) +
        '(?:\(|:|;).*?(?=^(?:function|procedure) |\z)'
    $matches = [regex]::Matches($installerSource, $pattern)
    if ($matches.Count -ne 1 -or $matches[0].Length -gt 10000) {
        throw "Could not extract one bounded source routine: $Name"
    }
    $routine = $matches[0].Value.Trim()
    # A future change requiring real I/O must not silently broaden this fixture.
    # The selected events may call only our mock guards/removal; the selected
    # path helpers use string operations, not live registration or file reads.
    if ($routine -match '(?im)^\s*(?:\[|#)|\bexternal\b|\b(?:Exec\w*|ShellExec\w*|Reg\w*|DeleteFile|DelTree|RemoveDir|Save\w*ToFile|Load\w*FromFile|CopyFile|MoveFile\w*)\s*\(') {
        throw "Source routine exceeds the fixture's no-system-I/O contract: $Name"
    }
    $routine
}

$pathHelpers = @('CleanPath', 'TryNormalizeRegistrationPath', 'TrySingleRegistrationPath', 'SamePath') |
    ForEach-Object { Get-SourceRoutine $_ }
$events = @('InitializeUninstall', 'CurUninstallStepChanged') |
    ForEach-Object { Get-SourceRoutine $_ }
$messages = @('PathRegistryOverride', 'UnregistrationFailed', 'SteamVRRunningUninstall', 'StatusRunningUninstall') |
    ForEach-Object {
        $messageMatches = [regex]::Matches($installerSource, '(?m)^english\.' + [regex]::Escape($_) + '=[^\r\n]*')
        if ($messageMatches.Count -ne 1) { throw "Missing fixture message: $_" }
        $messageMatches[0].Value
    }

New-Item -ItemType Directory -Path $testRoot | Out-Null
$payloadSource = Join-Path $testRoot 'payload.txt'
[IO.File]::WriteAllText($payloadSource, "Inert uninstaller fixture payload $fixtureId", $utf8)
$fixtureSource = @'
[Setup]
AppId=PoseAnchor.UninstallSafety.@FIXTURE_ID@
AppName=PoseAnchor isolated uninstaller test
AppVersion=0.0.0
DefaultDirName=@TEST_ROOT@\unused-default-app
DisableDirPage=yes
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
CreateUninstallRegKey=no
Uninstallable=yes
CloseApplications=no
RestartApplications=no
AllowCancelDuringInstall=no
SetupLogging=yes
UsePreviousAppDir=no
OutputDir=@TEST_ROOT@
OutputBaseFilename=FixtureSetup
Compression=none
SolidCompression=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
@MESSAGES@

[Files]
Source: "@TEST_ROOT@\payload.txt"; DestDir: "{app}"; Flags: ignoreversion

[Code]
var
  DriverUnregistrationAttempted: Boolean;
  LateUninstallBlockMessage: String;

@PATH_HELPERS@

procedure FixtureAssert(Condition: Boolean; const Detail: String);
begin
  if not Condition then
    RaiseException('Fixture assertion failed: ' + Detail);
end;

function InitializeSetup: Boolean;
var
  Lines: TArrayOfString;
  Path: String;
begin
  FixtureAssert(SamePath('c:/Fixture/a/../driver/', 'C:\Fixture\driver'), 'slash/dot/case aliases');
  FixtureAssert(SamePath(' "C:\Fixture\driver" ', 'C:\Fixture\driver'), 'quoted path');
  FixtureAssert(not SamePath('C:\Fixture\driver', 'C:\Other\driver'), 'different paths');
  FixtureAssert(not TryNormalizeRegistrationPath('', Path), 'empty path');
  FixtureAssert(not TryNormalizeRegistrationPath('relative\driver', Path), 'relative path');
  FixtureAssert(not TryNormalizeRegistrationPath('C:driver', Path), 'drive-relative path');
  FixtureAssert(not TryNormalizeRegistrationPath('warning: inspection failed', Path), 'diagnostic output');
  FixtureAssert(not TryNormalizeRegistrationPath('\\?\C:\Fixture', Path), 'device namespace');
  SetArrayLength(Lines, 0);
  FixtureAssert(not TrySingleRegistrationPath(Lines, Path), 'no lines');
  SetArrayLength(Lines, 1);
  Lines[0] := '  "C:/Fixture/driver/"  ';
  FixtureAssert(TrySingleRegistrationPath(Lines, Path), 'one complete path');
  FixtureAssert(SamePath(Path, 'C:\Fixture\driver'), 'normalized single path');
  SetArrayLength(Lines, 2);
  Lines[1] := 'C:\Other\driver';
  FixtureAssert(not TrySingleRegistrationPath(Lines, Path), 'multiple path lines');
  Lines[1] := 'warning: incomplete output';
  FixtureAssert(not TrySingleRegistrationPath(Lines, Path), 'extra diagnostic line');
  Lines[0] := '';
  Lines[1] := '   ';
  FixtureAssert(not TrySingleRegistrationPath(Lines, Path), 'blank-only lines');
  Log('Fixture: 14 native pure-path checks passed');
  Result := True;
end;

function FixtureScenario: String;
var
  Text: AnsiString;
begin
  if not LoadStringFromFile(ExpandConstant('{app}\fixture-scenario.txt'), Text) then
    RaiseException('Fixture scenario file missing');
  Result := Trim(String(Text));
end;

function ProcessBlockMessage(const SteamVrMessage, StatusMessage: String): String;
var
  Scenario: String;
begin
  Scenario := FixtureScenario;
  Result := '';
  if Scenario = 'initial-running' then
    Result := CustomMessage(SteamVrMessage)
  else if Scenario = 'initial-unknown' then
    Result := 'Fixture process inspection could not be completed';
end;

function RemoveInstalledDriver: Boolean;
var
  Scenario: String;
begin
  Scenario := FixtureScenario;
  if not SaveStringToFile(ExpandConstant('{app}\mock-removal-called.txt'), 'mock only', False) then
    RaiseException('Fixture could not record the mock removal call');
  LateUninstallBlockMessage := '';
  Result := False;
  if Scenario = 'success' then
    Result := True
  else if Scenario = 'late-running' then
    LateUninstallBlockMessage := CustomMessage('SteamVRRunningUninstall')
  else if Scenario = 'late-unknown' then
    LateUninstallBlockMessage := 'Fixture late process inspection could not be completed'
  else if Scenario <> 'unregistration-failed' then
    RaiseException('Unexpected fixture removal scenario: ' + Scenario);
end;

@EVENTS@
'@
$fixtureSource = $fixtureSource.Replace('@FIXTURE_ID@', $fixtureId).
    Replace('@TEST_ROOT@', $testRoot).
    Replace('@MESSAGES@', ($messages -join "`r`n")).
    Replace('@PATH_HELPERS@', ($pathHelpers -join "`r`n`r`n")).
    Replace('@EVENTS@', ($events -join "`r`n`r`n"))
$fixtureScript = Join-Path $testRoot 'fixture.iss'
[IO.File]::WriteAllText($fixtureScript, $fixtureSource, $utf8)
Write-Host "Isolated uninstaller fixture retained at: $testRoot"
$compileCode = Invoke-FixtureProcess -Executable $script:iscc -Arguments @('/Qp', $fixtureScript) `
    -LogPrefix (Join-Path $testRoot 'compile')
Assert-True ($compileCode -eq 0) "Fixture compilation failed ($compileCode); inspect $testRoot\compile.stdout.txt"
$setup = Join-Path $testRoot 'FixtureSetup.exe'
Assert-True (Test-Path -LiteralPath $setup -PathType Leaf) 'Compiled fixture installer missing.'
if ($CompileOnly) {
    Write-Host 'Fixture compilation passed. No installation/uninstallation was executed.'
    return
}

$results = [Collections.Generic.List[object]]::new()
foreach ($scenario in @('initial-running', 'initial-unknown', 'unregistration-failed', 'late-running', 'late-unknown', 'success')) {
    $caseRoot = Join-Path $testRoot $scenario
    $appDir = Join-Path $caseRoot 'app'
    Assert-FixturePath $appDir
    New-Item -ItemType Directory -Path $caseRoot | Out-Null
    $setupLog = Join-Path $caseRoot 'setup.log'
    $setupCode = Invoke-FixtureProcess -Executable $setup -Arguments @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/DIR=$appDir", "/LOG=$setupLog"
    ) -LogPrefix (Join-Path $caseRoot 'setup-process')
    Assert-True ($setupCode -eq 0) "Fixture installation failed ($setupCode): $scenario"
    Assert-True ((Get-Content -LiteralPath $setupLog -Raw) -match '14 native pure-path checks passed') `
        "Native path checks were not completed: $scenario"
    Assert-FixturePath $appDir
    $installedPayload = Join-Path $appDir 'payload.txt'
    $uninstaller = Join-Path $appDir 'unins000.exe'
    $uninstallData = Join-Path $appDir 'unins000.dat'
    $scenarioFile = Join-Path $appDir 'fixture-scenario.txt'
    $userFile = Join-Path $appDir 'user-added-file.txt'
    $callMarker = Join-Path $appDir 'mock-removal-called.txt'
    foreach ($required in @($installedPayload, $uninstaller, $uninstallData)) {
        Assert-True (Test-Path -LiteralPath $required -PathType Leaf) "Missing installed fixture file: $required"
    }
    $payloadHash = (Get-FileHash -LiteralPath $installedPayload -Algorithm SHA256).Hash
    $uninstallerHash = (Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash
    $uninstallDataHash = (Get-FileHash -LiteralPath $uninstallData -Algorithm SHA256).Hash
    [IO.File]::WriteAllText($scenarioFile, $scenario, [Text.Encoding]::ASCII)
    [IO.File]::WriteAllText($userFile, "Unlogged user data $fixtureId $scenario", $utf8)
    $userHash = (Get-FileHash -LiteralPath $userFile -Algorithm SHA256).Hash
    $uninstallCode = Invoke-FixtureProcess -Executable $uninstaller -Arguments @(
        '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=$(Join-Path $caseRoot 'uninstall.log')"
    ) -LogPrefix (Join-Path $caseRoot 'uninstall-process')
    Assert-True ((Get-FileHash -LiteralPath $userFile -Algorithm SHA256).Hash -eq $userHash) `
        "Unlogged user data changed: $scenario"
    if ($scenario -eq 'success') {
        Assert-True ($uninstallCode -eq 0) "Successful fixture uninstall returned $uninstallCode."
        # Inno returns the first-phase exit code before its temporary helper has
        # finished deleting the original uninstaller. Wait only for these three
        # fixture-owned paths, with a short upper bound, not arbitrary processes.
        $cleanupWait = [Diagnostics.Stopwatch]::StartNew()
        do {
            $remaining = @($installedPayload, $uninstaller, $uninstallData) |
                Where-Object { Test-Path -LiteralPath $_ }
            if (-not $remaining) { break }
            Start-Sleep -Milliseconds 50
        } while ($cleanupWait.ElapsedMilliseconds -lt 5000)
        foreach ($removed in @($installedPayload, $uninstaller, $uninstallData)) {
            Assert-True (-not (Test-Path -LiteralPath $removed)) "Logged fixture file was not removed: $removed"
        }
    } else {
        Assert-True ($uninstallCode -ne 0) "Blocked fixture uninstall reported success: $scenario"
        Assert-True ((Get-FileHash -LiteralPath $installedPayload -Algorithm SHA256).Hash -eq $payloadHash) `
            "Blocked uninstall changed its payload: $scenario"
        Assert-True ((Get-FileHash -LiteralPath $uninstaller -Algorithm SHA256).Hash -eq $uninstallerHash) `
            "Blocked uninstall changed its uninstaller: $scenario"
        Assert-True ((Get-FileHash -LiteralPath $uninstallData -Algorithm SHA256).Hash -eq $uninstallDataHash) `
            "Blocked uninstall changed its uninstall log: $scenario"
    }
    $expectCall = -not $scenario.StartsWith('initial-', [StringComparison]::Ordinal)
    Assert-True ((Test-Path -LiteralPath $callMarker -PathType Leaf) -eq $expectCall) `
        "Unexpected mock removal invocation: $scenario"
    $results.Add([PSCustomObject]@{ scenario = $scenario; exitCode = $uninstallCode; passed = $true })
    Write-Host "Uninstaller runtime scenario passed: $scenario"
}

$resultPath = Join-Path $testRoot 'results.json'
$result = [PSCustomObject]@{
    fixtureRoot = $testRoot
    installerSourceSha256 = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    checks = $script:checks
    scenarios = $results.ToArray()
    scope = 'Real Inno lifecycle events and pure path helpers; mocked registration/process inspection; inert payload only.'
}
[IO.File]::WriteAllText($resultPath, ($result | ConvertTo-Json -Depth 4), $utf8)
Write-Host "Isolated uninstaller runtime checks passed: $script:checks. Results retained: $resultPath"
