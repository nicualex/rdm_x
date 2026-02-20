; ──────────────────────────────────────────────────────────────
; RDM_X  –  Inno Setup Installer Script
; ──────────────────────────────────────────────────────────────
; Requires Inno Setup 6+   https://jrsoftware.org/isinfo.php
; Compile: iscc installer\RDM_X_Setup.iss
; ──────────────────────────────────────────────────────────────

#define MyAppName      "RDM_X"
#define MyAppVersion   "1.4.0"
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
Source: "{#PublishDir}\*";                           DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "..\thirdparty\peperoni\driver\*";           DestDir: "{app}\driver"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}";     Filename: "{app}\{#MyAppExeName}"
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\driver\dpinst64.exe"; Parameters: "/q /sa /se"; Flags: waituntilterminated; Check: IsWin64; StatusMsg: "Installing Peperoni USB DMX driver..."
Filename: "{app}\driver\dpinst32.exe"; Parameters: "/q /sa /se"; Flags: waituntilterminated; Check: not IsWin64; StatusMsg: "Installing Peperoni USB DMX driver..."
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent



