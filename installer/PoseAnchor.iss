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
SetupIconFile={#PackageDir}\PoseAnchor.ico
OutputDir={#OutputDir}
OutputBaseFilename=PoseAnchor-Setup-{#AppVersion}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern dynamic
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0.17763
; Never let Restart Manager close vrserver on the user's behalf. PrepareToInstall
; provides a controlled error before any files or registrations are changed.
CloseApplications=no
RestartApplications=no
SetupLogging=yes
Uninstallable=yes
UninstallDisplayName=PoseAnchor {#AppVersion}
UninstallDisplayIcon={app}\PoseAnchorStatus.exe
VersionInfoVersion={#AppVersionQuad}
VersionInfoProductVersion={#AppVersion}
VersionInfoProductName=PoseAnchor
VersionInfoDescription=PoseAnchor SteamVR Vive Tracker guard
VersionInfoCompany=PoseAnchor contributors
VersionInfoCopyright=Copyright (c) 2026 PoseAnchor contributors
#ifdef EnableSigning
; The named tool is supplied by scripts\build-installer.ps1 on ISCC's command
; line. Inno must perform this signing so the embedded uninstaller is signed too.
SignTool=poseanchorsign
SignedUninstaller=yes
#else
SignedUninstaller=no
#endif

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"; InfoBeforeFile: "info-before-en.txt"
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"; InfoBeforeFile: "info-before-ja.txt"

[CustomMessages]
english.SteamVRNotFound=SteamVR was not found. Install or repair SteamVR, then run PoseAnchor Setup again.
japanese.SteamVRNotFound=SteamVRが見つかりません。SteamVRをインストールまたは修復してから、PoseAnchor Setupをもう一度実行してください。
english.ExistingRegistration=PoseAnchor is currently registered from:%n%1%n%nSetup must replace it with:%n%2%n%nIf registration of the new copy fails, Setup will try to restore the old path. Continue?
japanese.ExistingRegistration=PoseAnchorは現在、次の場所から登録されています:%n%1%n%nセットアップは次の場所へ登録を置き換える必要があります:%n%2%n%n新しいコピーの登録に失敗した場合は、古い場所への復元を試みます。続行しますか？
english.DuplicateRegistration=Multiple old PoseAnchor driver registrations were found. Setup must remove registrations named "pose_anchor" and register this installed copy. Before changing anything, Setup will save an exact backup of the OpenVR registration file and restore it if registration fails. Continue?
japanese.DuplicateRegistration=古いPoseAnchorドライバの登録が複数見つかりました。「pose_anchor」という名前の登録を削除し、今回インストールするコピーを登録する必要があります。変更前にOpenVR登録ファイルをそのまま退避し、登録に失敗した場合は復元します。続行しますか？
english.RegistrationChangeDeclined=No files or driver registrations were changed. Accept the registration replacement to continue, or cancel Setup.
japanese.RegistrationChangeDeclined=ファイルとドライバ登録は変更されていません。登録の置き換えを許可して続行するか、セットアップをキャンセルしてください。
english.RegistrationCheckFailed=Setup could not safely inspect the existing PoseAnchor registration. No files or registrations were changed. See the setup log for details.
japanese.RegistrationCheckFailed=既存のPoseAnchor登録を安全に確認できませんでした。ファイルと登録は変更されていません。詳細はセットアップログを確認してください。
english.RegistrationChangedDuringSetup=The OpenVR driver registration changed while Setup was open. PoseAnchor did not modify the registration. Keep SteamVR closed and run Setup again.
japanese.RegistrationChangedDuringSetup=セットアップを開いている間にOpenVRドライバ登録が変更されました。PoseAnchorは登録を変更していません。SteamVRを終了したまま、セットアップをもう一度実行してください。
english.RegistrationFailed=PoseAnchor files were installed, but the new SteamVR driver registration failed. Any previous registration was restored when possible.%n%nKeep SteamVR closed and run this installer again. See the setup log for details.
japanese.RegistrationFailed=PoseAnchorのファイルはインストールされましたが、新しいSteamVRドライバの登録に失敗しました。以前の登録は可能な範囲で復元しました。%n%nSteamVRを終了したまま、このインストーラーをもう一度実行してください。詳細はセットアップログを確認してください。
english.RegistrationRollbackFailed=PoseAnchor files were installed, but the new registration failed and Setup could not fully restore the previous registration.%n%nDo not start SteamVR yet. See the setup log and repair the registration before continuing.
japanese.RegistrationRollbackFailed=PoseAnchorのファイルはインストールされましたが、新しい登録に失敗し、以前の登録も完全には復元できませんでした。%n%nSteamVRをまだ起動しないでください。セットアップログを確認し、登録を修復してから続行してください。
english.UnregistrationFailed=Setup could not confirm that the PoseAnchor registration was removed from SteamVR. Removing the files now may leave a broken external-driver entry.%n%nChoose No, repair or reinstall SteamVR if needed, and try again. Continue uninstalling anyway?
japanese.UnregistrationFailed=PoseAnchorのSteamVR登録が削除されたことを確認できませんでした。このままファイルを削除すると、壊れた外部ドライバ登録が残る可能性があります。%n%n「いいえ」を選び、必要に応じてSteamVRを修復または再インストールしてから、もう一度お試しください。それでもアンインストールを続行しますか？
english.SteamVRRunningSetup=SteamVR is currently running. Exit SteamVR completely, cancel this Setup, then run it again. Setup will not close SteamVR automatically.
japanese.SteamVRRunningSetup=SteamVRが実行中です。SteamVRを完全に終了し、このセットアップをキャンセルしてから、もう一度実行してください。セットアップがSteamVRを自動終了することはありません。
english.SteamVRRunningUninstall=SteamVR is currently running. Exit SteamVR completely, then run the PoseAnchor uninstaller again.
japanese.SteamVRRunningUninstall=SteamVRが実行中です。SteamVRを完全に終了してから、PoseAnchorのアンインストーラーをもう一度実行してください。
english.StatusRunningSetup=PoseAnchor Status is currently open. Close it, cancel this Setup, then run Setup again. Setup will not close it automatically.
japanese.StatusRunningSetup=PoseAnchor ステータスが開いています。画面を閉じ、このセットアップをキャンセルしてから、もう一度実行してください。セットアップが自動終了することはありません。
english.StatusRunningUninstall=PoseAnchor Status is currently open. Close it, then run the PoseAnchor uninstaller again.
japanese.StatusRunningUninstall=PoseAnchor ステータスが開いています。画面を閉じてから、PoseAnchorのアンインストーラーをもう一度実行してください。
english.FinishedSuccess=PoseAnchor has been installed and registered as a SteamVR driver. It loads automatically when you start SteamVR.%n%nStart with one tracker in a safe area. Click Finish to exit Setup.
japanese.FinishedSuccess=PoseAnchorをインストールし、SteamVRドライバとして登録しました。SteamVRを起動すると自動的に読み込まれます。%n%n最初は安全な場所でTracker 1個から試してください。「完了」をクリックしてセットアップを終了します。
english.FinishedFailure=PoseAnchor files were installed, but the SteamVR driver is not ready. Keep SteamVR closed and follow the error message before using PoseAnchor.%n%nClick Finish to exit Setup.
japanese.FinishedFailure=PoseAnchorのファイルはインストールされましたが、SteamVRドライバを使用できる状態ではありません。SteamVRを終了したまま、エラーメッセージに従って修復してください。%n%n「完了」をクリックしてセットアップを終了します。
english.OpenReadme=Open the PoseAnchor README
japanese.OpenReadme=PoseAnchor READMEを開く
english.StatusShortcut=PoseAnchor Status
japanese.StatusShortcut=PoseAnchor ステータス

[Files]
Source: "{#PackageDir}\driver.vrdrivermanifest"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\bin\win64\driver_pose_anchor.dll"; DestDir: "{app}\bin\win64"; Flags: ignoreversion
Source: "{#PackageDir}\PoseAnchorStatus.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\resources\settings\default.vrsettings"; DestDir: "{app}\resources\settings"; Flags: ignoreversion
Source: "{#PackageDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PackageDir}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{cm:StatusShortcut}"; Filename: "{app}\PoseAnchorStatus.exe"
Name: "{group}\PoseAnchor README"; Filename: "{sys}\notepad.exe"; Parameters: """{app}\README.md"""
Name: "{group}\{cm:UninstallProgram,PoseAnchor}"; Filename: "{uninstallexe}"

[Run]
Filename: "{sys}\notepad.exe"; Parameters: """{app}\README.md"""; Description: "{cm:OpenReadme}"; Flags: postinstall skipifsilent nowait

[InstallDelete]
; Development packages used to contain command-line helper scripts. They are not
; part of the end-user installation.
Type: files; Name: "{app}\scripts\install.ps1"
Type: files; Name: "{app}\scripts\uninstall.ps1"
Type: files; Name: "{app}\scripts\steamvr-path.ps1"
Type: dirifempty; Name: "{app}\scripts"
; Remove known localized shortcut names before writing the active language's
; shortcuts. This prevents duplicates when an update changes installer language.
Type: files; Name: "{group}\PoseAnchor Status.lnk"
Type: files; Name: "{group}\PoseAnchor ステータス.lnk"
Type: files; Name: "{group}\Uninstall PoseAnchor.lnk"
Type: files; Name: "{group}\PoseAnchor をアンインストールする.lnk"

[Code]
const
  SteamVrUninstallKey = 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App 250820';
  PoseAnchorDriverName = '{#DriverName}';
  CleanupNone = 0;
  CleanupOne = 1;
  CleanupAll = 2;
  MoveFileReplaceExisting = $1;
  MoveFileWriteThrough = $8;

var
  SteamVrRoot: String;
  VrPathRegPath: String;
  DriverRegistrationFailed: Boolean;
  DriverRegistrationRollbackFailed: Boolean;
  DriverUnregistrationAttempted: Boolean;
  LateSetupBlockMessage: String;
  LateUninstallBlockMessage: String;
  RegistrationCleanupMode: Integer;
  RegistrationCleanupPaths: TArrayOfString;
  RegistrationBackupPath: String;
  RegistrationBackupHash: String;
  RegistrationBackupReady: Boolean;

function MoveFileExW(const ExistingFileName, NewFileName: String;
  Flags: Cardinal): Boolean;
  external 'MoveFileExW@kernel32.dll stdcall';

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

procedure ResetRegistrationPlan;
begin
  if (RegistrationBackupPath <> '') and FileExists(RegistrationBackupPath) then
    DeleteFile(RegistrationBackupPath);
  RegistrationBackupPath := '';
  RegistrationBackupHash := '';
  RegistrationBackupReady := False;
  RegistrationCleanupMode := CleanupNone;
  SetArrayLength(RegistrationCleanupPaths, 0);
end;

function CreateRegistrationBackup: Boolean;
var
  RegistryPath: String;
  BackupHash: String;
begin
  Result := False;
  RegistryPath := ExpandConstant('{localappdata}\openvr\openvrpaths.vrpath');
  if not FileExists(RegistryPath) then
  begin
    Log('PoseAnchor: cannot back up OpenVR registrations because openvrpaths.vrpath is missing');
    Exit;
  end;

  // {tmp} is a per-Setup working directory and is removed automatically.
  RegistrationBackupPath := AddBackslash(ExpandConstant('{tmp}')) +
    'poseanchor-openvrpaths-backup.vrpath';
  if FileExists(RegistrationBackupPath) then
    DeleteFile(RegistrationBackupPath);

  RegistrationBackupHash := GetSHA256OfFile(RegistryPath);
  if (RegistrationBackupHash = '') or
     (not CopyFile(RegistryPath, RegistrationBackupPath, False)) then
  begin
    Log('PoseAnchor: could not copy the OpenVR registration backup');
    Exit;
  end;

  BackupHash := GetSHA256OfFile(RegistrationBackupPath);
  RegistrationBackupReady := CompareText(
    RegistrationBackupHash, BackupHash) = 0;
  if not RegistrationBackupReady then
  begin
    Log('PoseAnchor: OpenVR registration backup hash verification failed');
    Exit;
  end;

  Log('PoseAnchor: saved and verified an exact OpenVR registration backup');
  Result := True;
end;

function RestoreRegistrationBackup: Boolean;
var
  RegistryPath: String;
  RestoreStagePath: String;
  BackupHash: String;
  StageHash: String;
  RestoredHash: String;
begin
  Result := False;
  if (not RegistrationBackupReady) or
     (not FileExists(RegistrationBackupPath)) then
    Exit;

  // Validate the rollback source before it can overwrite the live registry.
  BackupHash := GetSHA256OfFile(RegistrationBackupPath);
  if (BackupHash = '') or
     (CompareText(RegistrationBackupHash, BackupHash) <> 0) then
  begin
    Log('PoseAnchor: refusing to restore a damaged registration backup');
    Exit;
  end;

  RegistryPath := ExpandConstant('{localappdata}\openvr\openvrpaths.vrpath');
  RestoreStagePath := RegistryPath + '.poseanchor-restore.tmp';
  if FileExists(RestoreStagePath) and (not DeleteFile(RestoreStagePath)) then
  begin
    Log('PoseAnchor: could not remove a stale registration restore stage');
    Exit;
  end;

  // Never stream-copy over the live file: an I/O failure could truncate it.
  // Stage and verify on the same volume, then atomically replace with write-through.
  if not CopyFile(RegistrationBackupPath, RestoreStagePath, False) then
  begin
    Log('PoseAnchor: could not stage the exact registration backup');
    DeleteFile(RestoreStagePath);
    Exit;
  end;
  StageHash := GetSHA256OfFile(RestoreStagePath);
  if (StageHash = '') or
     (CompareText(RegistrationBackupHash, StageHash) <> 0) then
  begin
    Log('PoseAnchor: staged registration backup failed hash verification');
    DeleteFile(RestoreStagePath);
    Exit;
  end;
  if not MoveFileExW(RestoreStagePath, RegistryPath,
    MoveFileReplaceExisting or MoveFileWriteThrough) then
  begin
    Log('PoseAnchor: atomic OpenVR registration backup restore failed');
    DeleteFile(RestoreStagePath);
    Exit;
  end;

  RestoredHash := GetSHA256OfFile(RegistryPath);
  Result := (RestoredHash <> '') and
    (CompareText(RegistrationBackupHash, RestoredHash) = 0);
  if Result then
    Log('PoseAnchor: exact OpenVR registration backup restored and verified')
  else
    Log('PoseAnchor: restored OpenVR registration file failed hash verification');
end;

function RegistrationBackupStillCurrent: Boolean;
var
  RegistryPath: String;
  BackupHash: String;
  CurrentHash: String;
begin
  Result := not RegistrationBackupReady;
  if Result then
    Exit;

  if not FileExists(RegistrationBackupPath) then
  begin
    Log('PoseAnchor: registration backup disappeared before mutation');
    Exit;
  end;
  BackupHash := GetSHA256OfFile(RegistrationBackupPath);
  if (BackupHash = '') or
     (CompareText(RegistrationBackupHash, BackupHash) <> 0) then
  begin
    Log('PoseAnchor: registration backup changed before mutation');
    Exit;
  end;

  RegistryPath := ExpandConstant('{localappdata}\openvr\openvrpaths.vrpath');
  if not FileExists(RegistryPath) then
    Exit;
  CurrentHash := GetSHA256OfFile(RegistryPath);
  Result := (CurrentHash <> '') and
    (CompareText(RegistrationBackupHash, CurrentHash) = 0);
  if not Result then
    Log('PoseAnchor: OpenVR registration changed after the pre-install backup');
end;

procedure AddRegistrationPath(const Path: String);
var
  Count: Integer;
begin
  if Trim(Path) = '' then
    Exit;
  Count := GetArrayLength(RegistrationCleanupPaths);
  SetArrayLength(RegistrationCleanupPaths, Count + 1);
  RegistrationCleanupPaths[Count] := CleanPath(Path);
end;

procedure CaptureRegistrationPaths(const Output: TExecOutput);
var
  I: Integer;
begin
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do
    if Trim(Output.StdOut[I]) <> '' then
      AddRegistrationPath(Output.StdOut[I]);
end;

function OutputContainsPath(const Lines: TArrayOfString;
  const Path: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(Lines) - 1 do
    if SamePath(Lines[I], Path) then
    begin
      Result := True;
      Exit;
    end;
end;

function TryRegistrationPathPresent(const Path: String;
  var Present: Boolean): Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
begin
  Present := False;
  Result := False;
  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
    Exit;
  if ResultCode = 1 then
  begin
    Result := True;
    Exit;
  end;
  if ResultCode = 0 then
  begin
    Present := OutputContainsPath(Output.StdOut, Path);
    Result := True;
  end;
  if ResultCode = 2 then
  begin
    // Duplicate stdout can prove presence when the exact path is shown, but
    // omission cannot prove absence because completeness is undocumented.
    Present := OutputContainsPath(Output.StdOut, Path);
    Result := Present;
  end;
end;

function PrepareRegistrationPlan: String;
var
  ResultCode: Integer;
  Output: TExecOutput;
  ExistingPath: String;
  AppPath: String;
begin
  Result := CustomMessage('RegistrationCheckFailed');
  ResetRegistrationPlan;
  AppPath := CleanPath(ExpandConstant('{app}'));

  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
    Exit;

  if ResultCode = 1 then
  begin
    Result := '';
    Exit;
  end
  else if ResultCode = 0 then
  begin
    ExistingPath := FirstNonEmptyLine(Output.StdOut);
    if ExistingPath = '' then
      Exit;
    if SamePath(ExistingPath, AppPath) then
    begin
      Result := '';
      Exit;
    end;

    if SuppressibleMsgBox(FmtMessage(CustomMessage('ExistingRegistration'), [ExistingPath,
      AppPath]), mbConfirmation, MB_YESNO, IDNO) <> IDYES then
    begin
      Result := CustomMessage('RegistrationChangeDeclined');
      Exit;
    end;
    RegistrationCleanupMode := CleanupOne;
    AddRegistrationPath(ExistingPath);
  end
  else if ResultCode = 2 then
  begin
    // vrpathreg's duplicate-result output is useful for best-effort repair, but
    // it is not a documented complete list. The exact registry-file backup is
    // the authoritative rollback source.
    CaptureRegistrationPaths(Output);
    if SuppressibleMsgBox(CustomMessage('DuplicateRegistration'), mbConfirmation,
       MB_YESNO, IDNO) <> IDYES then
    begin
      ResetRegistrationPlan;
      Result := CustomMessage('RegistrationChangeDeclined');
      Exit;
    end;
    RegistrationCleanupMode := CleanupAll;
  end
  else
    Exit;

  if not CreateRegistrationBackup then
  begin
    ResetRegistrationPlan;
    Exit;
  end;

  Result := '';
end;

procedure RollBackRegistrationPlan;
var
  I: Integer;
  ResultCode: Integer;
  Output: TExecOutput;
  AppPath: String;
  Present: Boolean;
  Restored: Boolean;
begin
  DriverRegistrationRollbackFailed := False;
  AppPath := CleanPath(ExpandConstant('{app}'));

  // For replacement and duplicate cleanup, restore the byte-exact registry
  // file first. This also preserves registrations that vrpathreg did not list.
  if RegistrationBackupReady then
  begin
    if RestoreRegistrationBackup then
      Exit;
    DriverRegistrationRollbackFailed := True;
  end;

  // Remove a partially-added new path, then verify final absence. Command exit
  // codes alone do not prove that SteamVR's path registry actually changed.
  RunVrPathReg('removedriver ' + AddQuotes(AppPath), ResultCode, Output);
  if (not TryRegistrationPathPresent(AppPath, Present)) or Present then
    DriverRegistrationRollbackFailed := True;

  for I := 0 to GetArrayLength(RegistrationCleanupPaths) - 1 do
  begin
    Restored := TryRegistrationPathPresent(
      RegistrationCleanupPaths[I], Present) and Present;
    if not Restored then
    begin
      RunVrPathReg('adddriver ' + AddQuotes(RegistrationCleanupPaths[I]),
        ResultCode, Output);
      Restored := TryRegistrationPathPresent(
        RegistrationCleanupPaths[I], Present) and Present;
    end;
    if not Restored then
      DriverRegistrationRollbackFailed := True;
  end;
end;

// tasklist is available without elevation and is more reliable here than
// WMI/COM. If process inspection itself fails, fail closed.
function IsImageRunning(const ImageName: String): Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
  I: Integer;
begin
  Result := True;
  if not ExecAndCaptureOutput(ExpandConstant('{sys}\tasklist.exe'),
    '/FI "IMAGENAME eq ' + ImageName + '" /NH', '', SW_HIDE,
    ewWaitUntilTerminated, ResultCode, Output) then
  begin
    Log('PoseAnchor: tasklist could not be launched; assuming ' +
      ImageName + ' is running');
    Exit;
  end;
  Log(Format('PoseAnchor: tasklist %s check exited %d', [ImageName, ResultCode]));
  LogCapturedOutput('PoseAnchor: tasklist', Output);
  if ResultCode <> 0 then
  begin
    Log('PoseAnchor: tasklist failed; assuming ' + ImageName + ' is running');
    Exit;
  end;

  Result := False;
  for I := 0 to GetArrayLength(Output.StdOut) - 1 do
    if Pos(Lowercase(ImageName), Lowercase(Output.StdOut[I])) > 0 then
    begin
      Result := True;
      Exit;
    end;
end;

// Registering or unregistering while vrserver is running does not take effect
// until the next restart, and an open Status executable can block replacement.
function IsSteamVrRunning: Boolean;
begin
  Result := IsImageRunning('vrserver.exe');
end;

function IsStatusRunning: Boolean;
begin
  Result := IsImageRunning('PoseAnchorStatus.exe');
end;

function AddInstalledDriver: Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
  ExistingPath: String;
  AppPath: String;
begin
  Result := False;
  LateSetupBlockMessage := '';
  AppPath := CleanPath(ExpandConstant('{app}'));
  if not LocateSteamVR then
    Exit;
  // Recheck immediately before touching the registry. Either process can be
  // launched while the wizard is waiting after PrepareToInstall.
  if IsStatusRunning then
  begin
    LateSetupBlockMessage := CustomMessage('StatusRunningSetup');
    Exit;
  end;
  if IsSteamVrRunning then
  begin
    LateSetupBlockMessage := CustomMessage('SteamVRRunningSetup');
    Exit;
  end;
  if not RegistrationBackupStillCurrent then
  begin
    LateSetupBlockMessage := CustomMessage('RegistrationChangedDuringSetup');
    ResetRegistrationPlan;
    Exit;
  end;

  if RegistrationCleanupMode = CleanupOne then
  begin
    if (not RunVrPathReg('removedriver ' +
        AddQuotes(RegistrationCleanupPaths[0]), ResultCode, Output)) or
       (ResultCode <> 0) then
    begin
      RollBackRegistrationPlan;
      Exit;
    end;
  end
  else if RegistrationCleanupMode = CleanupAll then
  begin
    if (not RunVrPathReg('removedriverswithname ' + PoseAnchorDriverName,
        ResultCode, Output)) or (ResultCode <> 0) then
    begin
      RollBackRegistrationPlan;
      Exit;
    end;
  end
  else
  begin
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
      LateSetupBlockMessage := CustomMessage('RegistrationChangedDuringSetup');
      Exit;
    end
    else if ResultCode <> 1 then
    begin
      LateSetupBlockMessage := CustomMessage('RegistrationChangedDuringSetup');
      Exit;
    end;
  end;

  if (not RunVrPathReg('adddriver ' + AddQuotes(AppPath), ResultCode, Output)) or
     (ResultCode <> 0) then
  begin
    RollBackRegistrationPlan;
    Exit;
  end;

  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
  begin
    RollBackRegistrationPlan;
    Exit;
  end;
  ExistingPath := FirstNonEmptyLine(Output.StdOut);
  Result := (ResultCode = 0) and SamePath(ExistingPath, AppPath);
  if not Result then
    RollBackRegistrationPlan
  else
    ResetRegistrationPlan;
end;

function RemoveInstalledDriver: Boolean;
var
  ResultCode: Integer;
  Output: TExecOutput;
  AppPath: String;
  ExistingPath: String;
  OpenVrPathsFile: String;
begin
  Result := False;
  LateUninstallBlockMessage := '';
  AppPath := CleanPath(ExpandConstant('{app}'));
  // Recheck at the actual mutation point to close the wizard-page TOCTOU gap.
  if IsStatusRunning then
  begin
    LateUninstallBlockMessage := CustomMessage('StatusRunningUninstall');
    Exit;
  end;
  if IsSteamVrRunning then
  begin
    LateUninstallBlockMessage := CustomMessage('SteamVRRunningUninstall');
    Exit;
  end;

  if not LocateSteamVR then
  begin
    // SteamVR can be removed while openvrpaths.vrpath still retains external
    // drivers. Without vrpathreg, do not guess from substrings or malformed JSON:
    // fail closed and let the user explicitly decide whether to keep the files.
    OpenVrPathsFile := ExpandConstant('{localappdata}\openvr\openvrpaths.vrpath');
    if not FileExists(OpenVrPathsFile) then
    begin
      Log('PoseAnchor: SteamVR and openvrpaths.vrpath were not found.');
      Result := True;
      Exit;
    end;

    Log('PoseAnchor: SteamVR not found; refusing to infer registration state from openvrpaths.vrpath.');
    Exit;
  end;

  Result := RunVrPathReg('removedriver ' + AddQuotes(AppPath), ResultCode, Output) and
    (ResultCode = 0);
  if not Result then
    Exit;

  // Exit code 0 only confirms that vrpathreg accepted the command. Verify that
  // finddriver no longer reports this exact path; other PoseAnchor paths may remain.
  if not RunVrPathReg('finddriver ' + PoseAnchorDriverName, ResultCode, Output) then
  begin
    Result := False;
    Exit;
  end;
  if ResultCode = 1 then
    Result := True
  else if ResultCode = 0 then
  begin
    // A single result is complete; duplicate-result stdout is not documented
    // as complete, so code 2 below must remain fail-closed.
    ExistingPath := FirstNonEmptyLine(Output.StdOut);
    Result := (ExistingPath <> '') and (not SamePath(ExistingPath, AppPath));
  end
  else
    Result := False;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  if not LocateSteamVR then
    Result := CustomMessage('SteamVRNotFound')
  else if IsSteamVrRunning then
    Result := CustomMessage('SteamVRRunningSetup')
  else if IsStatusRunning then
    Result := CustomMessage('StatusRunningSetup')
  else
    Result := PrepareRegistrationPlan;
end;

function InitializeUninstall: Boolean;
begin
  Result := True;
  if IsSteamVrRunning then
  begin
    SuppressibleMsgBox(CustomMessage('SteamVRRunningUninstall'), mbError, MB_OK, IDOK);
    Result := False;
  end;
  if Result and IsStatusRunning then
  begin
    SuppressibleMsgBox(CustomMessage('StatusRunningUninstall'), mbError, MB_OK, IDOK);
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
      if LateSetupBlockMessage <> '' then
        SuppressibleMsgBox(LateSetupBlockMessage, mbError, MB_OK, IDOK)
      else if DriverRegistrationRollbackFailed then
        SuppressibleMsgBox(CustomMessage('RegistrationRollbackFailed'), mbError,
          MB_OK, IDOK)
      else
        SuppressibleMsgBox(CustomMessage('RegistrationFailed'), mbError, MB_OK, IDOK);
    end;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID = wpFinished then
  begin
    if DriverRegistrationFailed then
      WizardForm.FinishedLabel.Caption := CustomMessage('FinishedFailure')
    else
      WizardForm.FinishedLabel.Caption := CustomMessage('FinishedSuccess');
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
      if LateUninstallBlockMessage <> '' then
      begin
        SuppressibleMsgBox(LateUninstallBlockMessage, mbError, MB_OK, IDOK);
        Abort;
      end
      else
      // Retaining the files is safer than leaving SteamVR pointed at a deleted DLL.
      // Suppressed/unattended removal therefore also defaults to not continuing.
      if SuppressibleMsgBox(CustomMessage('UnregistrationFailed'),
         mbConfirmation, MB_YESNO, IDNO) <> IDYES then
        Abort;
  end;
end;
