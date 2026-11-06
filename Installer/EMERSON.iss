; EMERSON installer (Inno Setup 6). Built by .github/workflows/build-windows.yml:
;   iscc /DAppVersion=1.0.12 Installer\EMERSON.iss
; Installs for the current user (no admin prompt), so EMERSON can update itself.

#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

[Setup]
AppId={{33570991-0630-4D91-81C8-B3E74162AFEA}
AppName=EMERSON
AppVersion={#AppVersion}
AppVerName=EMERSON {#AppVersion}
AppPublisher=PEQUOD
DefaultDirName={autopf}\EMERSON
DefaultGroupName=EMERSON
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
UninstallDisplayName=EMERSON
UninstallDisplayIcon={app}\EMERSON.exe
OutputDir=..\dist
OutputBaseFilename=EMERSON-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupIconFile=EMERSON.ico
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "desktopicon"; Description: "Put an EMERSON shortcut on the desktop"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\Emerson_artefacts\Release\EMERSON.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\Resources\Fonts\*.txt"; DestDir: "{app}\Licences"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\EMERSON"; Filename: "{app}\EMERSON.exe"
Name: "{autodesktop}\EMERSON"; Filename: "{app}\EMERSON.exe"; Tasks: desktopicon

[Run]
; normal install: offer to start it; update from inside EMERSON (silent): start it again by itself
Filename: "{app}\EMERSON.exe"; Description: "Start EMERSON"; Flags: nowait postinstall skipifsilent
Filename: "{app}\EMERSON.exe"; Flags: nowait; Check: WizardSilent
