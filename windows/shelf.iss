; The Windows installer, built with Inno Setup by .github/workflows/release.yml:
;   iscc /DVersion=0.1.0 /DSource=..\deploy windows\shelf.iss
; It installs for the current user (no administrator needed), offers Shelf
; for EPUB and PDF files in "Open with", and starts Shelf again after an
; update, which runs this installer with /SILENT.

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef Source
  #define Source "..\deploy"
#endif

[Setup]
AppId={{ECF7F184-9F2E-4360-A240-0FB344B8580E}
AppName=Shelf
AppVersion={#Version}
AppVerName=Shelf {#Version}
AppPublisher=Shelf
AppPublisherURL=https://github.com/RJUNCC/shelf
DefaultDirName={autopf}\Shelf
DefaultGroupName=Shelf
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\dist
OutputBaseFilename=shelf-{#Version}-setup
SetupIconFile=shelf.ico
UninstallDisplayIcon={app}\shelf.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
ChangesAssociations=yes

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#Source}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Shelf"; Filename: "{app}\shelf.exe"
Name: "{autodesktop}\Shelf"; Filename: "{app}\shelf.exe"; Tasks: desktopicon

[Registry]
; A program ID for books, offered for .epub and .pdf in "Open with". Where
; nothing else claims .epub, Windows uses Shelf for it straight away.
Root: HKA; Subkey: "Software\Classes\Shelf.Book"; ValueType: string; ValueName: ""; ValueData: "Book"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Shelf.Book\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\shelf.exe,0"
Root: HKA; Subkey: "Software\Classes\Shelf.Book\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\shelf.exe"" ""%1"""
Root: HKA; Subkey: "Software\Classes\.epub\OpenWithProgids"; ValueType: string; ValueName: "Shelf.Book"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "Shelf.Book"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\Applications\shelf.exe\SupportedTypes"; ValueType: string; ValueName: ".epub"; ValueData: ""; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\Applications\shelf.exe\SupportedTypes"; ValueType: string; ValueName: ".pdf"; ValueData: ""

[Run]
; Not skipped when silent, so Shelf comes back after an in-app update.
Filename: "{app}\shelf.exe"; Description: "{cm:LaunchProgram,Shelf}"; Flags: nowait postinstall
