; NanoTask — Instalador profissional (Inno Setup 6.2+)
; Uso: iscc installer\NanoTask.iss /DAppVersion=1.0.0
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif

#define AppName "NanoTask"
#define AppPublisher "NanoTask Labs"
#define AppURL "https://github.com/seu-usuario/nanotask"
#define AppExe "NanoTask.exe"

[Setup]
AppId={{8F3A2B1C-4D5E-4F60-9A1B-NANOTASK01}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}/issues
AppUpdatesURL={#AppURL}/releases
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
LicenseFile=..\LICENSE
OutputDir=..\dist
OutputBaseFilename=NanoTask-Setup-{#AppVersion}
SetupIconFile=..\assets\installer\setup.ico
WizardImageFile=..\assets\installer\banner.bmp
WizardSmallImageFile=..\assets\installer\header.bmp
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline dialog
CloseApplications=yes
CloseApplicationsFilter=*.exe
RestartApplications=no
UninstallDisplayIcon={app}\{#AppExe}
UninstallDisplayName={#AppName}
VersionInfoVersion={#AppVersion}
VersionInfoCompany={#AppPublisher}
VersionInfoDescription=NanoTask — system tray monitor fantasma
VersionInfoProductName={#AppName}
WizardStyle=modern
DisableWelcomePage=no

[Languages]
Name: "portuguese"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "{cm:AutoStartProgram,{#AppName}}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "trayonly"; Description: "Modo fantasma (apenas bandeja, sem janela na barra de tarefas)"; GroupDescription: "Comportamento:"; Flags: checkedonce

[Files]
; NOTA: o workflow release compila com Ninja (-> build\NanoTask.exe).
; Build local com VS generator: copie o exe para build\ ou ajuste o path.
Source: "..\build\{#AppExe}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\NanoTask.UI\bin\x64\Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs; Excludes: "*.pdb,*.ilk,*.exp,*.lib,*.tlog,*.rsp"
Source: "..\assets\tray\*.ico"; DestDir: "{app}\assets\tray"; Flags: ignoreversion recursesubdirs
Source: "..\config\settings.defaults.json"; DestDir: "{app}\config"; Flags: ignoreversion onlybelowversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion isreadme

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{group}\Desinstalar {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon

[Registry]
; Auto-start (HKCU, sem precisar de admin)
Root: HKCU; Subkey: "SOFTWARE\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
  ValueName: "{#AppName}"; ValueData: """{app}\{#AppExe}"" --tray"; Flags: uninsdeletevalue; Tasks: autostart
; Modo de exibição persistido (0=fantasma,1=fixo taskbar,2=minimizado)
Root: HKCU; Subkey: "SOFTWARE\{#AppName}"; ValueType: dword; \
  ValueName: "TaskbarMode"; ValueData: "0"; Flags: createvalueifdoesntexist uninsdeletekey; Tasks: trayonly

[Run]
Filename: "{app}\{#AppExe}"; Description: "{cm:LaunchProgram,{#AppName}}"; \
  Flags: nowait postinstall skipifsilent shellexec

[Code]
function InitializeSetup(): Boolean;
begin
  // Bloqueia instalação concorrente do próprio app
  if CheckForMutexes('Local\NanoTaskGhost') then begin
    if MsgBox('O NanoTask está em execução. Fechá-lo para continuar?', mbConfirmation, MB_YESNO) = IDYES then begin
      // O app detecta o mutex e sai; o [Setup] CloseApplications cuida do resto
      Result := True;
    end else Result := False;
  end else Result := True;
end;

[UninstallDelete]
Type: filesandordirs; Name: "{app}\logs"
