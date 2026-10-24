; Hawthorne installer (Inno Setup 6). Built by .github/workflows/build.yml:
;   iscc /DAppVersion=1.3.0 installer\Hawthorne.iss
; Installs for the current user (no admin prompt), so Hawthorne can update itself:
; the in-app UPDATE button runs this silently; it closes Hawthorne, installs, and starts it again.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{F9FD9908-410E-4A74-85C4-D6A625F2972B}
AppName=Hawthorne
AppVersion={#AppVersion}
AppVerName=Hawthorne {#AppVersion}
AppPublisher=Hawthorne
DefaultDirName={autopf}\Hawthorne
DefaultGroupName=Hawthorne
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=Hawthorne
UninstallDisplayIcon={app}\Hawthorne.exe
OutputDir=..\dist
OutputBaseFilename=Hawthorne-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Put a Hawthorne shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\Hawthorne_artefacts\Release\Hawthorne.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Hawthorne"; Filename: "{app}\Hawthorne.exe"
Name: "{autodesktop}\Hawthorne"; Filename: "{app}\Hawthorne.exe"; Tasks: desktopicon

[Run]
; normal install: offer to start it; update from inside Hawthorne (silent): start it again by itself
Filename: "{app}\Hawthorne.exe"; Description: "Start Hawthorne"; Flags: nowait postinstall skipifsilent
Filename: "{app}\Hawthorne.exe"; Flags: nowait; Check: WizardSilent

[Code]
// Versions up to 1.2.0 used an NSIS installer in Program Files. Remove it once,
// so there's only one Hawthorne (Windows asks for permission for this step).
function OldNsisUninstaller(): String;
var
  s: String;
begin
  Result := '';
  if RegQueryStringValue(HKLM32, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Hawthorne', 'UninstallString', s) or
     RegQueryStringValue(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\Hawthorne', 'UninstallString', s) then
    Result := RemoveQuotes(s);
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  u: String;
  rc: Integer;
begin
  Result := '';
  u := OldNsisUninstaller();
  if (u <> '') and FileExists(u) then
  begin
    if not ShellExec('runas', u, '/S _?=' + ExtractFileDir(u), '', SW_HIDE, ewWaitUntilTerminated, rc) then
      Log('Previous Hawthorne install was not removed');
  end;
end;
