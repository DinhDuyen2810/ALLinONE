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
; Ghi đè được qua dòng lệnh: ISCC.exe "/DMyAppVersion=1.16.1" OneForAll.iss - build_installer.bat tự đọc
; file VERSION ở gốc dự án rồi truyền vào, đồng bộ với CMakeLists.txt/UpdateChecker (xem VERSION,
; src/core/update/Version.h.in). Giá trị dưới đây CỐ Ý là "0.0.0-dev" (không phải một số bản phát hành
; thật) - dùng khi ai đó biên dịch TRỰC TIẾP file .iss này (vd mở bằng Inno Setup IDE rồi bấm Compile)
; thay vì chạy build_installer.bat: nếu để một con số thật (vd "1.15.0") làm mặc định, nó sẽ ngày càng
; CŨ ĐI và LÀM NHƯ THẬT mỗi khi dự án tăng version mà quên sửa chỗ này (đã từng xảy ra thật - xem
; PROJECT_OVERVIEW.md mục 4l) - "0.0.0-dev" không thể bị nhầm là một bản phát hành thật, báo ngay cho
; người build biết họ đã bỏ qua build_installer.bat.
#ifndef MyAppVersion
  #define MyAppVersion "0.0.0-dev"
#endif
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
; Tên Mutex PHẢI khớp CHÍNH XÁC với CreateMutexW() trong src/main.cpp - cho phép Setup/Windows Restart
; Manager tự phát hiện OneForAll.exe đang chạy. Dùng cho tính năng tự cập nhật (UpdateInstaller chạy lại
; Setup này với /CLOSEAPPLICATIONS /RESTARTAPPLICATIONS - tự đóng app đang chạy rồi tự mở lại sau khi cài
; xong, không cần người dùng bấm gì) - KHÔNG dùng để chặn chạy nhiều bản sao cùng lúc.
AppMutex=OneForAllRunningMutex
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
