; ──────────────────────────────────────────────────────────────
; RDM_X  –  Inno Setup Installer Script
; ──────────────────────────────────────────────────────────────
; Requires Inno Setup 6+   https://jrsoftware.org/isinfo.php
; Compile: iscc installer\RDM_X_Setup.iss
; ──────────────────────────────────────────────────────────────

#define MyAppName      "RDM_X"
#define MyAppVersion   "1.3.3"
#define MyAppPublisher "CK"
#define MyAppExeName   "RDM_X.exe"

; Paths are relative to this .iss file's location
#define PublishDir     "..\wpf\bin\Release\net8.0-windows\publish"

[Setup]
AppId={{E8A5C3F7-4B2D-4E9A-A1F0-3C7D8E2B9F6A}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\installer\output
OutputBaseFilename=RDM_X_Setup_{#MyAppVersion}
SetupIconFile=..\docs\pro_example_v2\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x86compatible
PrivilegesRequired=admin

; Branding
AppContact=support@ck.com
AppSupportURL=https://github.com/ck
LicenseFile=
InfoBeforeFile=

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main application files from dotnet publish output
Source: "{#PublishDir}\RDM_X.exe";                   DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\RDM_X.dll";                   DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\RDM_X.deps.json";             DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\RDM_X.runtimeconfig.json";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\rdm_x_core.dll";              DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\CommunityToolkit.Mvvm.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\Vaya_RDM_map.csv";            DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\vusbdmx.dll";                 DestDir: "{app}"; Flags: ignoreversion
Source: "{#PublishDir}\ftd2xx.dll";                  DestDir: "{app}"; Flags: ignoreversion
Source: "..\thirdparty\peperoni\driver\*";           DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}";     Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\driver\dpinst64.exe"; Parameters: "/q /sa /se"; Flags: waituntilterminated; Check: IsWin64; StatusMsg: "Installing Peperoni USB DMX driver..."
Filename: "{app}\driver\dpinst32.exe"; Parameters: "/q /sa /se"; Flags: waituntilterminated; Check: not IsWin64; StatusMsg: "Installing Peperoni USB DMX driver..."
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
{ .NET 8 Desktop Runtime detection }
function IsDotNet8DesktopInstalled: Boolean;
var
  ResultCode: Integer;
begin
  Result := False;
  if Exec('dotnet', '--list-runtimes', '', SW_HIDE, ewWaitUntilTerminated, ResultCode) then
  begin
    Result := RegKeyExists(HKLM,
      'SOFTWARE\dotnet\Setup\InstalledVersions\x86\sharedfx\Microsoft.WindowsDesktop.App');
  end;
end;

function InitializeSetup: Boolean;
var
  Msg: String;
begin
  Result := True;
  if not IsDotNet8DesktopInstalled then
  begin
    Msg := '.NET 8 Desktop Runtime (x86) does not appear to be installed.' + Chr(13) + Chr(10) +
           Chr(13) + Chr(10) +
           'RDM_X requires the .NET 8 Desktop Runtime (x86) to run.' + Chr(13) + Chr(10) +
           'You can download it from:' + Chr(13) + Chr(10) +
           'https://dotnet.microsoft.com/download/dotnet/8.0' + Chr(13) + Chr(10) +
           Chr(13) + Chr(10) +
           'Do you want to continue the installation anyway?';
    if MsgBox(Msg, mbConfirmation, MB_YESNO) = IDNO then
    begin
      Result := False;
    end;
  end;
end;

