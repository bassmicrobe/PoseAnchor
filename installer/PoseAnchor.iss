#ifndef PackageDir
  #define PackageDir "..\build\pose_anchor"
#endif
#ifndef OutputDir
  #define OutputDir "..\dist"
#endif
#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef AppVersionQuad
  #define AppVersionQuad "0.1.0.0"
#endif

#define AppName "PoseAnchor"
#define DriverName "pose_anchor"
#define ProjectUrl "https://github.com/bassmicrobe/PoseAnchor"

[Setup]
AppId={{2FC9BAA3-FE14-4C99-8C93-DC7F71157A7A}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=PoseAnchor contributors
AppPublisherURL={#ProjectUrl}
AppSupportURL={#ProjectUrl}/issues
AppUpdatesURL={#ProjectUrl}/releases
DefaultDirName={localappdata}\Programs\PoseAnchor
DefaultGroupName=PoseAnchor
DisableProgramGroupPage=yes
LicenseFile={#PackageDir}\LICENSE
OutputDir={#OutputDir}
OutputBaseFilename=PoseAnchor-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dynamic
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
CloseApplications=yes
RestartApplications=no
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName=PoseAnchor {#AppVersion}
UninstallDisplayIcon={uninstallexe}
VersionInfoVersion={#AppVersionQuad}
VersionInfoProductVersion={#AppVersion}
VersionInfoProductName=PoseAnchor
VersionInfoDescription=PoseAnchor SteamVR Vive Tracker guard
VersionInfoCompany=PoseAnchor contributors
VersionInfoCopyright=Copyright (c) 2026 PoseAnchor contributors

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"

[CustomMessages]
english.SteamVRNotFound=SteamVR was not found. Install or repair SteamVR, then run PoseAnchor Setup again.
japanese.SteamVRNotFound=SteamVRが見つかりません。SteamVRをインストールまたは修復してから、PoseAnchor Setupをもう一度実行してください。
english.RegistrationFailed=PoseAnchor files were installed, but SteamVR driver registration failed.%n%nClose SteamVR and run this installer again to repair the installation. See the setup log for details.
japanese.RegistrationFailed=PoseAnchorのファイルはインストールされましたが、SteamVRドライバの登録に失敗しました。%n%nSteamVRを終了して、このインストーラーをもう一度実行してください。詳細はセットアップログに記録されています。
english.DuplicateRegistration=Multiple old PoseAnchor driver registrations were found. Setup will remove only registrations named "pose_anchor" and register this installed copy. Continue?
japanese.DuplicateRegistration=古いPoseAnchorドライバの登録が複数見つかりました。「pose_anchor」という名前の登録だけを削除し、今回インストールするコピーを登録します。続行しますか？
english.UnregistrationFailed=PoseAnchor could not be unregistered from SteamVR. The files can still be removed, but a stale driver entry may remain. Continue uninstalling?
japanese.UnregistrationFailed=PoseAnchorをSteamVRから登録解除できませんでした。ファイルは削除できますが、古いドライバ登録が残る可能性があります。アンインストールを続行しますか？
english.SteamVRRunning=SteamVR is currently running. Exit SteamVR completely, then run Setup again.
japanese.SteamVRRunning=SteamVRが実行中です。SteamVRを完全に終了してから、もう一度実行してください。

[Files]
Source: "{#PackageDir}\driver.vrdrivermanifest"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\bin\win64\driver_pose_anchor.dll"; DestDir: "{app}\bin\win64"; Flags: ignoreversion
Source: "{#PackageDir}\resources\settings\default.vrsettings"; DestDir: "{app}\resources\settings"; Flags: ignoreversion
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PoseAnchor README"; Filename: "{app}\README.md"
Name: "{group}\Uninstall PoseAnchor"; Filename: "{uninstallexe}"

[InstallDelete]
; Development packages used to contain command-line helper scripts. They are not
; part of the end-user installation.
Type: files; Name: "{app}\scripts\install.ps1"
Type: files; Name: "{app}\scripts\uninstall.ps1"
Type: files; Name: "{app}\scripts\steamvr-path.ps1"
Type: dirifempty; Name: "{app}\scripts"

[Code]
const
  SteamVrUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820';
  PoseAnchorDriverName = '{#DriverName}';

var
  SteamVrRoot: String;
  VrPathRegPath: String;
  DriverRegistrationFailed: Boolean;
  DriverUnregistrationAttempted: Boolean;

function CleanPath(Value: String): String;
begin
  Value := Trim(Value);
  if (Length(Value) >= 2) and (Value[1] = '"') and
     (Value[Length(Value)] = '"') then
  begin
    Delete(Value, Length(Value), 1);
    Delete(Value, 1, 1);
  end;
  Result := RemoveBackslashUnlessRoot(Value);
end;

function TrySteamVrRoot(Candidate: String): Boolean;
var
  Tool: String;
begin
  Candidate := CleanPath(Candidate);
  Tool := AddBackslash(Candidate) + 'bin\win64\vrpathreg.exe';
  Result := (Candidate <> '') and FileExists(Tool);
  if Result then
  begin
    SteamVrRoot := Candidate;
    VrPathRegPath := Tool;
    Log('PoseAnchor: SteamVR found at ' + SteamVrRoot);
  end;
end;

function TryRegistrySteamVr(RootKey: Integer): Boolean;
var
  Candidate: String;
begin
  Result := False;
  if RegQueryStringValue(RootKey, SteamVrUninstallKey, 'InstallLocation', Candidate) then
    Result := TrySteamVrRoot(Candidate);
end;

function TrySteamInstallRoot(ValueName: String): Boolean;
var
  SteamRoot: String;
begin
  Result := False;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', ValueName, SteamRoot) then
    Result := TrySteamVrRoot(AddBackslash(CleanPath(SteamRoot)) +
      'steamapps\common\SteamVR');
end;

function LocateSteamVR: Boolean;
begin
  if FileExists(VrPathRegPath) then
  begin
    Result := True;
    Exit;
  end;

  SteamVrRoot := '';
  VrPathRegPath := '';
  Result := TryRegistrySteamVr(HKLM32) or
            TryRegistrySteamVr(HKLM64) or
            TryRegistrySteamVr(HKCU32) or
            TryRegistrySteamVr(HKCU64) or
            TrySteamInstallRoot('SteamPath') or
            TrySteamInstallRoot('InstallPath') or
            TrySteamVrRoot(ExpandConstant('{pf32}\Steam\steamapps\common\SteamVR'));
end;

function FirstNonEmptyLine(const Lines: TArrayOfString): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    if Trim(Lines[I]) <> '' then
    begin
      Result := Trim(Lines[I]);
      Exit;
    end;
  end;
end;

procedure LogCapturedOutput(const Prefix: String; const Output: TExecOutput);
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do
    if Trim(Output.StdOut[I]) <> '' then
      Log(Prefix + ' stdout: ' + Output.StdOut[I]);
  for I := 0 to GetArrayLength(Output.StdErr) - 1 do
    if Trim(Output.StdErr[I]) <> '' then
      Log(Prefix + ' stderr: ' + Output.StdErr[I]);
end;

function RunVrPathReg(const Parameters: String; var ResultCode: Integer;
  var Output: TExecOutput): Boolean;
begin
  ResultCode := -9999;
  Result := ExecAndCaptureOutput(VrPathRegPath, Parameters, SteamVrRoot,
    SW_HIDE, ewWaitUntilTerminated, ResultCode, Output);
  Log(Format('PoseAnchor: vrpathreg %s exited %d (launch=%d)', [Parameters, ResultCode, Ord(Result)]));
  if Result then
    LogCapturedOutput('PoseAnchor: vrpathreg', Output);
end;

function SamePath(Left, Right: String): Boolean;
begin
  Result := CompareText(CleanPath(Left), CleanPath(Right)) = 0;
end;

function AddInstalledDriver: Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
  ExistingPath: String;
  AppPath: String;
begin
  Result := False;
  AppPath := CleanPath(ExpandConstant('{app}'));
  if not LocateSteamVR then
    Exit;

  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
    Exit;

  if ResultCode = 0 then
  begin
    ExistingPath := FirstNonEmptyLine(Output.StdOut);
    if SamePath(ExistingPath, AppPath) then
    begin
      Result := True;
      Exit;
    end;
    if (ExistingPath = '') or
       (not RunVrPathReg('removedriver ' + AddQuotes(ExistingPath), ResultCode, Output)) or
       (ResultCode <> 0) then
      Exit;
  end
  else if ResultCode = 2 then
  begin
    if SuppressibleMsgBox(CustomMessage('DuplicateRegistration'), mbConfirmation,
      MB_YESNO, IDYES) <> IDYES then
      Exit;
    if (not RunVrPathReg('removedriverswithname ' + PoseAnchorDriverName,
        ResultCode, Output)) or (ResultCode <> 0) then
      Exit;
  end
  else if ResultCode <> 1 then
    Exit;

  if (not RunVrPathReg('adddriver ' + AddQuotes(AppPath), ResultCode, Output)) or
     (ResultCode <> 0) then
    Exit;

  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
    Exit;
  ExistingPath := FirstNonEmptyLine(Output.StdOut);
  Result := (ResultCode = 0) and SamePath(ExistingPath, AppPath);
end;

// Detected via WMI so it works with PrivilegesRequired=lowest. Registering or
// unregistering a driver while vrserver is running silently takes effect only
// after the next SteamVR restart, which reads as "the installer did nothing".
function IsSteamVrRunning: Boolean;
var
  WbemLocator, WbemServices, Processes: Variant;
begin
  Result := False;
  try
    WbemLocator := CreateOleObject('WbemScripting.SWbemLocator');
    WbemServices := WbemLocator.ConnectServer('.', 'root\CIMV2');
    Processes := WbemServices.ExecQuery(
      'SELECT ProcessId FROM Win32_Process WHERE Name=''vrserver.exe''');
    Result := Processes.Count > 0;
  except
    Log('PoseAnchor: WMI vrserver check failed; assuming SteamVR is not running');
  end;
end;

function RemoveInstalledDriver: Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
begin
  if not LocateSteamVR then
  begin
    // SteamVR already uninstalled: there is no registration left to remove, and
    // failing here would block or abort the uninstall for no benefit.
    Log('PoseAnchor: SteamVR not found during uninstall; skipping driver unregistration.');
    Result := True;
    Exit;
  end;
  Result := RunVrPathReg('removedriver ' + AddQuotes(CleanPath(ExpandConstant('{app}'))),
    ResultCode, Output) and (ResultCode = 0);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  if not LocateSteamVR then
    Result := CustomMessage('SteamVRNotFound')
  else if IsSteamVrRunning then
    Result := CustomMessage('SteamVRRunning')
  else
    Result := '';
end;

function InitializeUninstall: Boolean;
begin
  Result := True;
  if IsSteamVrRunning then
  begin
    SuppressibleMsgBox(CustomMessage('SteamVRRunning'), mbError, MB_OK, IDOK);
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if not AddInstalledDriver then
    begin
      DriverRegistrationFailed := True;
      SuppressibleMsgBox(CustomMessage('RegistrationFailed'), mbError, MB_OK, IDOK);
    end;
  end;
end;

function GetCustomSetupExitCode: Integer;
begin
  if DriverRegistrationFailed then
    Result := 10
  else
    Result := 0;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and
     (not DriverUnregistrationAttempted) then
  begin
    DriverUnregistrationAttempted := True;
    if not RemoveInstalledDriver then
      // Default to continuing: the message already frames removal as safe, and a
      // suppressed/unattended uninstall must not abort over a stale registration.
      if SuppressibleMsgBox(CustomMessage('UnregistrationFailed'),
         mbConfirmation, MB_YESNO, IDYES) <> IDYES then
        Abort;
  end;
end;
