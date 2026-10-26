; Inno Setup script for LabelForge (run from the repository root after windeployqt):
;   iscc packaging\windows\labelforge.iss
; SPDX-License-Identifier: GPL-3.0-or-later

#define AppName "LabelForge"
#define AppVersion "1.0.0"
#define AppExe "LabelForge.exe"

[Setup]
AppId={{6C1F0F3E-6A3B-4C7E-9B7A-1F5E2D3C4B5A}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher=LabelForge contributors
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
LicenseFile=..\..\LICENSE
OutputDir=..\..\dist
OutputBaseFilename=LabelForge-{#AppVersion}-setup
SetupIconFile=labelforge.ico
UninstallDisplayIcon={app}\{#AppExe}
Compression=lzma2
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
WizardStyle=modern

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "assoc"; Description: "Открывать файлы .lfl в LabelForge"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "..\..\dist\LabelForge\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Примеры"; Filename: "{app}\examples"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.lfl"; ValueType: string; ValueName: ""; ValueData: "LabelForge.Label"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LabelForge.Label"; ValueType: string; ValueName: ""; ValueData: "Этикетка LabelForge"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LabelForge.Label\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\LabelForge.Label\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""; Tasks: assoc

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; Flags: nowait postinstall skipifsilent
