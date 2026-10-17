; ============================================================================
;  installer.iss  --  CYSC 任务栏 Windows 安装程序（Inno Setup 6）
;
;  由 scripts\package.bat 调用，编译前会先准备好 build\package\CYSC 暂存目录
;  （程序本体 + windeployqt 收来的 Qt DLL/插件 + VC++ 运行库）。
;
;  产出: build\package\CYSC任务管理-安装程序.exe
;
;  这个文件必须是 UTF-8 **带 BOM**：Inno Setup 6 默认按 ANSI 读 .iss，
;  没 BOM 的话中文（应用名、快捷方式名）会变乱码。
; ============================================================================

#define AppName "CYSC 任务栏"
#define AppNameEn "CYSC TaskBar"
#define AppVersion "0.1.0"
#define AppExe "CYSC任务管理.exe"
#define AppPublisher "CYSC TaskBar"

[Setup]
AppId={{8F3A2C41-5D6E-4B97-9A21-1B2C3D4E5F60}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\{#AppNameEn}
DefaultGroupName={#AppName}
; 允许用户改安装路径
DisableDirPage=no
DisableProgramGroupPage=yes
; 单文件安装程序，内置压缩
OutputBaseFilename=CYSC任务管理-安装程序
OutputDir=..\build\package
Compression=lzma2/max
SolidCompression=yes
; 装到 Program Files 需要管理员权限
PrivilegesRequired=admin
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
; 用我们自己的图标（.ico 放在 .iss 同目录时可直接引用）
SetupIconFile=app.ico
UninstallDisplayIcon={app}\app.ico
; 不生成"这是旧版本"之类的噪音
AllowNoIcons=yes
; 卸载时删干净
UninstallDisplayName={#AppName}

[Languages]
; Inno Setup 6 自带 29 种语言，但**不含简体中文**（中文是社区维护的
; "unofficial" 语言包）。所以打包时把它放在 scripts\ 下，这里按相对路径引用。
Name: "chinese"; MessagesFile: "ChineseSimplified.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
; ⚠ 这里**不能用 checkedonce**。
; checkedonce 的意思是"Inno 记住用户上次的选择，下次安装沿用"—— 于是只要用户
; （或静默安装）曾经取消勾选过一次，以后每次安装都不会再建快捷方式，而且
; /VERYSILENT 下不会弹界面让他改回来。实测就是这么踩到的：一次 /NOICONS 之后
; 后面所有安装都建不出快捷方式。
; 改用默认勾选（flags 留空即默认 checked），让每次安装都以"建快捷方式"为默认。
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "附加任务："

[Files]
; 暂存目录里的全部内容（程序 + Qt DLL + 平台插件 + 图标）。
; 注意：Inno 里相对路径是相对 **.iss 文件所在目录**解析的，所以要从 scripts\ 回退两级。
Source: "..\build\package\CYSC\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs
; VC++ 运行库（不进安装目录，只在安装时执行）
Source: "..\build\package\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall

[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\app.ico"
Name: "{group}\卸载 {#AppName}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; WorkingDir: "{app}"; IconFilename: "{app}\app.ico"; Tasks: desktopicon

[Run]
; 先装 VC++ 运行库（目标机器上可能没有）。
; /install /quiet /norestart —— 已装过会自动跳过，返回 0 或 1638 都算成功。
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
    StatusMsg: "正在安装 VC++ 运行库..."; Flags: waituntilterminated; Check: VCRedistNeeded

; 安装完让用户选择是否立刻启动
Filename: "{app}\{#AppExe}"; Description: "立即启动 {#AppName}"; \
    Flags: nowait postinstall skipifsilent

[UninstallDelete]
; 用户数据（数据库、配置）在 %LOCALAPPDATA%，卸载时保留 ——
; 用户数据不该因为重装就丢。这里只清安装目录里的残留。
Type: filesandordirs; Name: "{app}"

[Code]
// 只在确实没装 VC++ 运行库时才跑 redist，省掉一次无谓的等待。
// 判断方式：查注册表里 VC++ 2015-2022 x64 的已安装项。
function VCRedistNeeded(): Boolean;
var
  found: Boolean;
begin
  found := RegKeyExists(HKLM,
    'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64');
  if not found then
    found := RegKeyExists(HKLM64,
      'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64');
  Result := not found;
end;
