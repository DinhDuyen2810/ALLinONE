; Script Inno Setup cho "One for ALL" - dựng MỘT file setup.exe duy nhất, tự giải nén toàn bộ thư
; viện/exe thật, cho chọn đường dẫn cài đặt, cho chọn tạo shortcut, có gỡ cài đặt đăng ký đầy đủ trong
; "Apps & features"/Control Panel - đúng như một ứng dụng Windows bình thường.
;
; Build: chạy build_installer.bat ở gốc dự án (tự gọi build_app.bat để dựng OneForAll_Release\ mới nhất
; rồi biên dịch script này bằng ISCC.exe). Không cần quyền Administrator để CÀI ĐẶT (PrivilegesRequired=
; lowest - cài vào {autopf}, tự chọn vị trí phù hợp theo quyền hiện có của người dùng, giống cách
; VS Code/Discord/nhiều ứng dụng hiện đại khác làm) - người dùng vẫn có thể bấm "Show details" để chọn
; cài cho mọi người dùng (cần quyền Administrator) nếu muốn, qua PrivilegesRequiredOverridesAllowed.

#define MyAppName "One for ALL"
#define MyAppVersion "1.11.0"
#define MyAppExeName "OneForAll.exe"
#define MyAppURL "https://github.com/DinhDuyen2810/ALLinONE"

[Setup]
AppId={{C82B22A6-50DC-4B99-A2A4-056B0A66BE55}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=..\dist
OutputBaseFilename=OneForAll_Setup
SetupIconFile=..\assets\app_icon.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
; Chỉ MỘT ngôn ngữ (tiếng Việt) -> Inno Setup tự dùng luôn, không hỏi chọn ngôn ngữ, đồng nhất với
; toàn bộ ứng dụng (mọi giao diện trong app đều thuần tiếng Việt).

[Languages]
Name: "vietnamese"; MessagesFile: "Vietnamese.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
; Toàn bộ OneForAll_Release\ (exe + DLL Qt + plugin + scrcpy/yt-dlp NẾU có sẵn lúc build cài đặt) - trừ
; logs\ (tự tạo lại khi chạy) và profiles\qr_history.json (lịch sử quét QR cá nhân, không phải dữ liệu
; mẫu) - vẫn giữ profiles\default.json (chuỗi Auto Click MẪU có chủ đích, hữu ích cho người dùng mới).
Source: "..\OneForAll_Release\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs; Excludes: "logs\*,profiles\qr_history.json"

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
