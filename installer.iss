; PrusaSlicer Calibration - Inno Setup Script
; Built from build-default/dist after cmake --install and DLL bundling

#define MyAppName "PrusaSlicer Calibration"
#define MyAppPublisher "hyiger"
#define MyAppURL "https://github.com/hyiger/PrusaSlicer"
#define MyAppExeName "prusa-slicer.exe"
#define MyGCodeViewerExeName "prusa-gcodeviewer.exe"

; Version is passed via /DMyAppVersion=x.y.z on the iscc command line
#ifndef MyAppVersion
  #define MyAppVersion "2.9.4"
#endif

[Setup]
AppId={{B8A0E6F2-9C3D-4A1E-B5F7-2D8C6E4A9F01}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
AllowNoIcons=yes
; MyAppVersion is the full SLIC3R_BUILD_ID (e.g. "PrusaSlicer-2.9.6-Filament-Edition-1.10.0"),
; which already carries the "PrusaSlicer-" prefix — so do NOT prepend it again here, or the
; artifact becomes "PrusaSlicer-PrusaSlicer-...". Matches the mac/linux naming (build_id directly).
OutputBaseFilename={#MyAppVersion}-win64-setup
Compression=lzma2/fast
SolidCompression=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
WizardStyle=modern
UninstallDisplayIcon={app}\{#MyAppExeName}
LicenseFile=LICENSE
SetupIconFile=resources\icons\PrusaSlicer.ico

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "associate_3mf"; Description: "Associate .3mf files"; GroupDescription: "File associations:"; Flags: unchecked
Name: "associate_stl"; Description: "Associate .stl files"; GroupDescription: "File associations:"; Flags: unchecked
Name: "associate_gcode"; Description: "Associate .gcode files with G-code Viewer"; GroupDescription: "File associations:"; Flags: unchecked

[Files]
Source: "build-default\dist\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\PrusaSlicer"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\PrusaSlicer G-code Viewer"; Filename: "{app}\{#MyGCodeViewerExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\PrusaSlicer"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon
Name: "{autodesktop}\PrusaSlicer G-code Viewer"; Filename: "{app}\{#MyGCodeViewerExeName}"; Tasks: desktopicon

[Registry]
Root: HKA; Subkey: "Software\Classes\.3mf\OpenWithProgids"; ValueType: string; ValueName: "PrusaSlicer.3mf"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate_3mf
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.3mf"; ValueType: string; ValueName: ""; ValueData: "3MF File"; Flags: uninsdeletekey; Tasks: associate_3mf
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.3mf\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate_3mf
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.3mf\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate_3mf

Root: HKA; Subkey: "Software\Classes\.stl\OpenWithProgids"; ValueType: string; ValueName: "PrusaSlicer.stl"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate_stl
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.stl"; ValueType: string; ValueName: ""; ValueData: "STL File"; Flags: uninsdeletekey; Tasks: associate_stl
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.stl\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"; Tasks: associate_stl
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.stl\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""; Tasks: associate_stl

Root: HKA; Subkey: "Software\Classes\.gcode\OpenWithProgids"; ValueType: string; ValueName: "PrusaSlicer.gcode"; ValueData: ""; Flags: uninsdeletevalue; Tasks: associate_gcode
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.gcode"; ValueType: string; ValueName: ""; ValueData: "G-code File"; Flags: uninsdeletekey; Tasks: associate_gcode
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.gcode\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyGCodeViewerExeName},0"; Tasks: associate_gcode
Root: HKA; Subkey: "Software\Classes\PrusaSlicer.gcode\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyGCodeViewerExeName}"" ""%1"""; Tasks: associate_gcode

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,PrusaSlicer}"; Flags: nowait postinstall skipifsilent
