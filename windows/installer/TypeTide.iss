; TypeTide Windows 安装器（Inno Setup）。
; 由 windows/scripts/build-release.ps1 调用：
;   ISCC.exe /DAppVersion=x.y.z TypeTide.iss
; 按用户级安装（无需管理员/UAC），带开始菜单项、卸载器和可选开机自启。

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{7A2E5B90-3C64-4D8F-9B1A-52E8C0A47F31}
AppName=TypeTide
AppVersion={#AppVersion}
AppPublisher=everettjf
AppPublisherURL=https://github.com/everettjf/typetide
AppSupportURL=https://github.com/everettjf/typetide/issues
AppUpdatesURL=https://github.com/everettjf/typetide/releases
DefaultDirName={autopf}\TypeTide
DefaultGroupName=TypeTide
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir=..\build
OutputBaseFilename=TypeTide-Setup-{#AppVersion}
SetupIconFile=..\assets\app.ico
UninstallDisplayIcon={app}\TypeTide.exe
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
CloseApplications=yes
RestartApplications=no
DisableProgramGroupPage=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "autostart"; Description: "Start TypeTide when I sign in"; GroupDescription: "Additional options:"

[Files]
Source: "..\build\TypeTide.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\llama\*"; DestDir: "{app}\llama"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\TypeTide"; Filename: "{app}\TypeTide.exe"
Name: "{group}\Uninstall TypeTide"; Filename: "{uninstallexe}"

[Registry]
; 开机自启（用户勾选时写入；卸载时清掉）
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
    ValueType: string; ValueName: "TypeTide"; ValueData: """{app}\TypeTide.exe"""; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
Filename: "{app}\TypeTide.exe"; Description: "Launch TypeTide now"; \
    Flags: nowait postinstall skipifsilent

[UninstallRun]
; 卸载前退出正在运行的实例（用户设置保留在 %APPDATA%\TypeTide，重装不丢配置）
Filename: "{cmd}"; Parameters: "/C taskkill /IM TypeTide.exe /F"; \
    Flags: runhidden skipifdoesntexist; RunOnceId: "KillTypeTide"
