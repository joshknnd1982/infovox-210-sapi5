; Infovox 210 SAPI5 -- Inno Setup script
;
; Installs the 32-bit and 64-bit SAPI engines, the 64-bit worker that owns the
; PowerPC emulator, the settings dialog and the diagnostics report, plus one
; resource pack per language the user chooses.

#define AppName "Infovox 210 SAPI5"
#define AppVersion "1.3.0"
#define AppPublisher "Infovox 210 SAPI5 project"
#define SourceDir "..\output"

[Setup]
AppId={{7C4E1B92-3A6D-4E5F-9C88-2D1F0B7A4E31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Infovox 210 SAPI5
DefaultGroupName=Infovox 210 SAPI5
DisableProgramGroupPage=yes
OutputDir={#SourceDir}
; The version is in the file name so a release asset is never quietly replaced.
OutputBaseFilename=Infovox210_SAPI5_{#AppVersion}_Setup
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
; SAPI reads voice tokens from HKLM only, so registration needs elevation.
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
UninstallDisplayName={#AppName} {#AppVersion}
CloseApplications=yes
RestartApplications=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "All languages (55 voices)"
Name: "english"; Description: "English only (10 voices)"
Name: "custom"; Description: "Choose languages"; Flags: iscustom

[Components]
Name: "core";     Description: "Engine, settings and diagnostics"; Types: full english custom; Flags: fixed
Name: "l";        Description: "Languages"; Types: full english custom; Flags: fixed
Name: "l\american";  Description: "American English (5 voices)";  Types: full english custom
Name: "l\british";   Description: "British English (5 voices)";   Types: full english custom
Name: "l\danish";    Description: "Danish (5 voices)";            Types: full
Name: "l\finnish";   Description: "Finnish (5 voices)";           Types: full
Name: "l\french";    Description: "French (5 voices)";            Types: full
Name: "l\german";    Description: "German (5 voices)";            Types: full
Name: "l\icelandic"; Description: "Icelandic (5 voices)";         Types: full
Name: "l\italian";   Description: "Italian (5 voices)";           Types: full
Name: "l\norwegian"; Description: "Norwegian (5 voices)";         Types: full
Name: "l\spanish";   Description: "Spanish (5 voices)";           Types: full
Name: "l\swedish";   Description: "Swedish (5 voices)";           Types: full

[Files]
; The worker holds these open while it is running, so everything is marked
; restartreplace: a mapped COM dll cannot be deleted, and without it a rollback
; leaves a half-replaced install behind.
Source: "{#SourceDir}\InfovoxSAPI.dll";        DestDir: "{app}";     Components: core; Flags: ignoreversion restartreplace uninsrestartdelete regserver 32bit
Source: "{#SourceDir}\x64\InfovoxSAPI.dll";    DestDir: "{app}\x64"; Components: core; Flags: ignoreversion restartreplace uninsrestartdelete regserver 64bit
Source: "{#SourceDir}\infovox_host.exe";       DestDir: "{app}";     Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SourceDir}\unicorn.dll";            DestDir: "{app}";     Components: core; Flags: ignoreversion restartreplace uninsrestartdelete
Source: "{#SourceDir}\InfovoxConfig.exe";      DestDir: "{app}";     Components: core; Flags: ignoreversion restartreplace
Source: "{#SourceDir}\InfovoxDiagnostics.exe"; DestDir: "{app}";     Components: core; Flags: ignoreversion restartreplace
Source: "{#SourceDir}\README.txt";             DestDir: "{app}";     Components: core; Flags: ignoreversion isreadme

; The component itself, without which nothing speaks.
Source: "{#SourceDir}\engine\infovox210.ivp";  DestDir: "{app}\engine"; Components: core; Flags: ignoreversion restartreplace

Source: "{#SourceDir}\engine\american.ivp";  DestDir: "{app}\engine"; Components: l\american;  Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\british.ivp";   DestDir: "{app}\engine"; Components: l\british;   Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\danish.ivp";    DestDir: "{app}\engine"; Components: l\danish;    Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\finnish.ivp";   DestDir: "{app}\engine"; Components: l\finnish;   Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\french.ivp";    DestDir: "{app}\engine"; Components: l\french;    Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\german.ivp";    DestDir: "{app}\engine"; Components: l\german;    Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\icelandic.ivp"; DestDir: "{app}\engine"; Components: l\icelandic; Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\italian.ivp";   DestDir: "{app}\engine"; Components: l\italian;   Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\norwegian.ivp"; DestDir: "{app}\engine"; Components: l\norwegian; Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\spanish.ivp";   DestDir: "{app}\engine"; Components: l\spanish;   Flags: ignoreversion restartreplace
Source: "{#SourceDir}\engine\swedish.ivp";   DestDir: "{app}\engine"; Components: l\swedish;   Flags: ignoreversion restartreplace

[Tasks]
; Offered rather than assumed: the settings dialog is not something most people
; open often, but it is the only place pitch modulation, breathiness and
; consonant clarity can be set, so it is worth putting within easy reach.
Name: "desktopicon"; Description: "Create a &desktop shortcut for Infovox 210 Settings";     GroupDescription: "Additional shortcuts:"

[Icons]
Name: "{group}\Infovox 210 Settings";    Filename: "{app}\InfovoxConfig.exe"
Name: "{autodesktop}\Infovox 210 Settings"; Filename: "{app}\InfovoxConfig.exe";     Comment: "Rate, pitch, breathiness and consonant clarity for the Infovox 210 voices";     Tasks: desktopicon
Name: "{group}\Infovox 210 Diagnostics"; Filename: "{app}\InfovoxDiagnostics.exe"
Name: "{group}\Uninstall {#AppName}";   Filename: "{uninstallexe}"

[Run]
Filename: "{app}\InfovoxConfig.exe"; Description: "Open Infovox 210 settings"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\engine"
Type: dirifempty;     Name: "{app}\x64"
Type: dirifempty;     Name: "{app}"

[Code]
// The worker keeps the engine packs and unicorn.dll mapped for as long as any
// application is speaking.  Setup asks it to exit before touching the files;
// without this the copy fails with a sharing violation part way through.
procedure StopWorker;
var
  ResultCode: Integer;
begin
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM infovox_host.exe',
       '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  StopWorker;
  Result := '';
end;

function InitializeUninstall(): Boolean;
begin
  StopWorker;
  Result := True;
end;

// The dlls carry the regserver flag so that uninstalling unregisters them, but
// that registration runs as each dll is copied -- before the engine packs
// further down the file list exist.  DllRegisterServer publishes a voice token
// for every pack it can see, so it has to run once more once everything is in
// place, or a fresh install would register no voices at all.  Running it twice
// is harmless; it rewrites the same keys.
procedure CurStepChanged(CurStep: TSetupStep);
var
  ResultCode: Integer;
begin
  if CurStep = ssPostInstall then
  begin
    Exec(ExpandConstant('{sys}\regsvr32.exe'),
         '/s "' + ExpandConstant('{app}\x64\InfovoxSAPI.dll') + '"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
    Exec(ExpandConstant('{syswow64}\regsvr32.exe'),
         '/s "' + ExpandConstant('{app}\InfovoxSAPI.dll') + '"',
         '', SW_HIDE, ewWaitUntilTerminated, ResultCode);
  end;
end;
