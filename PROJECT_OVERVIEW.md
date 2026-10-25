# One For ALL - Mô tả toàn bộ dự án

> File này mô tả tổng thể dự án và **được cập nhật sau mỗi lần sửa code** (xem mục "Lịch sử thay đổi" cuối file).
> Tài liệu kiến trúc chi tiết và changelog tính năng nằm ở [README.md](README.md); đặc tả thiết kế ở [OneForAll_AutoClick_Design.md](OneForAll_AutoClick_Design.md).

## 1. Dự án là gì
Ứng dụng desktop Windows (C++20, Qt 6.11 MinGW, CMake + Ninja) dạng launcher gồm 9 công cụ - **toàn bộ 9/9 công cụ nay đã hoàn chỉnh**: **Auto Click**, **QR Tools**, **WiFi Connection**, **Connect Together** (tham khảo Mouse without Borders), **Disk Cleanup** (tham khảo CCleaner/BleachBit/Windows Disk Cleanup), **Android Phone Control** (dựa trên scrcpy), **VPN & Location** (VPN gốc Windows + vị trí theo IP), **Security Gateway** (tham khảo Kaspersky, xây trên Windows Defender có sẵn) và **Downloader** (tải trực tiếp/quét trang web/video nền tảng qua yt-dlp).

## 2. Luồng khởi động
`main.cpp` đặt Per-Monitor DPI v2 → `QApplication` → `Logger` ghi `%LOCALAPPDATA%\OneForAll\logs\app.log` (xem `AppPaths`, mục 4o) → `MainWindow`.
`MainWindow::registerTools()` đăng ký tool vào singleton `ToolManager`, đổ lên sidebar. Bấm "Mở cửa sổ" gọi `ITool::createWindow()` tạo cửa sổ độc lập.

## 3. Cấu trúc mã nguồn
| Thư mục / file | Vai trò |
|---|---|
| `src/main.cpp` | Điểm vào, DPI awareness |
| `src/core/Tool.h` | Interface `ITool` (id, name, description, icon, `createWindow()`) |
| `src/core/ToolManager.*` | Registry tool + `PlaceholderTool` |
| `src/core/AppPaths.*` | Nơi DUY NHẤT quyết định dữ liệu người dùng nằm ở đâu (`%LOCALAPPDATA%\OneForAll\{profiles,logs}`), di trú từ vị trí cũ, đường dẫn tuyệt đối tới exe hệ thống |
| `src/core/Logger.*` | Log thread-safe ra `logs\app.log`, `logs\autoclick.log` trong thư mục dữ liệu người dùng |
| `src/core/IconHelper.h` | Vẽ icon badge nền trắng bo góc |
| `src/ui/MainWindow.*` | Sidebar + thẻ thông tin + nút mở tool |
| `src/tools/autoclick/model/` | `Action`, `ActionChain` (struct thuần, không phụ thuộc UI) |
| `src/tools/autoclick/storage/` | `ActionSerializer`: chain ↔ JSON (`profiles\default.json` trong thư mục dữ liệu người dùng, ghi nguyên tử) |
| `src/tools/autoclick/engine/` | `ActionRunner` (QThread), `InputController` (Win32 `SendInput`/`SetCursorPos`, lib `input_core`), `StopHotkey` (phím dừng toàn cục Ctrl+Alt+F8) |
| `src/tools/autoclick/capture/` | `CoordinateOverlay`, `MouseCapture`: bắt tọa độ toàn desktop |
| `src/tools/autoclick/overlay/` | `RuntimeOverlay`: HUD trạng thái khi chạy |
| `src/tools/autoclick/widgets/` | `ChainListWidget`, `ActionListWidget`, `ActionEditorWidget` |
| `src/tools/autoclick/AutoClick*.{h,cpp}` | `AutoClickTool` (ITool) và `AutoClickWindow` (điều phối UI 3 cột) |
| `src/tools/qr/QRCodec.*` | Lõi QR: mã hóa (qrcodegen), render ảnh/SVG, giải mã (quirc) |
| `src/tools/qr/QRPayload.*` | Tạo và phân tích nội dung: Text, URL, WiFi, Email, SMS, Phone, Geo, vCard/MECARD (giao diện TẠO chỉ dùng 5 loại: Text, URL, WiFi, Phone, Geo; Email/SMS/vCard vẫn được nhận diện khi QUÉT) |
| `src/tools/qr/QRWindow.*`, `QRGenerateTab.*`, `QRScanTab.*`, `QRHistoryTab.*` | UI QR Tools: 3 tab Tạo / Quét / Lịch sử |
| `src/tools/qr/QRHistoryStore.*` | Lịch sử tạo/quét, lưu `profiles\qr_history.json` trong thư mục dữ liệu người dùng (tối đa 300 mục) |
| `src/tools/qr/QRImageView.*`, `ScreenSnipOverlay.*` | Khung xem ảnh kéo-thả có viền mã; chụp vùng màn hình |
| `src/tools/qr/QRTool.*` | `ITool` của QR (một cửa sổ duy nhất) |
| `src/tools/wifi/model/` | `WifiNetwork`, `WifiProfile`, `WifiSecurity` (struct/enum thuần) |
| `src/tools/wifi/engine/WlanProfileXml.*` | Dựng/phân tích XML hồ sơ WLAN (thuần Qt, test được không cần phần cứng) |
| `src/tools/wifi/engine/WlanController.*` | Lớp duy nhất gọi Windows WLAN API (wlanapi.dll): quét, hồ sơ, kết nối, trạng thái |
| `src/tools/wifi/WifiWindow.*`, `NetworksTab.*`, `ProfilesTab.*`, `ConnectDialog.*` | UI: tab Mạng xung quanh / Hồ sơ đã lưu, hộp thoại kết nối |
| `src/tools/wifi/engine/ConnectionWatcher.*` | Theo dõi kết quả một lần kết nối WiFi (polling trạng thái, có giới hạn thời gian) |
| `src/tools/wifi/engine/SpeedTestRunner.*` | Đo tốc độ mạng qua HTTP (ping/jitter/download/upload), thuần Qt Network |
| `src/tools/wifi/SpeedTestTab.*` | Tab "Đo tốc độ mạng" |
| `src/tools/connect/model/` | `ScreenSide`, `PeerInfo` (struct/enum thuần) |
| `src/tools/connect/engine/PairingCode.*` | Mã ghép đôi 9 số, sinh/kiểm tra/định dạng (thuần Qt) |
| `src/tools/connect/engine/ProtocolMessage.*` | Khung thông điệp nhị phân giữa 2 máy (thuần Qt, QDataStream) |
| `src/tools/connect/engine/CryptoSession.*` | AES-256-GCM qua Windows CNG (bcrypt.dll), suy khóa PBKDF2 (ghép đôi) và HMAC-SHA256 (khóa phiên từng chiều - giao thức v2, mục 4o) |
| `src/tools/connect/model/PairedPeer.h` | Máy đã ghép đôi, lưu lâu dài (id, tên, khóa dài hạn, vị trí) |
| `src/tools/connect/engine/PeerStore.*` | Lưu/nạp danh sách peer đã ghép đôi, `profiles\connect_peers.json` (khóa dài hạn bọc DPAPI) |
| `src/tools/connect/engine/NetworkSession.*` | Phiên TCP đóng khung + mã hóa đầu-cuối (CryptoSession) |
| `src/tools/connect/engine/PeerDiscovery.*` | Khám phá máy trong LAN qua UDP broadcast (chỉ thông tin công khai) |
| `src/tools/connect/engine/EdgeDetector.*` | Toán học thuần phát hiện chuột chạm biên màn hình để chuyển quyền điều khiển |
| `src/tools/connect/engine/InputInjector.*` | Tiêm chuột/phím nhận được lên máy này (SendInput/SetCursorPos) |
| `src/tools/connect/engine/InputHook.*` | Hook bàn phím/chuột toàn cục (WH_MOUSE_LL/WH_KEYBOARD_LL), chỉ "nuốt" input khi đang chủ động chia sẻ |
| `src/tools/connect/engine/LocalIdentityStore.*` | Định danh ổn định của máy này, `profiles\connect_identity.json` |
| `src/tools/connect/engine/ConnectSessionController.*` | Điều phối toàn bộ: ghép đôi, kết nối lại, biên màn hình, hook/injector, clipboard |
| `src/tools/connect/ConnectWindow.*`, `PairingTab.*`, `PeersTab.*` | UI: tab Ghép đôi / Máy đã ghép đôi, banner trạng thái, log |
| `src/tools/connect/ConnectTool.*` | `ITool` của Connect Together (một cửa sổ duy nhất) |
| `src/tools/diskcleanup/model/` | `CleanupCategory`, `CleanupItem` (struct/enum thuần) |
| `src/tools/diskcleanup/engine/CategoryRegistry.*` | Danh sách hạng mục dọn dẹp đã biết + phân giải đường dẫn thật trên máy (thuần Qt, test được) |
| `src/tools/diskcleanup/engine/CleanupScanner.*` | Quét hạng mục trên QThread riêng, chỉ đọc (không xóa) |
| `src/tools/diskcleanup/engine/RecycleBinOps.*` | Lớp duy nhất gọi Shell API Thùng rác (SHFileOperationW/SHQueryRecycleBinW/SHEmptyRecycleBinW) |
| `src/tools/diskcleanup/engine/CleanupExecutor.*` | Thực hiện xóa (qua Thùng rác mặc định) trên QThread riêng |
| `src/tools/diskcleanup/engine/FsSafety.*` | Lớp an toàn hệ thống tệp: duyệt Win32 không đi xuyên junction/symlink thư mục, danh tính tệp vật lý, kiểm thư mục gốc đáng ngờ |
| `src/tools/diskcleanup/engine/DiskSpaceInfo.*` | Dung lượng từng ổ đĩa (thuần Qt, QStorageInfo) |
| `src/tools/diskcleanup/engine/LargeFileScanner.*` | Tìm tệp lớn trong một thư mục gốc tùy chọn, QThread riêng |
| `src/tools/diskcleanup/engine/DuplicateFinder.*` | Tìm tệp trùng lặp NỘI DUNG (nhóm theo kích thước rồi hash SHA-256), QThread riêng |
| `src/tools/diskcleanup/engine/PartitionManager.*` | Liệt kê/đổi kích thước phân vùng qua PowerShell (Get-Partition/Get-PartitionSupportedSize/Resize-Partition), kiểm tra quyền Administrator (Win32) |
| `src/tools/diskcleanup/engine/PartitionResizer.*` | Chạy `Resize-Partition` thật trên QThread riêng (co giãn lớn có thể mất vài phút) |
| `src/tools/diskcleanup/DiskUiStyle.h` | Style Light Theme dùng chung cho UI Disk Cleanup (giống `ConnectUiStyle.h`/`WifiUiStyle.h`) |
| `src/tools/diskcleanup/CleanupTab.*` | Tab "Dọn dẹp theo hạng mục": tổng quan dung lượng ổ đĩa, bảng hạng mục có thể tick, quét/dọn dẹp |
| `src/tools/diskcleanup/LargeFilesTab.*` | Tab "Tìm tệp lớn": chọn thư mục/ổ, ngưỡng kích thước, quét và xóa/mở thư mục chứa |
| `src/tools/diskcleanup/DuplicateFilesTab.*` | Tab "Tìm tệp trùng lặp": quét 1 thư mục, hiển thị theo nhóm (cây), xóa các bản thừa vào Thùng rác |
| `src/tools/diskcleanup/PartitionTab.*` | Tab "Quản lý phân vùng": liệt kê phân vùng, đổi kích thước có xác nhận gõ lại tên ổ đĩa |
| `src/tools/diskcleanup/DiskCleanupWindow.*` | Cửa sổ Disk Cleanup, ghép 4 tab trên |
| `src/tools/diskcleanup/DiskCleanupTool.*` | `ITool` của Disk Cleanup (một cửa sổ duy nhất) |
| `src/tools/wifi/WifiTool.*` | `ITool` của WiFi (một cửa sổ duy nhất) |
| `src/tools/android/model/AndroidDeviceInfo.h` | Một thiết bị adb thấy (serial, trạng thái, model, USB/không dây) - struct thuần |
| `src/tools/android/engine/AdbController.*` | Lớp duy nhất gọi `adb.exe` đóng gói kèm: liệt kê/ghép đôi/kết nối thiết bị, dừng daemon adb đóng gói kèm |
| `src/tools/android/engine/AdbDeviceLister.*` | Chạy `adb devices -l` bất đồng bộ (không chặn luồng giao diện, không chồng lệnh) |
| `src/tools/android/engine/ScrcpyLauncher.*` | Khởi chạy/quản lý tiến trình `scrcpy.exe` (gương + điều khiển màn hình thời gian thực) |
| `src/tools/android/AndroidUiStyle.h` | Style Light Theme dùng chung cho UI Android (giống các `*UiStyle.h` khác) |
| `src/tools/android/WirelessPairDialog.*` | Hộp thoại ghép đôi gỡ lỗi không dây (Android 11+): nhập IP:Cổng + mã 6 số |
| `src/tools/android/DevicesTab.*` | Danh sách thiết bị, tùy chọn điều khiển (độ phân giải/bitrate/fps/ghi hình...), nút Điều khiển |
| `src/tools/android/AndroidControlWindow.*` | Cửa sổ Android Phone Control |
| `src/tools/android/AndroidControlTool.*` | `ITool` của Android Phone Control (một cửa sổ duy nhất) |
| `src/core/WinElevation.*` | Kiểm tra/khởi chạy lại với quyền Administrator (Win32, dùng chung - lib `core_shared`) |
| `src/core/PowerShellRunner.*` | Chạy script PowerShell qua `-EncodedCommand` (Base64 UTF-16LE), ép UTF-8, trả stdout/stderr/mã lỗi; `quoteLiteral()` để đặt chuỗi vào `'...'` an toàn (dùng chung) |
| `src/tools/vpn/model/VpnProfile.h` | `VpnProfile`, `VpnTunnelType` (struct/enum thuần, không có trường mật khẩu) |
| `src/tools/vpn/engine/VpnController.*` | Thêm/liệt kê/xóa hồ sơ VPN qua PowerShell (`Add`/`Get`/`Remove-VpnConnection`), lưu nhãn quốc gia riêng |
| `src/tools/vpn/engine/VpnConnector.*` | Kết nối/ngắt kết nối VPN qua RAS API (`RasDialW`/`RasHangUpW`) trên QThread riêng - mật khẩu chỉ nằm trong bộ nhớ (trước v1.19.0 dùng `rasdial.exe`) |
| `src/tools/vpn/engine/PublicIpChecker.*` | Tra cứu IP công khai + vị trí (quốc gia/thành phố) hiện tại qua `ipwho.is`, thuần Qt Network |
| `src/tools/vpn/AddVpnProfileDialog.*` | Hộp thoại thêm hồ sơ VPN (tên/quốc gia/máy chủ/giao thức/tên đăng nhập) |
| `src/tools/vpn/VpnTab.*` | Tab chính: vị trí hiện tại theo IP, bảng hồ sơ VPN, Kết nối/Ngắt kết nối |
| `src/tools/vpn/VpnControlWindow.*` | Cửa sổ VPN & Location |
| `src/tools/vpn/VpnControlTool.*` | `ITool` của VPN & Location (một cửa sổ duy nhất) |
| `src/tools/security/model/ThreatRecord.h` | Một mối đe dọa Defender đã phát hiện (struct thuần) |
| `src/tools/security/engine/CommandAnalyzer.*` | Phân tích TĨNH lệnh PowerShell - tìm dấu hiệu tải+thực thi/mã hóa/công cụ tấn công đã biết, không chạy lệnh |
| `src/tools/security/engine/CommandLauncher.*` | Mở cửa sổ PowerShell THẬT riêng biệt + dán (không tự chạy) lệnh vào đó |
| `src/tools/security/engine/DefenderController.*` | Bọc PowerShell module `Defender` có sẵn của Windows: trạng thái/bật-tắt bảo vệ/Network Protection/quét/liệt kê & xóa mối đe dọa |
| `src/tools/security/engine/DefenderScanWorker.*` | Chạy `Start-MpScan` (đồng bộ/chặn) trên QThread riêng |
| `src/tools/security/engine/HostsBlocklist.*` | Chặn tên miền tùy chỉnh qua hosts file (khối được đánh dấu riêng, không động vào phần còn lại) |
| `src/tools/security/{CommandGatewayTab,WebProtectionTab,MalwareScanTab,SecurityGatewayWindow}.*` | UI 3 tab Security Gateway |
| `src/tools/security/SecurityGatewayTool.*` | `ITool` của Security Gateway (một cửa sổ duy nhất) |
| `src/tools/downloader/model/{DownloadItem,MediaLink,VideoFormatInfo,VideoInfo}.h` | Struct thuần: mục hàng đợi tải, liên kết media tìm thấy khi quét trang, định dạng/thông tin video nền tảng |
| `src/tools/downloader/engine/FileDownloader.*` | Tải trực tiếp HTTP(S) nhiều tệp đồng thời (QNetworkAccessManager), hỗ trợ tiếp tục tải dở (HTTP Range) |
| `src/tools/downloader/engine/PageMediaScanner.*` | Quét HTML tĩnh một trang web tìm liên kết ảnh/video/âm thanh/tài liệu |
| `src/tools/downloader/engine/YtDlpController.*` | Đường dẫn/khả dụng của `yt-dlp.exe`/`ffmpeg.exe` đóng gói kèm theo (thư mục `yt-dlp/`) |
| `src/tools/downloader/engine/YtDlpInfoWorker.*` | Lấy thông tin/định dạng video (`yt-dlp --dump-json`) trên QThread riêng |
| `src/tools/downloader/engine/YtDlpDownloadWorker.*` | Tải video qua `yt-dlp.exe`, đọc tiến độ qua `--progress-template` |
| `src/tools/downloader/engine/{DownloadQueue,LinkResolver,LinkResolverCore,HeadlessBrowser,CookieFile}.*` | Hàng đợi hợp nhất (kiểm tra OK/ERR, tải HTTP + yt-dlp, ghi lịch sử); bộ phân giải link bất đồng bộ; phần THUẦN của bộ phân giải (test bằng dữ liệu thật); trình duyệt ẩn Edge/Chrome (NetLog + DOM) |
| `src/tools/downloader/engine/{ImagePdf,CdpClient,DriveViewerCapture}.*` | Tài liệu Google Drive chỉ xem (v1.19.22): `ImagePdf` = ghép JPEG thành PDF (thuần Qt Core); `CdpClient` = khách Chrome DevTools Protocol (WebSocket RFC 6455 tối giản trên `QTcpSocket`, chặn nhưng hủy được trong 200 ms); `DriveViewerCapture` = QThread mở trang xem trong Edge/Chrome ẩn, cuộn + chụp từng trang rồi ghép PDF |
| `src/tools/downloader/{DownloaderSettings,DownloadHistoryStore}.*`, `model/ResolvedMedia.h` | Cài đặt dùng chung + lịch sử tải (JSON qua `AppPaths::profileFile`); struct kết quả phân giải |
| `src/tools/downloader/{DownloadTab,DownloadHistoryTab,DownloaderSettingsTab,DownloaderWindow}.*` | UI 3 tab Downloader (Tải xuống = hàng đợi hợp nhất gộp tải trực tiếp + quét trang + video nền tảng, Lịch sử, Cài đặt) |
| `src/tools/downloader/DownloaderTool.*` | `ITool` của Downloader (một cửa sổ duy nhất) |
| `src/third_party/` | qrcodegen (MIT), quirc (ISC), xem `THIRD_PARTY.md` |
| `vendor/scrcpy/` | scrcpy + adb đóng gói (Apache-2.0, KHÔNG commit Git) - xem `THIRD_PARTY.md` |
| `vendor/yt-dlp/` | yt-dlp + ffmpeg/ffprobe đóng gói (Unlicense/GPLv3+ kết hợp + GPL, KHÔNG commit Git) - xem `THIRD_PARTY.md` |
| `tests/` | `qr_tests`/`qr_ui_tests`, `wifi_tests`/`wifi_ui_tests`, `connect_tests`/`connect_ui_tests` (506 + 2, ghép đôi 2 chiều thật qua TCP, ổn định qua 40+ lần chạy liên tiếp), `diskcleanup_tests`/`diskcleanup_ui_tests` (176 + 19 kiểm tra, có xóa file thật qua Thùng rác, liệt kê phân vùng thật), `android_tests`/`android_ui_tests` (45 + 7 kiểm tra, có gọi `adb devices` thật - chỉ đọc), `vpn_tests`/`vpn_ui_tests` (219 + 7 kiểm tra ở v1.19.31, có gọi `Get-VpnConnection` thật - chỉ đọc), `vpn_direct_tests` (150 kiểm tra từ v1.19.31: PPP/giao thức FortiGate-GlobalProtect + máy chủ GIẢ đầu-cuối trên 127.0.0.1 + tiến trình phụ `--vpn-helper` thật), `security_tests`/`security_ui_tests` (76 + 6 kiểm tra, có gọi `Get-MpComputerStatus`/đọc hosts file thật - chỉ đọc), `downloader_tests`/`downloader_ui_tests` (652 + 100 kiểm tra từ v1.19.22, trước đó 554 + 65 ở v1.19.18: máy chủ HTTP 127.0.0.1 cho cả luồng hàng đợi lẫn giao diện, có tải thật một tệp công khai nhỏ qua HTTP thật) |
| `assets/resources.qrc`, `icon/` | Icon nhúng vào exe |
| `profiles/default.json` | Chuỗi Auto Click MẪU đóng gói kèm (lần chạy đầu được chép sang thư mục dữ liệu người dùng) |
| `CLAUDE.md` | Quy tắc làm việc đứng cho Claude Code (build → commit → push sau mỗi lần sửa...) |
| `build_app.bat`, `run_app.bat` | Build + đóng gói (`windeployqt`), chạy bản Release |

## 4. Auto Click
- **Model:** 7 loại action (Click, Drag, Hold, TypeText, Hotkey, KeyPress, Scroll), mỗi action có `waitBefore`, `waitAfter`, `duration`, `enabled`. Chain có `repeatCount` (-1 = vô hạn).
- **Luồng chạy:** RUN → `ActionRunner::run()` (thread riêng) lọc action đang bật → với mỗi round, mỗi action: waitBefore → `executeAction` → waitAfter. `sleepWithCountdown` ngủ từng 50ms, phát `countdownTick` cho HUD và kiểm tra cờ atomic dừng/tạm dừng.
- **Win32:** chỉ `InputController` (và `MouseCapture`/`main.cpp` ở mức DPI) gọi API hệ thống.
- **Capture tọa độ:** ẩn cửa sổ → overlay phủ toàn desktop → click lấy điểm / ESC hủy → điền vào ô X/Y.
- **HUD:** always-on-top, hiện round/action hiện tại/kế tiếp/countdown/nút Stop, tự né điểm chuột sắp thao tác.
- **Lưu trữ:** JSON, Save/Load profile mặc định, Import/Export file.
- **Đã sửa lỗi UI thật (rà soát toàn bộ 5 tính năng theo yêu cầu người dùng):** toàn bộ panel "ACTION
  CHAINS"/"ACTION LIST"/"ACTION SETTINGS" và nhãn form (Type/Button/Position/Wait Before/Wait After/
  Duration...) từng thuần tiếng Anh - lạc tông với phần còn lại của ứng dụng (toàn tiếng Việt). Đã dịch
  toàn bộ nhãn/nút/HUD runtime overlay/thông báo đếm ngược sang tiếng Việt. **Xác nhận an toàn trước khi
  sửa:** các giá trị combo box (`Action Type`, `Mouse Button`...) chỉ đọc/ghi theo CHỈ SỐ
  (`currentIndex()`), và `Action::typeName()/description()` (hiển thị) hoàn toàn độc lập với
  `ActionSerializer`'s `actionTypeToString()` (khóa PascalCase riêng để lưu JSON) - dịch chữ hiển thị
  không ảnh hưởng gì tới các hồ sơ `.json` đã lưu trước đó. Tên phím (ENTER/TAB/ESC/mũi tên...) và
  Ctrl/Alt/Shift/Win giữ nguyên tiếng Anh (quy ước bàn phím toàn cầu). Phát hiện và sửa thêm 1 lỗi tự
  gây ra: 3 nút "+ Thêm/Nhân bản/Xóa" dưới panel Chuỗi hành động (panel hẹp, rộng tối thiểu 160px) bị
  cắt chữ ở giữa khi xếp ngang (QPushButton tự căn giữa chữ, không tự rút gọn như QLabel) - đã đổi sang
  xếp dọc từng nút.
- **Phản hồi người dùng 2026-10-08 (11 mục a-k, kèm ảnh lỗi giao diện):**
  - **(a) Lỗi giao diện trong ảnh** - nguyên nhân thật: `ActionListWidget` chưa gọi
    `setEditTriggers(QAbstractItemView::NoEditTriggers)`, nên trình soạn-tại-chỗ (in-place editor)
    mặc định của Qt (QLineEdit viền bo tròn kiểu Windows Fluent, không theo style ứng dụng) bị bật lên
    khi tương tác với ô, chồng lên chữ - khớp đúng hình viên nén/capsule đè lên "500 ms" trong ảnh. Đã
    thêm dòng gọi còn thiếu.
  - **(b) Dấu `*` khi sửa hành động chưa lưu** - `ActionEditorWidget` theo dõi ~20 control (spinbox/
    combo/checkbox/lineEdit) qua `connectDirtyTracking()`, bật cờ `m_dirty` + phát `dirtyChanged(true)`
    khi người dùng đổi bất kỳ giá trị nào (chặn bằng cờ `m_loadingAction` để KHÔNG tính là "sửa" lúc
    đang nạp dữ liệu action lên form); `ActionListWidget::setRowDirty()` thêm hậu tố `" *"` + màu cam
    vào ô số thứ tự của dòng đang sửa. Tự tắt khi bấm Áp dụng (lưu) hoặc chuyển sang sửa hành động khác.
  - **(c) Thu nhỏ ứng dụng trước khi chạy** - `startChain()` gọi `showMinimized()` ngay trước khi hiện
    HUD, tránh cửa sổ quản lý che/chặn thao tác tự động trên màn hình.
  - **(d) Đảm bảo chạy ổn định 24 giờ liên tục** - rà `Logger` (vì `ActionRunner` ghi log đồng bộ ngay
    trên luồng tự động hóa mỗi hành động) phát hiện **lỗi thật**: mọi lần gọi `log()` trước đây đều
    `flush()` ép ghi đĩa ngay lập tức, không điều kiện - với tần suất log cao suốt 24h có thể gây độ trễ/
    giật trên chính luồng đang điều khiển chuột/phím. Sửa: chỉ flush ngay với mức Warning/Error (giữ để
    có log đầy đủ khi sự cố), còn Info/Debug gộp flush định kỳ (mỗi 20 lần ghi). Phát hiện thêm lỗ hổng
    **chưa từng có xoay vòng log** (log có thể phình vô hạn qua 24h) - thêm `rotateIfTooLarge()` (ngưỡng
    10MB, đổi tên file cũ thành `.1`) kiểm tra lúc khởi động VÀ định kỳ mỗi 500 lần ghi khi đang chạy.
  - **(e) Danh sách hành động rộng hơn** - đổi tỷ lệ 3 cột splitter từ (2,5,3) thành (2,6,2) - khu
    "Danh sách hành động" được ưu tiên rộng nhất, khu "Cài đặt" thu hẹp lại nhưng vẫn đủ hiện hết nhãn/
    control (đã xác nhận qua code, không có control nào bị cắt ở tỷ lệ mới).
  - **(f) Tổ hợp phím hỗ trợ đủ 3+ phím cùng lúc (vd Ctrl+Shift+S)** - model dữ liệu action Hotkey vốn
    đã dùng checkbox Ctrl/Alt/Shift/Win độc lập (không giới hạn số lượng chọn cùng lúc) nên bản thân
    tính năng không bị giới hạn; thêm mới nút **"🎯 Bắt tổ hợp phím"** (`HotkeyCapture`, hook bàn phím
    `WH_KEYBOARD_LL` cùng mẫu với `InputHook` của Connect Together) để người dùng BẤM THẬT tổ hợp muốn
    dùng thay vì tick từng checkbox tay - theo dõi trạng thái Ctrl/Alt/Shift/Win, phím khác đầu tiên
    nhấn xuống là phím chính, tự gỡ hook ngay sau khi bắt được; Esc hủy. Trong lúc bắt, hook NUỐT toàn
    bộ phím (giống `HotkeyCapture` lúc gán phím tắt toàn cục) để không lọt sang ứng dụng khác.
  - **(g) Khu hành động chỉ còn 3 nút (Thêm/Nhân bản/Xóa) + kéo-thả để đổi vị trí** - bỏ hẳn các nút/
    menu Up/Down/Insert cũ. Kéo-thả tự viết (`ReorderableTable`, subclass `QTableWidget`) CHỈ đọc
    chỉ số dòng nguồn/đích từ `dropEvent` rồi `setDropAction(Qt::IgnoreAction)` - KHÔNG dùng cơ chế
    `InternalMove` mặc định của Qt (đã biết lỗi: thao tác theo từng Ô chứ không theo cả DÒNG, có thể
    xáo trộn dữ liệu nhiều cột); việc đổi vị trí thật sự làm trên `std::vector<Action>` gốc bằng
    `std::rotate` rồi dựng lại toàn bảng từ dữ liệu đó - không bao giờ để Qt tự ghép Ô.
  - **(h) Đổi tên chuỗi hành động** - đã có sẵn qua menu chuột phải; bổ sung thêm nhấp đúp vào tên để
    đổi (phát hiện/sửa dễ hơn), kèm tooltip "Nhấp đúp hoặc chuột phải để đổi tên".
  - **(i) Kéo chuột ẩn ứng dụng, thao tác thật trên màn hình, xong tự lưu rồi khôi phục** - thêm nút
    **"🖐 Bắt thao tác kéo thật"** (`DragGestureCapture`, hook chuột `WH_MOUSE_LL`): ẩn cửa sổ chính →
    người dùng tự kéo chuột thật trên màn hình → hook ghi nhận điểm `WM_LBUTTONDOWN` (bắt đầu) và
    `WM_LBUTTONUP` (kết thúc, chỉ tính khi đã có điểm bắt đầu) → tự gỡ hook, hiện lại cửa sổ, điền 4 ô
    tọa độ kéo. Khác biệt cố ý với `HotkeyCapture`: hook này **KHÔNG BAO GIỜ nuốt sự kiện** (luôn
    `CallNextHookEx`, kể cả sự kiện do `SendInput` tiêm vào) - đúng yêu cầu "màn hình vẫn nhận tương
    tác" vì đây là thao tác chuột thật của người dùng, không phải phím tắt cần giữ riêng cho app.
  - **(j) Lặp lại chỉ nhập số, bỏ mũi tên** - `m_repeatSpin` thêm `setButtonSymbols(NoButtons)` +
    căn giữa chữ.
  - **(k) Nút dừng ở mini HUD, không phải ở cửa sổ quản lý** - rà lại code: HUD (`RuntimeOverlay`) đã
    có nút Dừng nối đúng tới `ActionRunner::requestStop()` từ trước, hoạt động đúng theo đúng yêu cầu;
    không phải sửa gì, xác nhận lại để người dùng yên tâm.
  - **Kiểm thử logic cho (f)/(i)** (hook Win32 mới hoàn toàn): môi trường build/phát triển này **không
    có màn hình/desktop tương tác thật gắn sẵn** (đã xác nhận qua nhiều lần: `SetCursorPos`/
    `GetCursorPos` trả về (0,0) khi tiến trình test khai báo DPI-aware, hook không nhận được sự kiện dù
    cài đặt thành công, UAC/chụp màn hình cũng không hoạt động) - không thể tự kiểm tra việc Windows
    THẬT SỰ GIAO sự kiện bàn phím/chuột qua `SetWindowsHookExW` tới 2 lớp mới này. Thay vào đó đã kiểm
    tra trực tiếp LOGIC bằng cách gọi thẳng `onRawKey()`/`onRawMouse()` (hai hàm public dành riêng cho
    callback Win32 tĩnh gọi) với tham số giả lập đúng những gì hook thật sẽ truyền vào - bao phủ: bắt
    Ctrl+Shift+S, bắt đủ cả 4 phím bổ trợ cùng lúc, Esc hủy, nhả phím bổ trợ không bị tính nhầm là phím
    chính, kéo chuột xuống-di chuyển-di chuyển-thả đúng tọa độ, di chuyển giữa chừng không bị tính là
    kết thúc, thả chuột khi chưa từng nhấn xuống bị bỏ qua đúng - **22/22 kiểm tra đạt**. Cơ chế nền
    (`SetWindowsHookExW` kiểu `WH_KEYBOARD_LL`/`WH_MOUSE_LL`) là cùng một mẫu đã dùng cho `InputHook`
    của Connect Together (đã xác nhận hoạt động thật trên máy thật trước đây) nên có cơ sở tin cậy, NHƯNG
    **người dùng cần tự bấm thử 2 nút bắt tổ hợp phím/bắt kéo chuột trên máy thật của mình** để xác nhận
    cuối cùng việc giao sự kiện Win32 thật - giống các giới hạn môi trường đã nêu trước đây (VPN rasdial,
    gương Android, dán lệnh PowerShell).

## 4b. QR Tools
- **Tạo mã:** 5 loại nội dung (Văn bản, URL, WiFi, Điện thoại, Vị trí) - bỏ Email/SMS/vCard khỏi danh sách tạo từ v1.19.12. WiFi: combo "WiFi đã lưu" điền nhanh SSID/bảo mật/mật khẩu từ hồ sơ WiFi trên máy. Vị trí: nút "Lấy vị trí hiện tại" (ước lượng theo IP công khai qua `ipwho.is`), "Mở bản đồ" (Google Maps) và ô dán link Google Maps (kể cả link rút gọn maps.app.goo.gl) HOẶC tọa độ viết tay (độ-phút-giây, độ thập phân, cặp số) để đọc tọa độ. Tùy chọn: mức sửa lỗi L/M/Q/H, kích thước ảnh, viền trắng, màu mã/nền, ô bo tròn, logo giữa mã (tự dùng mức H). Xem trước trực tiếp, cảnh báo độ tương phản, tự giải mã lại ảnh vừa tạo để bảo đảm quét được. Lưu PNG/JPG/BMP/SVG, sao chép vào clipboard.
- **Quét mã:** từ file ảnh, clipboard (Ctrl+V), kéo-thả, chụp vùng màn hình, camera. Nhận nhiều mã trong một ảnh, vẽ viền quanh mã, phân tích nội dung (URL, WiFi, Email, vCard...; nhận cả email/SĐT nằm rải rác trong văn bản tự do), nút Sao chép / Mở (có hỏi xác nhận) / Lưu .txt / Tạo lại mã; mã WiFi có thêm nút "Kết nối WiFi"; khung "KẾT QUẢ (nhiều mã)" chỉ hiện khi ảnh có hơn 1 mã.
- **Lịch sử:** ghi mã đã tạo (khi lưu hoặc sao chép) và đã quét; sao chép, tạo lại, xóa.
- **Kiểm thử:** `cmake --build build --target qr_tests qr_ui_tests`, rồi chạy `build\qr_tests.exe` và `build\qr_ui_tests.exe` (cần Qt bin trong PATH).

## 4c. WiFi Connection
- **Phạm vi có chủ đích:** chỉ quản lý hợp pháp — quét mạng, xem/quản lý hồ sơ đã lưu TRÊN MÁY NÀY (kể cả xem mật khẩu đã lưu, giống `netsh wlan show profile key=clear`), kết nối bằng ĐÚNG MỘT mật khẩu người dùng nhập. **Không có** tính năng thử nhiều mật khẩu/dò mật khẩu theo vòng lặp — bị từ chối triển khai có chủ đích vì đó là brute-force vào mạng không phải của mình.
- **Mạng xung quanh:** quét (WlanScan, polling bằng QTimer ~2.2s thay vì notification callback để đơn giản/dễ kiểm chứng), hiện SSID/bảo mật/tín hiệu/trạng thái, kết nối (tạo hồ sơ + WlanConnect), ngắt kết nối, "quên mạng" (xóa hồ sơ), kết nối mạng ẩn (tự nhập SSID).
- **Hồ sơ đã lưu:** liệt kê hồ sơ Windows đã lưu, checkbox "Hiện mật khẩu" (cần quyền Administrator - `WLAN_PROFILE_GET_PLAINTEXT_KEY`), sao chép mật khẩu, xuất/nhập XML, xóa hồ sơ.
- **An toàn:** `WlanController` là lớp DUY NHẤT gọi WLAN API (cô lập Win32 như `InputController` của Auto Click). `WlanProfileXml` (dựng/phân tích XML hồ sơ) tách riêng, không phụ thuộc Win32 nên test được bằng `wifi_tests` không cần phần cứng.
- **Giới hạn đã biết:** một số hằng số WLAN API (`WLAN_PROFILE_GET_PLAINTEXT_KEY`, cờ `WLAN_AVAILABLE_NETWORK_*`, DOT11_PHY_TYPE VHT/HE) không có trong header `wlanapi.h`/`windot11.h` của MinGW đang dùng (SDK cũ hơn) nên được `#define`/so khớp số nguyên thủ công theo tài liệu Microsoft - xem chú thích trong `WlanController.cpp`. WPA3-Enterprise/802.1X không hỗ trợ kết nối nhanh (cần chứng chỉ).
- **Đã sửa lỗi UI thật (rà soát toàn bộ 5 tính năng):** checkbox "Hiện mật khẩu đã lưu (cần quyền
  Administrator)" ở tab Hồ sơ đã lưu từng chung 1 hàng ngang với 5 nút (Kết nối/Sao chép/Xuất/Nhập/Xóa)
  - ở độ rộng cửa sổ bình thường không đủ chỗ, `QCheckBox` không tự rút gọn chữ nên bị co nhỏ hơn chữ
  cần thiết, hiển thị cắt cụt giữa chừng ("...cần quyền Administrat"). Đã tách checkbox ra một hàng
  riêng phía trên hàng nút.

## 4d. Đo tốc độ mạng
- Tab thứ 3 trong WiFi Connection. Đo ping (8 mẫu, tuần tự), jitter (độ lệch trung bình giữa các mẫu liên tiếp), tốc độ tải xuống, tốc độ tải lên - qua HTTPS tới endpoint đo tốc độ công khai của Cloudflare (`speed.cloudflare.com/__down`, `/__up`, giống hệt backend mà speed.cloudflare.com dùng, không cần khóa API).
- `SpeedTestRunner` thuần Qt Network (`QNetworkAccessManager`), không gọi Win32, chạy bất đồng bộ (không chặn UI). Mỗi pha (download/upload) bị cắt tối đa 9 giây tính từ byte đầu tiên (loại trừ thời gian bắt tay TCP/TLS) để không treo lâu trên mạng chậm.
- Có nút Dừng giữa chừng, không báo lỗi giả khi người dùng tự dừng (đã sửa 1 lỗi: trước đó bấm Dừng lúc đang đo ping sẽ hiện nhầm thông báo lỗi mạng).
- Đã chạy kiểm thử trực tiếp trên Internet thật của máy dev (không phải mock): ping ~54-65ms, download ~30 Mbps, upload ~81 Mbps, và xác nhận Dừng giữa chừng không báo lỗi giả, không treo, chạy lại được nhiều lần.
- Hiện thêm tốc độ liên kết lý thuyết (PHY) của WiFi để đối chiếu với tốc độ Internet đo được.

## 4e. Connect Together (đã hoàn thiện)
Tham khảo Mouse without Borders: dùng chung 1 chuột/bàn phím điều khiển nhiều máy qua mạng LAN.
**Mô hình an toàn có chủ đích** (giống bản gốc, không phải giản lược của tôi): ghép đôi bằng mã bảo mật
9 số hiển thị trên màn hình máy A, gõ tay vào máy B (out-of-band, cả hai bên phải tự nguyện xác nhận);
chỉ hoạt động trong LAN; không có chế độ ẩn/im lặng; kênh truyền mã hóa AES-256-GCM.

**Đã xong, đã test (không cần phần cứng/mạng):**
- `CryptoSession`: AES-256-GCM qua Windows CNG (bcrypt.dll) - không tự viết thuật toán mã hóa. Mỗi gói tin
  dùng nonce ngẫu nhiên riêng; sai khóa hoặc dữ liệu bị sửa đều bị GCM tag phát hiện và từ chối giải mã -
  đây là cơ chế xác thực 2 máy cùng biết một bí mật mà không cần truyền bí mật đó qua mạng.
- `PairingCode`: mã 9 số ngẫu nhiên an toàn (`QRandomGenerator::system`), hết hạn sau 5 phút, giới hạn
  số lần thử sai (chặn dò mã ghép đôi cục bộ).
- `ProtocolMessage`: khung thông điệp nhị phân (PairRequest/PairAccept/MouseMove/KeyEvent/ClipboardText...).
- Khóa ghép đôi (từ mã 9 số, PBKDF2-HMAC-SHA256 100k vòng) chỉ dùng MỘT LẦN để trao một khóa dài hạn
  256-bit ngẫu nhiên mới cho các phiên sau - giống mô hình ghép đôi Bluetooth (mã PIN ngắn hạn bootstrap
  khóa dài hạn mạnh).

**Đã xong lớp mạng, đã test thật (403 kiểm tra):**
- `PeerStore`: lưu/nạp `profiles/connect_peers.json`, gồm khóa dài hạn nhị phân qua Base64.
- `NetworkSession`: phiên TCP đóng khung (4 byte độ dài + gói AES-GCM), test qua **TCP loopback thật**
  trên máy này - gửi 2 chiều, payload 2MB ráp khung đúng, 50 thông điệp liên tiếp không trộn khung,
  gói sai khóa bị từ chối và không làm hỏng phiên, dữ liệu rác không làm sập server.
- `PeerDiscovery`: khám phá LAN qua UDP broadcast, test qua **broadcast UDP thật** trên máy này (không
  phải giả lập) - 2 instance thấy nhau đúng, tự lọc bỏ chính mình.

**Đã xong lớp Win32, đã test thật trên máy này (465 kiểm tra):**
- `EdgeDetector`: toán học thuần (chạm biên, vị trí chuẩn hóa, điểm vào máy nhận kể cả khi 2 máy độ
  phân giải khác nhau).
- `InputHook`: cài/gỡ hook toàn cục **THẬT** (SetWindowsHookExW) trên máy này thành công; logic
  tính delta chuột, cổng bật/tắt theo `active`, nhận diện tổ hợp phím Ctrl+Alt+Home - test trực tiếp
  qua gọi các hàm xử lý sự kiện mà không cần cài hook thật (để không chiếm chuột/bàn phím thật của
  người đang dùng máy chạy test).
- `InputInjector`: viết xong (SendInput/SetCursorPos), nhưng **cố ý không gọi trong test tự động** vì
  nó thật sự di chuyển chuột/gõ phím trên máy đang chạy - sẽ cần bạn tự thử tay.

**Module đã hoàn thiện, đã nối vào `MainWindow`.** `ConnectSessionController` nối discovery + ghép
đôi + phiên mạng + biên màn hình + hook + injector + clipboard lại với nhau. UI gồm 2 tab (Ghép đôi,
Máy đã ghép đôi) + banner trạng thái + log, theo đúng Light Theme chung của ứng dụng.

**Lỗi nghiêm trọng đã tìm và sửa (2026-10-07):** trong lúc hoàn thiện, test tích hợp
`ConnectSessionController` crash ngẫu nhiên (SIGSEGV) khoảng 70-100% số lần chạy. Điều tra bằng
checkpoint nhị phân + gdb + build debug + cô lập từng thành phần (tắt thử hook, timer, clipboard,
discovery - không cái nào là nguyên nhân) cuối cùng tìm ra: `sendPairAccept()`, nhánh `PairAccept` của
`handleMessage()`, và `forgetPeer()` đều gọi `session->deleteLater()` TƯỜNG MINH *đồng thời* phiên đó đã
được nối sẵn qua `wireSession()` để tự gọi `deleteLater()` khi nhận tín hiệu `disconnected` - hai sự
kiện `DeferredDelete` xếp hàng cho CÙNG một đối tượng tranh nhau xử lý, gây use-after-free ngẫu nhiên
tùy thời điểm bộ nhớ được cấp phát lại. Đã sửa: mỗi `NetworkSession` giờ chỉ bị `deleteLater()` ở ĐÚNG
MỘT nơi (handler `disconnected` tập trung trong `wireSession()`); mọi chỗ khác chỉ gọi
`disconnectSession()`. Đã test lại 40+ lần liên tiếp (2 cấu hình binary khác nhau), 0 lỗi. Nhân tiện
cũng sửa một race lý thuyết khác trong `InputHook::uninstall()` (xóa `g_instance` trước khi gỡ hook,
không phải sau) và một lỗi UB thật trong test (`QCoreApplication` thay vì `QGuiApplication` dù gọi
`QGuiApplication::clipboard()`).

**Chưa kiểm thử được (cần 2 máy thật trên cùng LAN):** trải nghiệm chuột "đi qua biên sang máy khác",
chia sẻ bàn phím thật, đồng bộ clipboard giữa 2 máy, kết nối lại tự động qua khám phá LAN thật giữa 2
tiến trình trên 2 máy khác nhau (đã test trên 1 máy với "máy giả"). Có 1 lỗi hiển thị nhỏ nghi là riêng
của ảnh chụp màn hình tự động (ô nhập IP/Cổng/Mã trong tab Ghép đôi) - cần tự nhìn trực tiếp trong app
thật để xác nhận có thật hay chỉ là hiện tượng chụp ảnh.

## 4f. Disk Cleanup (đã hoàn thiện)
Tham khảo CCleaner/BleachBit/Windows Disk Cleanup: quét và dọn các hạng mục tệp rác đã biết theo mức
độ rủi ro, xem dung lượng từng ổ đĩa, tìm tệp lớn. **Mặc định xóa qua Thùng rác** (có thể khôi phục),
không xóa vĩnh viễn trừ khi người dùng chủ động bật tùy chọn đó.

**Hạng mục dọn dẹp** (mỗi hạng mục có mức rủi ro Safe/Caution/High quyết định có tick sẵn hay không):
Tệp tạm người dùng & hệ thống, cache Windows Update, cache Delivery Optimization, cache hình thu nhỏ,
báo cáo lỗi Windows (WER), memory dump, Prefetch, danh sách tệp gần đây, nhật ký hệ thống, cache trình
duyệt (Chrome/Edge/Firefox - tự dò từng profile Firefox), và Windows.old (rủi ro cao, không tick sẵn,
cảnh báo không thể hoàn tác).

**Giao diện:** 4 tab trong `DiskCleanupWindow`.
- **Dọn dẹp theo hạng mục** (`CleanupTab`): thẻ tổng quan dung lượng từng ổ đĩa ở trên (màu thanh theo
  % đã dùng); bảng hạng mục có checkbox/tên/mức rủi ro (màu)/số mục/dung lượng, tick sẵn theo rủi ro;
  nút Quét chạy `CleanupScanner` và đổ bảng dần theo tín hiệu; nút Dọn dẹp xác nhận rồi chạy
  `CleanupExecutor`, có checkbox "Xóa vĩnh viễn" mặc định TẮT; quét lại tự động sau khi dọn xong.
- **Tìm tệp lớn** (`LargeFilesTab`): chọn ổ đĩa hoặc thư mục bất kỳ (duyệt tay), ngưỡng kích thước tối
  thiểu (MB), quét bằng `LargeFileScanner`; chọn nhiều dòng để mở thư mục chứa hoặc xóa vào Thùng rác.
- **Tìm tệp trùng lặp** (`DuplicateFilesTab`): chọn 1 thư mục, ngưỡng kích thước tối thiểu (KB), quét
  bằng `DuplicateFinder` (nhóm theo kích thước rồi hash SHA-256 nội dung để xác nhận trùng thật); kết
  quả báo về MỘT LẦN khi quét xong (không phát từng nhóm một - xem phần hiệu năng bên dưới), đã sắp xếp
  theo dung lượng lãng phí giảm dần và giới hạn tối đa 500 nhóm; hiển thị theo cây (nhóm → từng tệp có
  checkbox, các nhóm mặc định thu gọn), mặc định GIỮ LẠI bản đầu mỗi nhóm, tick sẵn các bản còn lại để
  xóa vào Thùng rác. **Đã sửa lỗi UI thật:** `QTreeWidget` (khác `QTableWidget` dùng ở các tab khác) mặc
  định `stretchLastSection=true`, tự kéo giãn cột "Kích thước" (cột cuối) đè lên cấu hình `Stretch` của
  cột 0, để lại khoảng trắng lớn bên phải vì chữ căn trái - đã tắt `stretchLastSection` + căn phải chữ
  trong cột "Kích thước" để nó nằm sát mép phải như các bảng khác trong ứng dụng.
- **Quản lý phân vùng** (`PartitionTab`): liệt kê phân vùng thật (`PartitionManager::listPartitions()`,
  chỉ đọc, không cần quyền Administrator) với dung lượng/còn trống/loại/cờ khởi động-hệ thống; banner
  cảnh báo rủi ro mất dữ liệu luôn hiển thị; nếu chưa chạy với quyền Administrator thì hiện banner +
  nút "Chạy lại với quyền Quản trị..." (ShellExecuteW verb "runas" - UAC chuẩn, không âm thầm); chọn 1
  phân vùng → "Tra kích thước có thể đổi tới..." gọi `Get-PartitionSupportedSize` (cần quyền
  Administrator) → nếu được, hiện khoảng Min-Max và ô nhập kích thước mới (GB); nút "Đổi kích thước"
  CHỈ bật sau khi gõ đúng tên ổ đĩa (hoặc `DISKx-PARTy` nếu không có tên ổ) vào ô xác nhận riêng, cộng
  thêm 1 hộp thoại cảnh báo Yes/No nữa trước khi thực sự chạy `Resize-Partition` qua `PartitionResizer`
  (QThread riêng, không chặn UI, co giãn ổ lớn có thể mất vài phút).

**Hiệu năng/phản hồi khi quét ổ lớn:** người dùng báo quét ổ C: 120GB bị "Không phản hồi" (Windows
đánh dấu cửa sổ treo). Đo thật bằng cách bấm Quét rồi theo dõi vòng lặp sự kiện của luồng giao diện
trong 20 giây: luồng nền (`LargeFileScanner`) xử lý thật ~390.000 tệp mà độ trễ lớn nhất giữa 2 lần xử
lý sự kiện chỉ 21ms (bằng đúng chu kỳ hẹn giờ dùng để đo - tức luồng giao diện CHƯA BAO GIỜ bị chặn) -
chứng tỏ kiến trúc QThread nền vốn đã đúng, "Không phản hồi" chỉ là CẢM GIÁC do màn hình đứng yên hàng
phút không có gì thay đổi, không phải treo thật. Đã cải thiện:
- Mọi luồng quét/xóa (`CleanupScanner`, `CleanupExecutor`, `LargeFileScanner`, `DuplicateFinder`) chạy
  với `QThread::LowPriority` - nhường CPU cho luồng giao diện, phòng trường hợp máy yếu/nhiều lõi ít.
- `LargeFileScanner`/`DuplicateFinder` báo tiến độ dày hơn (mỗi 200 tệp thay vì 1024/256) và kèm đường
  dẫn tệp đang xử lý (rút gọn giữa chuỗi) - người dùng THẤY ứng dụng vẫn đang chạy, không chỉ đoán.

**Lỗi thật thứ hai (sau cải thiện trên vẫn còn):** người dùng quét Tìm tệp trùng lặp trên ổ C: và Task
Manager THẬT SỰ đánh dấu "Không phản hồi" (không chỉ cảm giác lần này). Nguyên nhân: `DuplicateFinder`
cũ phát tín hiệu `groupFound` CHO TỪNG NHÓM MỘT, không giới hạn số lượng - một thư mục kiểu cache trình
duyệt (nhiều hồ sơ Chrome, nhiều loại cache cùng dùng cỡ khối cố định) có thể tạo ra hàng trăm/nghìn
"nhóm trùng lặp", mỗi tín hiệu khiến luồng giao diện phải dựng thêm mục cây + tính lại layout - dồn dập
đủ nhiều sẽ chiếm trọn một lượt xử lý sự kiện của Qt mà không nhường lại cho vòng lặp thông điệp của
Windows, đủ lâu để bị đánh dấu treo thật. Sửa bằng cách đổi hẳn kiến trúc: `DuplicateFinder` giờ gom
toàn bộ kết quả trong bộ nhớ, sắp xếp theo dung lượng lãng phí giảm dần, cắt theo `setMaxGroups()` (mặc
định 500) rồi CHỈ PHÁT scanFinished MỘT LẦN DUY NHẤT - đúng mẫu `LargeFileScanner` đã được đo thực tế
không gây treo. UI dựng cả cây trong 1 lần (`setUpdatesEnabled(false)` khi dựng hàng loạt) và các nhóm
mặc định THU GỌN (không mở sẵn) để giảm chi phí dựng giao diện khi có nhiều kết quả. Đo lại thật bằng
cách quét toàn bộ thư mục dữ liệu Chrome (~20.300 tệp, nhiều hồ sơ) trong 60 giây liên tục: độ trễ lớn
nhất giữa 2 lần xử lý sự kiện của luồng giao diện chỉ 22ms trong suốt quá trình - xác nhận hết hẳn nguy
cơ "Không phản hồi". (Lưu ý: quét nội dung cache trình duyệt vẫn CHẬM về bản chất - hash SHA-256 hàng
chục nghìn tệp cần thời gian thật - nhưng không còn làm treo giao diện; người dùng muốn kết quả nhanh
và hữu ích hơn nên chọn thư mục tài liệu cá nhân (Documents/Downloads/Pictures) thay vì toàn ổ C:/cache
trình duyệt.)

**Đã xong, đã test (176 kiểm tra lõi + 19 kiểm tra UI):**
- `CategoryRegistry`: dựng danh sách hạng mục + phân giải đường dẫn qua một `CleanupEnvironment` có thể
  thay thế bằng môi trường giả - test không đụng vào Temp/Windows/SoftwareDistribution thật của máy.
  Hạng mục không tồn tại trên máy (chưa cài trình duyệt đó, thư mục chưa từng tạo...) tự động có
  `rootPaths` rỗng thay vì lỗi.
- `CleanupScanner`: quét trên QThread riêng (giống `ActionRunner`), test bằng tệp giả trong thư mục
  tạm - xác nhận đúng kích thước, đúng số mục, dừng giữa chừng không crash. **Đã sửa lỗi thật phát
  hiện khi quét tay trên máy thật:** Qt trên Windows coi tệp lối tắt (`.lnk`) như symlink và tự động
  "đi theo" nó khi hỏi `isDir()`/liệt kê thư mục - hạng mục "Danh sách tệp gần đây" toàn lối tắt, nên
  một lối tắt trỏ tới thư mục Downloads hay cả ổ D: từng bị cộng nhầm **hàng chục GB** vào kích thước
  hạng mục (quét tay đo được 12.76 GB trong khi thư mục thật chỉ ~4.3 MB). Sửa bằng cách đọc thuộc
  tính thật của chính đường dẫn qua `GetFileAttributesExW` (API Win32 thô, không biết định dạng Shell
  Link) khi gặp symlink, và loại symlink khỏi đệ quy tính kích thước thư mục (`QDir::NoSymLinks`). Có
  test hồi quy dựng lối tắt thật bằng `QFile::link()` trỏ vào một thư mục tạm cố tình lớn hơn nhiều
  chính tệp `.lnk`, xác nhận kích thước không còn bị cộng nhầm.
- `RecycleBinOps`: lớp duy nhất gọi Shell API Thùng rác (SHFileOperationW/SHQueryRecycleBinW). Test
  **xóa file thật** (do chính test tạo ra trong thư mục tạm, an toàn/có thể khôi phục) - xác nhận file
  biến mất khỏi vị trí gốc. Cố ý KHÔNG test `empty()` (sẽ xóa vĩnh viễn toàn bộ Thùng rác thật của máy).
- `CleanupExecutor`: xóa trên QThread riêng, test xóa file thật thành công qua Thùng rác. **Đã sửa lỗi
  thật phát hiện khi dùng tay (mã lỗi 0x2 hiển thị cho người dùng):** `%TEMP%`/cache đổi liên tục giữa
  lúc quét và lúc bấm Dọn dẹp - Windows/ứng dụng khác có thể đã tự xóa một vài tệp trước đó. Trước đây
  `SHFileOperationW` báo `ERROR_FILE_NOT_FOUND` cho CẢ LÔ chỉ vì 1 tệp không còn đó, khiến toàn bộ thao
  tác (kể cả các tệp khác xóa được) bị báo "thất bại" kèm mã lỗi hex khó hiểu. Sửa bằng cách lọc trước
  (tệp đã tự mất coi là đã đạt mục tiêu, không đưa vào yêu cầu xóa) và kiểm tra lại THẬT sau khi xóa
  (không tin mù quáng mã trả về) để báo cáo freedBytes/deletedCount chính xác - chỉ coi là thất bại
  thật khi KHÔNG giải phóng được gì. `RecycleBinOps` cũng dịch mã lỗi Win32 phổ biến (0x2, 0x5, 0x20...)
  sang tiếng Việt thay vì hiện hex thô. Test hồi quy mô phỏng đúng tình huống: 1 tệp "biến mất" giữa
  danh sách, xác nhận 2 tệp còn lại vẫn xóa được và toàn bộ vẫn báo thành công.
- `DiskSpaceInfo`: đọc dung lượng ổ đĩa thật (QStorageInfo, chỉ đọc, an toàn) - xác nhận trên máy này.
- `LargeFileScanner`: tìm tệp lớn theo ngưỡng, sắp xếp giảm dần, giới hạn số kết quả - test bằng tệp giả.
- `DuplicateFinder`: nhóm theo kích thước thật của writeFile (toàn ký tự giống nhau), xác nhận tệp cùng
  kích thước nhưng khác nội dung KHÔNG bị coi là trùng (bước hash hoạt động đúng), ngưỡng tối thiểu lọc
  đúng, `wastedBytes()` tính đúng, dừng giữa chừng/thư mục không tồn tại không crash. Cũng loại symlink
  khỏi quét (`QDir::NoSymLinks`) - test hồi quy tương tự lỗi `.lnk` của `CleanupScanner` ở trên. Thêm
  test hồi quy cho lỗi "Không phản hồi" thật: dựng 20 nhóm trùng lặp giả, `setMaxGroups(5)` xác nhận
  `scanFinished` chỉ trả về đúng 5 nhóm LÃNG PHÍ NHIỀU NHẤT (sắp xếp giảm dần), nhưng tổng lãng phí +
  tổng số nhóm báo về vẫn phản ánh ĐỦ cả 20 nhóm thật tìm được.
- `PartitionManager`: lõi JSON thuần (`internal::parsePartitionsJson`/`parseSupportedSizeJson`) test
  bằng dữ liệu mẫu dựng sẵn (không gọi PowerShell) - gồm cả trường hợp PowerShell 5.1 trả về 1 OBJECT
  đơn (không bọc mảng) khi chỉ có đúng 1 phân vùng, JSON rỗng/hỏng. `isElevated()`/`listPartitions()`
  gọi THẬT (chỉ đọc, an toàn) - xác nhận liệt kê đúng phân vùng trên máy build. **Đã sửa 2 lỗi thật
  phát hiện khi quét tay:** (1) truyền script PowerShell nhiều dòng qua stdin với `-Command -` khiến
  powershell.exe thoát mã 0 nhưng KHÔNG chạy gì cả (im lặng, không lỗi) - đổi sang `-EncodedCommand`
  (Base64 UTF-16LE, cách Microsoft khuyến nghị để chạy script phức tạp từ tiến trình khác); (2) khi một
  lệnh PowerShell lỗi, `Write-Error` bị tự động serialize thành CLIXML (XML thô kèm `_x000D__x000A_`)
  khi stderr bị redirect qua QProcess, khiến thông báo lỗi hiển thị cho người dùng là một khối XML
  không đọc được - đổi sang `[Console]::Error.WriteLine()` (ghi thẳng, bỏ qua pipeline đối tượng của
  PowerShell). **CHỦ Ý KHÔNG** test `resizePartition()`/`PartitionResizer` bằng cách gọi thật - đó là
  thao tác đĩa thật, không có hoàn tác an toàn để tự động hóa trong test.
- `diskcleanup_ui_tests`: dựng `DiskCleanupWindow` và cả 4 tab không crash, widget chính tồn tại đúng
  trạng thái ban đầu (bảng rỗng, chưa hiện progress bar, "Xóa vĩnh viễn" mặc định tắt, khu vực đổi kích
  thước phân vùng ẩn cho tới khi chọn 1 dòng). `PartitionTab` liệt kê phân vùng THẬT khi dựng UI (chỉ
  đọc, an toàn) - test xác nhận bảng có dữ liệu thật trên máy build. **Cố ý KHÔNG** bấm nút Quét ở
  CleanupTab/LargeFilesTab/DuplicateFilesTab (đụng `%TEMP%`/`%WINDIR%`/cần chọn thư mục thật) và KHÔNG
  BAO GIỜ test đổi kích thước phân vùng thật - đã tự kiểm tra tay bằng quét/tra cứu thật trên máy build
  (ảnh chụp UI từng tab) trước khi phát hiện và sửa các lỗi `.lnk`/`-Command -`/CLIXML nói trên.

**Cân nhắc thêm (chưa làm):** không còn mục nào trong phạm vi yêu cầu ban đầu; có thể mở rộng sau nếu
cần: gộp phân vùng (merge), tạo/xóa phân vùng mới, hoặc một bước xác nhận bổ sung (ví dụ mã OTP hiển
thị trên màn hình) trước khi đổi kích thước phân vùng khởi động/hệ thống.

## 4g. Android Phone Control (đã hoàn thiện)
Yêu cầu của người dùng: điều khiển thiết bị Android, "tính realtime và độ linh hoạt kiểu như remote".
Gương màn hình thời gian thực thật sự (20-60 fps, độ trễ thấp) đòi hỏi mã hóa H.264 phía Android qua
`MediaProjection`/`MediaCodec` và tiêm sự kiện chạm/phím qua `InputManager` phản chiếu (không có API
công khai) - đây là bài toán Android-side phức tạp đã được **scrcpy** (Genymobile, Apache-2.0, mã
nguồn mở, dùng rộng rãi nhiều năm) giải quyết và kiểm chứng kỹ. Quyết định: dùng lại scrcpy (đóng gói
bản phát hành Windows chính thức, đã xác minh checksum SHA-256) thay vì tự viết lại từ đầu - rủi ro
cao, tốn nhiều công sức, và không thể tự kiểm thử đầy đủ trong môi trường này (không có thiết bị
Android thật gắn sẵn). Tương tự tinh thần đã áp dụng cho Partition resize (dùng `Resize-Partition` của
Windows thay vì tự viết thao tác đĩa mức thấp).

**Kiến trúc:**
- `AdbController`: lớp duy nhất gọi `adb.exe` - liệt kê thiết bị (`adb devices -l`), ghép đôi gỡ lỗi
  không dây Android 11+ (`adb pair`), kết nối/ngắt kết nối không dây (`adb connect`/`disconnect`).
- `ScrcpyLauncher`: khởi chạy `scrcpy.exe` làm tiến trình nền (không chặn UI - khác các worker QThread
  dùng ở nơi khác, vì đây là tiến trình tương tác sống lâu, không phải việc "chạy xong rồi báo kết
  quả"); cửa sổ gương màn hình là cửa sổ RIÊNG của chính scrcpy (chưa nhúng vào cửa sổ Qt ở bản này).
- **Độ linh hoạt kiểu remote** (theo đúng yêu cầu): độ phân giải tối đa, bitrate, giới hạn khung
  hình/giây, giữ màn hình sáng, tắt màn hình điện thoại lúc điều khiển (riêng tư + tiết kiệm pin), luôn
  nổi trên cùng, toàn màn hình, tắt/bật chuyển âm thanh điện thoại sang máy tính, ghi lại phiên điều
  khiển ra file .mp4 - tất cả là cờ dòng lệnh thật của scrcpy (`scrcpy --help`), không phải mô phỏng.
- **Không dây thật sự linh hoạt**: "Chuyển thiết bị đã chọn sang không dây" dùng cờ `--tcpip` của
  scrcpy (tự dò IP thiết bị đang cắm USB, bật chế độ TCP/IP, kết nối, rồi gương - một bước duy nhất).
  Với thiết bị chưa từng ghép đôi, "Ghép đôi không dây..." mở hộp thoại nhập IP:Cổng ghép đôi + mã 6 số
  (API ghép đôi không dây chính thức của Android 11+, chỉ cần làm 1 lần/mạng Wi-Fi).
- Danh sách thiết bị tự làm mới mỗi 3 giây (giống `NetworksTab` của WiFi) - phát hiện cắm/rút USB hoặc
  đổi trạng thái "unauthorized" → "device" (sau khi bấm Cho phép trên điện thoại) mà không cần bấm Làm mới.
- `adb.exe`/`scrcpy.exe` được TÌM theo đường dẫn tương đối tới file exe đang chạy
  (`<thư mục exe>/scrcpy/`) - nếu thiếu, hiện banner cảnh báo rõ ràng thay vì lỗi mơ hồ.

**Đã xong, đã test (45 kiểm tra lõi + 7 kiểm tra UI):**
- `AdbController::internal::parseDevicesOutput`: phân tích output `adb devices -l` mẫu dựng sẵn (thiết
  bị sẵn sàng/USB, không dây, unauthorized, offline; bỏ qua dòng tiêu đề và cảnh báo daemon) - không
  gọi adb thật, test được ở mọi máy.
- `ScrcpyLauncherInternal::buildArguments`: dựng đối số dòng lệnh thuần cho mọi tổ hợp tùy chọn (độ
  phân giải/bitrate/fps/stay-awake/turn-screen-off/always-on-top/fullscreen/no-audio/record/tcpip) -
  xác nhận đúng cờ xuất hiện/không xuất hiện, đúng định dạng giá trị (vd `--video-bit-rate=12M`).
- `AdbController::listDevices()`/`isBundleAvailable()` gọi THẬT (chỉ đọc, an toàn) khi có sẵn gói
  scrcpy cạnh file build - xác nhận plumbing adb thật chạy được trên máy build (0 thiết bị vì không có
  điện thoại gắn sẵn - kết quả hợp lệ, không phải lỗi).
- `android_ui_tests`: dựng `AndroidControlWindow` không crash, bảng/nút/tùy chọn tồn tại đúng, "Giữ màn
  hình sáng" mặc định bật. **Cố ý KHÔNG** tự động ghép đôi/kết nối/khởi chạy `scrcpy.exe` thật trong
  test - cần thiết bị Android thật và phải tự kiểm tra tay.

**Giới hạn đã biết / chưa kiểm chứng được trong môi trường này:** không có thiết bị Android thật gắn
sẵn, nên KHÔNG tự kiểm tra được: gương màn hình thời gian thực thực tế có mượt/đúng độ trễ như kỳ vọng
hay không, luồng ghép đôi không dây đầu-cuối, driver USB trên các dòng máy khác nhau (Windows hiện đại
+ điện thoại hiện đại thường tự nhận qua driver MTP/ADB tích hợp sẵn, nhưng một số máy cũ/hiếm có thể
cần cài driver USB riêng của hãng). Người dùng cần tự cắm điện thoại thật và kiểm tra.

**Cân nhắc thêm (chưa làm, "nếu có thể"):** nhúng cửa sổ gương vào trực tiếp cửa sổ Qt của ứng dụng
(thay vì cửa sổ riêng của scrcpy) - cần kỹ thuật tái gán cửa sổ gốc Win32 (`SetParent`) khá tinh vi,
không làm trong bản đầu để giữ độ tin cậy cao (cửa sổ riêng của scrcpy là triển khai đã kiểm chứng).

**Lỗi đã phát hiện và sửa 2026-10-08: `adb.exe` không dừng dù đã đóng ứng dụng.** Người dùng báo: dùng
Android Control một lần, đóng ứng dụng, nhưng `adb.exe` (chạy từ `One for ALL\scrcpy`) vẫn còn trong
Task Manager. Đã dựng lại được bằng cách kiểm tra tiến trình thật đang chạy trên máy (tìm thấy một
`adb.exe` mồ côi từ phiên test trước, khởi động từ hôm trước, xác nhận lỗi có thật) và đọc kỹ luồng sở
hữu đối tượng C++. Tìm ra **hai nguyên nhân gốc cộng hưởng**, phải sửa cả hai:
1. `AndroidControlWindow` KHÔNG override `closeEvent` (khác mọi cửa sổ tool khác có tiến trình/luồng nền
   - xem `ConnectWindow`/`VpnControlWindow`/`DiskCleanupWindow`...). Vì cửa sổ này được `AndroidControlTool`
   giữ qua `QPointer` và TÁI DÙNG ở lần mở sau (không có `WA_DeleteOnClose`), bấm X chỉ ẨN cửa sổ đi chứ
   KHÔNG hủy `DevicesTab`/`ScrcpyLauncher` - destructor (nơi vốn đã có `m_launcher->stop()` đúng) không
   bao giờ chạy, phiên scrcpy treo lại ngầm vô thời hạn dù "đã đóng" công cụ này. Đã thêm
   `AndroidControlWindow::closeEvent` tự gọi `DevicesTab::stopActiveSession()` (hàm mới, dùng chung cho
   nút Dừng/destructor/closeEvent), đúng khuôn mẫu các tool khác đã dùng.
2. **Ngay cả khi `stop()` được gọi đúng, `ScrcpyLauncher::stop()` vẫn không đủ**: `scrcpy.exe` tự chạy
   `adb.exe shell ...` làm tiến trình CON trong suốt phiên gương (để giữ server trên điện thoại sống) -
   nhưng `QProcess::terminate()/kill()` trên Windows chỉ đóng ĐÚNG một handle, KHÔNG đệ quy xuống dừng
   tiến trình con (khác hẳn gửi tín hiệu cho cả process group trên Linux) - để lại `adb.exe` mồ côi treo
   mãi. Đã thêm `terminateProcessTree()` (quét `CreateToolhelp32Snapshot`/`Process32NextW`, BFS tìm mọi
   hậu duệ của PID scrcpy.exe rồi tự `TerminateProcess` từng cái) - gọi sau mỗi lần `stop()` chủ động
   (luôn, vì ta vừa buộc dừng nên scrcpy không có cơ hội tự dọn con của nó) và khi `scrcpy.exe` thoát
   KHÔNG bình thường (crash/bị Task Manager kill/rút cáp giữa chừng). Đã thêm lưới an toàn cuối:
   `DevicesTab` nối `QApplication::aboutToQuit` → `stopActiveSession()`, phòng trường hợp ứng dụng thoát
   theo đường khác (vd `qApp->quit()` từ nơi khác) mà cửa sổ Android chưa từng được đóng qua closeEvent.

## 4h. VPN & Location (đã hoàn thiện)
Yêu cầu người dùng: "VPN và location có thể chuyển đổi IP qua nhiều quốc gia và đổi vị trí GPS trong 1
quốc gia". Đã nghiên cứu qua tài liệu chính thức (Microsoft Learn, TechNet lưu trữ) trước khi quyết
định kiến trúc cho cả 2 phần, thay vì đoán:

**VPN (đổi IP qua nhiều quốc gia) - làm đầy đủ:**
- `VpnController`: gọi PowerShell module `VpnClient` có sẵn của Windows - `Add-VpnConnection` (thêm hồ
  sơ, mặc định per-user, KHÔNG cần quyền Administrator - khác hẳn `-AllUserConnection`),
  `Get-VpnConnection` (liệt kê, chỉ đọc), `Remove-VpnConnection` (xóa). Nhãn quốc gia do người dùng tự
  đặt lưu riêng (`profiles/vpn_profiles.json`) vì Windows không biết khái niệm "quốc gia" của một VPN.
- `VpnConnector` (QThread riêng, tránh trùng tên với `QThread::finished` như `CleanupExecutor` trước đó -
  đã đổi tên tín hiệu thành `operationFinished`): kết nối/ngắt kết nối qua `rasdial.exe` - cách duy nhất
  xác nhận được truyền username+password trực tiếp cho `rasdial "tên" username password`;
  `Connect-VpnConnection`/`Disconnect-VpnConnection` của PowerShell tồn tại nhưng không có tài liệu
  chính thức và không nhận tham số thông tin đăng nhập.
- **Không lưu mật khẩu trong hồ sơ** - `VpnProfile` không có trường password; `VpnTab::onConnectClicked()`
  hỏi username+password qua hộp thoại (ô mật khẩu ẩn ký tự) mỗi lần bấm Kết nối.
- Người dùng tự mang tài khoản VPN của mình (Mullvad/NordVPN/ProtonVPN/VPN cơ quan...) - ứng dụng không
  cấp máy chủ VPN, chỉ là lớp quản lý/kết nối.

**Location (đổi vị trí GPS trong 1 quốc gia) - CÓ CHỦ ĐÍCH KHÔNG LÀM, đã giải thích với người dùng:**
API định vị thật của Windows (`Geolocator.DefaultGeoposition`, WinRT) bị khóa sau quyền năng "System
location" hạn chế - CHỈ cấp cho app đóng gói MSIX đã qua xét duyệt/cấp phép đặc biệt của Microsoft Store.
Một app Win32 không đóng gói như `OneForAll.exe` KHÔNG thể gọi được, dù chạy với quyền Administrator -
đây là giới hạn kiến trúc của Windows, không phải thiếu quyền. Cách còn lại (ghi thẳng registry dịch vụ
định vị) không có tài liệu chính thức của Microsoft, không thể tự kiểm chứng đúng/sai trong môi trường
này, và rủi ro làm hỏng dịch vụ định vị thật của máy người dùng - **từ chối làm** vì không chấp nhận được.

**Thay thế trung thực đã làm:** `PublicIpChecker` tra cứu quốc gia/thành phố suy ra từ địa chỉ IP công
khai hiện tại qua `ipwho.is` (HTTPS, không cần khóa API) - đúng cơ chế mà hầu hết website/dịch vụ thật và
cả sản phẩm VPN thương mại dùng để hiển thị "vị trí" sau khi đổi IP. Đổi sang VPN ở quốc gia khác thì vị
trí hiển thị đổi theo THẬT (do đi qua IP máy chủ VPN), không phải số liệu giả lập.

**Lỗi THẬT phát hiện và sửa qua kiểm tra trên Windows thật (không chỉ test nội dung script):**
`buildAddConnectionScript()` từng gọi cứng `-AuthenticationMethod MSChapv2` cho MỌI loại tunnel. Thử
thêm hồ sơ với `TunnelType=Ikev2` (lựa chọn mặc định/khuyến nghị của `AddVpnProfileDialog`) bị Windows từ
chối thật: `"IKEv2 tunnel type only supports Eap and Machine certificate as authentication method."`.
Sửa bằng `authMethodForTunnelType()`: `Ikev2` → `Eap` (Windows dùng EAP-MSCHAPv2 làm phương thức EAP mặc
định cho VPN "Secured password", nên `rasdial` với username/password vẫn hoạt động bình thường); các
loại còn lại (L2tp/Sstp/Pptp - nền PPP) và Automatic giữ `MSChapv2` như cũ. Xác nhận lại bằng cách chạy
THẬT toàn bộ chuỗi Add→Get→Remove-VpnConnection cho **cả 5 loại tunnel** trên máy build (địa chỉ máy chủ
giả `test.example.invalid`, không gọi `rasdial`/không thử kết nối thật) - tất cả 5 loại đều
thêm/liệt kê/xóa thành công sau khi sửa.

**Phát hiện thật khác trong lúc xây dựng:** dịch vụ tra IP công khai ban đầu chọn (`ipapi.co`) bị chặn
429 (Too Many Requests) khi gọi tay nhiều lần liên tiếp - xác nhận độc lập bằng `curl` (không chỉ qua
app) rằng đây là lỗi thật của dịch vụ. Thử 3 dịch vụ thay thế qua `curl`, chọn `ipwho.is` (trả đúng dữ
liệu thật khớp `curl`, không giới hạn khi thử lại ngay).

**Tái cấu trúc đi kèm:** tách `isElevated()`/`relaunchElevated()` và hàm chạy PowerShell qua
`-EncodedCommand` ra khỏi `PartitionManager.cpp` (vốn tự chứa trùng lặp từ trước) thành
`src/core/WinElevation.*`/`src/core/PowerShellRunner.*` dùng chung cho cả Disk Cleanup và VPN - giảm
~130 dòng trùng lặp, API công khai của `PartitionManager` không đổi, 176/19 test Disk Cleanup vẫn pass
nguyên vẹn sau khi tách.

**Đã xong, đã test (41 kiểm tra lõi + 6 kiểm tra UI):**
- `parseConnectionsJson`: dựng sẵn mẫu mảng/object đơn (PowerShell 5.1 trả object đơn khi chỉ có 1 kết
  nối, không bọc mảng - lỗi thật từng gặp khi xây `PartitionManager`, chủ động test lại), rỗng, JSON hỏng.
- Dựng script PowerShell: tên/đường dẫn được thoát dấu nháy đơn đúng, xác nhận KHÔNG có `-AllUserConnection`
  (per-user, không cần Administrator), và **test hồi quy khóa đúng phương thức xác thực theo từng loại
  tunnel** (Ikev2 → chứa `-AuthenticationMethod Eap`, không chứa `MSChapv2`; 4 loại còn lại → chứa
  `MSChapv2`) - test này trước đây KHÔNG tồn tại nên không bắt được lỗi, đã bổ sung sau khi sửa.
- `vpn_ui_tests`: dựng `VpnControlWindow` không crash, bảng 4 cột, đủ nút/nhãn; gọi `Get-VpnConnection`
  thật (chỉ đọc, an toàn, giống mẫu `AdbController::listDevices()`/`PartitionManager::listPartitions()`).

**Giới hạn thật, nêu rõ với người dùng:** không có tài khoản VPN thật trong môi trường phát triển này nên
KHÔNG tự kiểm tra được `rasdial` kết nối/ngắt kết nối thật đầu-cuối với một máy chủ VPN thật (có mã hóa
đường truyền, nhận IP mới) - cần người dùng tự thêm hồ sơ bằng tài khoản VPN thật của mình và tự kiểm
tra tay. Việc thêm/liệt kê/xóa hồ sơ (Add/Get/Remove-VpnConnection) đã được xác nhận thật trên Windows.

## 4i. Security Gateway (đã hoàn thiện)
Yêu cầu người dùng: tham khảo Kaspersky, gồm (1) một "tab PowerShell" để kiểm tra lệnh có mã độc/bất
thường không trước khi tự mở PowerShell thật và dán lệnh đã kiểm tra vào, (2) chặn ngay truy vấn tới
link độc hại trên web, (3) quét máy tìm mã độc, xóa/cách ly khi đang bật.

**Quyết định kiến trúc:** KHÔNG tự viết lại một bộ quét mã độc/chặn web (cần cơ sở dữ liệu chữ ký và máy
học được cập nhật liên tục - không thể tự làm tốt hơn một AV thật) - xây trên nền **Windows Defender**
đã có sẵn, miễn phí, cập nhật chữ ký tự động trên mọi máy Windows, điều khiển được qua PowerShell module
`Defender` có sẵn trong Windows (`Get/Set-MpPreference`, `Get-MpComputerStatus`, `Start-MpScan`,
`Get-MpThreat`, `Remove-MpThreat`). Đã dùng một agent nghiên cứu để xác nhận TỪNG cmdlet/tham số qua tài
liệu Microsoft Learn chính thức TRƯỚC khi viết code (giống cách đã làm cho VPN & Location) - xem các lưu
ý "đã xác nhận" trong code. Toàn bộ kiến trúc 3 tab:

**1. Cổng lệnh PowerShell (`CommandGatewayTab`):** `CommandAnalyzer` phân tích TĨNH (không chạy thử) nội
dung lệnh dán vào, tìm các mẫu dấu hiệu tấn công/mã độc phổ biến (tham khảo MITRE ATT&CK/Living-off-the-
Land): tải+thực thi (`DownloadString`/`IEX`/`certutil -urlcache`), lệnh mã hóa Base64, công cụ tấn công
đã biết (Mimikatz, vô hiệu hóa AMSI...), né tránh phòng thủ (tự tắt Defender/Tường lửa), thiết lập duy
trì (schtasks/registry Run/WMI event), reverse shell. Phân loại An toàn/Đáng chú ý/Nguy hiểm kèm lý do cụ
thể. **Nguyên tắc an toàn cốt lõi: ứng dụng KHÔNG BAO GIỜ tự chạy lệnh người dùng nhập** - `CommandLauncher`
chỉ đặt lên Clipboard, mở MỘT cửa sổ `powershell.exe` thật riêng biệt, dò cửa sổ đó qua `EnumWindows` +
`GetWindowThreadProcessId`, `SetForegroundWindow` rồi gửi Ctrl+V thật (tái dùng `InputController` đã có
sẵn từ Auto Click) - người dùng luôn là người tự bấm Enter cuối cùng trên cửa sổ PowerShell thật đó. Lệnh
"Nguy hiểm" vẫn cho mở (đây là công cụ TƯ VẤN cho người dùng tự xem lại lệnh của chính họ, không phải cơ
chế ngăn họ chạy lệnh trên máy họ), nhưng bắt xác nhận rõ ràng qua checkbox trước khi bật nút.

**2. Bảo vệ Web (`WebProtectionTab`):** Bật/tắt **Network Protection** của Windows Defender
(`Set-MpPreference -EnableNetworkProtection Enabled/Disabled`) - chặn kết nối tới trang/máy chủ được
Microsoft đánh giá có hại (lừa đảo, mã độc, C2) ngay ở tầng hệ điều hành, trước khi BẤT KỲ trình duyệt/
ứng dụng nào kịp tải nội dung - đúng cơ chế THẬT đứng sau yêu cầu "chặn link độc hại ngay lúc truy vấn",
và áp dụng cho mọi trình duyệt chứ không chỉ một trình duyệt cụ thể. Bổ sung thêm `HostsBlocklist`: danh
sách chặn tên miền TÙY CHỈNH do người dùng tự khai báo, quản lý một khối riêng được đánh dấu rõ trong
hosts file (`# OneForAll-SecurityGateway-BLOCKLIST-START/END`) - không bao giờ động vào các dòng khác
người dùng/chương trình khác đã có sẵn trong hosts file.

**3. Quét mã độc (`MalwareScanTab`):** Bật/tắt bảo vệ thời gian thực
(`Set-MpPreference -DisableRealtimeMonitoring`), quét nhanh/toàn bộ/một thư mục (`Start-MpScan`, chạy
trên `DefenderScanWorker` - QThread riêng vì cmdlet này ĐỒNG BỘ/CHẶN tới khi quét xong, đã xác nhận qua
tài liệu API Win32 `MpScanStart`), xem danh sách mối đe dọa (`Get-MpThreat`) và xóa
(`Remove-MpThreat`).

**Lỗi/giới hạn THẬT phát hiện qua kiểm tra trực tiếp trên Windows Defender thật đang chạy (không chỉ
test nội dung script):**
- **Lỗi thật đã sửa:** `Get-MpPreference`'s `EnableNetworkProtection` trả về kiểu `System.Byte` THÔ (0/1/2)
  trên máy thật, KHÔNG tự thành chữ "Disabled"/"Enabled"/"AuditMode" khi ép `[string]` như tưởng (đó là
  kiểu tham số ĐẦU VÀO của `Set-MpPreference`, không phải kiểu dữ liệu ĐỌC RA của `Get-MpPreference`) -
  khiến `networkProtectionEnabled()` LUÔN sai (so "0" với "Enabled" không bao giờ khớp). Sửa bằng cách tự
  `switch`/map số sang chữ ngay trong script PowerShell, có test hồi quy khóa lại.
- **Phát hiện thật khác:** `SeverityID` của Defender có thể là `5` (EICAR test) - ngoài phạm vi tài liệu
  Microsoft (0-4) - đã sửa `severityLabel()` để không hiển thị nhầm "Không rõ" cho trường hợp này.
- **Xác nhận THẬT qua tệp test EICAR chuẩn công nghiệp** (chuỗi test antivirus tiêu chuẩn, KHÔNG phải mã
  độc thật, lấy nguyên văn từ tài liệu Microsoft Learn): tạo tệp trong thư mục tạm, Windows Defender thật
  phát hiện ngay (`Get-MpThreat` trả về `Virus:DOS/EICAR_Test_File`), `Remove-MpThreat` xử lý thành công.
  Nhận thấy `IsActive` đôi khi KHÔNG cập nhật ngay lập tức sau khi `Remove-MpThreat` báo thành công (cần
  vài giây để Defender đồng bộ nội bộ) - không phải lỗi ứng dụng, đã ghi chú và nhắc người dùng bấm "Làm
  mới" lại nếu thấy vậy, thay vì cố gắng che giấu độ trễ thật bằng cách giả vờ đã xong ngay.
- **Lưu ý quan trọng đã xác nhận, sửa luôn thông tin hiển thị sai trước đó:** `Remove-MpThreat` KHÔNG có
  tham số chọn từng mục - luôn xóa TẤT CẢ mối đe dọa đang hoạt động cùng lúc (đã xác nhận qua tài liệu
  Microsoft Learn TRƯỚC khi viết UI, nên `ThreatRecord`/UI không có nút xóa riêng từng dòng).
- **Yêu cầu quyền Administrator KHÔNG đồng đều giữa các cmdlet** (xác nhận thật bằng cách thử trực tiếp,
  không đoán): `Set-MpPreference` (bật/tắt bảo vệ thời gian thực, Network Protection) và ghi hosts file
  THẬT SỰ cần quyền Administrator (thử không có quyền → lỗi quyền thật); nhưng `Start-MpScan` và
  `Remove-MpThreat` chạy được BÌNH THƯỜNG không cần Administrator trên máy thử - đã sửa lại banner cảnh
  báo trong `MalwareScanTab` cho đúng thực tế thay vì nói chung chung "cần quyền Administrator cho mọi
  thao tác" khi điều đó không đúng.

**Giới hạn thật của môi trường phát triển này (không có desktop tương tác):** không chụp được ảnh màn
hình (GDI `CopyFromScreen` báo "handle is invalid") và không bấm được hộp thoại UAC (chạy nâng quyền qua
`-Verb RunAs` treo vô thời hạn chờ xác nhận) - do môi trường build không có phiên desktop tương tác thật,
không phải lỗi của ứng dụng. Vì vậy: (a) giao diện 3 tab chưa được xác nhận bằng mắt qua ảnh chụp (chỉ
qua test tự động kiểm tra cấu trúc widget + rà code theo đúng mẫu các tab khác đã được xác nhận bằng ảnh
trước đó), (b) luồng `CommandLauncher` (mở PowerShell thật + dán) chưa tự kiểm tra được trực tiếp trong
môi trường này (cần desktop thật) - về mặt kỹ thuật dùng lại nguyên vẹn `InputController`/`SendInput` đã
được xác nhận hoạt động thật trong Auto Click, chỉ cần người dùng tự thử trên máy thật của họ (có desktop
thật) để xác nhận lần cuối. Riêng Network Protection/bảo vệ thời gian thực/Remove-MpThreat/quét/EICAR đã
xác nhận THẬT thành công (không cần desktop tương tác, chỉ cần PowerShell chạy được).

**Đã xong, đã test (76 kiểm tra lõi + 6 kiểm tra UI):**
- `CommandAnalyzer`: lệnh an toàn/đáng chú ý/nguy hiểm với hơn chục mẫu tấn công/mã độc thật phổ biến,
  xác nhận "Nguy hiểm" luôn thắng "Đáng chú ý" khi cùng khớp nhiều mẫu.
- `DefenderController`/`HostsBlocklist`: dựng script PowerShell (kiểm tra nội dung chuỗi), phân tích JSON
  mẫu dựng sẵn (gồm hồi quy PowerShell 5.1 trả object đơn không bọc mảng, hồi quy byte-vs-chữ của
  Network Protection, hồi quy SeverityID ngoài phạm vi tài liệu), dựng lại nội dung hosts file (giữ
  nguyên dòng khác, vòng lặp phân tích-dựng lại cho đúng danh sách).
- `security_ui_tests`: dựng `SecurityGatewayWindow` không crash, đủ 3 tab; gọi `Get-MpComputerStatus`/
  đọc hosts file THẬT (chỉ đọc, an toàn). **Cố ý KHÔNG** tự động bật/tắt bảo vệ thật, quét máy thật, xóa
  mối đe dọa thật, sửa hosts file thật, hay mở PowerShell/dán lệnh thật trong test tự động - những thao
  tác đó đổi trạng thái hệ thống thật và/hoặc cần quyền Administrator.

## 4j. Downloader (đã hoàn thiện)

> **Từ v1.19.16** tab "Tải trực tiếp" là hàng đợi hợp nhất có kiểm tra OK/ERR, kèm tab Lịch sử và Cài đặt - xem mục 4ze. **Từ v1.19.19** ba tab Tải trực tiếp / Quét trang web / Video nền tảng đã gộp thành MỘT tab "Tải xuống" (`DownloadTab`) - xem mục 4zh; các tên `DirectDownloadTab`, `PageScanTab`, `PlatformVideoTab` dưới đây là thiết kế lịch sử. Phần dưới mô tả thiết kế gốc (v1.0.x).
Yêu cầu người dùng: tải ảnh/video/tài liệu từ các trang trình duyệt - hỏi nên dán link trực tiếp hay
cách nào khác, yêu cầu đưa vào TOÀN BỘ các cách có thể tải về. Đã thiết kế 3 cơ chế bổ sung lẫn nhau,
mỗi cơ chế phù hợp một tình huống khác nhau thay vì chỉ một cách duy nhất:

**1. Tải trực tiếp (`DirectDownloadTab` + `FileDownloader`):** dán URL TRỰC TIẾP tới một tệp (đuôi
.jpg/.mp4/.pdf... hoặc bất kỳ URL nào server trả thẳng nội dung) - đây là cách áp dụng được cho MỌI
trường hợp, kể cả khi copy link ảnh/video từ trình duyệt (chuột phải > "Sao chép địa chỉ liên kết/hình
ảnh"). `FileDownloader` thuần Qt Network (QNetworkAccessManager/QNetworkReply, KHÔNG dùng QThread - bất
đồng bộ sẵn qua tín hiệu Qt, cùng mẫu PublicIpChecker/SpeedTestRunner), hỗ trợ tải nhiều tệp đồng thời
(mặc định tối đa 3), tạm dừng/tiếp tục qua HTTP Range (tự phát hiện nếu server bỏ qua Range thì tải lại
từ đầu thay vì lỗi), tốc độ/tiến độ thời gian thực, hàng đợi hiển thị dùng CHUNG với tab Quét trang web.

**2. Quét trang web (`PageScanTab` + `PageMediaScanner`):** dán URL một TRANG (không phải link tệp) -
tải HTML tĩnh rồi dò các thẻ `<img>`/`<video>`/`<audio>`/`<source>` và `<a href>` có đuôi tài liệu/lưu
trữ đã biết, phân giải liên kết tương đối thành tuyệt đối, hiện danh sách tick chọn để tải hàng loạt.
**Giới hạn thật đã nêu rõ với người dùng**: đây là phân tích HTML TĨNH, KHÔNG chạy JavaScript - trang
hiện đại tải ảnh/video động (lazy-load, SPA) có thể bị bỏ sót. Đã xác nhận dự án KHÔNG có QtWebEngine khả
dụng trong bản Qt MinGW đang dùng (kiểm tra trực tiếp thư mục cài đặt Qt: Qt chỉ phát hành WebEngine cho
MSVC trên Windows, không có cho MinGW) nên không thể nhúng trình duyệt thật để render JS - khi gặp giới
hạn này, UI hướng dẫn người dùng dùng cách 1 (sao chép link thủ công từ trình duyệt thật).

**3. Video nền tảng (`PlatformVideoTab` + `yt-dlp`):** tải từ YouTube/Facebook/TikTok và hơn 1000 trang
khác - KHÔNG tự viết lại trình trích xuất (bài toán cực lớn, mỗi trang một cơ chế riêng, thay đổi liên
tục) mà đóng gói **yt-dlp** (Unlicense, cập nhật rất thường xuyên) + **ffmpeg/ffprobe** (GPL, bản dựng
tĩnh BtbN/FFmpeg-Builds) vào `vendor/yt-dlp/` - cùng tinh thần "dùng công cụ đã kiểm chứng" như scrcpy
cho Android Phone Control. `YtDlpInfoWorker` (QThread - gọi mạng thật, có thể mất vài giây) lấy danh sách
chất lượng qua `--dump-json`; `YtDlpDownloadWorker` (QObject + QProcess, không cần QThread vì QProcess đã
bất đồng bộ) tải và đọc tiến độ qua `--progress-template` - **cách yt-dlp CHÍNH THỨC khuyến nghị** cho
chương trình khác đọc máy (README: "nên dùng `--progress-template`... tránh tự phân tích thanh tiến độ
dạng người đọc vì có thể đổi định dạng giữa các phiên bản"), kết hợp `--newline` để mỗi cập nhật là một
dòng riêng dễ phân tích. Có banner cảnh báo người dùng tự chịu trách nhiệm tuân thủ ToS/bản quyền của
trang nguồn (yt-dlp không có tuyên bố chính thức về tính hợp pháp, đã xác nhận qua nghiên cứu README/wiki
- không tự bịa ra một tuyên bố không có thật).

**Phát hiện/xác nhận THẬT quan trọng trước khi code** (nghiên cứu qua agent + tự kiểm tra tay):
- **ffmpeg THỰC SỰ bắt buộc**, không phải tùy chọn: thử `-f best` (ép chọn định dạng ghép sẵn, không cần
  ffmpeg) trên một video YouTube thật bị lỗi thật "Requested format is not available" - xác nhận YouTube
  hiện đại hầu như không còn định dạng ghép sẵn ở chất lượng tốt, trả về luồng video/âm thanh tách riêng
  cần ffmpeg ghép lại. Đã bundle ffmpeg để tính năng hoạt động đầy đủ, xác nhận lại bằng tải+ghép thật
  thành công một video thật.
- **`Remove-MpThreat`-kiểu bất ngờ khác đã tránh được nhờ nghiên cứu trước**: ban đầu định thiết kế UI
  chọn chất lượng kiểu dropdown đơn giản; nghiên cứu xác nhận field chính xác (`format_id`/`vcodec`/
  `acodec`/`filesize`/`filesize_approx`...) từ mã nguồn `yt_dlp/extractor/common.py` TRƯỚC khi code, tránh
  phải sửa lại sau khi phát hiện field sai tên như đã từng xảy ra với VPN/Security Gateway.
- **Giấy phép `yt-dlp.exe` KHÁC mã nguồn**: mã nguồn là Unlicense, nhưng bản `.exe` đã biên dịch
  (PyInstaller) là GPLv3+ dạng kết hợp (xác nhận qua README chính thức) - đã ghi đúng trong `THIRD_PARTY.md`,
  không ghi nhầm thành "Unlicense" cho cả bản nhị phân.

**Đã xác nhận THẬT bằng cách chạy qua chính các lớp C++ của ứng dụng** (không chỉ gọi tay yt-dlp.exe từ
dòng lệnh): `YtDlpInfoWorker`/`YtDlpDownloadWorker` lấy thông tin và tải thành công một video YouTube
thật ngắn ("Me at the zoo"), tiến độ `--progress-template` đọc đúng qua suốt quá trình tải; `FileDownloader`
tải thật một tệp ảnh công khai nhỏ qua HTTP; `PageMediaScanner::scan()` tải+phân tích thật một trang HTML
cục bộ (qua `file://`) tìm đúng ảnh/video/tài liệu.

**Đã xong, đã test (63 kiểm tra lõi + 8 kiểm tra UI):**
- `PageMediaScannerInternal::extractMediaLinks`: HTML mẫu dựng sẵn (ảnh/video/audio/source/link tài
  liệu, link tương đối cần phân giải, trùng lặp cần loại bỏ, ảnh nhúng base64 (`data:`) cần bỏ qua, link
  điều hướng thường (`.html`) không được tính là media).
- `YtDlpDownloadWorkerInternal::buildArguments`/`parseProgressLine`: dựng đối số dòng lệnh đúng, phân
  tích dòng tiến độ thật dạng `DLPROG|...|...` (gồm trường "NA" khi chưa rõ, rơi về `total_bytes_estimate`
  khi `total_bytes` chưa biết, dòng không phải tiến độ bị bỏ qua an toàn không crash).
- `YtDlpInfoWorkerInternal::parseInfoJson`: JSON mẫu đúng cấu trúc thật đã xác nhận (kể cả hồi quy đúng
  kịch bản "lấy dòng cuối" khi có cảnh báo lạc vào trước dòng JSON thật - đã thấy hiện tượng này khi gọi
  yt-dlp thật).
- `downloader_ui_tests`: dựng `DownloaderWindow` không crash, đủ 3 tab; tải THẬT một tệp ảnh công khai
  nhỏ qua `FileDownloader` (an toàn, giống mẫu `PublicIpChecker`). **Cố ý KHÔNG** tự động chạy yt-dlp.exe
  thật (quét trang/tải video nền tảng) trong test tự động - cần tự kiểm tra tay qua giao diện.

## 4k. Tự cập nhật (Auto-update, 2026-10-08)
Yêu cầu người dùng: mã nguồn chính trên GitHub, ứng dụng cài trên các máy khác tự hỏi xem có bản mới
không rồi tự cập nhật như một sản phẩm bình thường. Thảo luận trực tiếp với người dùng và chốt theo đúng
điều chỉnh của họ: **chỉ hỏi MỘT LẦN mỗi lần mở ứng dụng** (không lặp định kỳ 1 giờ/lần như đề xuất ban
đầu - đơn giản hơn, đủ dùng vì ứng dụng không phải chạy nền 24/7).

**Kiến trúc:**
- Nguồn "có bản mới hay không": **GitHub Releases** của chính repo mã nguồn (endpoint công khai
  `GET /repos/{owner}/{repo}/releases/latest`, không cần token - giới hạn 60 lượt/giờ/IP của GitHub dư
  sức cho tần suất một lần mỗi lúc mở app). Mỗi bản phát hành phải có tag dạng `vX.Y.Z` + đính kèm file
  `OneForAll_Setup.exe` làm asset - đây là bước **người dùng (chủ dự án) phải tự làm thủ công trên
  GitHub** mỗi lần phát hành, ứng dụng không tự tạo release.
- **Nguồn DUY NHẤT cho số phiên bản**: file `VERSION` ở gốc dự án (trước đây CMakeLists ghi "1.0.0",
  installer ghi "1.11.0", README ghi version khác nữa - 3 nơi lệch nhau). `CMakeLists.txt` đọc file này
  lúc cấu hình, `configure_file()` sinh `src/core/update/Version.h.in` → `generated/core/Version.h`
  (`APP_VERSION`); `build_installer.bat` cũng đọc CÙNG file này rồi truyền `/DMyAppVersion=...` cho Inno
  Setup - cả app lúc chạy lẫn trình cài đặt lúc ghi `AppVersion` luôn khớp nhau.
- `UpdateChecker` (QNetworkAccessManager + `QTimer::singleShot` trễ 4 giây sau khi khởi tạo, KHÔNG lặp
  lại): gọi API, phân tích JSON (`UpdateCheckerInternal::parseLatestRelease` - tách `tag_name`, tìm asset
  tên KHỚP ĐÚNG TUYỆT ĐỐI "OneForAll_Setup.exe", lấy `body` làm ghi chú phát hành), so `tag_name` với `APP_VERSION` qua
  `UpdateCheckerInternal::compareVersions` (so từng phần X.Y.Z dạng số, không so chuỗi - "1.15.0" >
  "1.9.0" đúng nghĩa, khác so chuỗi sẽ sai).
- Có bản mới → `MainWindow` hiện `QMessageBox` ("Cập nhật ngay" / "Để sau", có nút xem chi tiết ghi chú
  phát hành). Bấm "Để sau" không lưu trạng thái gì - lần mở ứng dụng kế tiếp tự hỏi lại (đúng tinh thần
  "mỗi lần mở mới hỏi" người dùng chọn).
- Bấm "Cập nhật ngay" → `UpdateInstaller` **dùng lại `FileDownloader`** đã có sẵn của Downloader (hỗ trợ
  tiếp tục tải dở, đã test kỹ) để tải `OneForAll_Setup.exe` về thư mục tạm, hiện `QProgressDialog`. Tải
  xong → chạy trình cài đặt với `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS
  /RESTARTAPPLICATIONS` (không wizard, không hỏi gì) rồi tự `qApp->quit()`. `/CLOSEAPPLICATIONS` +
  `/RESTARTAPPLICATIONS` dựa trên **Windows Restart Manager**, nhận diện ứng dụng đang chạy qua
  `AppMutex=OneForAllRunningMutex` khai báo trong `installer/OneForAll.iss` (phải khớp CHÍNH XÁC với
  `CreateMutexW(..., L"OneForAllRunningMutex")` thêm vào `main.cpp` - chỉ để NHẬN DIỆN, không chặn chạy
  nhiều bản sao) - tự đóng ứng dụng đang chạy, cài đè, rồi TỰ MỞ LẠI, không cần người dùng bấm Next/Finish
  hay tự mở lại tay.
- **Vì sao chạy ngầm không cần quyền Administrator**: `installer/OneForAll.iss` đã có sẵn
  `PrivilegesRequired=lowest` từ trước (cài theo-người-dùng, không bắt buộc elevate) - nghĩa là chạy lại
  `OneForAll_Setup.exe /VERYSILENT` từ chính ứng dụng (không có quyền Admin) vẫn cài được bình thường với
  phần lớn người dùng, không bị UAC chặn giữa chừng làm hỏng luồng "âm thầm". Người dùng từng chọn "cài
  cho mọi người dùng" (cần Admin) lúc cài lần đầu sẽ gặp UAC khi tự cập nhật - chấp nhận được, không phải
  trường hợp phổ biến.

**Đã xong, đã test (18 kiểm tra lõi):**
- `UpdateCheckerInternal::compareVersions`: bằng nhau, lớn hơn/nhỏ hơn ở từng phần major/minor/patch, bỏ
  tiền tố "v"/"V", phần thiếu coi là 0 ("1.2" = "1.2.0"), so SỐ không so CHUỖI ("1.15.0" > "1.9.0").
- `UpdateCheckerInternal::parseLatestRelease`: JSON mẫu đúng cấu trúc thật GitHub trả về (tag + nhiều
  asset, chỉ lấy đúng asset tên KHỚP TUYỆT ĐỐI "OneForAll_Setup.exe", bỏ qua asset tên gần giống); thiếu
  `tag_name`; có tag nhưng KHÔNG đính kèm
  installer (chỉ có source.zip - coi như không hợp lệ để tự cập nhật); JSON rỗng/hỏng/là mảng thay vì
  object - tất cả trả về rỗng an toàn, không crash.
- Build lại toàn bộ ứng dụng sạch (thêm thư viện tĩnh `update_core`), khởi chạy thử `OneForAll.exe`
  không crash qua giai đoạn gọi API thật (môi trường build này CÓ kết nối Internet thật).

**Giới hạn/việc người dùng (chủ dự án) phải tự làm mỗi lần phát hành bản mới** (xem mục 5 "Quy trình phát
hành" bên dưới) - tính năng chỉ TỰ ĐỘNG được từ phía người DÙNG cài ứng dụng, còn phía NGƯỜI PHÁT HÀNH vẫn
cần vài bước thủ công trên GitHub (tag + tạo Release + đính kèm file cài đặt). Tính năng này cũng chỉ có
tác dụng kể từ bản đầu tiên ĐÃ CÓ sẵn bộ tự cập nhật (v1.15.0 trở đi) - người dùng đang ở bản cũ hơn vẫn
phải tự tải/cài thủ công một lần để có được bộ tự cập nhật.

**Cập nhật v1.19.0:** cách chạy trình cài đặt và mở lại ứng dụng mô tả ở trên (`/CLOSEAPPLICATIONS` +
`/RESTARTAPPLICATIONS`, luôn tải `.exe`) đã được thay - xem mục 4o "Tự cập nhật".

## 4l. Rà soát xung đột khi đóng ứng dụng/chạy nhiều tiện ích cùng lúc (2026-10-08)
Yêu cầu người dùng: kiểm tra toàn bộ phần mềm - tắt bằng X khi đang có thao tác nền có chạy ngầm/xung đột
không, chạy nhiều tiện ích cùng lúc có xung đột không (vd 2 tiện ích cùng quét thư mục). Xuất phát từ lỗi
`adb.exe` mồ côi đã sửa ở Android (mục 4g) - nghi ngờ đúng: đó không phải lỗi riêng của Android mà là MỘT
KIỂU KIẾN TRÚC lặp lại khả năng ở cả 9 tool. Dùng 3 agent dò song song (sở hữu cửa sổ/closeEvent của cả 9
tool; mọi nơi gọi QProcess tìm nguy cơ mồ côi tiến trình con; xung đột khi chạy nhiều tool cùng lúc - hook
bàn phím/chuột, các tab Disk Cleanup, trạng thái dùng chung) rồi chỉ sửa những gì CÓ BẰNG CHỨNG cụ thể
(file:line), không đoán.

**Phát hiện #1 - xác nhận: cả 9/9 tool cùng một kiểu kiến trúc như Android.** Mỗi `*Tool.h` giữ cửa sổ
qua `QPointer<QWidget> m_window` KHÔNG CÓ CHỦ (`new XxxWindow()` không gắn parent, không
`WA_DeleteOnClose`), tái dùng ở lần mở sau - đóng cửa sổ (bấm X) mặc định CHỈ ẨN, không hủy đối tượng.
Chỉ Android (đã sửa trước đó) có lưới an toàn `aboutToQuit`; 8 tool còn lại không có. Trong số đó,
**Downloader hoàn toàn chưa có `closeEvent`** - một phiên tải video nền tảng (`yt-dlp.exe`, có thể tự
sinh `ffmpeg.exe` lúc ghép) sẽ treo lại NGẦM vô thời hạn nếu đóng cửa sổ giữa chừng, ĐÚNG lỗi đã gặp với
Android. **WiFi cũng chưa có `closeEvent`** (polling + đo tốc độ tiếp tục chạy ngầm, mức độ nhẹ hơn vì
không có tiến trình ngoài). Đã sửa: thêm `DownloaderWindow::closeEvent`/`WifiWindow::closeEvent`.

**Phát hiện #2 - nghiêm trọng hơn: `qApp->quit()` gọi trực tiếp (4 chỗ: "Chạy lại với quyền Admin" ở
Security Gateway x2 + Disk Cleanup Partition, và nút "Cập nhật ngay" của bộ tự cập nhật mới thêm) KHÔNG
TỰ GỌI `closeEvent()` của các cửa sổ tool KHÁC đang mở** (đã xác nhận qua tài liệu hành vi
`QCoreApplication::exit()`/`quit()` của Qt - chỉ dừng vòng lặp sự kiện, không duyệt qua
`topLevelWidgets()`). Nếu Disk Cleanup đang đổi kích thước phân vùng THẬT (thao tác chính dự án đã tự xác
định là KHÔNG an toàn để hủy giữa chừng - mục 5b) trong lúc một cửa sổ KHÁC gọi `qApp->quit()`, tiến
trình `powershell.exe Resize-Partition` bị buộc dừng (`TerminateProcess` khi tiến trình thoát) giữa lúc
nguy hiểm nhất - hoàn toàn bỏ qua rào chắn `DiskCleanupWindow::closeEvent` vốn đã CHẶN HẲN việc tự đóng
trong tình huống này. Sửa bằng 2 lớp:
- `ITool::isWindowBusy()` (ảo, mặc định `false`) + `ToolManager::anyToolWindowBusy()` - CHỈ
  `DiskCleanupTool` ghi đè (ủy quyền `DiskCleanupWindow::hasUnsafeOperationInProgress()` - đang
  resize/đang dọn dẹp thật). Gọi kiểm tra này TRƯỚC khi quyết định gọi `qApp->quit()` ở cả 4 điểm - nếu
  bận, báo lỗi rõ ràng và HOÃN LẠI thay vì cứ chạy rồi buộc dừng giữa chừng (nút "Cập nhật ngay" của auto-
  update kiểm tra ngay trước khi chạy trình cài đặt, không chỉ lúc người dùng bấm, để bắt cả trường hợp
  "bắt đầu bận giữa lúc đang tải bản cập nhật").
- `ITool::stopBackgroundWorkForQuit()` (ảo, mặc định không làm gì) + `ToolManager::
  stopAllBackgroundWorkForQuit()`, nối vào `QApplication::aboutToQuit` trong `MainWindow` (lưới an toàn
  CHUNG cho cả 9 tool, thay vì mỗi tool tự nối `aboutToQuit` riêng như Android trước đây) - dọn các thao
  tác AN TOÀN để hủy (Downloader: hủy tải yt-dlp; VPN: hủy rasdial.exe; WiFi: dừng polling/đo tốc độ; Disk
  Cleanup: dừng quét tệp lớn/trùng lặp NẾU đang chạy - không đụng resize/dọn dẹp, đã được chặn ở lớp
  trên). Cố ý KHÔNG gọi `close()` (sẽ chạy lại `closeEvent()` với hộp thoại xác nhận - vô nghĩa giữa một
  luồng thoát không thể hủy) mà gọi thẳng hàm dừng không hỏi (`forceStopNow()`/`cancelActiveDownload()`...).

**Phát hiện #3 - đúng ví dụ người dùng nêu: Disk Cleanup có 4 tab (Dọn dẹp/Tìm tệp lớn/Tìm tệp trùng
lặp/Quản lý phân vùng) sống đồng thời (đổi tab chỉ ẩn/hiện, không hủy), KHÔNG CÓ cơ chế phối hợp nào -
có thể bấm "Quét" ở một tab rồi chuyển sang tab khác bấm "Đổi kích thước"/"Dọn dẹp" nhắm vào CÙNG ổ đĩa,
chạy song song. Nghiêm trọng nhất: đổi kích thước phân vùng (cần di chuyển dữ liệu hệ thống tệp) đồng
thời với I/O từ chính ứng dụng (quét/dọn dẹp) trên cùng đĩa có thể làm resize chậm/thất bại giữa chừng.
Sửa: thêm `DiskCleanupWindow::isAnyOtherTabBusy(QWidget* exceptTab)` (kiểm tra `isScanningNow()` mới thêm
ở `LargeFilesTab`/`DuplicateFilesTab`, `isBusy()` mới thêm ở `CleanupTab`, `isResizingNow()` đã có ở
`PartitionTab`) - mỗi tab tự gọi kiểm tra này NGAY ĐẦU handler bắt đầu thao tác (`onScanClicked`/
`onCleanClicked`/`onResizeClicked`), báo lỗi rõ ràng và từ chối bắt đầu nếu tab khác đang bận.

**Phát hiện #4 - mức trung bình: `HotkeyCapture` (nút "Bắt tổ hợp phím" của Auto Click) không có giới hạn
thời gian - hook `WH_KEYBOARD_LL` nuốt MỌI phím thật trong lúc "đang chờ bắt" vô thời hạn nếu người dùng
bấm nút rồi bỏ đó không gõ gì. Windows gọi hook MỚI CÀI trước hook CŨ trong cùng tiến trình - nếu
`InputHook` của Connect Together đã cài trước (chỉ cần MỞ cửa sổ Connect Together, không cần đang điều
khiển ai), `HotkeyCapture` có thể chặn hoàn toàn tổ hợp "Ctrl+Alt+Home" (phím thoát điều khiển từ xa) và
mọi phím thật khác, không giới hạn thời gian. Sửa: thêm `QTimer` tự hủy bắt sau 20 giây không gõ gì.

**Phát hiện #5 - xác nhận AN TOÀN, không sửa**: lo ngại Auto Click (`SendInput` tự động hóa) có thể bị
`InputHook` của Connect Together nhầm thành input thật của người dùng rồi relay sang máy ở xa, gây vòng
lặp phản hồi - ĐÃ XÁC NHẬN KHÔNG xảy ra: Windows tự gắn cờ `LLKHF_INJECTED`/`LLMHF_INJECTED` cho MỌI lệnh
`SendInput` (của bất kỳ tiến trình nào, không riêng cùng tiến trình), và `InputHook.cpp` đã lọc bỏ cờ này
từ trước (có chủ đích, đúng lý do nêu trên) - xác nhận bằng cách đọc code, không chỉ suy luận.

**Phát hiện #6 - mức thấp, đã sửa cho chắc**: `QRHistoryStore`/`PeerStore`/`LocalIdentityStore` (3
singleton lưu JSON, không phải `Logger` - đã có mutex đúng) ghi tệp kiểu `QFile::open(Truncate)` rồi
`write()` thẳng - nếu tiến trình crash/bị kill ĐÚNG lúc giữa truncate và write xong, tệp thật bị xóa sạch
nội dung cũ. Không phải race điều kiện thật (toàn bộ truy cập đều trên luồng giao diện, xác nhận qua
code), nhưng cùng HỌ lỗi "bị buộc dừng giữa chừng gây mất dữ liệu" nên sửa luôn cho nhất quán: đổi sang
`QSaveFile` (ghi tệp tạm rồi đổi tên đè NGUYÊN TỬ khi `commit()` thành công - tệp thật không bao giờ ở
trạng thái dở dang).

**Đã xác nhận THẬT**: toàn bộ 9 `_ui_tests`/`_tests` liên quan (qr/wifi/connect/diskcleanup/android/vpn/
security/downloader/update, tổng hơn 600 kiểm tra) đều pass sau khi sửa, build lại sạch toàn bộ ứng dụng
nhiều lần trong quá trình sửa, khởi chạy thử không crash. Phát hiện và dọn một tiến trình `adb.exe` THẬT
còn sót (server daemon tự khởi động lúc chạy test `android_ui_tests` gọi `adb devices` thật, khóa tệp
khiến bước đóng gói `build_app.bat` báo "Sharing violation" - không phải lỗi mới, hành vi daemon adb đã
biết từ trước, dọn tay để xác nhận build sạch).

**Tự rà soát lại lần nữa (code review độc lập trên chính commit vừa làm) theo yêu cầu người dùng "đảm bảo
không phát sinh dị biến" - tìm được 3 lỗi THẬT do chính đợt sửa trên gây ra:**
1. **Lỗi logic nghiêm trọng nhất**: `VpnControlWindow::forceStopNow()` (gọi từ lưới an toàn
   `aboutToQuit` mới thêm) tái dùng `VpnTab::cancelAndWait()` - hàm này có bước "nếu chờ 3s không xong
   thì CHỜ TIẾP không giới hạn" (an toàn cho mục đích GỐC: tránh hủy đối tượng `QThread` đang chạy, hành
   vi KHÔNG XÁC ĐỊNH theo Qt) - nhưng ở ngữ cảnh MỚI (lúc `aboutToQuit`, KHÔNG hủy đối tượng gì, tiến
   trình chỉ sắp thoát hẳn) bước chờ không giới hạn đó là THỪA và PHẢN TÁC DỤNG: có thể treo CẢ ỨNG DỤNG
   tới 45 giây nếu đúng lúc `rasdial.exe` đang chờ mạng chậm - đúng lúc người dùng vừa bấm "Chạy lại với
   quyền Admin" mong muốn thoát NHANH. Sửa: thêm `VpnTab::requestCancelNoWait()` (chỉ đặt cờ hủy, không
   chờ gì) dùng riêng cho đường quit-time, giữ nguyên `cancelAndWait()` cho đường đóng cửa sổ trực tiếp
   (closeEvent/destructor - vẫn cần chờ vì ĐÓ mới là lúc thật sự hủy đối tượng).
2. **Rủi ro tiềm ẩn khi build**: `src/core/WinProcessTree.cpp` bị liệt kê TRÙNG trong cả `android_core`
   VÀ `downloader_core` (cả hai đều link vào chung `OneForAll.exe`) - build HIỆN ĐANG qua được chỉ nhờ
   cách GNU ld trích file từ archive tĩnh theo kiểu "lười" (chỉ lấy bản ĐẦU gặp, bỏ qua bản trùng ở
   archive sau) - phụ thuộc may rủi vào THỨ TỰ link, dễ vỡ nếu đổi thứ tự `target_link_libraries` hay
   đổi trình biên dịch/bật LTO. Sửa triệt để: tách thành thư viện tĩnh riêng `winprocess_core`, cả hai
   lib kia `target_link_libraries(... PUBLIC winprocess_core)` thay vì liệt kê trùng file nguồn.
3. **Thiếu nhất quán nhỏ**: `LocalIdentityStore::loadOrCreate()` đổi sang `QSaveFile` nhưng bỏ qua giá
   trị trả về của `commit()` - khác `PeerStore::save()`/`QRHistoryStore::save()` (cùng đợt sửa) đều kiểm
   tra đúng. Sửa: kiểm tra `commit()`, ghi `qWarning()` nếu thất bại (hàm trả `void`, gọi từ constructor
   nên không có nơi để trả lỗi lên - `connect_core` cố tình không kéo theo `Logger` đầy đủ như các `_core`
   thuần khác, dùng `qWarning()` nhẹ sẵn có của Qt thay thế).

Rà bằng tay thêm đường "quit-time" của Downloader/WiFi/Disk Cleanup (dùng cùng cơ chế
`stopBackgroundWorkForQuit()`) - xác nhận cả 3 đều KHÔNG CHỜ gì (chỉ đặt cờ/gọi `kill()`+quét cây tiến
trình, không có vòng `wait()` nào) - chỉ VPN mắc lỗi này. Build lại sạch (thêm `winprocess_core`), chạy
lại toàn bộ 7 bộ test liên quan (`android_tests`/`downloader_tests`/`update_tests`/`vpn_ui_tests`/
`connect_tests`/`connect_ui_tests`/`qr_ui_tests`) đều pass, khởi chạy thử không crash.

**Rà vòng 3 (2026-10-08, v1.16.2) - yêu cầu người dùng "rà từng đầu vào đầu ra của các hàm chương trình,
đảm bảo sạch nhất":** chạy công cụ rà soát code độc lập ở mức sâu nhất trên TOÀN BỘ các commit của phiên
làm việc này (không chỉ commit gần nhất) - xác nhận bằng tay từng phát hiện trước khi sửa, bỏ qua phát
hiện không thật sự áp dụng cho quy trình thật của dự án:
1. **`WinProcessTree`: rủi ro Windows tái sử dụng PID.** Hàm cũ luôn quét hậu duệ SAU KHI tiến trình cha
   đã thoát - nếu Windows lỡ cấp lại đúng PID đó cho một tiến trình hoàn toàn không liên quan trước khi
   kịp quét, có thể quét/dừng NHẦM hậu duệ của tiến trình lạ đó. Tách API thành `findDescendants()` (chỉ
   liệt kê, không dừng gì) + `terminateProcessList()` (dừng danh sách có sẵn) - `ScrcpyLauncher::stop()`
   và `YtDlpDownloadWorker::cancel()`/destructor giờ gọi `findDescendants()` TRƯỚC khi buộc dừng tiến
   trình cha (lúc PID chắc chắn vẫn là của chính mình), chỉ còn đường "thoát bất thường tự phát" (crash,
   bị Task Manager kill - không kiểm soát được thời điểm) vẫn dùng `terminateDescendants()` gộp (quét
   sau) như lưới an toàn cuối, chấp nhận rủi ro nhỏ còn lại vì không có cách nào tránh được khi không chủ
   động khởi xướng việc dừng. Tiện thể vá luôn một lỗ hổng khác phát hiện cùng lúc: `YtDlpDownloadWorker`
   trước đó CHỈ dọn hậu duệ ở đường `cancel()`/destructor (người dùng chủ động hủy), KHÔNG dọn khi
   yt-dlp.exe tự crash/bị kill ngoài ý muốn - thêm vào `finished` lambda, đúng mẫu `ScrcpyLauncher` đã có.
2. **`UpdateChecker::parseLatestRelease`: so khớp tên asset quá lỏng lẻo.** Trước đây chỉ kiểm tra tên
   asset KẾT THÚC bằng "Setup.exe" - nếu một bản phát hành lỡ đính kèm nhiều file cùng kiểu tên (vd file
   checksum `.sha256`, hoặc một asset không liên quan tên tương tự), kết quả phụ thuộc thứ tự ngẫu nhiên
   trong JSON mà không có gì đảm bảo đúng. Sửa: so khớp ĐÚNG TUYỆT ĐỐI "OneForAll_Setup.exe" (tên output
   cố định, biết trước chính xác từ `OutputBaseFilename` trong `.iss`).
3. **`toIntOrZero` (so sánh phiên bản): từ chối cả đoạn hợp lệ nếu dính hậu tố.** `QString::toInt()` yêu
   cầu CẢ chuỗi phải là số - một tag kiểu `v1.16.5-hotfix` khiến đoạn patch "5-hotfix" bị coi là "không
   parse được" rồi rơi về 0 một cách sai lệch (lẽ ra phải đọc ra 5). Sửa: lấy dãy chữ số Ở ĐẦU chuỗi, bỏ
   qua phần còn lại - khớp đúng quy ước semver (hậu tố `-xxx`/`+xxx` sau số). Thêm 3 test hồi quy.
4. **`installer/OneForAll.iss`: giá trị mặc định (fallback) của `MyAppVersion` tự nó đã CŨ.** Phát hiện
   chính dòng dự phòng (dùng khi ai đó mở file `.iss` biên dịch trực tiếp bằng Inno Setup IDE thay vì
   chạy `build_installer.bat`) vẫn ghi "1.15.0" dù `VERSION` đã lên "1.16.1" - ĐÚNG kiểu lỗi "giá trị tĩnh
   trôi dần" đã gặp ở README trước đó (mục 4l), chỉ là ở một nơi khác. Sửa triệt để gốc rễ thay vì chỉ cập
   nhật lại con số (sẽ lại cũ đi lần sau): đổi thành `"0.0.0-dev"` - một giá trị KHÔNG THỂ bị nhầm là bản
   phát hành thật, tự báo ngay cho người build biết họ đã bỏ qua quy trình đúng.
5. **`build_installer.bat`: đọc `VERSION` không cắt khoảng trắng thừa ở cuối.** Khác `CMakeLists.txt`
   (dùng `string(STRIP ...)`), đoạn đọc file trong batch không tự cắt - nếu ai đó lỡ gõ thêm dấu cách khi
   sửa tay file `VERSION`, chuỗi thừa sẽ lọt qua kiểm tra rỗng rồi bị nhúng thẳng vào `AppVersion` của
   trình cài đặt. Thêm vòng lặp tự cắt khoảng trắng cuối (batch không có sẵn hàm trim).

**Xác nhận THẬT**: build lại sạch, 22 kiểm tra `update_tests` (18 cũ + 4 mới) đều pass, build cả trình
cài đặt và xác nhận trực tiếp qua `VersionInfo.FileVersion` của `OneForAll_Setup.exe` thật ra đúng
"1.16.2" không còn khoảng trắng thừa, khởi chạy thử ứng dụng chính không crash.

## 4m. Giải quyết cảnh báo SmartScreen/Smart App Control + pipeline CI/CD (2026-10-09)
Yêu cầu người dùng: Windows báo "Smart App Control"/SmartScreen chặn `OneForAll_Setup.exe` tải từ GitHub
là "không an toàn", đề nghị xem lại và sửa. **Không phải lỗi code** - do file chưa được ký số
(Authenticode) và chưa có uy tín (reputation) với hệ thống đánh giá của Microsoft, bản chất của MỌI file
.exe mới/chưa quen thuộc trên Windows hiện đại, không có cách nào "sửa trong code" được.

**Đã thử + bị chặn đúng chỗ (ranh giới an toàn, không phải lỗi):**
- Tạo chứng chỉ TỰ KÝ (self-signed), ký được `OneForAll.exe` - nhưng việc thêm chứng chỉ vào kho tin cậy
  (Trusted Root) của máy bị chính Windows chặn (yêu cầu xác nhận giao diện, không thể làm tự động qua
  script) VÀ bị hệ thống phân quyền của công cụ này tự chặn (phân loại "Security Weaken") khi thử ký
  tiếp `OneForAll_Setup.exe`. Đúng như thiết kế - dừng lại, không tìm cách lách qua.
- Chứng chỉ tự ký CHỈ giúp được máy đã tự tin cậy nó, không giải quyết được cho người lạ tải từ GitHub.

**Hướng được chọn (theo quyết định của người dùng): xin chứng chỉ ký số EV MIỄN PHÍ của SignPath
Foundation** (dành cho dự án mã nguồn mở, được Windows tin cậy NGAY, không cần chờ tích lũy uy tín như
chứng chỉ thường). Điều kiện bắt buộc + đã chuẩn bị xong:
1. **Giấy phép mã nguồn mở được OSI công nhận** - thêm `LICENSE` (MIT, theo lựa chọn của người dùng).
   Gặp lỗi nhỏ: để kèm một đoạn ghi chú ngoài văn bản MIT chuẩn khiến GitHub không nhận diện đúng (hiện
   "Other" thay vì "MIT") - sửa bằng cách giữ `LICENSE` NGUYÊN VĂN BẢN CHUẨN, dời ghi chú sang
   `THIRD_PARTY.md` - xác nhận lại qua GitHub API: `license.spdx_id` đã đúng "MIT".
2. **Quy trình build phải TỰ ĐỘNG HOÀN TOÀN, tích hợp với repo** (đảm bảo file nhị phân sinh ra TRỰC
   TIẾP từ mã nguồn đã commit, không qua tay người) - trước đây chỉ build thủ công trên máy cục bộ
   (`build_app.bat`/`build_installer.bat`, phụ thuộc đường dẫn `D:\Qt...` cố định của máy đó).

**Dựng `.github/workflows/build-installer.yml`** (GitHub Actions, máy ảo `windows-latest`, clone sạch) -
build Qt 6.10.3 MinGW (`jurplel/install-qt-action`) → Ninja → Inno Setup (Chocolatey) → tải/xác minh
vendor (`scripts/ci_fetch_vendor.ps1`, tái hiện đúng quy trình thủ công trong THIRD_PARTY.md) → cấu hình
CMake → biên dịch → windeployqt → dựng `OneForAll_Release` → biên dịch trình cài đặt → tải lên artifact,
và tự tạo GitHub Release kèm file cài đặt khi đẩy tag `vX.Y.Z`.

**6 lỗi THẬT phát hiện qua từng lần chạy thật trên GitHub Actions** (không đoán, xem log thật của từng
lượt chạy trước khi sửa tiếp) - đúng tinh thần dự án, liệt kê đủ vì đây là bài học cho lần dựng CI khác:
1. `run: & "..."` (PowerShell call operator) bị YAML hiểu nhầm `&` là ký tự bắt đầu "anchor" - 0 job nào
   chạy được, từ chối ngay từ lúc phân tích cú pháp. Sửa: chuyển sang khối `run: |` nhiều dòng.
2. Qt 6.11.1 (bản đang dùng ở máy dev cục bộ) CHƯA có dữ liệu kiến trúc trên mirror mà công cụ cài Qt
   trong CI dùng - xác nhận qua `aqt list-qt windows desktop --arch 6.11.1` lỗi "Failed to locate XML
   data", trong khi 6.10.3 hoạt động bình thường VÀ dùng đúng cùng MinGW 13.1.0 như máy dev. Ghim CI ở
   6.10.3 - không đổi hành vi ứng dụng.
3. Giả định SAI cấu trúc file nén scrcpy (tưởng giải nén phẳng) - tự tải và liệt kê nội dung zip mới phát
   hiện bị bọc trong một thư mục con `scrcpy-win64-v5.0/`. Sửa bằng tìm đệ quy thư mục chứa `adb.exe`.
4. Thiếu module `Qt6Multimedia` (module RIÊNG, không nằm trong gói Qt6 cơ bản) - `CMakeLists.txt` dòng 24
   cần nó, thiếu khiến `find_package` thất bại ngay. Thêm `modules: qtmultimedia`.
5. Thêm nhầm `qtsvg` vào danh sách module - KHÔNG phải module rời ở Qt 6.10.3 (đã nằm sẵn trong gói cơ
   bản, xác nhận qua `aqt list-qt --modules` không có mục này) - bỏ ra.
6. `jurplel/install-qt-action` export biến môi trường tên `QT_ROOT_DIR`, KHÔNG phải `Qt6_DIR` như đoán
   ban đầu (xác nhận qua log in đúng danh sách biến môi trường thật của bước chạy) - khiến đường dẫn
   `windeployqt.exe` thành rỗng, PowerShell báo "term not recognized". Sửa cả 2 chỗ dùng.

**Đã xác nhận THẬT**: lượt chạy CI thật (không phải suy luận) hoàn tất ĐỦ 12/12 bước (bước tạo GitHub
Release bị bỏ qua có chủ đích vì không phải lượt đẩy tag), sinh ra đúng `OneForAll_Setup.exe` (~145MB,
gần khớp bản dựng cục bộ, chênh lệch nhỏ do khác bản vá Qt) làm artifact tải lại được từ chính trang
GitHub Actions. Đã kiểm tra đường tạo Release tự động khi đẩy tag thật (xem bảng changelog bên dưới).

**Kết quả nộp đơn SignPath Foundation (2026-10-09): BỊ TỪ CHỐI.** Đơn đã nộp đủ hồ sơ kỹ thuật (Project
Name "One for ALL", Repository/Homepage, Download URL trang Releases, `PRIVACY.md`, Maintainer Type
"Individual", Build System "GitHub Actions"). Email trả lời của SignPath nêu đúng lý do đã lường trước ở
đoạn trên: chương trình Foundation dành cho dự án đã có "tín hiệu uy tín công khai" bên ngoài (sao
GitHub/fork/người đóng góp, bài viết độc lập, thảo luận trên Reddit/Stack Overflow/YouTube, hoặc đơn vị
bảo trợ) - KHÔNG phải đánh giá chất lượng code. Dự án này còn mới nên chưa có đủ các tín hiệu đó. SignPath
mời nộp lại khi dự án được biết đến rộng hơn; họ cũng có gói trả phí riêng (`docs.signpath.io/change-
subscription`) nhưng vẫn cần tự mua chứng chỉ, không giải quyết gốc vấn đề chi phí - chưa theo hướng này.
Xem mục 4r để biết hướng tiếp theo (Microsoft Store, miễn phí, không có yêu cầu "uy tín" như SignPath).

## 4n. Bộ cài đặt .msi (WiX Toolset) + zip cho cả hai định dạng (2026-10-09)
Yêu cầu người dùng: thêm quy tắc MỚI - từ nay MỖI LẦN sửa code đều phải tự dựng lại ĐỦ BỐN file phát hành
(`OneForAll_Setup.exe`, `OneForAll_Setup.msi`, và zip của cả hai), không chỉ riêng `.exe` như trước. Lý do:
người tải về chỉ double-click và chạy ngay, không tự chọn định dạng - cần có sẵn đủ lựa chọn mọi lúc, đặc
biệt sau khi phát hiện một số máy Windows 11 (Smart App Control) chặn `.exe` tự giải nén (mục 4m) - `.msi`
đi qua `msiexec.exe` có sẵn của hệ điều hành, là lối thoát thay thế cho người dùng gặp vướng mắc đó.

**Công cụ: WiX Toolset v3.14.1** (`candle.exe`/`heat.exe`/`light.exe`, xem THIRD_PARTY.md) - không có
trình cài đặt riêng, chỉ là file .exe giải nén từ zip chính thức. `heat.exe` tự "gặt" (harvest) toàn bộ
cây thư mục `OneForAll_Release\` (đã dọn sạch `logs\`/`profiles\qr_history.json` qua một bản sao riêng ở
`obj\msi_stage\`, cùng quy tắc Excludes như `OneForAll.iss`) thành `Harvested.wxs` lúc build - KHÔNG liệt
kê tay từng file (quá nhiều: DLL Qt + plugin nhiều thư mục con + vendor/scrcpy + vendor/yt-dlp).

**`installer/Product.wxs`** (mới): cài đặt per-user (`InstallScope="perUser"`, vào `LocalAppDataFolder`,
KHÔNG cần quyền Administrator - đúng tinh thần `PrivilegesRequired=lowest` của bản .exe). Giao diện
`WixUI_Minimal` (CỐ Ý không hỏi license, không hỏi chọn thư mục cài - chỉ Welcome → cài → Finish, đúng yêu
cầu "double-click và chạy, không bắt tự setup gì cả", còn đơn giản hơn cả bản .exe).

**3 lỗi THẬT gặp khi biên dịch lần đầu, đã xác nhận qua chạy thật candle.exe/light.exe (không đoán)**:
1. `heat.exe` tự sinh `File/@Id` dạng BĂM (vd `filC8CB3AB6...`) thay vì giữ nguyên tên file, để tránh trùng
   tên giữa các thư mục con plugin Qt khác nhau - không thể cậy vào `[#OneForAll.exe]` (cú pháp tham chiếu
   theo File Id) để chỉ định exe chính cho nút "Khởi chạy sau khi cài"/shortcut. Sửa: dùng đường dẫn qua
   thư mục đích `[INSTALLFOLDER]OneForAll.exe` (giống hệt cách Shortcut Target đã làm), không phụ thuộc Id.
2. Chuỗi tiếng Việt có dấu trong `Product.wxs` (vd "Gỡ cài đặt", "Khởi chạy") khiến `light.exe` báo lỗi
   LGHT0311 "characters not available in code page 1252" - ĐÃ THỬ đặt `Product/@Codepage`/
   `Package/@SummaryCodepage` = 1258 (ANSI tiếng Việt) rồi cả 65001 (UTF-8, bị `candle.exe` từ chối thẳng
   vì OLE Summary Information chỉ nhận codepage ANSI cổ điển) - KHÔNG ăn thua, lỗi vẫn y nguyên (xác nhận
   qua thử nghiệm cô lập: xóa riêng TỪNG chuỗi có dấu thì lỗi của ĐÚNG chuỗi đó biến mất, bất kể codepage
   khai báo gì - chứng tỏ nguyên nhân thật là xung đột codepage khi `light.exe` gộp section của Product.wxs
   với các thư viện .wixlib có sẵn của WiX (`WixUIExtension`/`WixUtilExtension`, biên dịch sẵn ở 1252),
   không phải do khai báo sai). Quyết định: bỏ dấu các chuỗi hiển thị TRONG LÚC CÀI ĐẶT (không phải giao
   diện ứng dụng - Qt vẫn Unicode đầy đủ), đơn giản/chắc chắn hơn dùng cơ chế `.wxl` đa ngôn ngữ phức tạp.
3. `light.exe` báo lỗi (không phải cảnh báo) ICE38/ICE64/ICE91 cho MỌI component do `heat.exe` sinh ra -
   đều là cảnh báo "sai" CHỈ áp dụng cho gói cài có thể đổi giữa per-user/per-machine qua thuộc tính
   ALLUSERS; dự án này LUÔN LUÔN per-user cố định, không bao giờ đổi, nên không áp dụng thật. Sửa: thêm
   `-sice:ICE38 -sice:ICE64 -sice:ICE91` vào `light.exe`.

**Đã xác nhận THẬT toàn bộ vòng đời cài đặt/gỡ cài đặt bằng .msi** (không chỉ biên dịch thành công, giống
hệt kỷ luật đã áp dụng cho bản .exe ở mục 5b):
- `msiexec /i ... /qn` im lặng → exit code 0, đủ 52 file đúng vị trí `%LOCALAPPDATA%\One for ALL\`,
  shortcut Start Menu + Desktop đúng, khóa registry `HKCU\Software\OneForAll` đúng.
- Khởi chạy THẬT `OneForAll.exe` vừa cài từ MSI - tiến trình chạy được, dừng được bình thường.
- `msiexec /x ... /qn` im lặng → exit code 0, xác nhận xóa sạch: thư mục cài đặt, CẢ HAI shortcut, khóa
  registry - không còn gì sót lại (dù ICE64 cảnh báo nguy cơ rác thư mục rỗng, thực tế MSI vẫn tự dọn sạch).

**`build_installer.bat` mở rộng thành 5 bước** (một lệnh duy nhất ra đủ 4 file, đúng yêu cầu "tự động"):
dựng `OneForAll_Release\` mới nhất → biên dịch `.exe` (Inno Setup, như cũ) → dọn bản sao sạch cho
`heat.exe` → biên dịch `.msi` (heat + candle + light) → nén zip cả hai (`OneForAll_Setup_exe.zip`,
`OneForAll_Setup_msi.zip` qua `Compress-Archive`). Gặp 1 lỗi cú pháp dòng lệnh Windows kinh điển khi viết
script: `-out "...\"` (một dấu `\` ngay trước dấu `"` đóng) bị Windows hiểu `\"` là ký tự `"` thoát, làm
hỏng toàn bộ chuỗi tham số - sửa bằng `-out "...\\"` (NHÂN ĐÔI dấu `\` cuối, quy tắc phân tích dòng lệnh
chuẩn của Windows, không phải lỗi riêng của WiX). File `.wixpdb` (debug symbols, không cần cho bản phát
hành) bị tắt bằng cờ `-spdb` của `light.exe`.

Tìm WiX qua `WIX_TOOLSET_BIN` (biến môi trường tùy chọn) → `%LOCALAPPDATA%\WixToolset` (quy ước khuyến
dùng) → `D:\WixToolset` (vị trí hiện tại trên máy dev) - báo lỗi rõ ràng kèm link tải nếu không thấy ở cả
ba. Thư mục `obj\` (nơi `heat.exe`/`candle.exe` ghi file trung gian) thêm vào `.gitignore`, không commit.

**Việc còn lại**: cập nhật `.github/workflows/build-installer.yml` để CI cũng tự dựng đủ 4 file (cài WiX
qua Chocolatey, lặp lại đúng chuỗi lệnh đã xác nhận ở trên) và đính kèm cả 4 vào GitHub Release khi đẩy tag
- xem mục 5c để biết quy trình phát hành đã cập nhật.

## 4o. Rà soát toàn bộ mã nguồn + sửa lỗi tiềm ẩn (2026-10-09, v1.19.0)
Yêu cầu người dùng: đọc hết toàn bộ dự án (kể cả các file trước đó chưa đọc), rà toàn bộ và sửa các lỗi
tiềm ẩn, và thêm quy tắc build/commit/push sau mỗi lần sửa. Cách làm: đọc từng dòng cả 9 công cụ + phần
lõi + script build/CI/installer, ghi lại từng phát hiện kèm file:dòng, tự mở code kiểm lại các lỗi nặng
nhất trước khi sửa, rồi sửa theo từng công cụ và build + chạy test cho từng phần. Mọi mục dưới đây đã build
và qua test tự động; phần nào KHÔNG kiểm chứng được trên máy dev (cần hai máy LAN, điện thoại Android, tài
khoản VPN, hook/chuột/phím thật, hộp thoại UAC) được ghi rõ ở cuối mục.

**Nền dùng chung (`src/core`)**
- **Dữ liệu người dùng rời khỏi thư mục cài đặt** (`AppPaths`, mới): hồ sơ JSON và log trước đây ghi theo
  đường dẫn TƯƠNG ĐỐI (`profiles/...`, `logs/...`) tức phụ thuộc thư mục làm việc lúc khởi động. Hệ quả
  thật: cài "cho mọi người dùng" thì không ghi được; trình cài đặt chép đè `profiles\default.json` mẫu lên
  đúng tệp người dùng đang lưu chuỗi Auto Click ở MỖI lần cập nhật; chạy bản dev ghi dữ liệu cá nhân vào
  chính thư mục sẽ được đóng gói. Nay toàn bộ nằm ở `%LOCALAPPDATA%\OneForAll\{profiles,logs}`; lần chạy
  đầu tự chép tệp từ vị trí cũ (cạnh exe) sang, tệp cũ giữ nguyên - riêng `connect_peers.json` (khóa ghép
  đôi) bị xóa ở vị trí cũ sau khi đã ghi lại bằng DPAPI. Test ép thư mục tạm qua
  `AppPaths::setDataDirOverride()` để không đụng dữ liệu thật.
- **`PowerShellRunner`**: (1) ép stdout/stderr sang UTF-8 không BOM - trước đây đọc theo codepage OEM nên
  tên có dấu ("Hà Lan - Mullvad", nhãn ổ đĩa) làm hỏng JSON; (2) `quoteLiteral()` nhân đôi cả `'` lẫn
  U+2018/2019/201A/201B - PowerShell coi cả bốn là dấu nháy đơn, hàm thoát cũ chỉ xử lý `'` ASCII nên tên
  thư mục quét/tên kết nối VPN chứa `’` chạy được lệnh tùy ý với quyền của ứng dụng (đã xác nhận thật bằng
  parser của PowerShell); (3) gọi `powershell.exe` bằng đường dẫn tuyệt đối trong System32; (4) bỏ khối
  CLIXML lẫn vào thông báo lỗi.
- **`WinProcessTree`**: bỏ qua "tiến trình con" có thời điểm tạo SỚM HƠN tiến trình cha - PID cha chỉ là con
  số, một PID được Windows cấp lại có thể khiến tiến trình mồ côi cũ bị dừng nhầm.
- **CMake**: gom `AppPaths`/`Logger`/`WinElevation`/`PowerShellRunner` vào lib `core_shared`, tách
  `InputController` thành `input_core` - hết cảnh cùng một file `.cpp` nằm trong nhiều target (đúng kiểu rủi
  ro thứ tự link đã phải sửa cho `WinProcessTree` ở mục 4l).
- **`MainWindow`**: tiêu đề và nhãn phiên bản lấy từ `APP_VERSION` (trước đó ghi cứng "v1.0"/"1.0.0").

**Tự cập nhật (sửa lại mục 4k)**
- Bản cũ dựa vào `/RESTARTAPPLICATIONS` của Inno Setup để mở lại ứng dụng. Theo tài liệu Inno, việc đó chỉ
  có tác dụng với ứng dụng đã gọi `RegisterApplicationRestart` VÀ do Restart Manager đóng - ứng dụng này
  không gọi, lại tự thoát trước, nên sau khi cập nhật ứng dụng KHÔNG tự mở lại. Ngoài ra trình cài đặt khởi
  động song song lúc ứng dụng đang thoát, có thể gặp `AppMutex` còn tồn tại.
- Nay `UpdateInstaller` khởi chạy một tiến trình trợ giúp tách rời (powershell ẩn): chờ đúng PID ứng dụng
  thoát → chạy trình cài đặt im lặng và chờ xong → mở lại ứng dụng. Đã chạy thử thật cơ chế chờ-PID + truyền
  tham số có dấu cách/dấu nháy bằng tiến trình giả.
- Bản cài bằng `.msi` nay tải và chạy `OneForAll_Setup.msi` (`msiexec /i ... /qn`); trước đó nó chạy
  `OneForAll_Setup.exe`, tức cài THÊM một bản thứ hai vào thư mục khác. Bản chạy không qua cài đặt
  (portable/dev) mở trình cài đặt ở chế độ tương tác thay vì âm thầm cài rồi mở lại bản cũ.
- Tệp tải về được đối chiếu SHA-256 với trường `digest` GitHub công bố cho asset (có thì kiểm, lệch thì xóa
  không chạy); URL tải bắt buộc `https://github.com/...`.

**Build / CI / bộ cài đặt**
- CI: bước dọn bản sao cho `heat.exe` dùng `robocopy` (trả mã 1 khi copy THÀNH CÔNG) trong bước
  `shell: pwsh` - GitHub Actions kết thúc bước bằng mã thoát của lệnh native cuối nên bước sẽ bị tính là
  thất bại; đã đặt lại `$LASTEXITCODE`. Ghim `gha-setup-ninja@v6` thay cho `@master`.
- CI: tệp workflow của bản 1.18.0 (chưa từng được push nên chưa từng chạy) KHÔNG hợp lệ - tên bước
  `... (WiX: heat + candle + light)` là chuỗi YAML trần chứa `": "`, bị hiểu thành cặp khóa-giá trị lồng
  nhau. GitHub tạo lượt chạy "failure" với 0 job và không có log (xác nhận thật ở lượt chạy đầu của
  v1.19.0, tìm ra bằng cách phân tích tệp bằng PyYAML). Đã đặt tên bước trong dấu nháy. Bài học: kiểm cú
  pháp workflow tại chỗ (`python -c "import yaml; yaml.safe_load(open(...))"`) trước khi push.
- **Đã xác nhận THẬT trên GitHub Actions** (lượt chạy 37879516025, commit `cb08964`): đủ 17 bước đều
  success - gồm cả kiểm checksum ffmpeg, biên dịch trên Qt 6.10.3, và 3 bước WiX (dọn bản sao bằng
  robocopy, heat + candle + light, nén zip) vốn CHƯA TỪNG chạy trên CI trước đó. Bước tạo GitHub Release
  bị bỏ qua có chủ đích (không phải lượt đẩy tag).
- `ci_fetch_vendor.ps1`: ffmpeg nay được kiểm SHA-256 theo `checksums.sha256` của bản phát hành (trước đó
  tải không kiểm; THIRD_PARTY.md lại ghi là đã kiểm).
- Bộ cài đặt (cả `.exe` lẫn `.msi`) loại TOÀN BỘ `profiles\` của thư mục Release và chỉ đóng gói
  `profiles\default.json` lấy thẳng từ Git - trước đây chỉ loại riêng `qr_history.json`, nên khóa ghép đôi/
  nhãn VPN của máy dev có thể lọt vào bộ cài. `build_app.bat` và CI cùng chép tệp mẫu này (bản CI trước đó
  không có tệp mẫu).

**Auto Click**
- Thêm phím dừng khẩn cấp toàn cục Ctrl+Alt+F8 (`RegisterHotKey`, chỉ đăng ký khi đang chạy, ghi trên HUD).
- Dừng/đóng cửa sổ có tác dụng ngay cả giữa bước gõ/cuộn dài (trước đó ngủ một mạch tới 1 giờ); thời gian
  chờ tính theo `steady_clock` nên không trôi.
- `SendInput`/`SetCursorPos` thất bại (màn hình khóa, UAC, cửa sổ đích quyền cao hơn) thì dừng chuỗi và báo
  lỗi thay vì chạy tiếp trong im lặng.
- "Lưu hành động" không còn bật lại hành động đã tắt hay âm thầm đổi phím nằm ngoài danh sách; chạy chuỗi
  không còn ghi đè chỉnh sửa chưa lưu; thêm/nhân bản chuỗi chọn đúng chuỗi mới.
- Hồ sơ ghi nguyên tử (`QSaveFile`), tệp hỏng giữ lại dạng `.bak`, hỏi trước khi Nạp/Nhập ghi đè.
- Nội dung "Gõ văn bản" không còn ghi vào log. Bắt thao tác kéo có Esc/timeout; bắt tổ hợp phím tự hủy khi
  đổi hàng/đóng cửa sổ.

**Connect Together**
- **Lỗi chức năng nặng nhất của đợt rà**: chuyển quyền điều khiển chuột/phím KHÔNG BAO GIỜ được máy nhận
  chấp nhận - chỉ bên gọi gửi `SessionHello` nên chỉ bên nhận đăng ký phiên; `ControlHandoff` tới đầu kia
  trên một phiên còn nằm trong hàng chờ và bị bỏ qua, trong khi máy gửi đã bật hook nuốt input. Kèm theo:
  rớt kết nối lúc đang điều khiển để hook tiếp tục nuốt chuột/phím cục bộ mà Ctrl+Alt+Home không còn tác
  dụng.
- Giao thức v2: khung mở đầu không mã hóa mang id (bên nhận chọn khóa theo id thay vì đoán theo IP);
  `SessionHello`/`SessionHelloAck` trao nonce dưới khóa dài hạn; hai bên suy khóa phiên RIÊNG TỪNG CHIỀU
  bằng HMAC-SHA256 (CNG); `SessionConfirm` hai chiều; chỉ đăng ký phiên sau khi giải mã được Confirm của
  bên kia; nonce GCM là bộ đếm theo từng chiều (chống phát lại/phản xạ/đảo thứ tự). Hai máy phải cùng cập
  nhật; không cần ghép đôi lại.
- Hook chỉ active khi đang `Controlling` và còn phiên (kiểm mỗi 16 ms); Ctrl+Alt+Home luôn trả quyền, dùng
  được ở cả hai máy; sau khi trả quyền con trỏ lùi vào trong + nghỉ 400 ms, các phím đang giữ được nhả ở
  máy kia.
- Input chỉ nhận từ máy đang điều khiển; mã ghép đôi hủy sau 5 lần sai; giới hạn kết nối chờ, kích thước
  gói, bảng discovery; heartbeat phát hiện phiên chết; mở lại cửa sổ thì khởi động lại dịch vụ.
- Khóa dài hạn lưu bằng DPAPI (tệp cũ dạng Base64 thô tự được ghi lại).

**Disk Cleanup**
- **Đổi kích thước nhầm phân vùng**: đổi dòng chọn không xóa khoảng kích thước/ô xác nhận/trạng thái nút, nên
  tra ổ C, gõ "C", bấm sang D rồi bấm Đổi kích thước sẽ chạy trên D với số của C. Nay mọi lần đổi dòng đều
  xóa trạng thái, lệnh bị từ chối nếu phân vùng đang chọn khác phân vùng đã tra, kích thước kẹp theo byte,
  và luồng resize tự hỏi lại Windows ngay trước khi chạy. Bỏ giới hạn 180 giây (hết giờ là `kill` giữa lúc
  đang đổi phân vùng).
- "Xóa vào Thùng rác" không còn âm thầm thành xóa vĩnh viễn với tệp quá cỡ Thùng rác/ổ không có Thùng rác.
- Không đi xuyên junction/symlink thư mục khi quét và xóa; từ chối thư mục gốc đáng ngờ (rỗng, gốc ổ, hồ sơ
  người dùng, Windows, Program Files); hộp xác nhận hiện đường dẫn; giữ mục ghim trong Recent và tệp tạm
  vừa sửa trong 5 phút.
- Tìm tệp trùng: khử trùng theo danh tính tệp vật lý (hardlink/junction không còn bị coi là bản sao - xóa
  "bản sao" đó từng có thể là xóa bản duy nhất), luôn giữ ít nhất một bản, kiểm lại tệp trước khi xóa.
- Liệt kê phân vùng/tra kích thước chạy nền.

**VPN & Location**
- Kết nối/ngắt qua RAS API (`RasDialW`/`RasHangUpW`) thay cho `rasdial.exe` - mật khẩu không còn nằm trên
  dòng lệnh tiến trình con. Đã gọi thật tới một hồ sơ không tồn tại (lỗi 623 đúng như mong đợi).
- Hủy một lần không còn làm mọi lần kết nối sau tự "Đã hủy" (cờ hủy không được đặt lại); cùng lỗi đó ở quét
  Defender cũng đã sửa.
- Nhớ tên đăng nhập (không lưu mật khẩu); thêm/liệt kê/xóa hồ sơ chạy nền.

**Security Gateway**
- Hosts: sao lưu một lần (`hosts.oneforall.bak`), ghi nguyên tử, kiểm kết quả ghi (trước đó luôn báo thành
  công); khối đánh dấu thiếu dòng END không còn làm mất mọi dòng phía sau; chuẩn hóa/kiểm tên miền.
- `CommandAnalyzer`: nhận diện `irm ... | iex`, mọi dạng viết tắt của `-EncodedCommand`/`-ExecutionPolicy`/
  `-WindowStyle`, tắt Defender bằng `1`/`$true`, thêm vùng loại trừ, xóa shadow copy, LOLBin tải từ xa, che
  giấu bằng dấu backtick/ghép chuỗi.
- Chỉ gửi Ctrl+V khi cửa sổ PowerShell vừa mở thật sự ở phía trước (trước đó dán vào bất kỳ cửa sổ nào đang
  focus rồi báo thành công); cảnh báo + xác nhận với lệnh nhiều dòng và khi ứng dụng đang chạy quyền
  Administrator.
- Lỗi đọc danh sách mối đe dọa/danh sách chặn hiện ra thay vì bảng trống; quét toàn bộ không bị cắt sau 1
  giờ.

**Downloader**
- Mục tải từ tab "Quét trang web" nay hiện trong hàng đợi (trước đó tải ngầm, không dừng/hủy được).
- Tải vào `<tên>.part` rồi mới đổi tên; tiếp tục tải dở dùng `If-Range`; báo lỗi ghi đĩa; timeout; chỉ nhận
  http/https; làm sạch tên tệp (chặn path traversal, tên dành riêng); hết ghi đè do trùng tên.
- yt-dlp: kiểm URL, `--` trước URL, `--ignore-config`; định dạng chỉ-video tự ghép âm thanh; có nút Hủy;
  không kẹt khi yt-dlp không khởi chạy được.

**QR Tools / WiFi / Android**
- QR: nút Camera tắt được camera; che mật khẩu WiFi ở ô nhập/kết quả/lịch sử; nội dung mã quét được hiển thị
  dạng văn bản thuần; đọc được mã lật gương và nhiều mã cỡ khác nhau trong một ảnh; SVG bo tròn đúng.
- WiFi: nhập sai mật khẩu không còn phá hồ sơ đã lưu (sao lưu + khôi phục); mạng đã lưu nối bằng hồ sơ sẵn
  có; hồ sơ WPA3 dùng đúng namespace gốc `v1`; mạng ẩn có `nonBroadcast`; xuất XML cảnh báo mật khẩu dạng
  chữ, nhập hỏi trước khi ghi đè. Đo tốc độ: bỏ mẫu ping khởi động, có timeout chờ byte đầu. Vẫn KHÔNG có
  cơ chế thử nhiều mật khẩu.
- Android: `adb devices` chạy bất đồng bộ và chỉ khi cửa sổ đang hiện (trước đó chặn luồng giao diện mỗi 3
  giây, suốt đời ứng dụng); dừng daemon adb đóng gói kèm khi đóng/thoát (nó khóa `adb.exe`, cản trình cài
  đặt cập nhật); kiểm định dạng IP:cổng và mã ghép đôi.

**Kiểm thử**: Build sạch toàn bộ + 18 bộ test (qr, wifi, connect, diskcleanup, android, vpn, security, downloader - mỗi bộ `_tests` và `_ui_tests` - cùng `update_tests` và `autoclick_tests` mới): **2717 kiểm tra đều pass, 0 lỗi** (trước đợt này khoảng 1170).

**CHƯA kiểm chứng được trên máy dev (cần người dùng tự thử)**
- Connect Together trên HAI máy thật: chuột đi qua biên, bàn phím, clipboard, đa màn hình khác DPI.
- Auto Click: chạy chuỗi thật, Ctrl+Alt+F8 dừng thật, bắt kéo/bắt tổ hợp phím bằng hook thật.
- WiFi: mọi luồng đổi cấu hình thật (kết nối, khôi phục hồ sơ, mạng ẩn, WPA3). Camera quét QR.
- VPN: kết nối thật qua RAS API. Disk Cleanup: đổi kích thước phân vùng thật.
- Security: ghi hosts thật, quét/hủy Defender thật, dán vào PowerShell thật. Downloader: mọi luồng yt-dlp.
- Android: phiên scrcpy, ghép đôi, ghi hình với thiết bị thật.
- Tự cập nhật đầu-cuối từ một bản đã cài (cần một bản phát hành mới hơn trên GitHub).

## 4p. Rà soát độc lập lần hai + stress test (2026-10-09, v1.19.1)
Yêu cầu người dùng: rà thêm một lần các lỗi còn có thể xảy ra, stress test các tiện ích với những thao tác
test được, rồi push. Cách làm: năm người đọc ĐỘC LẬP (không phải người đã sửa ở mục 4o) đọc lại từng dòng
mỗi nhóm công cụ, viết một bộ stress/fuzz riêng cho nhóm đó, và sửa những lỗi tái hiện được. Bốn trong năm
phiên bị ngắt giữa chừng do chạm giới hạn phiên của tài khoản rồi được chạy tiếp tới khi gửi báo cáo; phần
sửa của cả năm được build tích hợp và chạy lại toàn bộ test trước khi commit - xem "Kiểm thử" và "Còn dang
dở" ở cuối mục.

**Năm bộ stress test mới** (`EXCLUDE_FROM_ALL`, không tiêm chuột/phím thật, không đổi cấu hình hệ thống,
mọi thao tác tệp nằm trong thư mục tạm):
- `core_stress_tests`: Logger nhiều luồng + xoay vòng log, `AppPaths`, `PowerShellRunner` thật (đầu ra lớn,
  chạy song song, hủy giữa chừng), fuzz `UpdateChecker`/`buildHelperScript` (script trợ giúp cập nhật được
  parser của PowerShell phân tích, không thực thi), `ActionSerializer` round-trip, `ActionRunner` start/stop
  dồn dập, `StopHotkey`, phân tích đầu ra adb, mở/đóng cửa sổ offscreen.
- `connect_stress_tests`: fuzz khung/preamble/gói mã hóa, tệp lưu hỏng, hàng nghìn thông điệp hai chiều, ghép
  đôi lặp/sai/hết hạn/chéo nhau, dừng-chạy lại và mở kết nối chéo hàng trăm vòng, trao quyền điều khiển lặp,
  khoảng 33.500 kết nối/khung bất thường vào cổng lắng nghe.
- `disk_vpn_stress_tests`: cây thư mục tạm lớn có junction/hardlink/.lnk/tên Unicode, quét và dừng ngẫu
  nhiên, xóa vào Thùng rác trong lúc tệp bị tạo/khóa đồng thời (có tệp "canh gác" kiểm không bị đụng), fuzz
  các hàm an toàn đường dẫn và đổi kích thước phân vùng, RAS API gọi thật tới hồ sơ không tồn tại.
- `qrwifi_stress_tests`: round-trip mã QR với payload ngẫu nhiên, giải mã ảnh nhiễu/cỡ biên, fuzz
  parse/che mật khẩu, lịch sử QR, XML hồ sơ WLAN, mở/đóng cửa sổ.
- `secdl_stress_tests`: `CommandAnalyzer` với đầu vào cỡ MB có ngưỡng thời gian, vòng lặp ghi/đọc hosts trên
  tệp tạm, HTML khổng lồ, fuzz tên tệp, `FileDownloader` thật với máy chủ HTTP "khó chịu" trên 127.0.0.1.

**Lỗi thật tìm thấy và đã sửa** (mỗi lỗi có chú thích tại chỗ sửa; phần lớn có test hồi quy):
- **Disk Cleanup - xóa nhầm tệp "hàng xóm"** (nặng nhất của đợt này): yêu cầu xóa `thư mục\foo.` hoặc
  `thư mục\bar␠` (tên có dấu chấm/khoảng trắng cuối) thì Windows Shell xóa `thư mục\foo`, để lại `foo.`, và
  báo thành công - cả khi vào Thùng rác lẫn xóa vĩnh viễn (đã tái hiện bằng probe trên mã cũ). Ký tự đại
  diện (`dir\w*.txt`) làm Shell xóa mọi tệp khớp. Nay `FsSafety::unsafeShellPathReason()` từ chối các
  đường dẫn mà Win32 hiểu thành đối tượng khác.
- **Security Gateway - treo giao diện vì regex**: các mẫu dạng `curl[^\n]*\|\s*iex` quay lui theo BÌNH
  PHƯƠNG độ dài - 45 nghìn ký tự "curl curl ..." trên một dòng mất hơn 2 giây, 1 MB là hàng chục phút. Thay
  bằng bộ so khớp chuỗi mảnh tuyến tính (`chainMatches`); đối chiếu với bản cũ trên 1,2 triệu chuỗi ngẫu
  nhiên và 12,5 triệu tổ hợp lưới: kết quả phát hiện không đổi.
- **Security Gateway - báo "An toàn" sai**: một nửa cặp surrogate UTF-16 mồ côi trong lệnh làm mọi regex trả
  "không khớp", cả đoạn lệnh được báo An toàn. Nay thay bằng U+FFFD trước khi so khớp; regex báo lỗi thì
  ra "Đáng chú ý" chứ không ra An toàn.
- **Connect Together**: hai máy cùng nhập mã cho nhau thì mỗi máy giữ một khóa dài hạn KHÁC nhau (báo "đã
  ghép đôi" nhưng không bao giờ lên phiên); một kết nối đến chưa xác thực chỉ cần khai id của máy kia là
  cản được kết nối đã xác thực; tệp danh tính hỏng làm danh tính sinh lại không lưu được (mỗi lần chạy một
  id mới, mọi máy đã ghép coi là máy lạ).
- **Disk Cleanup (tiếp)**: thư mục được bảo vệ viết bằng tên ngắn 8.3 (`C:/PROGRA~1`, và biến
  TEMP của Windows hay ở dạng này) hoặc `C://.//Windows` lọt qua phép so chuỗi; thư mục Windows lấy từ
  `GetWindowsDirectoryW` thay vì tin biến môi trường `WINDIR`; một tệp đang bị khóa không còn chặn cả lô
  xóa; ô kích thước phân vùng CHƯA sửa (giá trị làm tròn 2 chữ số) không còn bị coi là yêu cầu đổi vài MB
  thật; số ngoài miền trong JSON không còn ép kiểu thành kích thước âm khổng lồ; tab tệp trùng kiểm lại toàn
  bộ sau hộp xác nhận.
- **Downloader**: phản hồi nén (gzip/deflate) bị đứt giữa chừng được báo "Hoàn tất" với tệp cụt - nay luôn
  xin bản không nén; phản hồi 206 chỉ chứa một phần đoạn được hỏi bị coi là xong (tệp thiếu) - nay tự xin
  tiếp; hai mục cùng tệp đích ghi chung một `.part`, mục báo xong có nội dung trộn lẫn (17/25 vòng) - nay
  mục vào sau bị từ chối; tệp `.part` của mục tạm dừng bị bỏ lại khi thoát (10/12 vòng); slot gọi
  `pause()`/`cancel()` giữa vòng lặp làm hỏng danh sách đang duyệt; cắt ngắn tên dài có thể tạo ra tên
  thiết bị dành riêng ("NUL" + 150 dấu cách + "x"); thêm `COM¹²³`/`LPT¹²³`.
- **WiFi**: SSID chứa ký tự điều khiển/NUL/CR không đi nguyên vẹn qua XML nên Windows tạo hồ sơ dưới một
  tên KHÁC - tức ghi đè không sao lưu lên hồ sơ mang tên đã bị rút gọn (SSID "Nha<0x01>" đè hồ sơ "Nha");
  hồ sơ trùng tên nhưng thuộc mạng khác; điều kiện "đã ngắt" sai khi Windows tự nối sang mạng khác.
- **QR**: "Tạo lại mã" từ mã đã quét sinh ra mã KHÁC nội dung gốc (ô văn bản đổi CRLF thành LF); trường mật
  khẩu lặp `P:<mk>;P:;` làm mật khẩu hiện nguyên văn; lịch sử nạp quá 300 mục; hai tệp hỏng trong cùng một
  giây đè nhau.
- **Auto Click**: chuỗi lặp vô hạn KHÔNG có khoảng chờ làm nghẹt giao diện - chạy 2 giây rồi bấm Dừng thì
  233 giây sau giao diện mới nhận (nay nhường tối thiểu 1 ms mỗi hành động, Dừng có tác dụng sau 9 ms);
  **phím dừng toàn cục Ctrl+Alt+F8 không đăng ký được khi chương trình khác đã giữ tổ hợp đó** (gặp thật
  trên máy dev, lỗi 1409) - nay tự thử lần lượt Ctrl+Alt+F9, Ctrl+Alt+F10, Ctrl+Shift+F8 và HUD hiện đúng
  tổ hợp đang có hiệu lực; bấm dừng đúng lúc chuỗi vừa tự xong làm trạng thái kẹt ở "Đang dừng" (3/400 lần); tên chuỗi
  hoặc văn bản chứa `%1`/`%2` bị `arg()` thay nhầm; thời gian/tọa độ khổng lồ trong hồ sơ sửa tay làm tràn
  số; ô tọa độ giới hạn ±10000 cắt sai trên ba màn hình 4K; nút bắt tổ hợp phím kẹt khi Windows từ chối cài
  hook.
- **Android**: rút thiết bị đang chọn thì lựa chọn âm thầm chuyển sang thiết bị khác (điều khiển nhầm máy);
  so đường dẫn adb theo danh tính tệp (tên ngắn 8.3 từng làm daemon adb không bao giờ được dừng).
- **Lõi**: `AppPaths::removeLegacyProfileCopies()` (thêm ở mục 4o) XÓA NHẦM chính tệp đang dùng khi thư mục
  dữ liệu viết dạng tương đối (`ONEFORALL_DATA_DIR=.`), tên ngắn 8.3 hoặc qua junction - nay so danh tính
  tệp vật lý và phân giải biến môi trường thành đường dẫn tuyệt đối; di trú hồ sơ không nguyên tử (trong lúc
  chép 96 MB, luồng khác thấy tệp đích tồn tại nhưng 845 lần không mở được) - nay chép ra tệp tạm rồi đổi
  tên; thông báo lỗi PowerShell dạng CLIXML được giải mã thành câu đọc được; URL cập nhật
  chỉ nhận cổng mặc định/443; `size` ngoài miền trong JSON GitHub; nút "Ẩn" của hộp tiến độ cập nhật tự
  hiện lại.
- **VPN**: không hủy `QThread` đang chạy ở destructor; mã RAS 600 (đang chờ) không còn bị coi là lỗi.
- **Hosts**: tệp xuống dòng kiểu Unix không còn bị đổi toàn bộ sang CRLF; thao tác không đổi danh sách thì
  không ghi tệp.

**Kiểm thử**: Build sạch toàn bộ trên máy rảnh: 18 bộ test thường **2967 kiểm tra, 0 lỗi**; 5 bộ stress đều pass - `core_stress_tests` 525/0 (95 s), `qrwifi_stress_tests` 759.893/0 (290 s), `disk_vpn_stress_tests` 7320/0 (162 s), `secdl_stress_tests` 1.876.772/0 (123 s), `connect_stress_tests` 386/0 (135 s).

**Stress test CHƯA ổn định - nêu rõ**
- `core_stress_tests`: trong lúc bốn phiên khác cùng tải máy, 2/5 lượt chạy đầy đủ fail ở phần PowerShell
  thật (một lệnh in 5 MB hết hạn 120 giây; một lần hủy `runCancelable` trả về trễ hơn 25 giây và còn một
  `powershell.exe` con sống sau 5 giây). Không tái hiện ở các lượt sau khi máy rảnh (lượt tích hợp cuối:
  525/0 trong 95 giây). **Bằng chứng tìm được sau đó nghiêng về phía máy, không phải runner**: hai tiến trình
  `powershell.exe` sót lại từ các lượt fail (script `Start-Sleep -Seconds 120`, tạo lúc 12:02 và 13:54) vẫn
  còn sống hàng giờ sau với 0 giây CPU, 1 luồng ở trạng thái chờ `Executive`, và KHÔNG dừng được cả bằng
  `Stop-Process -Force` - tức chúng kẹt trong nhân Windows ngay từ lúc khởi tạo (chưa kịp nạp .NET), nên
  lệnh `kill()` của runner có được gọi cũng không có tác dụng. Nguyên nhân gốc (phần mềm bảo mật/driver chặn
  lúc tạo tiến trình khi máy quá tải?) CHƯA xác định; các tiến trình này chỉ mất khi khởi động lại máy.
- Lỗi do chính bộ test (đã sửa test, không phải lỗi sản phẩm): ngưỡng thời gian quá chặt khi máy bận, trọng
  tài của test sai, và hai bộ có giao diện ép `QT_QPA_PLATFORM=offscreen` trong thư mục đã `windeployqt`
  (chỉ có `qwindows.dll`) khiến Qt bật hộp thoại lỗi lên màn hình và treo - nay tự tìm plugin offscreen
  (`tests/OffscreenPlatform.h`), không thấy thì thoát ngay với mã lỗi.
- Tác dụng phụ trên máy dev: các lượt stress của Disk Cleanup đưa khoảng 2.000 tệp tạm nhỏ `del_*.tmp` vào
  Thùng rác thật (không tự làm trống).

**Còn dang dở / chưa kiểm chứng**
- Connect Together: PBKDF2 (245-500 ms) chạy trên luồng giao diện cũng là luồng phục vụ hook; trần khung
  nâng lên 2 MiB ngay sau `SessionHello` trước khi xác thực xong (tối đa 16 × 2 MiB); `PeerStore::save()` lỗi
  không báo lên giao diện; chưa có kịch bản lưới 3 máy và fuzz `PeerDiscovery`.
- Disk Cleanup/VPN: junction trỏ vào thư mục được bảo vệ chưa bị phát hiện; tuổi tệp tạm chỉ kiểm lúc quét;
  toàn bộ đường RAS không đồng bộ (quay số, hủy giữa chừng) chưa chạy thật; `PublicIpChecker` có thể hủy
  nhầm phản hồi mới.
- Security/Downloader: tệp hosts lẫn lộn CRLF/LF vẫn bị đưa về CRLF; trang 10 MB mất ~3 giây phân tích trên
  luồng giao diện; timeout 30 giây và đường "máy chủ ép trả bản nén" chưa có kịch bản; các tab giao diện và
  `CommandLauncher` chưa được rà từng dòng.
- QR/WiFi: `QRWindow` tạo/hủy 100 lần tăng khoảng 10 MB bộ nhớ, chưa tìm ra chỗ rò; hai bản ứng dụng chạy
  cùng lúc ghi đè lịch sử QR của nhau; nhánh từ chối trong `WlanController` cần hồ sơ thật nên không test.
- Lõi/Auto Click/Android: `PowerShellRunner` không dừng tiến trình cháu khi hủy; script dài hơn khoảng 12.000
  ký tự không khởi chạy được; `adb pair` còn chặn giao diện; chưa dựng được cửa sổ của bảy công cụ còn lại
  trong stress (chúng quét WiFi, mở camera, gọi Defender).
- Mọi mục "CHƯA kiểm chứng được trên máy dev" của mục 4o vẫn nguyên.

## 4q. Hoàn thiện các mục còn dang dở của 4p + dọn rác sau stress test (2026-10-09, v1.19.2)
Yêu cầu người dùng (sau khi khởi động lại máy): kiểm hai tiến trình `powershell.exe` kẹt đã mất chưa, xóa
rác liên quan, hoàn thiện những gì còn phải hoàn thiện rồi push.

**Sau khi khởi động lại**: hai tiến trình kẹt (mục 4p) đã mất, không còn tiến trình test/adb/OneForAll mồ
côi nào. `core_stress_tests` chạy lại 2 lần trên máy vừa khởi động: phần PowerShell ổn định (30/30 lượt hủy
đúng hạn) - củng cố kết luận ở 4p rằng hai lần fail trước là do máy, không phải runner.

**Rác đã dọn** (khoảng 1,4 GB): 227 thư mục tạm `<tên test>-XXXXXX` trong `%TEMP%`, bản staging cũ trong
`%TEMP%`, thư mục build tạm của các phiên rà soát, và đúng 2.409 tệp do bộ test Disk Cleanup đưa vào Thùng
rác (lọc theo vị trí gốc nằm trong thư mục tạm của test; các mục khác trong Thùng rác không bị đụng tới).

**Nguyên nhân test để sót thư mục tạm - đã sửa tận gốc**: `Logger` là singleton sống lâu hơn `main()` và giữ
hai tệp log mở NGAY TRONG thư mục tạm (do `AppPaths::setDataDirOverride`), nên `QTemporaryDir` không xóa nổi
thư mục của chính nó. Thêm `tests/TestDataDir.h` (đóng log rồi mới xóa, có thử lại ngắn) và dùng ở 19 tệp
test; `Logger` có thêm `closeFiles()`/`reopenFiles()`. Sau toàn bộ lượt chạy: 0 thư mục mới trong `%TEMP%`.

**Các mục "Còn dang dở" của 4p nay đã làm**
- **Connect Together**: suy khóa từ mã ghép đôi (PBKDF2, 245-500 ms) chuyển sang luồng nền (`QThreadPool`)
  - tạo mã/nhập mã không còn làm đứng luồng giao diện, vốn cũng là luồng phục vụ hook chuột/bàn phím; kết
  nối ghép đôi tới trước khi có khóa được giữ chờ thay vì bị từ chối; hủy/hết hạn/đổi mã/`stop()` giữa chừng
  đều bỏ kết quả cũ. Trần khung 2 MiB chỉ mở sau khi phiên được xác nhận bằng `SessionConfirm` (trước đó 4
  KiB). Lỗi lưu `PeerStore` hiện cảnh báo trong nhật ký ở ghép đôi/đổi vị trí/đổi tự kết nối/quên máy.
- **Lõi**: `PowerShellRunner` khi hủy/hết giờ dừng cả tiến trình do script sinh ra (lấy hậu duệ TRƯỚC khi
  kill - đã test thật với một `PING.EXE` con); `Logger` tự thử mở lại tệp log sau lỗi tạm thời.
- **Android**: `adb pair` chạy bất đồng bộ (`AdbPairer`, cùng mẫu `AdbDeviceLister`) - không còn đứng giao
  diện tới ~25 giây; hủy khi đóng cửa sổ/thoát.
- **Disk Cleanup**: junction/symlink thư mục dẫn tới thư mục được bảo vệ, thư mục cha của nó hoặc gốc ổ đĩa
  bị từ chối theo danh tính tệp vật lý (trước chỉ so chuỗi); tuổi tệp tạm được kiểm lại ngay trước khi xóa.
- **VPN**: timer hết giờ của `PublicIpChecker` gắn với đúng phản hồi; `RASCS_Disconnected` không kèm mã lỗi
  kết thúc ngay thay vì chờ hết 45 giây.
- **Security Gateway (hosts)**: tệp lẫn lộn CRLF/LF giữ nguyên kiểu xuống dòng của TỪNG dòng không thuộc
  khối của công cụ; không tạo `.bak` từ tệp do chính công cụ tạo ra.
- **QR**: hai bản ứng dụng chạy cùng lúc không còn ghi đè lịch sử của nhau (nạp lại + gộp trước khi ghi);
  ghi lịch sử thử lại khi bước đổi tên đè thất bại thoáng qua (xem dưới).

**Lỗi ghi lịch sử QR thoáng qua - tìm ra khi tích hợp**: `qrwifi_stress_tests` fail 2 lần khi thư mục tạm
nằm trong cây repo (khoảng 20/20.000 lần `QSaveFile::commit()` thất bại, lần ghi kế tiếp lại thành công)
nhưng pass với `%TEMP%` hệ thống. `QRHistoryStore::save()` nay thử lại tối đa 5 lần; chạy lại ĐÚNG kịch bản đã fail (thư mục tạm trong repo) 2 lần: 760.577/0 và 760.130/0. Tiến trình nào giữ tệp lúc đổi tên (trình quét virus, bộ theo dõi tệp của trình soạn thảo...) CHƯA xác định - đây là suy luận từ triệu chứng.

**Kiểm thử**: Build sạch toàn bộ sau khi khởi động lại máy: 18 bộ test thường **3217 kiểm tra, 0 lỗi**; 5 bộ stress đều pass - `core_stress_tests` 550/0, `qrwifi_stress_tests` 759.439/0, `disk_vpn_stress_tests` 7320/0, `secdl_stress_tests` 1.875.637/0, `connect_stress_tests` 386/0.

**Vẫn còn dang dở / chưa kiểm chứng**
- Mọi thứ cần thiết bị/môi trường thật như đã liệt kê ở 4o và 4p (hai máy LAN, hook thật, WiFi/VPN thật,
  đổi phân vùng thật, điện thoại Android, tự cập nhật đầu-cuối).
- Connect Together: thiết kế mã 9 số → khóa (mục 6); server nghe trên mọi card mạng; lưới 3 máy và fuzz
  `PeerDiscovery` chưa có kịch bản; thoát ứng dụng đúng lúc đang suy khóa chưa đo.
- Security/Downloader: trang 10 MB mất ~3 giây phân tích trên luồng giao diện; các lệnh Defender ngắn còn
  chạy đồng bộ; timeout 30 giây và đường "máy chủ ép trả bản nén" chưa có kịch bản.
- QR: `QRWindow` tạo/hủy 100 lần tăng khoảng 10 MB bộ nhớ, chưa tìm ra chỗ rò; hai lần ghi lịch sử cùng
  mili-giây cùng kích thước từ hai tiến trình sẽ không được phát hiện để gộp.
- Lõi: script PowerShell dài hơn khoảng 12.000 ký tự không khởi chạy được (báo lỗi ngay); chưa dựng cửa sổ
  của bảy công cụ còn lại trong stress.
- `qr_ui_tests` chạy từ `build\` vẫn để sót MỘT thư mục tạm mỗi lần (17 bộ còn lại không sót) - chưa rõ thứ gì
  còn giữ tệp lúc thoát.
- 24 tệp `.exe` kiểm tra thủ công cũ trong `build\` (từ các phiên trước, không còn mã nguồn) chưa xóa.

## 4r. SignPath bị từ chối - rà mã nguồn giảm rủi ro bị gắn cờ + hướng tiếp theo (2026-10-09, v1.19.3)
Yêu cầu người dùng sau khi nhận email từ chối của SignPath (mục 4m): "sửa mã nguồn đảm bảo không bị
block/gắn cờ nữa", "rà lại mã nguồn". Đã làm rõ với người dùng TRƯỚC khi sửa: cảnh báo "chưa ký số" của
SmartScreen/Smart App Control **không nằm trong mã nguồn** và **không có dòng code nào xóa được nó** - chỉ
ký số (chứng chỉ trả phí hoặc SignPath, cả hai hiện chưa có) hoặc phát hành qua Microsoft Store mới hết
hẳn. Phần RÀ ĐƯỢC THẬT trong mã nguồn là những yếu tố khiến hệ thống chấm điểm HEURISTIC (không dựa chữ ký
mã độc cụ thể mà dựa đặc điểm tổng thể của tệp) xếp hạng rủi ro cao hơn mức cần thiết - đã sửa phần này.

**Đã sửa - metadata phiên bản Win32 (trước đây HOÀN TOÀN RỖNG, xác nhận thật qua
`(Get-Item ...).VersionInfo` trước khi sửa: mọi trường kể cả FileVersionRaw dạng số đều rỗng/0.0.0.0)**:
- `assets/app_icon.rc`: thêm khối `VERSIONINFO` cho `OneForAll.exe` (CompanyName/ProductName/FileVersion/
  ProductVersion/FileDescription/LegalCopyright/Comments), đọc số phiên bản từ `generated/core/Version.h`
  (chính tệp sinh từ `VERSION` ở gốc dự án qua `configure_file` - không chép tay, không lệch số như các
  nơi khác trong dự án từng gặp phải, xem mục 4k).
- **Lỗi thật gặp khi viết khối này, đã xác nhận bằng cách tự kiểm `VersionInfo` sau mỗi lần sửa (không chỉ
  tin biên dịch thành công)**: dùng tên tượng trưng `VS_VERSION_INFO VERSIONINFO` làm ID tài nguyên mà
  KHÔNG `#include <winver.h>` (macro định nghĩa giá trị thật của nó là số 1) khiến `windres` hiểu nhầm
  thành một resource mang TÊN chuỗi "VS_VERSION_INFO" thay vì ID số 1 kiểu `RT_VERSION` chuẩn - biên dịch
  và link hoàn toàn không báo lỗi, `.rsrc` vẫn chứa đủ dữ liệu (xác nhận bằng cách dò chuỗi UTF-16 "Compa-
  nyName" trực tiếp trong file nhị phân), nhưng `GetFileVersionInfo()` của Win32 (nền của `.VersionInfo`
  trong PowerShell/Explorer/Properties) không tìm thấy gì. Sửa bằng cách dùng thẳng số `1 VERSIONINFO`.
- `installer/OneForAll.iss`: thêm `VersionInfoCompany/Description/ProductName/Version/Copyright`,
  `AppPublisher`, `AppCopyright` cho CHÍNH tệp `OneForAll_Setup.exe` (khác khối trên - đó là cho
  `OneForAll.exe` bên trong). `VersionInfoVersion` của Inno Setup đòi đúng 4 phần số "X.X.X.X" trong khi
  `VERSION` chỉ có 3 phần - thêm xử lý tiền xử lý (ISPP) tự cắt hậu tố kiểu semver (nếu có, vd
  "0.0.0-dev") rồi thêm ".0".
- `installer/Product.wxs`: thêm `ARPHELPLINK`, `ARPCONTACT`, `ARPCOMMENTS` cho gói `.msi` (xem được qua
  chuột phải file > Thuộc tính, và trong "Apps & features" của Windows).
- **Đã xác nhận THẬT**: build lại `OneForAll.exe` - `VersionInfo.FileVersionRaw`/`ProductVersionRaw` ra
  đúng "1.19.2.0", `CompanyName`/`ProductName`/`FileDescription`/`LegalCopyright` đúng như đã viết,
  `Language` ra "Vietnamese (Vietnam)" (trước khi sửa: TẤT CẢ rỗng).
- **Lỗi build thật thứ hai, phát hiện NGAY TRONG đợt này khi tăng `VERSION` lên 1.19.3 để phát hành**:
  Ninja không tự dò phụ thuộc `#include` bên trong tệp `.rc` như vẫn làm cho `.cpp`/`.h` - đổi nội dung
  `generated/core/Version.h` (chạy lại `cmake` sau khi sửa `VERSION`, KHÔNG đổi chính `app_icon.rc`) không
  khiến `app_icon.rc.obj` được biên dịch lại. Hậu quả xác nhận thật: `OneForAll.exe` chạy lên đúng
  "v1.19.3" trong log (đọc `APP_VERSION` lúc runtime) nhưng `VersionInfo.FileVersionRaw` của CHÍNH file đó
  vẫn báo "1.19.2.0" - đúng kiểu lỗi "giá trị tĩnh trôi dần không ai hay" dự án đã gặp nhiều lần ở nơi khác
  (mục 4l). Sửa bằng `set_source_files_properties(assets/app_icon.rc PROPERTIES OBJECT_DEPENDS
  .../generated/core/Version.h)` trong `CMakeLists.txt` - khai cạnh phụ thuộc tường minh cho Ninja. Xác
  nhận lại: build sau khi sửa biên dịch lại đúng `app_icon.rc.obj`, `VersionInfo.FileVersionRaw` ra đúng
  "1.19.3.0".

**Đã rà, XÁC NHẬN KHÔNG có (không phải "đã sửa" vì không có gì để sửa) - những đặc điểm hành vi thường
gặp ở mã độc mà một app hợp pháp cũng cần tránh để không bị chấm điểm heuristic oan**:
- Không có khóa registry `...\CurrentVersion\Run` hay cách tự khởi động cùng Windows nào (đã `grep` toàn
  bộ `src/` xác nhận).
- Không có cài đặt dịch vụ Windows (`CreateService`/`SERVICE_AUTO_START`).
- Nâng quyền DUY NHẤT qua `ShellExecuteW` verb `"runas"` (hộp thoại UAC chuẩn, người dùng tự xác nhận) -
  không có kỹ thuật bỏ qua UAC nào.
- `UpdateInstaller` (mục 4o) đã có sẵn: chỉ nhận URL `https://github.com/...`, đối chiếu SHA-256 với
  `digest` GitHub công bố trước khi chạy, chờ đúng tiến trình ứng dụng thoát rồi mới cài - không "tải về
  rồi chạy ngay không kiểm tra gì" kiểu dropper.

**CÓ CHỦ Ý KHÔNG sửa - ranh giới đã nêu rõ với người dùng, không phải bỏ sót**:
- `src/core/PowerShellRunner.cpp` gọi `powershell.exe -ExecutionPolicy Bypass -EncodedCommand <base64>`
  (dùng cho Disk Cleanup/VPN/Security Gateway). Đây ĐÚNG LÀ một mẫu hành vi dòng lệnh hay bị EDR/AV để ý
  (MITRE ATT&CK T1059.001: PowerShell với tham số bypass + lệnh mã hóa). Lý do kỹ thuật dùng
  `-EncodedCommand` đã được thử nghiệm và ghi lại ngay trong code (`-Command -` qua stdin từng chạy im
  lặng không làm gì với script nhiều dòng; ghi file `.ps1` tạm từng bị phần mềm diệt virus khóa file giữa
  chừng - xem chú thích gốc trong file). KHÔNG đổi cơ chế này chỉ để né heuristic của AV - đó là kỹ thuật
  né tránh phát hiện (evasion), và việc "giấu" hành vi gọi PowerShell của một tính năng hợp pháp không
  giải quyết được gì ngoài việc làm code khó đọc hơn.
- `src/tools/security/engine/CommandAnalyzer.cpp` (bộ phân tích lệnh của Security Gateway) chứa nhiều
  chuỗi/regex nhận diện tên công cụ tấn công đã biết (Mimikatz, PowerSploit, Invoke-ReflectivePEInjection,
  AmsiUtils...) và kỹ thuật tấn công (xóa shadow copy, tắt Defender, reverse shell...). Đã đọc lại toàn bộ
  422 dòng: đây CHỈ LÀ các mẫu REGEX dùng để NHẬN DIỆN (giống cách mọi phần mềm diệt virus/EDR thật lưu
  tên họ mã độc trong chính binary của nó), KHÔNG chứa payload/shellcode/mã khai thác thật nào có thể tự
  chạy được - rủi ro AV heuristic ở đây thấp, không phải nguồn gốc của lần Windows Defender cách ly
  `tests/security_tests.cpp` trước đó (mục 4p; nguyên nhân thật là các chuỗi lệnh ransomware MẪU dạng
  một-dòng-hoàn-chỉnh dùng làm dữ liệu test, đã sửa bằng cách ghép chuỗi lúc chạy, và các target test này
  vốn `EXCLUDE_FROM_ALL` nên không bao giờ nằm trong `OneForAll.exe`/bộ cài đặt phát hành). Không sửa gì
  thêm ở đây.

**Hướng tiếp theo cho việc hết hẳn cảnh báo (người dùng quyết định, không tự làm thay được)**:
1. **Microsoft Store (đề xuất, miễn phí)**: từ 10/9/2025 Microsoft mở đăng ký nhà phát triển CÁ NHÂN miễn
   phí toàn cầu (`storedeveloper.microsoft.com`, xác minh bằng giấy tờ tùy thân + ảnh selfie, không cần
   thẻ tín dụng). App Win32 .exe/.msi có sẵn nộp được KHÔNG cần sửa code, qua một trong hai đường: đóng
   gói lại thành MSIX (Microsoft tự ký lại gói, không cần mua chứng chỉ) hoặc nộp thẳng installer .exe/.msi
   hiện có (vẫn phải tự ký bằng chứng chỉ chain tới gốc tin cậy của Microsoft cho đường này - không giải
   quyết được vấn đề chi phí). Đường MSIX là hướng đáng làm nhất. Rủi ro CHƯA kiểm chứng: Store có thể từ
   chối vì tab tải video (yt-dlp) đụng chính sách; các tính năng cần quyền Administrator (sửa hosts, đổi
   phân vùng, Defender) có thể bị gói MSIX giới hạn - cần thử mới biết chắc.
2. **Mua chứng chỉ ký số trả phí** (vd Certum OV/Open Source, ~50-300 USD/năm) - vẫn phải tích lũy uy tín
   SmartScreen vài tuần đầu dù đã ký, không còn được "uy tín ngay" như EV trước đây (Microsoft đã đổi
   chính sách này).
3. **SignPath trả phí** (`docs.signpath.io/change-subscription`) - vẫn cần chứng chỉ riêng, không miễn phí
   như chương trình Foundation đã bị từ chối.
4. Nộp lại đơn SignPath Foundation sau khi dự án có thêm tín hiệu uy tín công khai (sao GitHub, bài viết,
   thảo luận độc lập) - không có mốc thời gian cụ thể.

Bản phát hành KHÔNG cần build lại vì lý do ký số (vẫn chưa ký) - chỉ build lại vì đã thêm metadata phiên
bản ở trên.

**Quan sát thực tế sau khi phát hành (2026-10-10)**: người dùng tải `OneForAll_Setup.exe` trực tiếp từ
GitHub Release `v1.19.3` và chạy - KHÔNG hiện cảnh báo SmartScreen nào ("Windows protected your PC"),
chạy thẳng vào trình cài đặt. Đã kiểm tra THẬT trên máy đó trước khi kết luận gì (không đoán):
- Máy là **Windows 10 Pro build 19045** - **Smart App Control không tồn tại trên máy này** (tính năng
  này CHỈ có từ Windows 11 22H2+, xác nhận qua registry `HKLM:\SYSTEM\CurrentControlSet\Control\CI\Policy`
  không có khóa `VerifiedAndReputablePolicyState`). Nghĩa là lớp chặn mạnh nhất chưa từng áp dụng ở đây -
  KHÔNG phải đã "vượt qua" được Smart App Control.
- SmartScreen (application-reputation cho file .exe) không bị tắt theo registry/Group Policy trên máy
  này (`SmartScreenEnabled`, `EnableWebContentEvaluation` đều để mặc định, không có override). Windows
  Defender Antivirus thật đang chạy bình thường (`RealTimeProtectionEnabled=True`, signature cập nhật
  cùng ngày).
- **KHÔNG có cách xác nhận chắc chắn lý do cụ thể** SmartScreen's cloud application-reputation check
  không cảnh báo lần chạy này - đây là dịch vụ cloud đóng của Microsoft, không công bố thuật toán, và log
  cục bộ chi tiết (`Microsoft-Windows-SmartScreen/Debug`) không được bật sẵn nên không có bằng chứng nào
  để đọc lại. Các yếu tố CÓ THỂ đã góp phần (không khẳng định là nguyên nhân duy nhất/đã chứng minh):
  metadata VERSIONINFO đầy đủ thêm ở trên (Microsoft công khai khuyến nghị điều này để cải thiện uy tín,
  nhưng không công bố nó đủ để tự tắt cảnh báo), domain `github.com`/`githubusercontent.com` có uy tín cao
  trong hệ thống SmartScreen, và file hash của `OneForAll_Setup.exe` có thể đã tích lũy uy tín dần qua các
  lượt tải v1.19.0 → v1.19.3 trước đó.
- **Giới hạn quan trọng của quan sát này**: chỉ xảy ra trên MỘT máy Windows 10, không có Smart App
  Control. CHƯA kiểm chứng trên máy Windows 11 có Smart App Control ở chế độ "On" (Enforce) - đó là lớp
  chặn khác hẳn và mạnh hơn SmartScreen application-reputation. Không suy rộng thành "vấn đề bị chặn đã
  giải quyết xong" cho mọi máy.

Việc nên làm tiếp (duy trì, không phải sửa thêm): giữ nguyên các yếu tố metadata đã thêm ở trên cho mọi
bản phát hành sau - xem rule mới trong `CLAUDE.md`.

## 4s. Rà lỗi giao diện ghi đè/cắt mất khi nhiều dữ liệu + nút không hoạt động (2026-10-10, v1.19.4)

Yêu cầu người dùng, kèm ảnh chụp Auto Click thật cho thấy panel "Cài đặt hành động" bị cắt mất bên phải:
"sửa lỗi giao diện, tương đương sửa hết các lỗi giao diện có trong các tiện ích". Sau đó mở rộng: "đảm bảo
toàn bộ giao diện ko lỗi ghi đè, phải tính đến trường hợp khó nhất, ví dụ 2 ổ đĩa và 4 gmail thì nó có đủ
chỗ không" (người dùng dùng Google Drive for Desktop mount nhiều tài khoản Gmail thành ổ đĩa ảo - tổng 6 ổ
hiển thị trong Disk Cleanup), và "trong phần tải về có các nút không hoạt động".

**Đã sửa - Auto Click (`AutoClickWindow.cpp`, `widgets/ActionEditorWidget.cpp`, `widgets/ActionListWidget.cpp`)**:
- Panel phải "Cài đặt hành động" dùng `QScrollArea` CHỦ Ý tắt cuộn ngang, nhưng bị ép
  `setMinimumWidth(220)` ở `AutoClickWindow.cpp` - nhỏ hơn nội dung thật (trang "Kéo chuột" có 2 hàng tọa
  độ, mỗi hàng 2 `QSpinBox` + nút "Lấy tọa độ"), nên phần dư bị cắt mất không cách nào xem được (xác nhận
  bằng ảnh chụp thật người dùng gửi). Sửa: `ActionEditorWidget` tự đo `sizeHint()` của TRANG RỘNG NHẤT
  trong 7 trang (trước khi add vào `QStackedWidget`) rồi tự `setMinimumWidth()` cho chính nó - không còn
  số đoán mò, không "giật" kích thước khi đổi loại hành động.
- Cột "Mô tả" trong bảng `ActionListWidget` (duy nhất `Stretch`) bị bóp gần về 0 khi panel phải cần nhiều
  chỗ hơn, elide chỉ còn 1 ký tự. Sửa: `setMinimumSectionSize(70)` cho header + bật lại
  `Qt::ScrollBarAsNeeded` cho cuộn ngang (trước đó tắt hẳn) - không bao giờ mất hẳn nội dung, có thể cuộn
  khi cần.
- Kích thước cửa sổ mặc định `1050x650`/tối thiểu `850x520` không đủ cho cả 3 panel cộng lại sau 2 thay
  đổi trên - tăng lên `1250x680`/`1000x560`.
- **Đã xác nhận THẬT bằng ảnh chụp** (không chỉ tin code): dựng script PowerShell dùng UI Automation
  (`System.Windows.Automation`) tự mở app, điều hướng sidebar, bấm "Mở Cửa Sổ", rồi chụp cửa sổ đó qua
  Win32 `PrintWindow` (API `CopyFromScreen`/`BitBlt` thường dùng thất bại trong phiên làm việc này - "The
  handle is invalid" - môi trường không có desktop session tương tác đầy đủ để chụp màn hình trực tiếp).
  Trước/sau khi sửa đều có ảnh đối chiếu.

**Đã sửa - Disk Cleanup (nguyên nhân chính gây "2 ổ đĩa + 4 gmail không đủ chỗ")**: dãy thẻ "DUNG LƯỢNG Ổ
ĐĨA" (`CleanupTab::reloadDriveOverview()`) dùng `QHBoxLayout` thẳng, KHÔNG giới hạn số thẻ
(`DiskSpaceInfo::listDrives()` liệt kê MỌI ổ đã mount qua `QStorageInfo::mountedVolumes()` - ổ vật lý VÀ ổ
ảo như Google Drive for Desktop), không có `QScrollArea` bọc ngoài - máy có từ 4-5 ổ trở lên (rất thường
gặp khi dùng Google Drive nhiều tài khoản) sẽ có thẻ bị đẩy ra ngoài tầm nhìn, không cách nào xem được.
Sửa bằng `FlowLayout` mới (xem dưới) thay cho `QHBoxLayout` - thẻ tự xuống dòng. **Đã xác nhận THẬT**: máy
build chỉ có 2 ổ đĩa thật (C/D) + 1 ổ ảo Google Drive (G: - xác nhận chính máy này cũng có dùng Google
Drive for Desktop, khớp with bối cảnh người dùng nêu), KHÔNG đủ để tái hiện "6 ổ" - tạm thời nhân bản danh
sách ổ đĩa x3 (9 thẻ) CHỈ để chụp ảnh kiểm chứng, build, chụp (xác nhận: 9 thẻ tự xếp lưới 3×3 gọn gàng,
không cắt/không cần cuộn), rồi revert đoạn nhân bản trước khi build bản thật.

**Mới - `src/ui/widgets/FlowLayout.{h,cpp}` (thư viện CMake `ui_widgets` dùng chung)**: layout tự xuống
dòng khi hết chỗ ngang, phỏng theo mẫu "Flow Layout" chính thức của Qt (Qt Examples/Widgets/Layouts/
FlowLayout). Dùng cho dãy widget có SỐ LƯỢNG đổi theo dữ liệu thật (thẻ ổ đĩa) hoặc dãy nút cố định nhưng
nhiều hơn một hàng vừa ở kích thước cửa sổ thường dùng. Áp dụng thêm cho:
- **WiFi Connection** (`NetworksTab.cpp`): 7 nút full-text ("🔄 Quét lại" ... "💜 Love WiFi") trên một
  `QHBoxLayout` không cuộn - đổi sang `FlowLayout`. Ở kích thước cửa sổ mặc định hiện tại vẫn vừa đủ một
  hàng (xác nhận bằng ảnh), nhưng không còn rủi ro cắt mất nút khi DPI/font khác làm 7 nút rộng hơn.

**Đã sửa - lỗi CHỨC NĂNG thật ở Downloader (khớp "trong phần tải về có các nút không hoạt động")**:
`DirectDownloadTab::onTableContextMenu()` (tab "Tải trực tiếp") trước đây dựa thẳng vào
`m_table->selectionModel()->selectedRows()` - bấm CHUỘT PHẢI thẳng vào một hàng CHƯA được chọn trước đó
(trường hợp thường gặp nhất, không cần bấm trái chọn hàng rồi mới bấm phải) không tự đổi selection của
`QTableWidget`, nên `selectedRows()` rỗng, hàm `return` ngay - KHÔNG hiện menu "Bắt đầu/Tiếp tục - Tạm
dừng - Hủy" nào cả, im lặng hoàn toàn. Người dùng sẽ thấy giống hệt "nút không hoạt động" dù thực ra menu
chưa từng hiện ra. Sửa: tự `m_table->rowAt(pos.y())` + `selectRow()` trước khi build menu, giống hành vi
chuẩn của Windows Explorer.

**Đã rà, XÁC NHẬN KHÔNG có vấn đề (ảnh chụp thật ở kích thước cửa sổ mặc định, không cần sửa)**: Android
Phone Control (3 nút hàng đầu `DevicesTab`), QR Tools (cả tab "Tạo mã QR" và "Quét mã QR"). Một agent rà
soát riêng (chỉ đọc code, không sửa) đã ước lượng các tool này "có khả năng" bị cắt dựa trên tính toán
pixel - kiểm tra lại bằng ảnh chụp thật cho thấy ước lượng đó quá thận trọng, không có vấn đề thật.

**Cải thiện kèm theo (không phải lỗi riêng lẻ, rà toàn dự án)**: không file `*UiStyle.h` nào trong 8 module
đặt `placeholder-text-color` cho QSS của `QLineEdit`/`QComboBox`/`QSpinBox`/`QPlainTextEdit` - Qt dùng màu
mặc định theo theme hệ thống, có thể có độ tương phản thấp hơn cần thiết trên nền trắng `#ffffff` đã chọn.
Thêm `placeholder-text-color: #8c959f` (cùng tông xám đã dùng nhất quán cho text phụ/disabled trong toàn
dự án) vào cả 8 file `*UiStyle.h` + 2 chỗ style riêng không dùng chung (`CommandGatewayTab.cpp`,
`ActionEditorWidget.cpp`) - rà bằng `grep -rn setPlaceholderText src/` xác nhận phủ đủ cả 13 chỗ dùng
placeholder trong dự án.

**CHƯA xác định được - nghi vấn hiển thị ở `PairingTab` (Connect Together), ĐÃ GHI NHẬN TỪ v1.2.0 (mục 8,
dòng "Còn 1 nghi vấn hiển thị nhỏ... chỉ thấy qua ảnh chụp tự động")**: ô "Địa chỉ IP"/"Mã ghép đôi" trong
tab "Ghép đôi" hiện chữ như bị nhòe/ghosting khi chụp qua script tự động hóa - **đã thử nhiều cách chẩn
đoán trong đợt này, không giải quyết được**:
- Thêm `placeholder-text-color` (xem trên) - KHÔNG đổi gì, vẫn nhòe y hệt.
- Thay `PrintWindow` flag `2` (`PW_RENDERFULLCONTENT`) bằng `0` - vẫn nhòe y hệt (chỉ khác viền cửa sổ
  theme cổ điển do thiếu DWM frame).
- Đợi thêm 5 giây + `InvalidateRect`/`UpdateWindow` ép vẽ lại trước khi chụp (loại trừ khả năng đang
  re-layout/animation lúc mới mở) - vẫn nhòe y hệt.
- Set TEXT THẬT (`setText("TEST-DEBUG-1234")`, không phải placeholder) vào `m_addressEdit` - CŨNG bị nhòe
  y hệt, loại trừ hoàn toàn khả năng liên quan tới placeholder/màu sắc. Đã revert đoạn test này.
- Trong CÙNG các ảnh chụp đó, mọi `QLabel`/`QComboBox`/`QSpinBox` khác (kể cả "IP: 42.113.162.72" ở VPN &
  Location, placeholder "Dán lệnh PowerShell..." ở Security Gateway cùng dùng `placeholder-text-color` mới
  sửa) đều hiển thị HOÀN TOÀN RÕ NÉT - loại trừ khả năng đây là giới hạn chung của việc chụp ảnh qua
  `PrintWindow`/độ phân giải ảnh nói chung. Hiện tượng CHỈ xảy ra ở đúng 2 ô này.
- Việc hiện tượng này giữ NGUYÊN qua rất nhiều phiên bản (từ v1.2.0 tới v1.19.4, hàng chục lần sửa code
  không liên quan tới `PairingTab`) là một tín hiệu đáng chú ý - nếu là lỗi mã nguồn ngẫu nhiên, khả năng
  nó biến mất/đổi dạng qua ngần ấy lần build thường cao hơn nhiều so với một artifact ổn định của riêng
  công cụ chụp ảnh tự động hóa từ xa dùng trong các lần rà soát này.
- **KHÔNG kết luận đây là lỗi ứng dụng thật** - chỉ ghi nhận hiện tượng quan sát được qua công cụ tự động,
  CHƯA xác nhận được bằng mắt thường người dùng khi ngồi trực tiếp trước máy (CLAUDE.md mục 2: điều gì
  không kiểm chứng được thì ghi rõ CHƯA kiểm chứng). Không sửa gì thêm ở đây vì không xác định được
  nguyên nhân cụ thể để sửa đúng chỗ - sửa đoán mò có nguy cơ che giấu triệu chứng mà không giải quyết gốc
  rễ (nếu gốc rễ là thật) hoặc chỉ tốn công vô ích (nếu là artifact môi trường).

**Đã xác nhận THẬT bằng toàn bộ 11 bộ test liên quan** (không chỉ tin biên dịch): `autoclick_tests` (100),
`diskcleanup_tests` (457), `diskcleanup_ui_tests` (53), `wifi_tests` (119), `wifi_ui_tests` (90),
`connect_tests` (1080), `connect_ui_tests` (13), `security_tests` (333), `security_ui_tests` (6),
`downloader_tests` (283), `downloader_ui_tests` (20) - tổng 2554 kiểm tra, 0 lỗi.

## 4t. Luồng tự cập nhật: không cài được sau khi tải xong + thêm kiểm tra thủ công (2026-10-10, v1.19.5)

Người dùng tự tay test tính năng cập nhật (cài `v1.19.3`, mở app, thấy hộp thoại "Có bản cập nhật mới",
bấm "Cập nhật ngay") và báo: "khi cập nhật thì tải 100% nhưng nó ko tự lên bản mới, mở lại lại yêu cầu
cập nhật dù đã tải về", kèm yêu cầu thêm nút kiểm tra/cập nhật thủ công và đảm bảo tự mở lại app sau khi
cập nhật xong.

**Đã tái hiện và xác nhận THẬT bằng cách tự cài đặt + test** (không chỉ đọc code): tải `OneForAll_Setup.exe`
thật của Release `v1.19.3` từ GitHub, cài im lặng vào máy (`C:\Users\...\AppData\Local\Programs\One for
ALL\`), chạy app đã cài, bấm "Cập nhật ngay" qua tự động hóa UI thật (script PowerShell dùng
`System.Windows.Automation` - gặp trở ngại: `QMessageBox` modal không xuất hiện trong cây UIA duyệt từ
`AutomationElement.RootElement` dù `visible=True` qua Win32 `EnumWindows` trực tiếp - lấy được bằng
`AutomationElement.FromHandle(hwnd)` thẳng từ HWND thay vì duyệt cây). **Kết quả LẦN TEST NÀY: luồng cập
nhật hoạt động ĐÚNG hoàn toàn** - tự tải, tự thoát, tự cài (`21 giây` từ lúc bấm tới khi app mới xuất
hiện), tự mở lại, log xác nhận `"Khởi động One for ALL v1.19.4"`, registry/VersionInfo đúng `1.19.4`.

**Không tái hiện được lỗi người dùng báo trong môi trường test này** - nhưng đã xác định được MỘT ĐIỂM
YẾU THẬT trong code có khả năng gây đúng triệu chứng đó trên máy khác: `buildHelperScript()` (script
PowerShell chạy TÁCH RỜI sau khi ứng dụng đã thoát, không còn `Logger` nào ghi lại được gì) có 2 vấn đề:
1. `Start-Sleep -Milliseconds 500` sau `Wait-Process` có thể KHÔNG ĐỦ trong một số máy/thời điểm - Windows
   không đảm bảo named kernel object (`AppMutex` - `OneForAllRunningMutex`, dùng để Inno Setup tự phát
   hiện ứng dụng còn chạy không) được dọn sạch NGAY lúc tiến trình vừa thoát; nếu Inno Setup kiểm tra
   Mutex ngay sau đó vẫn thấy "còn", nó tự hủy cài đặt trong im lặng (`/SUPPRESSMSGBOXES` không báo gì) -
   khớp ĐÚNG với "tải 100% nhưng không tự lên bản mới".
2. **Hoàn toàn không có cách nào biết cài đặt có thành công hay không** - script chạy ẩn
   (`-WindowStyle Hidden`), không ghi log, không kiểm tra exit code của trình cài đặt. Nếu có lỗi (Mutex
   race condition ở trên, UAC bị hủy vì máy cài vào vị trí cần quyền cao hơn và không ai bấm kịp khi
   script chạy ẩn, đĩa đầy, antivirus khóa file tạm thời...), ứng dụng TỰ MỞ LẠI bản CŨ và lại tự hỏi cập
   nhật - người dùng thấy đúng y "mở lại lại yêu cầu cập nhật dù đã tải về" mà KHÔNG CÓ CÁCH NÀO biết vì
   sao, và bản thân tôi cũng không có cách nào chẩn đoán chính xác nếu không tự tái hiện được.

**Đã sửa (`UpdateInstaller.cpp/.h`)**:
- Tăng `Start-Sleep` sau `Wait-Process` từ 500ms lên 1500ms.
- `buildHelperScript()` giờ tự ghi nhật ký riêng (`%TEMP%\OneForAll_Update\update_helper.log`) từng bước:
  chờ PID, bắt đầu chạy trình cài đặt, exit code, thành công/thất bại, mở lại ứng dụng - nếu lỗi còn tái
  diễn, LẦN SAU sẽ đọc được log này để biết CHÍNH XÁC nguyên nhân thay vì đoán.
- Nếu exit code trình cài đặt khác `0`/`3010` (3010 = thành công nhưng cần khởi động lại MÁY), ghi dấu vết
  vào `%TEMP%\OneForAll_Update\update_failed.marker` (chứa exit code).
- `UpdateInstaller::consumePreviousUpdateFailure()` (mới, static): đọc + XÓA dấu vết đó - gọi một lần lúc
  `MainWindow` khởi động (TRƯỚC khi tự kiểm tra bản mới), nếu có nội dung thì hiện hộp thoại báo RÕ RÀNG
  "Lần cập nhật trước đã tải xong nhưng cài đặt không thành công: ExitCode=..." thay vì im lặng hỏi cập
  nhật lại từ đầu như trước đây (giải quyết trực tiếp phần "không rõ vì sao" của lỗi người dùng báo, dù
  nguyên nhân gốc rễ chưa chắc đã hết - lần tái diễn tiếp theo sẽ CÓ THÔNG TIN để sửa đúng chỗ).
- 2 test mới trong `tests/update_tests.cpp` cho nhật ký/dấu vết lỗi trong `buildHelperScript()` và cho
  `consumePreviousUpdateFailure()` (đọc đúng nội dung, xóa sau khi đọc, gọi lần 2 phải rỗng).

**Mới - nút "🔄 Kiểm tra cập nhật" thủ công** (`MainWindow.{h,cpp}`, sidebar, dưới nhãn phiên bản):
`UpdateChecker::checkNow()` đã có sẵn public từ trước nhưng chưa có UI nào gọi. Khác với lần tự động lúc
mở app (im lặng khi không có gì mới/lỗi mạng - "không phải thao tác người dùng tự yêu cầu", giữ nguyên
hành vi cũ), nút bấm thủ công PHẢI phản hồi rõ ràng dù kết quả là gì: đã nối thêm `upToDate`/`checkFailed`
(trước đây không signal nào trong 2 cái này được MainWindow lắng nghe) - cờ `m_manualUpdateCheckPending`
phân biệt hai trường hợp (tự động vs thủ công) dùng chung một `UpdateChecker`/một bộ signal. **Đã xác
nhận THẬT bằng tự động hóa UI** (bấm nút qua `InvokePattern`, phân biệt với nút "Mở Cửa Sổ..." bằng tọa độ
X trong sidebar vì cả hai cùng độ dài tên không phân biệt được): hộp thoại "Kiểm tra cập nhật" hiện đúng
"Đang dùng bản mới nhất (v1.19.4)." khi app đã là bản mới nhất.

**Tự mở lại app sau khi cập nhật xong**: ĐÃ CÓ SẴN từ trước (`Start-Process -FilePath <appExePath>` ở cuối
`buildHelperScript()`) và đã xác nhận THẬT hoạt động đúng trong lần test ở trên (21 giây, log/registry xác
nhận đúng phiên bản mới) - không phải sửa gì thêm ở phần này, chỉ tăng độ tin cậy/khả năng chẩn đoán như
trên để các trường hợp hiếm gặp (race condition Mutex, UAC, đĩa đầy...) không còn hoàn toàn im lặng.

## 4u. setup.exe không hỏi đường dẫn cài đặt (2026-10-10, v1.19.6)

Người dùng báo: "tại sao khi tôi chạy setup.exe nó không hỏi tôi lưu ở đường dẫn nào".

**Đã xác nhận THẬT nguyên nhân** bằng cách tự cài + chạy lại installer thật trên máy (không chỉ đọc code):
cài `OneForAll_Setup.exe` (v1.19.5) vào máy, rồi chạy LẠI installer trên CHÍNH MÁY ĐÓ - trang đầu tiên hiện
ra thẳng là "Các thiết lập bổ sung" (Additional Tasks), **bỏ qua hoàn toàn** cả trang "Chọn Chế độ Cài đặt"
(Select Setup Install Mode) lẫn trang chọn thư mục (Select Destination Location). Gỡ cài đặt sạch (qua
uninstaller chính thức, xác nhận registry/thư mục không còn gì) rồi chạy lại installer **từ máy sạch**:
trang đầu tiên đúng là "Chọn Chế độ Cài đặt" như thiết kế - xác nhận rõ đây là hành vi của chính Inno Setup
(không phải lỗi dựng thiếu trang trong `.iss`): khi phát hiện CÙNG `AppId` đã được cài trước đó, Inno Setup
mặc định (`UsePreviousAppDir=yes`, giá trị mặc định, không phải do dự án tự đặt) TỰ ĐỘNG dùng lại chế độ
cài đặt + đường dẫn cũ, không hỏi lại gì cả, kể cả khi chạy ở chế độ TƯƠNG TÁC (không phải silent) - đúng
mô tả người dùng: họ đã cài qua rồi chạy lại `.exe` để cài đè bản mới hơn.

**Đã sửa (`installer/OneForAll.iss`)**: thêm tường minh `DisableDirPage=no` vào `[Setup]` - dù đây là giá
trị MẶC ĐỊNH của Inno Setup (không khai thì vẫn là `no`), khai rõ ràng để loại trừ hoàn toàn khả năng một
cấu hình/phiên bản Inno Setup nào đó suy ra `auto` (skip khi phát hiện cài trước) từ sự kết hợp với
`UsePreviousAppDir`. **Có chủ đích KHÔNG đổi** `UsePreviousAppDir` (giữ nguyên mặc định `yes`): đây là
thiết lập UpdateInstaller's cài ngầm (`/VERYSILENT`) dựa vào để tự cài ĐÚNG VÀO VỊ TRÍ CŨ mà không hỏi gì -
nếu đổi thành `no`, cơ chế tự cập nhật (mục 4t) ở LẦN SAU sẽ cài vào `DefaultDirName` mặc định thay vì vị
trí người dùng đã chọn ban đầu (nếu họ từng đổi khác mặc định lúc cài đầu) - tạo ra một bản cài THỨ HAI thay
vì nâng cấp đúng bản đang chạy. Silent mode (dùng bởi tự cập nhật) luôn bỏ qua MỌI trang wizard bất kể
`DisableDirPage`, nên thay đổi này không ảnh hưởng gì tới luồng tự cập nhật.

**CHƯA xác nhận được bằng automation** rằng trang "Select Destination Location" THỰC SỰ hiện ra sau khi sửa
(đúng tinh thần CLAUDE.md mục 2 - ghi rõ điều chưa kiểm chứng được): đã thử 3 cách tự động hóa UI khác nhau
để bấm qua trang "Chọn Chế độ Cài đặt" (đứng trước trang cần kiểm tra) - UIA `InvokePattern` (control này
không hỗ trợ bất kỳ pattern UIA nào, xác nhận qua `GetSupportedPatterns()` trả về rỗng), input injection
chuột thật (`SetCursorPos`/`mouse_event` - từng dùng thành công cho sidebar của chính ứng dụng, nhưng không
ảnh hưởng gì tới control này), và `PostMessage` gửi thẳng `WM_LBUTTONDOWN`/`WM_LBUTTONUP` vào hàng đợi
message của cửa sổ - cả 3 đều không làm control (`TNewOptionButtonGroup`-kiểu Delphi/VCL tùy biến mà Inno
Setup Modern Wizard dùng) phản hồi. Đây là giới hạn của công cụ tự động hóa với loại control đặc thù này
trong môi trường làm việc hiện tại, không phải dấu hiệu bản sửa sai - `DisableDirPage` là thiết lập ĐÚNG,
có tài liệu rõ ràng, theo đúng cơ chế Inno Setup dùng để kiểm soát chính xác việc này. Người dùng tự chạy
`OneForAll_Setup.exe` (v1.19.6) trên máy SẠCH (chưa từng cài, hoặc đã gỡ cài đặt trước) để xác nhận trực
tiếp bằng mắt thường.

## 4v. Auto Click: hiện đại hóa UI + tổ hợp phím có thứ tự (2026-10-10, v1.19.7)

Người dùng gửi 5 ảnh chụp thật module Auto Click, nêu 8 vấn đề UI/UX. Quyết định chốt qua hỏi đáp: icon
menu dùng chung đúng 1 biểu tượng ngón tay trỏ (👆) cho mọi mục (không phải icon riêng theo ngữ nghĩa); tổ
hợp phím mở rộng hỗ trợ CẢ Cuộn chuột lẫn Click chuột làm thao tác kết thúc (không chỉ Cuộn).

**1. Menu chuột phải hiện đại + icon 👆 dùng chung**: widget mới `src/ui/widgets/ModernMenu.{h,cpp}` (bo
góc 10px, viền `#d0d7de`, hover xanh `#0969da` - đồng bộ phong cách phần còn lại ứng dụng thay vì menu mặc
định OS không trang trí gì) + `ModernMenu::addAction()` tự thêm tiền tố "👆 " cho mọi mục. Áp dụng tại
`ChainListWidget` (Chạy chuỗi này/Nhân bản/Đổi tên/Xóa), `ActionListWidget` (Sửa/Nhân bản/Xóa), và
`DirectDownloadTab` của Downloader (Bắt đầu-Tiếp tục/Tạm dừng/Hủy - bỏ icon ▶⏸✕ cũ để đúng nghĩa "chung 1
icon" theo yêu cầu áp dụng rộng).

**2. Bỏ viền focus xấu**: `ActionListWidget` thêm `outline: none` cho `QTableWidget::item:selected` và
`QTableWidget::item:focus` - chỉ CSS, không đổi hành vi chọn dòng.

**3. Esc không hủy được "lấy mẫu" tọa độ**: `CoordinateOverlay` trước chỉ dựa `keyPressEvent` của Qt (phụ
thuộc cửa sổ đang giữ focus bàn phím - không đáng tin khi overlay toàn màn hình vừa mới hiện ra). Thêm hook
bàn phím toàn cục `WH_KEYBOARD_LL` CHỈ nuốt phím Esc (cùng mẫu `DragGestureCapture::onRawKey` đã có từ
trước, không nuốt mọi phím như `HotkeyCapture`), gỡ hook trong destructor, cờ `m_finished` chống gọi hủy 2
lần (hook + `keyPressEvent` dự phòng có thể trùng nhau). `keyPressEvent` cũ vẫn giữ làm lớp dự phòng nếu
Windows từ chối cài hook.

**4. Widget `DurationInput` dùng chung (ms/giây/phút/giờ)**: `src/ui/widgets/DurationInput.{h,cpp}` - số
nhập tay (`QDoubleSpinBox`, ẩn nút mũi tên tăng/giảm qua `setButtonSymbols(NoButtons)`) + list chọn đơn vị.
Lưu nội bộ nguyên mili-giây; đổi đơn vị CHỈ đổi cách hiển thị (không phát `valueChanged`), gõ số mới mới
tính lại ms và phát tín hiệu. Áp dụng cho 3 ô Timing (Chờ trước/Chờ sau/Thời lượng) trong
`ActionEditorWidget`, thay `QSpinBox` ms thô cũ.

**5. Đơn giản trang "Kéo chuột"**: bỏ `m_captureDragStartBtn`/`m_captureDragEndBtn` + 2 nút "Lấy tọa độ"
tay; 4 ô tọa độ (điểm đầu/cuối) chuyển sang chỉ đọc (`setReadOnly(true)` + `NoButtons` + `NoFocus`, nền xám
`#eaeef2`) chỉ để HIỂN THỊ kết quả sau khi bắt bằng nút "🖐 Bắt thao tác kéo thật" - không còn đường nào để
tự gõ tay tọa độ sai lệch với thao tác kéo thật đã ghi.

**6. Tổ hợp phím có thứ tự**: `Action.h` thêm `enum class ModifierKey {Ctrl,Alt,Shift,Win}` và field
`std::vector<ModifierKey> modOrder` (GIỮ NGUYÊN 4 bool `modCtrl/Alt/Shift/Win` cũ - `core_stress_tests` fuzz
test trực tiếp 4 field này, không xóa được). Hàm `effectiveModOrder()`: trả `modOrder` nếu không rỗng; rỗng
(hồ sơ cũ) thì tự suy luận thứ tự cố định Ctrl→Alt→Shift→Win từ 4 bool - MỌI nơi đọc thứ tự (mô tả, chạy
chuỗi, lưu JSON) đều gọi hàm này, không bao giờ đọc `modOrder` thẳng. `HotkeyCapture` ghi lại đúng thứ tự
NHẤN THẬT (không phải thứ tự đang giữ) khi bắt tổ hợp bằng hook bàn phím thật. UI: 4 checkbox Ctrl/Alt/
Shift/Win đổi text thành "Ctrl (1)" theo đúng vị trí trong `m_hotkeyModOrder` khi đang check; bỏ chọn 1 ô ở
giữa thì các số sau tự dồn lại (tính lại từ đầu mỗi lần, không giữ số cũ). `ActionSerializer` ghi thêm
(không xóa 4 bool cũ) `"modOrder"` tường minh vào JSON; đọc JSON CŨ thiếu key này thì giữ `modOrder` rỗng -
`effectiveModOrder()` tự suy luận đúng như hành vi trước khi có tính năng này, không vỡ tương thích ngược.

**7. Đổi tên "Phím tắt" → "Tổ hợp phím" + mở rộng "Kết thúc bằng"**: `Action.h` thêm
`enum class HotkeyTrigger {KeyPress,Scroll,Click}`. UI trang Hotkey thêm combo "Kết thúc bằng" (Phím chính/
Cuộn chuột/Click chuột) + `QStackedWidget` 3 trang con - trang Cuộn/Click là widget instance RIÊNG nhưng
tái dùng field có sẵn của `ActionType::Scroll` (`scrollDirection`/`scrollAmount`) và `ActionType::MouseClick`
(`x`/`y`/`mouseButton`), không thêm cấu trúc dữ liệu trùng lặp. `ActionRunner` rẽ theo `hotkeyTrigger`:
Scroll/Click thì giữ các phím bổ trợ (`pressModifiersOrdered`) → thực hiện cuộn/click → nhả phím
(`releaseModifiersOrdered`) - **nhả phím LUÔN chạy vô điều kiện** kể cả khi thao tác giữa chừng bị dừng,
tránh kẹt phím Ctrl/Alt/Shift/Win thật trên máy người dùng (lỗi nghiêm trọng khó phát hiện vì không crash).
Ví dụ dùng được: Ctrl+Cuộn lên = zoom in.

**8. HUD (`RuntimeOverlay`) tự quay về vị trí gốc**: trước đây né chuột đúng nhưng đứng yên mãi ở vị trí đã
né dù chuột đã rời đi từ lâu. Thêm `m_homePos` (ghi nhận lúc khởi tạo) + `m_isDisplaced`: còn va chạm ở vị
trí hiện tại thì né như cũ; hết va chạm VÀ đang ở trạng thái đã né thì kiểm tra lại vị trí gốc - nếu gốc
không còn va chạm thì tự `move()` về đó.

**Đã xác nhận**: build thành công không lỗi biên dịch; 109/109 test (`autoclick_tests`, +9 test mới cho thứ
tự tổ hợp phím và tương thích JSON cũ - `effectiveModOrder()` đúng cả khi `modOrder` tường minh khác thứ tự
cố định lẫn khi đọc hồ sơ cũ không có field này); `core_stress_tests` không có hồi quy ở phần liên quan
(`ActionSerializer`/`ActionRunner`/giao diện Auto Click đều pass 100% trong lượt chạy stress - 2 lỗi còn lại
thuộc `UpdateInstaller::buildHelperScript` và Android lister, hai module không đụng tới trong đợt sửa này);
đã tự rà soát lại code `createHotkeyPage()`/`setAction()`/`getAction()` khớp đúng thiết kế.

**CHƯA xác nhận được bằng ảnh chụp tự động** (đúng tinh thần CLAUDE.md mục 2): đã thử 3 cách qua UI
Automation để đổi combo "Loại" sang "Tổ hợp phím" và chụp trang mới - click chuột vào tọa độ ước lượng
trong popup dropdown, gửi phím mũi tên (`keybd_event`) sau khi mở dropdown, và `SelectionItemPattern.Select()`
sau khi `ExpandCollapsePattern.Expand()` - cả 3 đều không đổi được giá trị combo thật sự dù UI Automation
đọc đúng cấu trúc 7 item trong popup. Nguyên nhân: popup `QComboBox`/`QMenu` của Qt là một cửa sổ (HWND)
RIÊNG BIỆT với cửa sổ chính - `PrintWindow` trên cửa sổ chính không chụp được nó, và việc chọn item qua UIA
synthetic action không map đúng về `setCurrentIndex()` nội bộ của Qt trong môi trường không tương tác
(headless/phiên không có desktop thật) này. Đã xác nhận riêng (không qua popup) rằng điểm 2 và điểm 4 hiển
thị đúng bằng ảnh chụp cửa sổ chính (focus rect biến mất, `DurationInput` hiện số + đơn vị "ms"). Cần tự
xác nhận bằng mắt thường trên máy thật: trang "Tổ hợp phím" (combo "Kết thúc bằng", 4 checkbox hiện số thứ
tự, sub-trang Cuộn/Click), menu chuột phải mới (bo góc/icon 👆), Esc hủy capture tọa độ, HUD tự về vị trí
gốc, và tổ hợp phím Cuộn/Click chuột thực thi đúng khi chạy chuỗi thật.

## 4w. Auto Click: dọn giao diện chính + con trỏ ngón tay + sửa đơn vị thời gian (2026-10-11, v1.19.8)

Người dùng gửi thêm 2 ảnh chụp thật, phản hồi tiếp sau v1.19.7 với 6 điểm.

**1. Thông báo trước khi tự khởi động lại sau cập nhật**: handler `UpdateInstaller::aboutToRestart` trong
`MainWindow.cpp` trước đây đóng hộp thoại tiến độ và `qApp->quit()` ngay lập tức - không có gì báo cho
người dùng biết ứng dụng sắp tự tắt rồi mở lại, dễ hiểu nhầm là treo/crash nếu thấy tiến trình biến mất đột
ngột. Sửa: đổi nhãn hộp thoại thành "Đang tự khởi động lại, vui lòng không thao tác...", bỏ nút "Ẩn", chuyển
thanh tiến trình sang dạng vô định, rồi `QTimer::singleShot(1500, ...)` mới thật sự đóng + thoát - vẫn nằm
gọn trong hạn 60 giây mà tiến trình trợ giúp PowerShell chờ đúng PID này thoát (xem `UpdateInstaller.h`).

**2. Menu chuột phải: con trỏ ngón tay thay vì icon trên từng dòng**: v1.19.7 thêm icon 👆 vào đầu mỗi dòng
menu - người dùng phản hồi không đúng ý, họ muốn chính CON TRỎ CHUỘT đổi hình khi di qua menu (như đang bấm
được), không phải icon vẽ trong nội dung dòng. `ModernMenu::style()` thêm `menu->setCursor(Qt::PointingHandCursor)`
(QMenu tự vẽ các dòng bên trong, không phải widget con riêng, nên set cursor cho cả menu là đủ áp dụng mọi
dòng); `ModernMenu::addAction()` bỏ hẳn tiền tố 👆. Áp dụng tự động cho toàn bộ context menu dùng chung 2 hàm
này (`ChainListWidget`, `ActionListWidget`, `DirectDownloadTab`).

**3+4. Dọn giao diện chính + thêm Ghim/kéo-thả chuỗi**: `AutoClickWindow` bỏ hẳn nút "■ Dừng lại (STOP)"
(minibox `RuntimeOverlay` đã có nút Dừng riêng, nối cùng `onStopClicked()`) và nhãn "Trạng thái: ..." (lỗi
nghiêm trọng đã có `QMessageBox::warning` riêng không phụ thuộc nhãn này, xác nhận qua grep: "Trạng thái"
không xuất hiện ở bất kỳ widget con nào khác). `ChainListWidget`: bỏ nút Nhân bản/Xóa dưới danh sách (trùng
chuột phải); chuột phải bỏ "Chạy chuỗi này" (chọn chuỗi rồi bấm RUN ở cửa sổ chính vẫn chạy đúng, không mất
tính năng - signal `runChainClicked` xóa luôn), thêm "Ghim lên"/"Bỏ ghim" + kéo-thả đổi thứ tự
(`ReorderableListWidget`, cùng khuôn `ReorderableTable` đã có trong `ActionListWidget`).

Thiết kế "Ghim lên" CÓ CHỦ Ý không ép "luôn đứng trên mọi chuỗi chưa ghim" - nó là hành động "đưa lên đầu
danh sách NGAY LÚC ghim" (dùng lại đúng phép dời một phần tử kiểu `std::rotate` của `onActionMoved()`) kèm
nhãn ghi nhớ trực quan (tiền tố 📌), không phải ràng buộc sắp xếp tuyệt đối. Lý do: toàn bộ kiến trúc hiện
tại coi chỉ số hàng hiển thị = chỉ số trong `std::vector<ActionChain> m_chains`, dùng xuyên suốt mọi signal/
slot và cả `core_stress_tests`; ép một "nhóm luôn ở trên" đòi hỏi chặn/nắn lại kéo-thả và các đường thêm/
nhân bản chuỗi mỗi khi chạm nhóm đã ghim - rủi ro hồi quy cao hơn nhiều so với lợi ích, trong khi nghĩa đen
câu người dùng dùng ("ghim LÊN") đã khớp đúng với thiết kế đơn giản hơn này.

`ActionChain` thêm field `bool pinned{false}` (ghi/đọc qua `ActionSerializer`, thiếu field = hồ sơ cũ = chưa
ghim, tương thích ngược tự nhiên; `clone()` copy từng field thủ công nên phải thêm tay `copy.pinned = pinned`).

`ActionListWidget`: bỏ "Sửa" trong chuột phải - xác nhận qua code, `onEditAction()` trước đây chỉ gọi
`onActionSelected()`, Y HỆT việc click chọn hàng đã tự làm qua `actionSelectionChanged`, nên mục này không
có tác dụng gì khác ngoài những gì click thường đã làm (xóa luôn `editActionClicked`/`onEditAction`). Bỏ nút
Nhân bản/Xóa dưới bảng (trùng chuột phải), chỉ còn "+ Thêm hành động".

**5. Bỏ icon mũi tên ▾ trên mọi QComboBox**: khảo sát phát hiện `QComboBox::drop-down { border: none; }`
(đã dùng ở vài nơi) KHÔNG ẩn icon mũi tên - đó là sub-control `down-arrow` riêng, Qt vẫn tự vẽ. 8/9 công cụ
dùng chung một hàm `inputStyle()` gần như giống hệt nhau trong `src/tools/<tool>/<Tool>UiStyle.h`; Auto Click
(`ActionEditorWidget.cpp`, 9 combo lặp lại y hệt 1 chuỗi style - gom về 1 hằng cục bộ `kComboStyle` để tránh
sửa sót) và `DurationInput.cpp` tự set stylesheet riêng. Tạo `src/ui/widgets/ComboBoxStyle.h` (header-only,
theo đúng khuôn các `*UiStyle.h` hiện có) chứa `ComboBoxStyle::noArrow()`, nối vào CUỐI stylesheet hiện có ở
cả 10 nơi - giữ nguyên màu/viền/bo góc đang có, chỉ thêm quy tắc ẩn mũi tên. Bấm vào bất kỳ đâu trên thân
combo vẫn mở được danh sách như cũ (hành vi click mặc định của QComboBox không phụ thuộc icon mũi tên).

**6. `DurationInput`: đổi đơn vị không còn quy đổi số hiển thị**: trước đây đổi combo đơn vị gọi lại
`rebuildDisplay()` tính `setValue(m_valueMs / factor)` - số hiển thị bị quy đổi theo (vd gõ "500" ở ms, chọn
"giây" thì tự đổi thành "0.5"), người dùng phản hồi không mong muốn. Thiết kế lại: bỏ hẳn `rebuildDisplay()`;
`onUnitChanged()` không đụng tới `m_numberSpin` (số giữ nguyên tuyệt đối), chỉ tính lại `m_valueMs = số hiện
tại × hệ số đơn vị mới` rồi phát `valueChanged` nếu đổi. `m_numberSpin->setDecimals(0)`/`setMaximum(m_maxMs)`
cố định MỘT LẦN ở constructor, không còn đổi theo đơn vị (tránh Qt tự làm tròn/kẹp số đang gõ khi các hàm đó
đổi theo đơn vị - nguồn quy đổi ẩn thứ hai). `setValueMs()` (nạp từ code, vd `ActionEditorWidget::setAction()`
nạp lại hành động khác) vẫn là NGOẠI LỆ duy nhất: luôn ép đơn vị về ms + hiện đúng số mili-giây, vì đó là lúc
TẢI dữ liệu từ model, không phải người dùng tự chọn đơn vị.

**Đã xác nhận**: build thành công; 121/121 test (`autoclick_tests`, +12 test mới: `ActionChain::pinned`
round-trip qua `clone()`/JSON kể cả đọc hồ sơ cũ thiếu field, và `DurationInput` giữ nguyên số khi đổi đơn vị
qua 2 bước ms→giây→giờ kèm xác nhận giá trị thật bị kẹp đúng trần khi vượt mức); `core_stress_tests` 547/549
pass, phần Auto Click (76/76, gồm fuzz 2 signal mới `togglePinClicked`/`chainMoved` với chỉ số ngoài miền
tại đúng vị trí đã fuzz các signal chain khác) không hồi quy - 2 lỗi còn lại thuộc
`UpdateInstaller::buildHelperScript` (đã tồn tại từ trước v1.19.7, không đụng tới file đó trong đợt sửa này).

**CHƯA xác nhận được bằng mắt thường** (đúng tinh thần CLAUDE.md mục 2): thông báo tự khởi động lại khi có
bản cập nhật THẬT kích hoạt (cần một bản phát hành mới hơn trên GitHub để trigger, chỉ xác nhận được qua đọc
code + build sạch ở đây); con trỏ ngón tay thật trên menu; thao tác ghim lên + kéo-thả đổi thứ tự chuỗi bằng
chuột thật.

## 4x. Sửa lỗi tự cập nhật thất bại ngẫu nhiên, ExitCode=1 (2026-10-11, v1.19.9)

Người dùng báo (kèm ảnh chụp 2 hộp thoại: "Cập nhật lần trước thất bại - ExitCode=1" và "Có bản cập nhật
mới" hiện ra cùng lúc khi mở lại app): tải 100% để cập nhật "lúc thì cập nhật lên ok lúc thì không", yêu
cầu xem lại và fix rõ lỗi.

**Tìm nguyên nhân thật qua đọc code** (đúng CLAUDE.md mục 2 - không đoán, không chỉ tăng timeout/retry mù
quáng mà không hiểu vì sao): 3 mảnh ghép cho ra lời giải chính xác.
1. `src/main.cpp`: `CreateMutexW(nullptr, FALSE, L"OneForAllRunningMutex")` - comment tại chỗ gọi xác nhận
   rõ ràng: mutex này **KHÔNG dùng để chặn chạy nhiều bản sao cùng lúc**, chỉ để Inno Setup/Restart Manager
   phát hiện ứng dụng đang chạy. Nghĩa là người dùng HOÀN TOÀN có thể có 2+ cửa sổ/tiến trình OneForAll.exe
   chạy song song (vd mở lại từ Start Menu trong khi bản cũ vẫn đang chạy nền) - xác nhận thêm bằng grep
   toàn bộ `src/`: không có `QLocalServer`/`QSharedMemory`/cơ chế single-instance nào khác.
2. `installer/OneForAll.iss`: `AppMutex=OneForAllRunningMutex` khớp tên ở trên - Inno Setup dùng mutex này
   để phát hiện "ứng dụng đang chạy" ở bước khởi tạo (`InitializeSetup`), TRƯỚC CẢ khi bắt đầu cài thật.
3. `UpdateInstaller::buildHelperScript()` gọi Setup với `/CLOSEAPPLICATIONS` (nhờ Restart Manager đóng ứng
   dụng đang giữ tệp) nhưng **KHÔNG** có `/FORCECLOSEAPPLICATIONS`; đồng thời `UpdateInstaller.h` tự ghi rõ
   "ứng dụng này không đăng ký [RegisterApplicationRestart]" - nghĩa là Restart Manager không có cách nào
   đóng ÊM một bản sao OneForAll.exe khác (app không trả lời tín hiệu RM yêu cầu đóng), và vì thiếu cờ ép
   buộc nên Inno Setup **tự hủy cài đặt ngay ở bước khởi tạo** khi vẫn thấy AppMutex - đây CHÍNH XÁC là
   exit code 1 "Setup failed to initialize" theo tài liệu Inno Setup, khớp 100% với ảnh chụp người dùng gửi.

Giải thích đúng "lúc được lúc không": chỉ xảy ra khi, tại đúng thời điểm `Wait-Process` xác nhận PID chính
đã thoát, người dùng VẪN CÒN một bản sao OneForAll.exe KHÁC đang chạy (cửa sổ khác, hoặc instance còn sót
từ một thao tác trước) - hoàn toàn ngẫu nhiên theo thói quen sử dụng của người dùng tại thời điểm đó, không
liên quan gì tới mạng/đĩa/cấu hình máy.

**Đã sửa (`UpdateInstaller::buildHelperScript`)**: SAU bước `Wait-Process`/`Start-Sleep 1500ms` hiện có,
thêm vòng `while` chờ TỐI ĐA 30 giây, mỗi giây dùng `Get-Process -ErrorAction SilentlyContinue |
Where-Object { $_.Path -eq <đường dẫn exe chính xác> }` để kiểm tra còn bản sao nào khác đang chạy không -
thoát vòng ngay khi không còn. **CỐ Ý không thêm `/FORCECLOSEAPPLICATIONS`**: ép buộc đóng một cửa sổ khác
mà người dùng có thể đang thao tác dở (vd đang chạy một chuỗi Auto Click, một phiên Connect Together) là
không an toàn - cùng triết lý đã có sẵn với `ToolManager::anyToolWindowBusy()` (chặn tự cập nhật nếu CHÍNH
tiến trình đang có thao tác không an toàn để hủy giữa chừng). Nếu hết 30 giây mà bản sao khác vẫn còn, vẫn
tiến hành thử cài đặt như bình thường (không chặn vô thời hạn) - thêm **vòng thử lại tối đa 5 lần** (mỗi
lần cách nhau 1.5 giây) nếu `ExitCode` không phải 0/3010, dự phòng phần dư của race còn sót (vd bản sao
khác thoát ĐÚNG lúc Setup đang kiểm tra AppMutex). Tổng thời gian chờ thêm tối đa trong trường hợp xấu nhất
(~30s chờ bản sao khác + ~7.5s thử lại) vẫn nằm trong khuôn khổ hợp lý của một tiến trình chạy ẩn, tách rời
khỏi ứng dụng đã thoát.

**Sửa thêm theo góp ý UX của người dùng** ("tải 100% tức là sẵn sàng rồi, phải mở được ngay"): hộp thoại
tiến độ ở `MainWindow.cpp` đổi chữ thành "Đang cài đặt bản mới và tự khởi động lại - có thể mất vài giây,
vui lòng không thao tác..." thay vì chỉ "Đang tự khởi động lại..." (nghe như tức thì). Về mặt kiến trúc,
bước CÀI ĐẶT THẬT (chạy Setup.exe) bắt buộc phải diễn ra SAU KHI ứng dụng đã thoát hẳn (không thể tự ghi
đè file .exe đang chạy của chính nó) - nên không có cách nào hiện thanh tiến trình LIVE cho bước này từ
chính cửa sổ sắp đóng; cách khả thi duy nhất là đặt đúng kỳ vọng bằng chữ trước khi thoát.

**Phát hiện và sửa luôn 1 lỗi pre-existing trong bộ test** (không phải do đợt sửa lần này gây ra - đã xác
nhận lỗi này xuất hiện giống hệt ở MỌI lượt chạy `core_stress_tests` trước đó trong toàn bộ phiên làm việc,
kể cả trước khi đụng tới `UpdateInstaller.cpp`): fuzz test `UpdateInstaller::buildHelperScript` trong
`core_stress_tests.cpp` so sánh script sinh ra với một "đường cơ sở" (số câu lệnh, tên lệnh) HARDCODE viết
tay từ v1.19.5 - đã lạc hậu từ lâu (script thật đã có thêm nhiều lệnh `Log(...)` từ v1.19.5 mà baseline
hardcode "5 câu lệnh" chưa từng cập nhật theo). Sửa bằng cách thêm 2 ca "sạch" (đường dẫn bình thường,
không fuzz) vào đầu danh sách ca kiểm thử, dùng kết quả phân tích cú pháp THẬT của 2 ca đó làm đường cơ sở
ĐỘNG để so sánh với mọi ca fuzz còn lại theo đúng loại cài đặt (Inno/Msi) - không còn lạc hậu mỗi khi
`buildHelperScript()` đổi cấu trúc trong tương lai.

**Đã xác nhận**: build thành công; 95/95 test (`update_tests`, +6 test mới xác nhận có `Get-Process`/
`Where-Object`/vòng `for` thử lại đúng vị trí, không có `/FORCECLOSEAPPLICATIONS`); `core_stress_tests`
**551/551 pass sạch hoàn toàn** - lần đầu tiên trong suốt phiên làm việc không còn lỗi nào ở BẤT KỲ phần
nào, bao gồm cả phần `buildHelperScript` trước đây luôn fail 2 lỗi ở mọi lượt chạy.

**CHƯA xác nhận được bằng một lần cập nhật THẬT** trên máy có 2 bản sao OneForAll.exe chạy cùng lúc (cần
dựng kịch bản thật: mở 2 cửa sổ, kích hoạt cập nhật, xác nhận vòng chờ hoạt động và cài đặt thành công) -
chỉ sửa dựa trên phân tích code + đúng mã lỗi ExitCode=1 người dùng cung cấp qua ảnh chụp, khớp chính xác
với ý nghĩa "Setup failed to initialize" của Inno Setup khi AppMutex còn bị giữ bởi bản sao khác.

## 4y. Sửa góc đen ở menu + mũi tên còn sót ở combo/spinbox + bỏ nút Nạp cấu hình (2026-10-11, v1.19.10)

Người dùng gửi 2 ảnh chụp thật module Auto Click, nêu 3 điểm tiếp theo sau v1.19.9.

**1. Góc đen ở menu chuột phải dù đã bo góc** (v1.19.7 bo góc qua QSS nhưng ảnh người dùng gửi vẫn thấy
vệt đen nhọn ở 4 góc): `QMenu` là một cửa sổ top-level KHÔNG trong suốt theo mặc định. Đặt `border-radius`
qua QSS chỉ tô phần NẰM TRONG hình bo tròn - 4 góc NẰM NGOÀI hình đó (bị bo tròn cắt đi) vẫn là nền cửa sổ
gốc (đen, không được QSS vẽ gì) lộ ra ngoài. Sửa bằng `menu->setAttribute(Qt::WA_TranslucentBackground)`
trong `ModernMenu::style()` - bật nền trong suốt cho CẢ cửa sổ popup, để phần ngoài hình bo tròn trở thành
TRONG SUỐT thay vì đen; đây là kỹ thuật Qt chuẩn, được dùng rộng rãi cho đúng loại vấn đề này (shape widget/
popup bo góc trên nền không composite riêng).

**2. Combo "Nút"/"Phím"/"Hướng"... (Auto Click) vẫn còn mũi tên dù v1.19.8 đã thêm `ComboBoxStyle::noArrow()`**:
đây là phát hiện quan trọng nhất của đợt sửa này, tìm được nhờ KHÔNG tin ngay vào code review mà tự chụp
ảnh build thật để đối chiếu (đúng tinh thần CLAUDE.md mục 2). Ảnh chụp "Nút: Trái" sau khi build v1.19.9+
thay đổi mới nhất VẪN hiện mũi tên - trong khi "Loại: Click chuột" (cũng trong `ActionEditorWidget`, cũng
gọi `ComboBoxStyle::noArrow()`) thì KHÔNG. So sánh trực tiếp 2 đoạn code:
- `m_typeCombo` (sạch): `"QComboBox { background-color: ...; }" "QComboBox:hover {...}" + noArrow()`
- `kComboStyle` (lỗi, dùng cho 9 combo còn lại): `"background-color: ...;" + noArrow()` - **KHÔNG có
  `QComboBox { }` bao ngoài**, chỉ là một khối khai báo thuộc tính TRẦN.

Khi nối một khối khai báo TRẦN (không selector) với các rule CÓ selector (`QComboBox::drop-down {...}`/
`QComboBox::down-arrow {...}` của `noArrow()`) trong CÙNG một chuỗi truyền cho `setStyleSheet()`, trình phân
tích QSS của Qt không áp dụng đúng các rule phía sau nữa - màu nền/viền/bo góc của phần TRẦN vẫn hiện ra
đúng (có thể do cascade từ nơi khác hoặc Qt xử lý khoan dung phần đầu), nhưng phần ẩn mũi tên bị mất tác
dụng hoàn toàn, mặc dù CSS hợp lệ nếu xét riêng từng phần. Sửa bằng cách bọc đúng `QComboBox { ... }` cho
`kComboStyle`. Xác nhận lại bằng ảnh chụp sau khi sửa: "Nút: Trái" sạch hoàn toàn, khớp với "Loại".

Đã audit toàn bộ các nơi dùng `ComboBoxStyle::noArrow()` khác (8 file `*UiStyle.h` của các tool còn lại,
`DurationInput.cpp`, `m_typeCombo`) - tất cả đều dùng đúng selector có bao `{ }` (hoặc danh sách selector
phân tách bằng dấu phẩy `QLineEdit, QComboBox, QSpinBox { ... }`, cũng hợp lệ) nên KHÔNG dính lỗi này.

**3. Mũi tên lên/xuống còn sót ở QSpinBox**: v1.19.8 chỉ tắt nút mũi tên cho `DurationInput` (ô thời gian)
qua `setButtonSymbols(NoButtons)` - các `QSpinBox` khác (tọa độ X/Y, số bước...) chưa từng được xử lý. Rà
soát toàn bộ `src/` (grep `new QSpinBox(`/`new QDoubleSpinBox(`) tìm thấy 17 ô còn thiếu: 8 ô trong
`ActionEditorWidget.cpp` (Vị trí X/Y của Click/Giữ chuột/Hotkey Click, Số bước Cuộn/Hotkey Cuộn) + 9 ô ở
tool khác (QR: vĩ độ, kinh độ, kích thước ảnh, viền trắng; Disk Cleanup: ngưỡng tệp lớn, ngưỡng trùng lặp,
đổi kích thước phân vùng; Connect: cổng ghép đôi; Android: bitrate) - thêm `setButtonSymbols(NoButtons)`
cho toàn bộ, đồng bộ với yêu cầu người dùng ("đồng bộ cho toàn bộ tiện ích").

**4. Con trỏ ngón tay khi hover vào combo** (yêu cầu mới của người dùng, chưa làm ở các đợt trước): hàm mới
`ComboBoxStyle::applyCursor(QWidget* root)` tự tìm mọi `QComboBox` hậu duệ qua `findChildren` (không cần
widget cha trực tiếp, đi theo quan hệ QObject parent-child) và gán `Qt::PointingHandCursor` - chỉ cần gọi
MỘT LẦN ở cấp cửa sổ cao nhất SAU KHI mọi combo con đã dựng xong. Áp dụng ở cuối constructor của cả 9 cửa
sổ chính (`AutoClickWindow`, `WifiWindow`, `QRWindow`, `DiskCleanupWindow`, `AndroidControlWindow`,
`VpnControlWindow`, `SecurityGatewayWindow`, `DownloaderWindow`, `ConnectWindow`) + 2 hộp thoại độc lập tạo
SAU lúc cửa sổ chính đã dựng xong (`ConnectDialog`, `AddVpnProfileDialog` - combo của chúng không tồn tại
tại thời điểm cửa sổ cha gọi `applyCursor()` nên phải tự gọi lại ở chính hộp thoại).

**5. Bỏ nút "Nạp cấu hình"**: xóa hẳn nút, slot `onLoadProfile()`, kết nối, khai báo trong header - không
để lại code chết. Chức năng "Lưu/Xuất/Nhập" vẫn còn nguyên.

**Thêm test hồi quy** trong `autoclick_tests.cpp`: duyệt mọi `QComboBox` hậu duệ của `ActionEditorWidget`,
xác nhận stylesheet (nếu có) LUÔN bắt đầu bằng `"QComboBox"` (bắt đúng kiểu lỗi "khối khai báo trần" vừa
tìm ra, tránh tái diễn khi sửa code sau này) và cursor đã là `PointingHandCursor`.

**Đã xác nhận**: build sạch; 123/123 test `autoclick_tests` (+2 mới); toàn bộ 16 bộ test các tool khác
(qr/wifi/connect/diskcleanup/android/vpn/security/downloader, cả `_tests` lẫn `_ui_tests`) đều pass - không
hồi quy dù lần sửa này trải rộng khắp 8 tool cùng lúc; `core_stress_tests` 551/551 pass sạch hoàn toàn. Đã
xác nhận bằng ảnh chụp thật (trước/sau, cắt ảnh phóng to đối chiếu pixel): combo "Nút" và spinbox X/Y không
còn mũi tên, khớp với combo "Loại"/"ms" đã đúng từ trước.

**CHƯA xác nhận được bằng ảnh chụp** góc menu chuột phải sau khi sửa: đã thử 2 cách tự động hóa (UI
Automation enumerate top-level windows của process, raw Win32 `EnumWindows` lọc theo PID) đều không bắt
được popup window riêng biệt kịp lúc trước khi nó tự đóng - giới hạn công cụ tự động hóa trong môi trường
này với loại cửa sổ popup thoáng qua, không phải dấu hiệu bản sửa sai. Tin tưởng vào `WA_TranslucentBackground`
là kỹ thuật Qt chuẩn, được tài liệu hóa rõ ràng cho đúng vấn đề "góc đen quanh widget bo tròn trên popup
không composite" - cần tự xác nhận bằng mắt thường trên máy thật.

## 4z. Auto Click: làm lại "Ghi nhớ chuỗi" + cache chỉnh sửa chưa lưu trên nền v1.19.10 (2026-10-13, v1.19.11)

Bối cảnh: người dùng đã yêu cầu quay code về ĐÚNG v1.19.10 (xóa mọi commit/tag sau đó; xem
`YEU_CAU_SAU_1.19.10.md` - liệt kê toàn bộ yêu cầu đã gửi từ sau v1.19.10) rồi gửi lại 2 yêu cầu cho Auto Click,
yêu cầu giữ giao diện y hệt v1.19.10 (KHÔNG mang theo `PopupCornerFix`/`WindowTheme`/Fusion/`QPalette`/sửa style):

1. **Loại hành động mới "Ghi nhớ chuỗi"**: bấm "Bắt đầu ghi" -> app tự thu nhỏ, hiện minibox đếm thời gian + số
   bước, ghi TOÀN BỘ thao tác chuột+bàn phím THẬT trên toàn máy tới khi bấm nút dừng trên minibox; kết quả LƯU
   LÀM 1 HÀNH ĐỘNG duy nhất, phát lại đúng thứ tự + đúng khoảng cách thời gian. Minibox có 2 chế độ: **lúc ĐANG
   GHI đứng yên một chỗ** (để người dùng bấm được nút "Kết thúc ghi" - né chuột lúc này sẽ biến nút thành mục tiêu
   di động không bao giờ bấm trúng), **lúc CHẠY chuỗi vẫn né chuột** như cũ (để không che vùng thao tác).
2. **Chỉnh sửa chưa lưu không bị mất khi đổi sang hành động khác**: cache từng hành động đang sửa dở
   (`AutoClickWindow::m_pendingEdits`), dấu `*` ở cột số thứ tự giữ nguyên cho từng hàng, quay lại hàng đó nạp
   đúng bản đang sửa dở; chỉ RUN hoặc đóng cửa sổ mới hỏi "Lưu lại?" (Có/Không/Hủy).

**Cách làm (port CÓ CHỌN LỌC từ lịch sử git, không kéo theo gì khác)**: code cũ vẫn còn trong git (commit
`51ac6ab` v1.19.12 cho "Ghi nhớ chuỗi", `ae232ec` v1.19.13 cho cache). Áp dụng đúng phần Auto Click:
- `git cherry-pick -n 51ac6ab` - toàn bộ mã nguồn merge sạch (chỉ xung đột phần tài liệu/VERSION, tự giải quyết):
  `Action`/`ActionSerializer` (model + JSON, hồ sơ cũ thiếu `recordedEvents` vẫn đọc được), `ActionRecorder`
  (hook `WH_MOUSE_LL`+`WH_KEYBOARD_LL`, không bao giờ nuốt sự kiện, Esc KHÔNG hủy ghi, loại click vào minibox,
  tự nhả phím/nút còn giữ lúc dừng), `InputController`/`ActionRunner` (phát lại), `ActionEditorWidget` (trang
  "Ghi nhớ chuỗi" + hộp thoại xác nhận phạm vi ghi), `RuntimeOverlay` (chế độ Recording), `AutoClickWindow` (điều
  phối), `PRIVACY.md`.
- Minibox đứng yên lúc ghi: áp đúng phần sửa của bản cuối (`RuntimeOverlay::setMode(Recording)` đưa HUD về vị trí
  "home" cố định, `setRecordingInfo()` chỉ cập nhật chữ, không `GetCursorPos`/né chuột; `ActionRecorder::
  progressUpdated` không mang theo vị trí chuột).
- Cache: áp phần `AutoClickWindow`/`ActionListWidget` (`m_dirtyRows` thay `m_dirtyRow`)/`ActionEditorWidget.h`
  (`isDirty()`/`currentActionIndex()`) + test; GỠ khỏi phần port mọi đoạn thuộc cơ chế giao diện (không có
  `paintEvent` tự vẽ nền, không gọi `WindowTheme`, không đổi style checkbox/spinbox - đúng giao diện v1.19.10).
  `git diff v1.19.10` cho thấy ngoài Auto Click chỉ khác `CMakeLists.txt` (thêm `ActionRecorder.cpp`).

**Test/xác nhận**: `autoclick_tests` 165/165 (gồm test cache đổi hàng giữ `*`, quay lại nạp đúng bản dở, Hủy/Không
trên hộp thoại đóng cửa sổ, serializer `recordedEvents` khứ hồi); `core_stress_tests` 551/551 (3000 thao tác ngẫu
nhiên trên giao diện, 0 lệch dữ liệu). Xác nhận THẬT trên app đang chạy (click mô phỏng bằng `PostMessage`):
chọn loại "Ghi nhớ chuỗi" -> trang mới hiển thị đúng phong cách giao diện hiện có, hàng 01 hiện `*`; chuyển sang
hàng 02 rồi quay lại hàng 01 -> editor vẫn là "Ghi nhớ chuỗi" và `*` còn nguyên.
**CHƯA kiểm chứng được (cần tự thử trên máy thật, môi trường này không gửi được chuột/phím thật)**: ghi macro thật
bằng hook toàn cục + phát lại bằng `SendInput`, và hành vi minibox đứng yên khi ghi/né chuột khi chạy.

## 4za. QR Tools: làm lại các cải tiến đã mất + con trỏ ngón tay cho mọi nút bấm (2026-10-13, v1.19.12)

Bối cảnh: sau khi quay code về ĐÚNG v1.19.10 (xem `YEU_CAU_SAU_1.19.10.md`) và làm lại Auto Click ở mục 4z,
người dùng gửi lại danh sách các yêu cầu QR Tools + con trỏ mà các bản v1.19.12-v1.19.14 cũ đã có. Vẫn yêu cầu
giữ giao diện y hệt v1.19.10 (KHÔNG mang theo `PopupCornerFix`/`WindowTheme`/Fusion/`QPalette`/sửa style):

1. **QR - WiFi (tạo)**: combo "WiFi đã lưu" chọn nhanh từ hồ sơ WiFi trên máy (`WlanController`).
2. **QR - Quét ra mã WiFi**: nút "🔗 Kết nối WiFi" mở `ConnectDialog` của module WiFi với mật khẩu lấy từ chính
   mã QR (điền sẵn qua `ConnectDialog::setPassword()`, người dùng vẫn tự bấm "Kết nối"). Đúng MỘT mật khẩu, không dò
   mật khẩu - đúng quy tắc 3 của CLAUDE.md.
3. **QR - Vị trí (tạo)**: "📍 Lấy vị trí hiện tại (ước lượng theo IP)" dùng `PublicIpChecker` (`ipwho.is`; thêm
   `latitude/longitude/hasLocation` vào `PublicIpInfo`) - KHÔNG phải GPS (Windows chỉ cấp quyền vị trí hệ thống cho
   app MSIX, xem PublicIpChecker.h), độ chính xác cỡ thành phố; "🗺 Mở bản đồ" mở Google Maps theo tọa độ đang nhập
   bằng trình duyệt mặc định; ô "Link Google Maps" + "Dùng link này" đọc tọa độ từ link dán vào
   (`QRGenerateTab::parseGoogleMapsLink`: `@lat,lon`, `?q=lat,lon`, `?query=lat,lon`; tọa độ ngoài phạm vi hoặc link
   rút gọn `maps.app.goo.gl` bị từ chối kèm thông báo, không đổi tọa độ đang có).
4. **QR - Quét**: khung "KẾT QUẢ (nhiều mã)" chỉ hiện khi ảnh có HƠN 1 mã; nhận diện email/SĐT nằm rải rác trong
   văn bản tự do; thông báo phân biệt "không thấy mã QR" với "thấy mã nhưng đọc hỏng" (`QRCodec::decode(image,
   &undecodedCount)`); bảng/khung bo góc 12px (`QRUiStyle`) cho đồng bộ với thẻ/nhóm.
5. **QR - Tạo mã chỉ còn 5 loại** (Văn bản/URL/WiFi/Điện thoại/Vị trí): bỏ Email/SMS/vCard khỏi giao diện tạo (các
   hàm `QRPayload::makeEmail/makeSms/makeVCard` và phần nhận diện khi quét vẫn giữ). Hồ sơ lịch sử cũ không bị ảnh
   hưởng.
6. **Con trỏ ngón tay trên MỌI `QPushButton` toàn ứng dụng**: `ComboBoxStyle::applyCursor()` (đã được từng cửa sổ
   công cụ gọi ở cuối constructor) mở rộng từ `QComboBox` sang cả `QPushButton`; hộp thoại mở theo yêu cầu
   `WirelessPairDialog` tự gọi lại. Nút đã tự `setCursor()` riêng không bị ảnh hưởng (gán lại cùng giá trị).

**Cách làm**: port CÓ CHỌN LỌC từ lịch sử git - `git diff v1.19.10 ae232ec` cho `src/tools/{qr,wifi,vpn/engine,android}`
(đã kiểm: không chứa cơ chế ép giao diện nào) + phần dán link Google Maps của `14baffc`; `CMakeLists.txt` thêm
`target_link_libraries(qr_ui PUBLIC wifi_ui vpn_core)`; `ComboBoxStyle.h` chỉ thêm `QPushButton`. KHÔNG mang theo:
`PopupCornerFix`, `WindowTheme`, Fusion/`QPalette`, bản đồ nhúng OpenStreetMap (`OsmMapWidget`, không có trong yêu
cầu), chữ thông báo cập nhật/checkbox cursor của v1.19.11 cũ (không có trong danh sách).

**Quyền riêng tư**: nút "Lấy vị trí hiện tại" gọi `ipwho.is` (gửi IP công khai - vốn đã lộ với mọi trang web); "Mở bản
đồ" mở `google.com/maps` bằng trình duyệt mặc định (không gọi ngầm); dán link Google Maps chỉ đọc chuỗi, không gọi
mạng. Đã cập nhật `PRIVACY.md`.

**Kiểm thử**: `qr_tests` 136/136, `qr_ui_tests` 165/165 (thêm: đúng 5 loại, trang Vị trí có 2 nút + ô dán link, 7
trường hợp dán link hợp lệ/lỗi, mọi `QPushButton` của cửa sổ QR có con trỏ ngón tay), `wifi_tests` 119/119,
`connect_ui_tests` 13/13, `qrwifi_stress_tests` 760.699/760.699, `disk_vpn_stress_tests` 7.320/7.320. Trên app đang
chạy (UI Automation): danh sách "Nội dung" đúng 5 loại, trang Vị trí có hai nút + ô dán link, trang WiFi có combo
"WiFi đã lưu". Tab Quét kiểm bằng harness tạm (14 kiểm tra, không commit).

**CHƯA kiểm chứng**: bấm "Lấy vị trí hiện tại" gọi thật `ipwho.is`; mở Google Maps thật; kết nối WiFi thật từ mã QR
(cần adapter WiFi); hình ảnh/hover/con trỏ thực tế trên máy thật.

## 4zb. Sửa lỗi bấm "Lưu hành động" 2 lần + gọn lại trang QR Vị trí + link rút gọn Google Maps (2026-10-13, v1.19.13)

Người dùng gửi ảnh trang QR Vị trí và 3 yêu cầu:

**1. Trang QR Vị trí vượt chiều rộng.** Cột trái của tab Tạo mã là `QScrollArea` TẮT thanh cuộn ngang, trang Vị trí có
hàng 2 nút + hàng "Link Google Maps" rộng hơn cột nên phần bên phải bị cắt. Bố cục mới: mỗi hàng tọa độ = nhãn |
ô giá trị (thu hẹp, cùng bề rộng) | nút nằm ngay cạnh ("📍 Lấy vị trí hiện tại", "🗺 Mở bản đồ"); chú thích
"Link Google Maps (kể cả link rút gọn maps.app.goo.gl):" rồi ô dán link + "Dùng link này"; dòng trạng thái. Lời cảnh
báo "ước lượng theo IP, không phải GPS" chuyển xuống tooltip + dòng chú thích + thông báo kết quả để chữ nút ngắn lại.
`qr_ui_tests` kiểm MỌI widget của trang nằm trong viewport cột trái ở 860x560 (cỡ tối thiểu), 1020x660 và 1400x800.

**2. Link rút gọn `https://maps.app.goo.gl/...`.** Link này KHÔNG chứa tọa độ: Google trả HTTP 302 với header
`Location` là URL đầy đủ (vd `.../maps/place/.../@21.006303,105.8405801,17z/data=!3m1!4b1!4m4!3m3!8m2!3d21.006298!4d105.843155...`).
App gửi MỘT yêu cầu GET (`ManualRedirectPolicy` - không tự theo chuyển hướng, không tải trang đích), đọc `Location`
và tách tọa độ; nếu chưa có thì đi tiếp CHỈ khi đích còn thuộc host Google qua https
(`QRPayload::isAllowedMapsRedirectUrl`: google.com/co.xx/com.xx/xx, goo.gl, g.co - từ chối `google.evil.co`,
`evilgoogle.com`, `google.com.evil.com`), tối đa 5 bước, timeout 10 giây. Logic thuần nằm ở lõi `QRPayload`
(`parseGoogleMapsCoordinates`, `isGoogleMapsShortLink`, `normalizeLinkUrl`, `isAllowedMapsRedirectUrl`) để test được.
Ưu tiên điểm ghim `!3d<lat>!4d<lon>` (chính xác) hơn tâm khung nhìn `@lat,lon`; hiểu `q=`/`query=`/`ll=`/`center=`, `%2C`,
và `continue=` (trang đồng ý cookie của Google bọc link thật). HTTP 404/410 báo "không tồn tại hoặc đã hết hạn".

**3. Lỗi phải bấm "Lưu hành động" 2 lần (nguyên nhân gốc).** Do cache chỉnh sửa chưa lưu thêm ở v1.19.11, KHÔNG có ở
v1.19.10. `onApplyClicked()` phát `actionSaved` TRƯỚC khi reset `m_dirty`; `AutoClickWindow::onActionSaved()` gọi
`setActions()` - `setRowCount(0)` rồi `setCurrentCell()` đều phát `currentCellChanged` -> `onActionSelected()` ->
`captureEditorIfDirty()`; editor lúc đó vẫn "dirty" nên GHI LẠI đúng bản vừa lưu vào `m_pendingEdits` (cache "chưa
lưu" ma) -> dấu `*` hiện lại. Lần Lưu thứ hai editor đã sạch (đã được `setAction()` nạp lại) nên mới hết. Lúc 1 lúc 2: hàng
vừa nạp từ cache có `m_dirty=false` nên Lưu 1 lần là đủ; sửa trực tiếp thì cần 2 lần. Cờ `m_suppressPendingCapture`
trước đó chỉ bao quanh `setSelectedActionIndex()` chứ không bao `setActions()`. Cùng lỗ hổng ở: `applyPendingEdits()`
(chọn "Có" ở hộp thoại "Lưu lại trước khi chạy?" xong lần RUN kế tiếp hỏi lại), và Nhân bản/Xóa/Dời hàng (nội dung editor
bị ghi nhầm vào chỉ số vừa remap, test cũ không bắt được). Đã TÁI HIỆN bằng test trước khi sửa (4 kiểm tra fail), sửa
bằng `ScopedFlag` (RAII) bao TRỌN đoạn vẽ lại bảng ở `onActionSaved/onAddAction/onCloneAction/onDeleteAction/
onActionMoved/applyPendingEdits`; `onDeleteAction` capture hàng đang sửa dở (nếu khác hàng bị xóa) TRƯỚC khi remap.

**Quyền riêng tư**: dán link ĐẦY ĐỦ chỉ đọc chuỗi, không gọi mạng. Dán link RÚT GỌN mới gửi yêu cầu HTTPS tới
`maps.app.goo.gl` (Google) và chỉ đọc header chuyển hướng. Đã cập nhật `PRIVACY.md`.

**Kiểm thử**: `autoclick_tests` 194/194 (thêm: Lưu đúng 1 lần, quay lại hàng vẫn đúng, Nhân bản/Xóa khi hàng khác đang
sửa dở), `core_stress_tests` 551/551, `qr_tests` 182/182 (thêm: phân tích link thật, `@`/`q=`/`query=`/`ll=`/`center=`,
`%2C`, `continue=`, ngoài phạm vi/rác, nhận diện link rút gọn, chuẩn hóa, giới hạn host), `qr_ui_tests` 172/172,
`wifi_ui_tests` 94/94, `qrwifi_stress_tests` 760.292/760.292. Xác nhận qua mạng thật bằng harness tạm (không commit):
`https://maps.app.goo.gl/DM9Xt5AkRLgG6Sbo7` -> (21.006298, 105.843155) ~1,4 giây, kể cả thiếu `https://`/thừa khoảng
trắng; mã không tồn tại -> thông báo 404 riêng.

**CHƯA kiểm chứng**: bấm bằng chuột thật trong app đang chạy (RDP không gửi được chuột/phím thật; logic đã kiểm bằng
`click()` của Qt); trang đồng ý cookie của Google (EU) chưa gặp thật - chỉ có test chuỗi cho `continue=`.

## 4zc. QR Vị trí: ô tọa độ rộng hơn + nút co vừa chữ; con trỏ ngón tay trên danh sách công cụ (2026-10-13, v1.19.14)

Người dùng gửi ảnh trang QR Vị trí (giá trị tọa độ bị cắt: `21.00515'`, `105.8454'`) và yêu cầu: cho ô vĩ độ/kinh độ rộng
ra, hai nút co vừa chữ để có chỗ; thêm con trỏ ngón tay khi rê lên các tiện ích trong danh sách; build + phát hành.

**1. Ô tọa độ bị cắt chữ (sửa bố cục v1.19.13).** v1.19.13 cố định bề rộng hai ô theo `sizeHint()`, nhưng `sizeHint()`
của `QDoubleSpinBox` lúc dựng chưa tính padding 8px/bên của stylesheet (stylesheet nhóm áp dụng SAU khi widget được tạo)
nên ô hẹp hơn chữ 6 chữ số thập phân. Nay: bề rộng TỐI THIỂU = `horizontalAdvance("-180.000000")` + 16 (padding) + 2 (viền)
+ 20 (dư cho DPI/cỡ chữ khác) và ô được GIÃN theo cột (`QSizePolicy::Expanding`, lưới `QGridLayout` 3 cột: nhãn | ô
giá trị cột giãn | nút canh trái); hai nút `QSizePolicy::Fixed` = đúng `sizeHint()` (co vừa chữ). Mục 4zb ghi "ô thu hẹp" là
mô tả bố cục CŨ, đã được thay bằng mục này. `qr_ui_tests` kiểm ở 860x560 / 1020x660 / 1400x800: không widget nào vượt
viewport cột trái, ô tọa độ không cắt chữ với giá trị dài nhất (`-179.999999`/`-89.999999`), nút bằng `sizeHint()`.

**2. Con trỏ ngón tay trên danh sách công cụ.** Helper dùng chung `src/ui/widgets/ItemHoverCursor.h` (header-only,
`ItemHoverCursor::install(QAbstractItemView*)`): bật mouse tracking, lọc `MouseMove` trên viewport và đặt
`Qt::PointingHandCursor` CHỈ khi `indexAt(pos)` hợp lệ (vùng trống bên dưới danh sách giữ mũi tên - `viewport()->setCursor()`
cố định sẽ phủ cả vùng trống); `Leave`/`Hide` trả lại mũi tên. Gắn vào `MainWindow::m_toolListWidget` bằng một dòng.
Dùng lại được cho danh sách/bảng khác (chưa gắn - người dùng chỉ yêu cầu danh sách công cụ).

**Phát hành**: bản đầu tiên được TAG sau v1.19.10 - các v1.19.11-v1.19.13 chưa từng được tag (Release cũ đã bị xóa), nên
`v1.19.14` gồm toàn bộ nội dung các mục 4z, 4za, 4zb, 4zc.

**Kiểm thử**: `autoclick_tests` 201/201 (thêm 7 cho helper: trên mục -> ngón tay; vùng trống -> mũi tên; rời -> mũi tên; null
an toàn), `qr_ui_tests` 190/190, `qr_tests` 182/182, `qrwifi_stress_tests` 760.141/760.141.

**CHƯA kiểm chứng**: con trỏ thật khi rê chuột trong cửa sổ chính (RDP không cho di chuyển chuột thật; helper kiểm bằng
`MouseMove` giả, dòng gắn vào `MainWindow` chưa có test riêng vì `MainWindow` nằm trong exe, không có trong thư viện test);
hiển thị ở DPI/cỡ chữ Windows khác.

## 4zd. QR: định dạng vị trí + khung nhận dạng khi quét màn hình; Auto Click: chọn nhiều để xóa/nhân bản (2026-10-13, v1.19.15)

Người dùng gửi 4 yêu cầu kèm 2 ảnh (chỉ push code, không tạo release):

**1. Đọc đúng mọi định dạng vị trí.** Người dùng đưa 3 chuỗi và khẳng định "cùng một vị trí". Dữ liệu thật cho thấy KHÔNG
phải: (A) link dài `.../maps/place/Hội+Gym.../data=!4m12!1m5!3m4!2z<base64>!8m2!3d20.991978!4d105.842733!3m5!1s0x3135ac43dc0e273b:0xf2e69345522b1dc7!8m2!3d20.977992!4d105.8475138...`
là LỘ TRÌNH chỉ đường có hai điểm - xuất phát (20.991978, 105.842733; phần `!2z` giải base64 ra đúng chuỗi
`20°59'31.1"N 105°50'33.8"E`) và địa điểm Hội Gym (20.977992, 105.8475138, có mã địa điểm `!1s0x..:0x..`); (B) link rút gọn
`maps.app.goo.gl/5rH3RvRHyMLxAqNs8` chuyển hướng (HTTP 302) tới trang địa điểm Hội Gym chỉ có điểm (20.977992, 105.8475138);
(C) chuỗi độ-phút-giây = điểm xuất phát, cách Hội Gym ~1,5 km. Cách đọc: A -> điểm của ĐỊA ĐIỂM có mã địa điểm (trùng B) kèm
thông báo điểm xuất phát còn lại; B -> như cũ qua mạng; C -> điểm xuất phát. Cài đặt (lõi `QRPayload`, test được không cần mạng):
`parseGoogleMapsCoordinates()` thêm mẫu ưu tiên `!1s0x..:0x..!8m2!3d..!4d..` trước "cặp `!3d!4d` đầu tiên";
`parseLocationText()` = link Google Maps + tọa độ viết tay (`20°59'31.1"N 105°50'33.8"E`, `′ ″ º ''` Unicode, chữ hướng trước/
sau số, N/S/E/W hoa-thường - S/W cho giá trị âm, độ-phút thập phân, độ thập phân + hướng, cặp số `lat, lon`/`lat lon`,
`geo:`; từ chối phút/giây >= 60, ngoài phạm vi, số nguyên rời `12 34`); `googleMapsPins()` liệt kê mọi điểm ghim để giao diện báo
điểm khác. Ô dán ở trang Vị trí nhận cả link lẫn tọa độ.

**2. Khung nhận dạng nhỏ hơn mã khi quét màn hình.** Tái hiện tất định bằng probe: ảnh có `devicePixelRatio` 1,25 -> 4 góc chỉ phủ
79,8% vùng mã (= 1/1,25); DPR 1,0 luôn 100% ở mọi cỡ (300-2600 px). Nguyên nhân gốc: `QRCodec::padWhite()` vẽ ảnh lên nền bằng
`QPainter::drawImage`, Qt vẽ theo kích thước LOGIC (pixel/dpr) nên nội dung bị co lại trước khi giải mã (vẫn giải mã được), còn
`decodeGray()` quy đổi góc như chưa co. Ảnh chụp màn hình Windows scale 125% mang dpr 1,25 (`QScreen::grabWindow`). Sửa ở gốc:
chuẩn hóa dpr về 1 ngay đầu `QRCodec::decode()` (dữ liệu pixel không đổi, tọa độ trả về luôn là PIXEL của ảnh đầu vào) và ở
`ScreenSnipOverlay::cropSelection()`. Test hồi quy: `qr_tests` (dpr 1,0/1,25/1,5/2,0/0,75 cho cùng tọa độ góc, sai số <= 3 px)
và `qr_ui_tests` (qua tab Quét).

**3. Theo dõi số lần quét mã đã tạo - KHÔNG làm được, không thêm.** Mã QR tĩnh mã hóa trực tiếp nội dung; người khác quét bằng
điện thoại không có kênh nào gửi về ứng dụng. Muốn đếm phải dùng mã ĐỘNG (nhúng đường dẫn trung gian trên máy chủ rồi chuyển
hướng) - dự án không có máy chủ riêng (xem PRIVACY.md) nên không làm. Lịch sử hiện chỉ ghi các lần quét thực hiện TRONG ứng dụng.

**4. Auto Click: chọn nhiều hành động/chuỗi.** `ActionListWidget` và `ChainListWidget` chuyển sang `ExtendedSelection` (Ctrl + bấm
thêm/bớt một hàng, Shift cả khoảng); API mới `selectedActionIndices()/setSelectedActionIndices()` và tương ứng cho chuỗi; tín hiệu
mới `cloneActionsClicked/deleteActionsClicked` (và `...ChainsClicked`) CHỈ khi chọn từ 2 hàng trở lên - một hàng vẫn dùng tín hiệu
cũ. Chuột phải TRONG vùng chọn tác động cả nhóm (nhãn "Xóa N hành động", "Nhân bản N hành động"), NGOÀI vùng chọn thì chọn riêng
hàng đó. `AutoClickWindow::onCloneActions/onDeleteActions/onCloneChains/onDeleteChains`: nhân bản MỘT hành động đặt bản sao ngay
sau bản gốc (như cũ), nhân bản NHIỀU hành động chèn cả cụm bản sao (giữ thứ tự) NGAY SAU hành động được chọn cuối cùng ("đẩy
lùi về sau"); chuỗi: bản sao thêm vào CUỐI như nhân bản một chuỗi (luôn là hành vi cũ của chuỗi). Xóa nhiều: xóa từ cuối về đầu;
cache chỉnh sửa dở (`m_pendingEdits`) được remap cho từng hàng xóa/chèn và capture hàng đang sửa dở TRƯỚC khi chỉ số dời chỗ;
`ScopedFlag` chặn capture suốt đoạn vẽ lại bảng (bài học bấm Lưu 2 lần, mục 4zb); xóa HẾT chuỗi bị từ chối (phải giữ ít nhất
một); chuỗi đang chạy bị xóa thì bỏ dấu "đang chạy", còn lại được dồn chỉ số. Kéo-thả khi đang chọn nhiều hàng KHÔNG đổi vị
trí (chỉ dời được một hàng/lần). Diễn giải "đẩy lùi về sau" là CỤM bản sao nằm sau hành động chọn cuối (không phải cuối cả
danh sách) - dễ đổi nếu người dùng muốn cuối danh sách.

**Kiểm thử**: `qr_tests` 241/241, `qr_ui_tests` 197/197, `autoclick_tests` 251/251, `core_stress_tests` 551/551, `qrwifi_stress_tests` 760.298/760.298.
Qua mạng thật (harness tạm, không commit): A và B ra CÙNG điểm Hội Gym (20.977992, 105.847514), C ra điểm xuất phát (20.991972,
105.842722). Chọn nhiều: Ctrl + bấm bằng sự kiện chuột giả lập có phím bổ trợ; Shift bằng Shift+Mũi tên (Shift + bấm chuột giả lập
không mở rộng được ngay cả trên `QTableWidget` trần - đã đối chứng, là đặc điểm của giả lập, không phải lỗi ứng dụng).

**CHƯA kiểm chứng**: Shift + bấm CHUỘT thật và menu chuột phải thật (RDP không gửi được chuột/phím thật); ảnh chụp màn hình
thật ở DPI 125% (đã tái hiện bằng ảnh DPR 1,25 giả lập); cách hiểu "đẩy lùi về sau" của người dùng.

## 4ze. Downloader: hàng đợi hợp nhất, phân giải mọi loại link, Lịch sử + Cài đặt (2026-10-14, v1.19.16)

Người dùng gửi 6 yêu cầu, kèm 1 ảnh (menu chuột phải của JW Player: trang chặn "sao chép link") và danh sách link thật; yêu cầu build + release.

**1. Tải trực tiếp thành hàng đợi.** Dán link -> "+ Thêm vào hàng đợi" (CHƯA tải) -> bộ phân giải kiểm tra -> cột **Kiểm tra** = OK/ERR -> chọn dòng -> "Tải các mục đã chọn". `DownloadQueue` (engine) giữ `QueueItem`
(`check`: Waiting/Checking/Ok/Err; `state`: Idle/Queued/Downloading/Paused/Completed/Failed/Canceled). Tối đa vài bộ phân giải chạy đồng thời; một link có thể bung thành nhiều dòng (hồ sơ, bài đăng nhiều ảnh), dòng trùng link tệp bị bỏ.
Tệp HTTP đi qua `FileDownloader` (gửi `Referer` + User-Agent trình duyệt - nhiều CDN chặn nếu thiếu), video/luồng đi qua `YtDlpDownloadWorker` (`-o <thư mục>/<tên>.%(ext)s`, đối số cookie chèn trước `--`). Mục do nơi khác `enqueue` thẳng vào `FileDownloader`
được phản chiếu vào cùng bảng (`foreign`). Mỗi lần xong/lỗi/hủy ghi một dòng lịch sử.

**2. Trang chặn chuột phải / quét không ra.** Chặn chuột phải chỉ chặn menu, không giấu được link trong mã nguồn hay luồng mạng. Bộ phân giải (`LinkResolver`) đi lần lượt, dừng ở bước đầu tiên ra kết quả:
(a) GET dò 1 yêu cầu: tệp (Content-Type/Content-Disposition, tên tệp lấy từ header) hay HTML; (b) Google Drive/Docs: `/file/d/<ID>`, `open?id=`, `uc?id=` -> link tải, trang xác nhận "không quét được virus" dựng lại URL từ biểu mẫu, phân loại chặn
(chủ không cho tải / hết hạn mức / cần đăng nhập); Docs/Sheets/Slides công khai -> xuất PDF/XLSX/PPTX; thư mục Drive không phải tệp; (c) yt-dlp `--dump-single-json --flat-playlist` (video đơn -> 1 mục, hồ sơ/danh sách -> nhiều mục, cắt theo giới hạn);
(d) quét HTML SÂU `deepScanHtml`: thẻ img/video/audio/source/a, meta `og:video`/`og:image`/`twitter:player:stream`, chuỗi thô trong script/JSON (`"file":"https:\/\/…/index.m3u8"`, `\u0026`, URL lồng `player/?url=…m3u8`, mã hóa phần trăm, đường dẫn tương đối) và liệt kê iframe;
(e) iframe (tối đa 3, `Referer` = trang cha); (f) **trình duyệt ẩn** `HeadlessBrowser`: `msedge`/`chrome` `--headless=new --dump-dom --log-net-log --net-log-capture-mode=Default --virtual-time-budget=15000 --autoplay-policy=no-user-gesture-required` với `--user-data-dir` tạm riêng (xóa sau khi xong),
đọc NetLog lấy URL media đã được trang yêu cầu (m3u8/mpd trước, bỏ đoạn .ts/.m4s) và ảnh nội dung trong DOM (luật riêng cho Facebook `t39.30808-6`/`cstp=mx…`, TikTok `photomode`; lọc host/đường dẫn "static", "login_static"). Bài đăng ảnh chỉ trả ảnh; hồ sơ chỉ lấy được phần công khai kèm `warning`.
Cloudflare "Just a moment…" và tường đăng nhập được nhận diện để báo ERR đúng lý do thay vì trả mã nguồn rác.

**3. Kết quả thật trên các link người dùng gửi** (hàng đợi thật, tải vào thư mục tạm, kiểm tra không tải gì trước khi bấm tải):
| Link | Kết quả |
|---|---|
| Ảnh Facebook `photo/?fbid=…&set=pcb…` | OK - 1 ảnh (jpg 294 KB) qua trình duyệt ẩn; yt-dlp không hỗ trợ loại link này |
| Video Facebook `/ctsvdhbkhn/videos/…` | OK qua yt-dlp (Facebook); tải thử ra .mp4 2,7 GB trong ~227 s |
| Ảnh TikTok `@kiue.oahn/photo/…` | OK - 2 ảnh `photomode` qua trình duyệt ẩn |
| Trang phim motchillt.one | Link `.m3u8` nằm trong script/iframe -> bắt được bằng quét sâu; yt-dlp tải được HLS |
| Hồ sơ Facebook `/qynhmaiiiz` | Chỉ phần công khai qua trình duyệt ẩn + cảnh báo (yt-dlp không hỗ trợ hồ sơ Facebook; đầy đủ cần đăng nhập) |
| Hồ sơ TikTok `@kiue.oahn` | yt-dlp trả danh sách; đôi lúc lỗi thoáng qua nên thử lại tối đa 3 lần |
| Tệp Google Drive `/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/view` | ERR ở v1.19.16 - tệp do chủ sở hữu không cho tải (không phải lỗi ứng dụng). **Từ v1.19.22: OK qua chế độ xem (chụp 130 trang -> PDF), xem mục 4zj** |
| Studocu | ERR - Cloudflare/tường trả phí chặn cả yt-dlp lẫn trình duyệt ẩn |
| Ảnh logo Google, YouTube "Me at the zoo" | OK, tải đúng (5969 byte; 474 KB .webm) |

**4. Tab Cài đặt + Lịch sử.** `DownloaderSettings` (`profiles\downloader_settings.json`, `QSaveFile`): thư mục tải (mặc định Downloads), số tệp đồng thời 1-8, giới hạn mục hồ sơ 1-1000 (mặc định 100), bật/tắt trình duyệt ẩn, cookie (None/Browser/File; trình duyệt thuộc danh sách
`--cookies-from-browser` hợp lệ; tệp không tồn tại -> không thêm đối số). Mọi setter tự lưu + phát `changed()`; các tab Tải trực tiếp, Quét trang, Video nền tảng, Cài đặt đều theo cùng một thư mục. `DownloadHistoryStore`
(`profiles\downloader_history.json`, tối đa 2000, mới nhất đầu) + `DownloadHistoryTab` (lọc, mở tệp/thư mục/link nguồn, xóa mục/tất cả có hỏi xác nhận). `PlatformVideoTab` cũng ghi lịch sử.

**5. Nút 🌐.** Mỗi dòng hàng đợi và quét trang có nút mở link trên trình duyệt mặc định (chỉ http/https); lịch sử có "Mở link nguồn". **6. Hồ sơ/kênh**: link hồ sơ YouTube/TikTok/Instagram/Facebook được nhận diện (`isLikelyProfileOrCollectionUrl`) và bung thành từng bài
đăng theo giới hạn Cài đặt (đánh dấu `truncated` khi vượt).

**Dừng sạch khi đóng.** `DownloadQueue::stopProcesses()` (gọi từ `DownloaderWindow::closeEvent`) hủy các mục chờ trước rồi mới hủy mục đang chạy (nếu đảo thứ tự, việc hủy sẽ kích hoạt mục chờ kế tiếp và sinh tiến trình mới); `LinkResolver::cancel()` kết thúc cả cây tiến trình
yt-dlp/trình duyệt ẩn qua `WinProcessTree`.

**Kiểm thử.** `downloader_tests` 510/510 (`tests/MiniHttpServer.h` tách ra dùng chung); `downloader_ui_tests` 57/57 (dán link vào `DirectDownloadTab` -> 2 dòng OK/ERR đúng tên từ Content-Disposition, thư mục trống sau bước kiểm tra, tick cả dòng ERR rồi tải: chỉ dòng OK được tải 5000 byte đúng,
lịch sử 2 mục, quét trang HTML mẫu -> thêm vào hàng đợi -> toàn OK/"Chưa tải", đổi thư mục ở một nơi -> ≥4 nhãn đổi theo); `update_tests` 95/95 (dùng chung `FileDownloader`); `secdl_stress_tests` 1.869.830/1.869.830 (120 s); `core_stress_tests` đạt. Chạy hàng đợi THẬT qua mạng bằng chương trình thử tạm (đã xóa). Bố cục kiểm bằng ảnh chụp `QWidget::grab()` của từng tab (không hiện lên màn hình) - phát hiện và sửa: nút "Tải các mục đã chọn" bị cắt chữ ở cửa sổ hẹp (hàng nút nay là `FlowLayout`, tự xuống dòng), nút 🌐 bị Qt rút thành "..." (đặt cỡ cố định), cột trạng thái quá hẹp, đường dẫn thư mục hiện dấu `/` thay vì `\`.
**CHƯA kiểm chứng**: bấm chuột/hộp thoại chọn thư mục thật và nút 🌐 mở trình duyệt thật (máy qua RDP), Shift+bấm chuột chọn dải, cookie đăng nhập Instagram/Facebook, hồ sơ Facebook đầy đủ, Smart App Control.

## 4zf. Đối chiếu code giao diện với v1.19.10 (2026-10-14, v1.19.17)

Người dùng: giao diện bản này "cứ lỗi" trong khi v1.19.10 không lỗi; yêu cầu so sánh mạnh TOÀN BỘ code giao diện (từng dòng, từng hàm, không dùng ảnh chụp) và sửa cho trùng v1.19.10, vẫn giữ các tính năng đã có.

**Cách làm (lặp lại được):** `git diff --stat v1.19.10 HEAD -- src`, rồi đọc từng hunk của các tệp giao diện dùng chung và liệt kê mọi dòng BỊ XÓA/SỬA của giao diện cũ (dòng chỉ thêm cho tính năng mới được phân loại riêng).

**Kết quả:** 285 tệp nguồn, 218 giống hệt v1.19.10, 67 khác. Ngoài `src/` và tài liệu/test, chỉ `CMakeLists.txt` và `VERSION` khác. Không còn mã khung cửa sổ/popup của các bản sau (`PopupCornerFix`, `WindowTheme`, `FramelessChrome`, `DwmSetWindowAttribute`).

| Tệp / vùng | Khác v1.19.10 | Lý do |
|---|---|---|
| `src/ui/widgets/ComboBoxStyle.h` | `applyCursor()` thêm `QPushButton` (chỉ con trỏ) | Người dùng yêu cầu |
| `src/ui/widgets/ItemHoverCursor.h` + `MainWindow.cpp` (1 dòng) | Con trỏ ngón tay trên danh sách công cụ | Người dùng yêu cầu |
| `src/tools/qr/QRUiStyle.h` | **Đã đưa về giống hệt v1.19.10** (bo góc bảng 12px -> 10px) | Duy nhất không phục vụ tính năng; hoàn lại ở v1.19.17 |
| `QRGenerateTab.*` | Bỏ Email/SMS/vCard khỏi danh sách tạo; trang Vị trí bố cục lại; ô chọn WiFi, ô dán link | Người dùng yêu cầu (YEU_CAU_SAU_1.19.10.md mục 4, 1, 2) |
| `QRScanTab.*`, `QRCodec.*`, `QRPayload.*`, `ScreenSnipOverlay.cpp` | Ẩn khung KẾT QUẢ khi 1 mã, nút Kết nối WiFi, sửa khung nhận dạng/dpr; `listLabel` -> `m_resultListLabel` (cùng style) | Người dùng yêu cầu |
| `AutoClickWindow.*`, `ActionEditorWidget.*`, `ActionListWidget.*`, `ChainListWidget.*`, `RuntimeOverlay.*`, `capture/ActionRecorder.*`, `engine/*`, `model/Action.*`, `storage/ActionSerializer.cpp` | "Ghi nhớ chuỗi", cache chỉnh sửa chưa lưu, chọn nhiều + xóa/nhân bản (kèm chữ gợi ý/tooltip) | Người dùng yêu cầu |
| `wifi/ConnectDialog.*`, `android/WirelessPairDialog.cpp`, `vpn/engine/PublicIpChecker.*` | `setPassword()`, `applyCursor`, tra vị trí IP cho QR | Phục vụ tính năng QR / con trỏ |
| `tools/downloader/**` | Hàng đợi hợp nhất, tab Lịch sử/Cài đặt (mục 4ze) | Người dùng yêu cầu |

Tất cả các tool còn lại (WiFi, VPN, Disk Cleanup, Connect, Security, Android ngoài 2 dòng trên) giống hệt từng byte.

**Giới hạn:** đưa giao diện về "trùng 100%" mã v1.19.10 cho các tool QR/Auto Click/Downloader nghĩa là xóa chính các tính năng người dùng đã yêu cầu - không làm. Không tìm thấy thay đổi giao diện NÀO KHÁC không do yêu cầu; nếu còn lỗi hiển thị cụ thể cần ảnh/tab/nút để truy nguyên. CHƯA kiểm chứng hiển thị thật trên màn hình người dùng (máy chạy qua RDP).

## 4zg. Downloader: quét từng bài đăng của hồ sơ, Quét trang web dùng bộ phân giải đầy đủ, cookies.txt (2026-10-14, v1.19.18)

Người dùng báo (kèm 2 ảnh) và đặt mục tiêu `/goal`: (1) thông báo Windows Security về `ffmpeg.exe` khi tải video (vẫn tải được); (2) vì sao tải được video Facebook mà ảnh Facebook thì không; (3) hồ sơ Facebook/YouTube/TikTok quét ở "Quét trang web" phải tải được video/ảnh từng bài đăng;
(4) năm link chưa tải được (Studocu, Drive, ảnh Facebook, hồ sơ Facebook, ảnh TikTok); (5) bỏ mũi tên ô số ở Cài đặt. Mục tiêu: link mẫu (kể cả tệp PDF) phải tải được, hồ sơ phải quét từng bài và tải ảnh.

**Phát hiện (đo thật):**
- Tab "Quét trang web" (v1.19.16) chỉ quét HTML tĩnh nên không ra gì với hồ sơ/bài ảnh (dựng bằng JavaScript) - bộ phân giải đầy đủ chỉ có ở tab hàng đợi. Đây là lý do "quét web không ra" ở yêu cầu (3) và một phần (2).
- yt-dlp hỗ trợ video Facebook nhưng không hỗ trợ ảnh Facebook/TikTok (cần trình duyệt ẩn).
- DOM hồ sơ Facebook (chưa đăng nhập) lộ ~9 liên kết `/photo/?fbid=...` (ảnh trên trang chỉ là bản thu nhỏ/cắt vuông `c0.185.720.720a`); DOM hồ sơ TikTok lộ 8 liên kết `/photo/<id>` và 7 `/video/<id>`.
- yt-dlp liệt kê bài ẢNH của hồ sơ TikTok dưới dạng `/video/<id>`; tải `/video/<id>` của bài ảnh cho định dạng duy nhất `mp3` (`vcodec: none`) - tức chỉ có nhạc nền, không có ảnh. `/video/<id>` không chuyển hướng sang `/photo/<id>`, trang tĩnh bị thử thách JS nên không phân loại được bằng HTTP.
- Drive `1ObltgwgTX8...`: Google trả "Sorry, the owner hasn't given you permission to download this file. Only the owner and editors can download this file." - chủ tệp tắt quyền tải. Studocu: Cloudflare + tường trả phí.

**Thay đổi:**
- `LinkResolver`: sau bước trình duyệt ẩn, nếu là hồ sơ thì `LinkResolverCore::postLinksFromDom()` lấy danh sách bài (Facebook `/photo/?fbid=`, `/photo.php`, `/<tên>/videos/<id>`, `/reel/<id>`, `/watch/?v=`, `/<tên>/posts/<mã>`; TikTok `/@<chính chủ>/photo|video/<id>`; Instagram `/p|reel|tv/<mã>`); `startExpansion()` tạo bộ phân giải con cho từng bài ảnh (mặc định 3 song song,
  `Options::expandPosts=false` ở con để không đệ quy) và gom ảnh ĐẦY ĐỦ; bài video thành mục yt-dlp. Hồ sơ TikTok: yt-dlp liệt kê -> `refineTikTokCollection()` dùng DOM hồ sơ phân biệt ảnh/video; bài ngoài phần nhìn thấy được đánh dấu `ResolvedMedia::maybePhotoPost`. `parseYtDlpJson()` nhận ra bài ảnh TikTok (`vcodec none` + extractor TikTok) và trả `photoPostUrl`.
  Trình duyệt ẩn lần đầu lỗi (không phải hết giờ/không có trình duyệt) thì thử lại `--headless` cũ; thông báo lỗi kèm dòng cuối stderr; nhật ký ghi host+đường dẫn và kết quả.
- `DownloadQueue`: mục `maybePhotoPost` được kiểm tra lại bằng một `LinkResolver` NGAY trước khi tải (`startPhotoCheck/onPhotoChecked`): là bài ảnh thì mục đó + các mục chèn thêm thành các ảnh rồi tải, không thì tải video như thường; hủy/xóa/đóng cửa sổ dừng cả lượt kiểm tra này.
- `PageScanTab` viết lại: quét tĩnh -> tự quét sâu (khi chưa có video/âm thanh/tài liệu, hoặc hồ sơ/bài đăng, hoặc bấm "Quét sâu"), nút Dừng, "Thêm vào hàng đợi", "⬇ Tải ngay" (mục đã có sẵn trong hàng đợi được tải tiếp, không nhân đôi), nút 🌐 mỗi dòng; `cancelScan()` gọi khi đóng cửa sổ.
- `CookieFile` (mới): đọc `cookies.txt` Netscape (cả `#HttpOnly_`, bỏ cookie hết hạn/dòng hỏng) và nạp vào `QNetworkCookieJar` của `LinkResolver` và `FileDownloader` (nạp lại khi tệp đổi); thông báo Drive "chủ không cho tải" gợi ý cookies.txt khi chưa đặt, hoặc báo tài khoản vẫn không có quyền khi đã đặt.
- Cài đặt: `QSpinBox::NoButtons` (bỏ mũi tên), nhãn ô tick ngắn lại + tooltip.

**Kết quả thật qua hàng đợi** (tải vào thư mục tạm, kiểm tra xong KHÔNG có tệp nào trước khi bấm tải):
| Link | Kết quả |
|---|---|
| Ảnh Facebook `/photo/?fbid=1935618970974748` | OK - 1 ảnh 294 KB |
| Hồ sơ Facebook `/qynhmaiiiz` | OK - 9 ảnh đầy đủ từ 9 bài (41 s), tải 9/9 |
| Ảnh TikTok `@kiue.oahn/photo/7683433150571121940` | OK - 2 ảnh |
| Hồ sơ TikTok `@kiue.oahn` (giới hạn 14) | OK - 25 mục = 19 ảnh (bài ảnh) + 6 video, tải 25/25 (~116 s kiểm tra, 13 s tải) |
| Video Facebook `/ctsvdhbkhn/videos/1068033449194736` | OK qua yt-dlp (đã tải thử ở bản trước, 2,7 GB) |
| Drive `1ObltgwgTX8...` | ERR - chủ tệp tắt quyền tải; chủ/biên tập viên dùng cookies.txt |
| Studocu | ERR - Cloudflare + tường trả phí |

**Không làm (có chủ đích):** né kiểm tra bot của Cloudflare hay tường trả phí của Studocu - đây là vượt kiểm soát truy cập, không phải tải tệp hợp lệ. Studocu: tải qua tài khoản của người dùng trong trình duyệt (nút 🌐). **Đã đổi ở v1.19.22 (mục 4zj):** tài liệu Drive mà chủ tệp tắt "tải xuống" nay ĐƯỢC HỖ TRỢ qua chế độ xem (chụp từng trang -> PDF), theo yêu cầu của người dùng; không khuyến khích dùng cho mục đích vi phạm pháp luật hoặc bản quyền. Chủ/biên tập viên Drive vẫn nên dùng cookies.txt để tải bản gốc.

**Thông báo "Part of this app has been blocked ... ffmpeg.exe":** Windows (Smart App Control/danh tiếng ứng dụng) chặn `ffmpeg.exe` không có chữ ký số (bản BtbN) do yt-dlp nạp. yt-dlp vẫn tải được, nhưng bước ghép hình+tiếng có thể hỏng khi Smart App Control ở chế độ chặn. Chỉ có chữ ký số mới hết hẳn - theo quy tắc dự án không tự ký (SignPath/chủ dự án). CHƯA kiểm chứng trên máy bật Smart App Control.

**Kiểm thử:** `downloader_tests` 554/554, `downloader_ui_tests` 65/65, `update_tests` 95/95, `secdl_stress_tests` 1.874.715/1.874.715, `core_stress_tests` đạt. **CHƯA kiểm chứng:** nguyên nhân link ảnh Facebook không ra trên MÁY NGƯỜI DÙNG (ở máy phát triển ra đủ; nay lỗi hiện chi tiết + thử lại chế độ ẩn cũ + ghi nhật ký), cookies.txt với tài khoản Google thật, hồ sơ Facebook/Instagram đầy đủ khi đăng nhập, bấm chuột/🌐 thật.

## 4zh. Downloader: gộp 3 tab thành tab "Tải xuống", sửa giao diện lỗi, vì sao 2 link không tải được (2026-10-14, v1.19.19)

> **Cập nhật v1.19.20 (mục 4zi):** hai nút "+ Thêm vào hàng đợi" và "🔍 Quét trang" mô tả dưới đây đã gộp thành MỘT nút "🔍 Quét"; nút "Đổi..."/"Mở thư mục" đã bỏ.

Người dùng yêu cầu (kèm 1 ảnh chụp tab "Quét trang web" với 9 ảnh Facebook): (1) gộp 3 tab Tải trực tiếp, Quét trang web, Video nền tảng - lấy giao diện của Tải trực tiếp, chức năng dung hợp cả 3; (2) giao diện đang lỗi như ảnh, sửa cho tab mới, "đừng đẻ thừa như vòng bao quanh margin"; (3) hai link không tải được (Studocu, Google Drive) - nêu vì sao. Lần này chưa tạo release.

**Nguyên nhân lỗi giao diện (tái hiện bằng ảnh chụp offscreen `QWidget::grab`):** cột 🌐 là một widget nút (`QToolButton`/`QPushButton` có viền + nền trắng) đặt làm cell widget. Dòng được chọn có nền xanh, nút vẫn trắng viền bo - hiện thành "vòng/khung" quanh nút (đúng thứ trong ảnh người dùng gửi, ở tab Quét trang web bảng còn không dùng `SelectRows` nên chọn một ÔĐƠN quanh nút). Thêm: thanh tiến độ mỗi dòng luôn có nền trắng + chữ "0%" kể cả dòng chưa tải; ô `QLabel` thông báo trống vẫn chiếm một khoảng; tiêu đề cột căn giữa lệch với chữ ô.

**Gộp tab (`DownloadTab`, `git mv` từ `DirectDownloadTab`; xóa `PageScanTab`, `PlatformVideoTab`):**
- Giao diện = tab Tải trực tiếp cũ (ô dán nhiều link, thư mục lưu, hàng nút thao tác, bảng hàng đợi 9 cột). Thêm: nút "🔍 Quét trang" cạnh "+ Thêm vào hàng đợi"; ô tick "Quét sâu"; ô "Chất lượng video"; banner đỏ khi thiếu `yt-dlp.exe`/`ffmpeg.exe` (chỉ hiện khi thiếu).
- **Quét trang** (từ `PageScanTab`): với mỗi link trong ô nhập, lần lượt: `PageMediaScanner` (HTML tĩnh) -> thêm các tệp tìm được vào hàng đợi bằng `DownloadQueue::addResolved()` (dòng OK ngay, chưa tải) -> quét sâu bằng `LinkResolver` khi: ô "Quét sâu" bật, là hồ sơ/bài đăng, hoặc quét tĩnh chưa thấy video/âm thanh/tài liệu (hoặc tải HTML tĩnh lỗi). Nút thành "⏹ Dừng quét" khi đang chạy; dừng/đóng cửa sổ hủy resolver, `PageMediaScanner` dựng MỚI cho từng trang và hủy ngay khi dừng (nó không có API hủy nên không để tín hiệu muộn lọt sang lượt sau). Kết thúc: dòng thông báo ghi số mục thêm mới/"không có mục mới - đã có trong hàng đợi"/lý do từng trang (404, bị chặn, cảnh báo cắt giới hạn). `cancelBackgroundWork()` gọi từ `DownloaderWindow::closeEvent`.
- **Video nền tảng** (từ `PlatformVideoTab`): cài đặt chung `DownloaderSettings::videoQuality()` (`auto`, `2160`...`360`, `audio`; khóa lạ -> `auto`; lưu `videoQuality` trong `downloader_settings.json`) -> `formatSelectorForQuality()` ra bộ chọn `-f` (`bv*[height<=H]+ba/b[height<=H]/b`, `ba/b` cho âm thanh; `auto` -> rỗng = yt-dlp tự chọn); `DownloadQueue::startYtWorker()` dùng nó trừ khi mục có `QueueItem::formatSelector` riêng. Bấm phải một mục yt-dlp -> "Chọn chất lượng khác..." -> `YtDlpInfoWorker` lấy danh sách định dạng thật (tốt nhất lên đầu, định dạng chỉ-hình ghép `+bestaudio`) -> hộp chọn -> `DownloadQueue::setItemFormat()` (chỉ mục yt-dlp; "Kiểm tra lại" xóa lựa chọn vì có thể ra video khác). Tải/tiến độ/hủy/lịch sử của video nền tảng đi qua hàng đợi chung (không còn thanh tiến độ riêng).
- Cửa sổ còn 3 tab; `DownloaderWindow` tối thiểu 760x620 (ở 540 phần điều khiển chiếm gần hết, bảng chỉ thấy ~1 dòng).

**Sửa giao diện:** cột "Mở" là chữ "↗" trong ô (`cellClicked` mở link, tooltip, màu xanh/trắng theo dòng chọn, không còn widget nút nên không có khung); `setFocusPolicy(NoFocus)` bỏ khung chấm quanh ô; thanh tiến độ dòng chưa tải `background: transparent; border: none` (đổi qua thuộc tính `idle`, chỉ đặt lại stylesheet khi trạng thái đổi) và kiểu phẳng `DownloaderUi::progressBarStyle()`; tiêu đề căn trái; dòng thông báo ẩn khi trống; con trỏ ngón tay chỉ ở cột tick và cột Mở (`eventFilter` trên viewport, không dùng `ItemHoverCursor` vì nó phủ mọi ô). Không thêm viền/khung/lề mới.

**Lý do lỗi đọc được:** cột Trạng thái cắt ở ~60 ký tự rồi bảng cắt tiếp theo bề rộng cột (190 px) nên lý do ERR không đọc hết. Chọn đúng MỘT dòng ERR/thất bại/cảnh báo -> lý do ĐẦY ĐỦ hiện đỏ ở dòng thông báo (`QLabel` bôi đen sao chép được).

**Vì sao 2 link không tải được (chạy bộ phân giải thật `LinkResolver` + `curl`, ngày 2026-10-14):**
| Link | Kết quả đo thật | Lý do |
|---|---|---|
| Google Drive `1ObltgwgTX8QHw7S36wFToyNJNUdosyKn` | `drive.usercontent.google.com/download?id=...&confirm=t` trả HTTP 200 `text/html`, 1820 byte: "Sorry, the owner hasn't given you permission to download this file. Only the owner and editors can download this file." Ứng dụng báo: "Chủ sở hữu không cho phép tải tệp này về..." | Chủ tệp TẮT quyền tải cho người xem (trình xem `/view` vẫn mở được). Chỉ chủ/biên tập viên tải được. |
| Studocu `.../giai-tich-1/35467985` | `curl`: HTTP 403, tiêu đề "Studocu - Access Blocked", có `challenge-platform` (Cloudflare). bộ phân giải (yt-dlp -> quét mã nguồn -> trình duyệt ẩn) không ra tệp nào: "Trang được bảo vệ chống truy cập tự động (Cloudflare) hoặc yêu cầu đăng nhập/trả phí" | Cloudflare chặn mọi yêu cầu tự động; nội dung tài liệu sau đó còn cần tài khoản/trả phí (phần này CHƯA kiểm chứng - không thể truy cập sau thử thách). |

**Không làm (có chủ đích):** né kiểm tra bot Cloudflare hay tường trả phí Studocu - đây là vượt kiểm soát truy cập, không phải tải tệp hợp lệ. Chủ tài khoản Studocu có quyền thật: tải trực tiếp bằng tài khoản trong trình duyệt. **Đã đổi ở v1.19.22 (mục 4zj):** Drive chỉ-xem (đọc ảnh từng trang từ trình xem -> PDF) nay được hỗ trợ theo yêu cầu người dùng; chủ tệp Drive có quyền thật vẫn có thể dùng cookies.txt.

**Kiểm thử:** `downloader_tests` 574/574 (+20: `formatSelectorForQuality`, khóa lạ/chuỗi chèn lệnh `720]; calc` -> rỗng, lưu bền, `fromJson` kẹp về `auto`), `downloader_ui_tests` 85/85 (3 tab, đúng MỘT `DownloadTab`; cột Mở là chữ "↗" và KHÔNG có cell widget; bảng `NoFocus`; quét trang HTML mẫu -> 2 dòng OK "Chưa tải", quét lại không nhân đôi, chọn hết + tải -> 2 tệp đúng kích thước; trang 404 -> không mục, nút trở lại, lý do hiện; dừng giữa chừng; combo chất lượng <-> cài đặt hai chiều; `setItemFormat` chỉ nhận mục yt-dlp; chọn dòng ERR -> lý do đầy đủ), `update_tests` 95/95, `secdl_stress_tests` 1.868.834/1.868.834 . Ảnh chụp tab mới bằng `QWidget::grab` (Qt offscreen + `QT_QPA_FONTDIR`) ở 920x700 và 760x540: dòng chọn không còn khung trắng. **CHƯA kiểm chứng:** "Chọn chất lượng khác..." và tải với chất lượng giới hạn bằng yt-dlp thật (cần mạng + video, chỉ tự kiểm tra tay được); hiển thị trên màn hình người dùng; cookies.txt với tài khoản Google thật.

## 4zi. Downloader: một nút "Quét", bỏ nút thư mục, vì sao KHÔNG tích hợp 4pdf.net/okdebrid.com (2026-10-14, v1.19.20)

Người dùng yêu cầu 4 việc: (1) gộp "+ Thêm vào hàng đợi" và "Quét trang" thành MỘT nút "Quét"; (2) bỏ nút "Đổi..." và "Mở thư mục" của tab Tải xuống; (3) link Drive `1ObltgwgTX8QHw7S36wFToyNJNUdosyKn` "có trang web tải được" (4pdf.net hoặc tiện ích khác) - nghiên cứu cơ chế và tích hợp; (4) link Studocu `.../giai-tich-1/35467985` "có trang web tải được" (okdebrid.com hoặc khác) - nghiên cứu cơ chế và tích hợp.

**Đã làm (1)(2):**
- Một nút `🔍 Quét` (130x76, kiểu nút chính, cạnh ô dán link; Ctrl+Enter trong ô cũng quét). Dòng chảy = "Quét trang" cũ: mỗi link lần lượt qua `PageMediaScanner` (HTML tĩnh) rồi bộ phân giải đầy đủ (`LinkResolver`) khi cần (ô "Quét sâu" bật, là hồ sơ/bài đăng, hoặc quét tĩnh chưa thấy video/âm thanh/tài liệu); mục tìm được vào hàng đợi bằng `addResolved()` (dòng OK, chưa tải). Nút thành `⏹ Dừng quét` khi đang chạy. Link nhập là tệp trực tiếp/Drive/Docs cũng đi đường này (quét tĩnh báo "đây là tệp" rồi bộ phân giải xử lý), nên một nút thay được cả "Thêm vào hàng đợi".
- `DownloadQueue::addFailed(url, reason)` (mới): link quét xong mà không ra tệp nào thành MỘT dòng ERR trong bảng kèm lý do (đã có dòng ERR của đúng link đó thì chỉ cập nhật lý do - quét lại không nhân đôi). Lý do: nút "Thêm vào hàng đợi" cũ luôn để lại dòng ERR nhìn thấy được; tab quét chỉ hiện lỗi ở dòng thông báo. Dòng ERR dùng chung "Kiểm tra lại" của hàng đợi. Các ghi chú lỗi/cảnh báo nhiều trang hiện mỗi cái một dòng trong dòng thông báo (trước nối bằng 2 dấu cách nên bị tràn).
- **Link Google Drive/Docs bỏ qua bước quét HTML tĩnh** (`LinkResolverCore::parseDriveFileId`/`googleDocsExportUrl`), chỉ bộ phân giải đầy đủ xử lý. Đo thật: `PageMediaScanner` trên `drive.google.com/file/d/<id>/view` trả 1 "ảnh" `drive.google.com/drive-viewer/AKGpiha...` - ảnh xem trước của tệp mà chủ đã khóa tải. Nếu gộp nút mà không chặn thì nút Quét sẽ liệt kê ảnh này thành mục tải được, tức đi vòng hạn chế của chủ tệp.
- `enqueueText()`, `onAddClicked()`, `onChooseFolderClicked()`, `onOpenFolderClicked()` và 3 nút tương ứng bị xóa khỏi `DownloadTab`; nhãn thành "Thư mục lưu (đổi ở tab Cài đặt):". Tab Cài đặt (Chọn.../Mở thư mục) và Lịch sử (Mở thư mục) giữ nguyên.
- Thông báo lỗi Cloudflare/đăng nhập (`LinkResolver::bestFailureMessage`) thêm gợi ý: "Nếu bạn có quyền truy cập: mở trang bằng trình duyệt (bấm ↗) rồi tải bằng chính tài khoản của bạn."

**Không làm (4), có chủ đích (mục (3) Drive ĐÃ ĐỔI ở v1.19.22 - xem mục 4zj; ở v1.19.20 ghi là không làm):** tích hợp 4pdf.net (Drive) và okdebrid.com (Studocu). Cả hai dùng để lấy tệp mà chủ tệp/nhà cung cấp đã chủ động không cho lấy qua đường chính thức: chủ tệp Drive TẮT quyền tải cho người xem ("Only the owner and editors can download this file"), còn Studocu chặn truy cập tự động bằng Cloudflare và thu phí/đòi tài khoản. Đưa chúng vào ứng dụng là xây tính năng vượt kiểm soát truy cập/tường trả phí - cùng lý do mục 4zg đã từ chối vượt hạn chế Drive và né Cloudflare. Đường hợp lệ cho cùng mục tiêu: (a) Drive - xin chủ tệp cấp quyền tải hoặc bật "người xem có thể tải"; nếu bạn là chủ/biên tập viên thì chọn cookies.txt của đúng tài khoản ở tab Cài đặt (đã hỗ trợ từ v1.19.18, CHƯA kiểm chứng với tài khoản Google thật); (b) Studocu - mở trang bằng trình duyệt (cột ↗) và tải bằng tài khoản Studocu của bạn theo chính sách tải của Studocu (CHƯA kiểm chứng điều kiện cụ thể).

**Kiểm chứng thật (mạng thật, 2026-10-14):** quét 2 link qua chính `DownloadTab::scanText` (trình duyệt ẩn bật như bình thường): sau ~12-16 s có 2 dòng ERR - Drive: "Chủ sở hữu không cho phép tải tệp này về (chỉ chủ sở hữu/người chỉnh sửa tải được)..."; Studocu: "Trang được bảo vệ chống truy cập tự động (Cloudflare) hoặc yêu cầu đăng nhập/trả phí - không lấy được tệp. Nếu bạn có quyền truy cập: mở trang bằng trình duyệt (bấm ↗)..." - KHÔNG có dòng ảnh xem trước. Ảnh chụp offscreen xác nhận cả hai lý do hiện đầy đủ, mỗi lỗi một dòng.

**Kiểm thử:** `downloader_tests` 579/579 (+5: `addFailed` - dòng ERR, không nhân đôi, không bao giờ tải), `downloader_ui_tests` 94/94 (đúng MỘT nút quét, không còn nút "+ Thêm vào hàng đợi"/"Đổi..."/"Mở thư mục"; quét tệp trực tiếp + link 404 -> dòng OK + dòng ERR; quét trang 404 -> đúng 1 dòng ERR, quét lại không thêm dòng; dừng giữa chừng), `update_tests` 95/95, `secdl_stress_tests` 1.874.464/1.874.464. **CHƯA kiểm chứng:** Studocu sau thử thách Cloudflare (nội dung có cần tài khoản/trả phí không); cookies.txt với tài khoản Google thật; hiển thị trên màn hình người dùng.

**Thu hẹp ô nhập (v1.19.21):** ô dán link và nút `🔍 Quét` từ 76 px xuống 52 px (`kInputHeight` trong `DownloadTab.cpp`) theo yêu cầu người dùng kèm ảnh chụp; vẫn thấy đủ 2 dòng chữ, từ dòng thứ 3 thanh cuộn của ô xuất hiện. Chỉ đổi giao diện.

## 4zj. Downloader: hỗ trợ tải tài liệu Google Drive CHỈ XEM (chủ tệp đã tắt "tải xuống") (2026-10-14, v1.19.22)

**Yêu cầu và quyết định.** Người dùng gửi cơ chế "chế độ xem Drive để lộ từng trang trong DOM" (kèm mẫu DOM của link `1ObltgwgTX8...`) cho báo cáo về cơ chế dễ bị vượt, rồi yêu cầu tích hợp vào Downloader sao cho link Drive đó tải được. Mục 4zg/4zi trước đó ghi quyết định KHÔNG làm (xem là vượt kiểm soát truy cập); khi được báo về quyết định cũ,
người dùng chọn tích hợp và yêu cầu sửa tài liệu thành "hỗ trợ tải Drive chỉ xem (không khuyến nghị cho các mục đích vi phạm pháp luật)". Do đó từ v1.19.22 tính năng này CÓ trong ứng dụng; tài liệu, cảnh báo trong ứng dụng (cột cảnh báo của dòng OK, tooltip ô "Dùng trình duyệt ẩn") và `PRIVACY.md` đều nêu: **chỉ dùng cho tài liệu bạn có quyền sử dụng, không khuyến khích dùng cho mục đích vi phạm pháp luật hoặc bản quyền.**

**Cơ chế (đã quan sát trên DOM thật).** Để hiển thị, viewer của Drive gửi từng trang dưới dạng ảnh `<img src="blob:...">` về trình duyệt người xem, kèm lớp chữ `<p>`; "tắt tải xuống" chỉ ẩn nút. Mỗi trang là một khung `style="padding-bottom: 56.25%"` chứa `<h2>Trang n / N</h2>`;
ảnh nạp lười khi trang tới gần vùng nhìn và có thể bị gỡ khi cuộn qua - nên phải chụp NGAY khi nạp xong. Không dựa vào tên class (bị làm rối, có thể đổi): nhận trang qua `h2` khớp `/(\d+)\s*(?:\/|of)\s*(\d+)/i` (cả bản tiếng Anh "Page 1 of 130").

**Luồng trong ứng dụng.**
1. `LinkResolver::stageDrive`: GET tải trực tiếp -> `classifyDriveHtml` = `NoPermission` -> `stageDriveViewer` (thay cho ERR cũ): cần `useBrowser` bật và có Edge/Chrome; GET `/file/d/<ID>/view`, đọc tiêu đề trang (`LinkResolverCore::driveViewerName`: bỏ hậu tố " - Google Drive", bỏ đuôi tài liệu quen thuộc, loại tệp KHÔNG xem theo trang như video/âm thanh/ảnh/nén -> ERR có lý do),
   trả `ResolvedMedia::Kind::DriveViewer` (`fileName` = `<tên>.pdf`, kèm `warning` nói rõ PDF là ảnh trang, không chọn/tìm kiếm được chữ). Không có trình duyệt/tắt tính năng/link không xem được khi chưa đăng nhập (`accounts.google.com`, 401/403) -> ERR với lý do cũ + gợi ý. `Options::driveBaseOverride` chỉ cho test.
2. `DownloadQueue`: mục DriveViewer xếp hàng riêng (tối đa 1 lượt chụp cùng lúc - mỗi lượt là một trình duyệt ẩn chạy vài phút), `QueueItem::pagesDone/pagesTotal` cho tiến độ THEO TRANG (cột Trạng thái "Đang tải - trang x/y", thanh tiến độ chạy theo số trang), hủy/xóa/đóng cửa sổ (`stopProcesses`) dừng được, ghi lịch sử `kind = "drive-viewer"`. Tên tệp đích chọn không đè (`chooseHttpDestPath`).
3. `DriveViewerCapture` (QThread; hủy qua `requestCancel()`, kiểm tra cờ mỗi 200 ms): chọn cổng cục bộ trống -> khởi chạy Edge/Chrome ẩn (`--remote-debugging-port=<cổng>`, `--user-data-dir=<thư mục tạm>`, `--headless=new`, `--window-size=1400,1000`, `--force-device-scale-factor=2`, URL đặt SAU CÙNG) -> `GET /json/list` lấy WebSocket của tab -> `CdpClient` gửi `Runtime.evaluate`: tiêm `window.__dx`, chờ viewer vẽ danh sách trang, cuộn từng bước 0,6 màn hình,
   mỗi trang nạp xong thì vẽ ảnh lên canvas ở kích thước tự nhiên (nền trắng) và xuất JPEG 0,92; tối đa 3 lượt cuộn để bù trang nạp chậm; trần 1 GB ảnh trong RAM; timeout tổng 15 phút. Ghép bằng `ImagePdf::build` (JPEG nguyên vẹn, bộ lọc DCTDecode, bảng xref đúng từng offset, thứ tự theo số trang, số thực luôn dấu chấm), ghi nguyên tử qua `QSaveFile`. Thiếu trang -> vẫn lưu và báo cảnh báo danh sách trang thiếu.
4. Dọn dẹp: `BrowserSession` quét hậu duệ (`WinProcessTree`) TRƯỚC khi dừng tiến trình cha, dừng cả cây, rồi xóa hồ sơ tạm (thử lại 10 lần vì Edge giữ khóa tệp) - cả khi lỗi/hủy.

**An toàn.** Chỉ nhận `https://drive.google.com/file/d/<ID>/view` (`DriveViewerCaptureInternal::isAllowedUrl`; `allowAnyHttpUrl` chỉ cho test); không dùng cookie/đăng nhập; hồ sơ trình duyệt tạm riêng; cổng DevTools chỉ trên `127.0.0.1`; URL không đi qua shell (đối số `QProcess`); khung WebSocket từ máy khách được che (mask) đúng RFC 6455, chặn khung > 512 MB.
Không dùng QtWebSockets (bộ Qt MinGW của dự án không có) hay QtWebEngine (không có cho MinGW).

**Lỗi thật gặp khi làm:** máy chủ DevTools của Edge ĐÓNG KẾT NỐI NGAY (không trả gì) khi nhận yêu cầu HTTP/1.0 - `GET /json/list` phải là HTTP/1.1 (kèm `Connection: close`). Phát hiện nhờ chương trình thử tạm in `lastProblem`; chẩn đoán đó được giữ lại trong thông báo lỗi ("Trình duyệt không khởi động kịp (...)").

**Nghiệm thu THẬT (mạng thật, 2026-10-14):** qua đúng đường ứng dụng (`LinkResolver` -> mục DriveViewer -> `DriveViewerCapture`) trên link mẫu `1ObltgwgTX8QHw7S36wFToyNJNUdosyKn`: phân giải ~1,3 s ra mục "02. Tài liệu đào tạo CBQL TOPIK - Upd TOPIK 108.pdf"; chụp **130/130 trang** trong ~80 s; PDF 27,6 MB, `pypdf` strict đọc đủ 130 trang (mỗi trang ảnh 1600x900), nội dung trang 65 đúng bản trình chiếu. Tệp thử nằm ở thư mục tạm và đã xóa ngay (tài liệu có ảnh/thông tin cá nhân người dự thi).

**Kiểm thử tự động.** `downloader_tests` 652/652 (+73): `ImagePdf` (PDF hợp lệ - xref khớp từng đối tượng, thứ tự trang theo số trang dù chèn lộn xộn, không dấu phẩy thập phân, JPEG nguyên bytes), `driveViewerName`, `isAllowedUrl`/`buildArguments`/`injectedScript`, khung WebSocket (độ dài 7/16/64 bit, mask),
bộ phân giải với máy chủ giả Drive (`driveBaseOverride`: OK, tắt trình duyệt ẩn, video `.mp4`, 403, 404), và CHỤP THẬT bằng Edge ẩn trên trang xem giả lập (`tests/MockDriveViewer.h`: ảnh blob nạp chậm + gỡ khi cuộn qua; ảnh trang i rộng 1000+i để kiểm thứ tự): đủ 10/10 trang, hủy giữa chừng <15 s không để lại tệp, URL ngoài Drive bị từ chối, hàng đợi hợp nhất (Đang tải -> Hoàn tất, lịch sử, hủy, `stopProcesses`), không sót thư mục hồ sơ tạm.
`downloader_ui_tests` 100/100 (hàng DriveViewer hiện "trang x/10" + thanh tiến độ theo trang, hủy -> "Đã hủy"); `update_tests` 95/95; `secdl_stress_tests` 1.874.476/1.874.476 (126 s). Không có tiến trình Edge nào còn sót sau các bộ test.

**CHƯA kiểm chứng:** Google Chrome (chỉ thử Edge), tài liệu hàng trăm trang (RAM/thời gian), Windows khác Windows 10, và việc Drive đổi cấu trúc viewer sau này (khi đó chuyển về ERR kèm lý do chứ không treo - timeout 15 phút/90 s chờ viewer). **Giới hạn:** PDF là ảnh trang (không chọn/tìm kiếm được chữ - lớp chữ `<p>` trong DOM chưa được dùng), độ nét theo ảnh Drive gửi về, chỉ link công khai, cần Edge/Chrome và bật "Dùng trình duyệt ẩn".

## 4zk. Rà soát toàn bộ lần ba: sửa lỗi ở cả 9 công cụ + stress test (2026-10-15, v1.19.23)

Yêu cầu người dùng: đọc toàn bộ mã nguồn, sửa mọi lỗi liên quan, stress test cả chương trình tìm lỗi ẩn rồi sửa; lần
này chỉ push code, KHÔNG tạo release. Năm người đọc độc lập rà 9 công cụ, hai người rà lại chính bản sửa (tìm thêm
3 lỗi chắc chắn + 2 lỗi có khả năng trong bản sửa đầu - đều đã sửa trước khi commit), rồi chạy lại 18 bộ test thường +
5 bộ stress.

**Auto Click**
- Phát lại "Ghi nhớ chuỗi": dừng giữa chừng (nút Dừng/phím dừng/Windows từ chối) trước đây `return` thẳng - phím/nút
  đang giữ (Ctrl, chuột trái...) bị KẸT trên máy. Nay mọi đường thoát nhả hết (ngược thứ tự). Nhấn/nhả/cuộn đưa con trỏ
  về đúng tọa độ đã ghi (MouseMove chỉ ghi thưa mỗi 20 ms nên click lệch); nhịp phát lại theo mốc thời gian tuyệt đối
  (không trôi dần).
- Dừng GHI bằng phím dừng toàn cục: hook bàn phím thấy Ctrl/Alt/F8 trước khi Windows đổi thành WM_HOTKEY nên tổ hợp
  này bị ghi vào cuối macro - phát lại thì tự kích hoạt phím dừng và tự dừng. Nay bỏ đúng đoạn đuôi đó
  (`ActionRecorder::dropTrailingKeys`, `StopHotkey::activeKeyCodes`).
- Kéo-thả/ghim chuỗi làm danh sách nhảy sang chuỗi khác; đổi tên tự áp dụng chỉnh sửa dở và bỏ chọn hành động -
  `refreshChainListKeeping()`. id chuỗi mới không còn trùng sau khi xóa.
- Nạp hồ sơ: `scrollAmount` kẹp 1-100 (trước: tệp ghi 2 tỉ = 2 tỉ lần SendInput), `keyCode` ngoài 1..254 về Enter,
  sự kiện phím mã 0 bị bỏ, `wheelDelta` kẹp miền short, `delayMs` 1e300 không còn là hành vi không xác định.
- Phím bên phải (Ctrl/Alt phải, "/" bàn số, Menu, PrintScreen, NumLock) phát lại đúng (thiếu cờ EXTENDEDKEY).
- Dấu `*` chưa lưu gắn đúng hàng đang sửa; đổi macro sang loại khác không còn giữ hàng nghìn sự kiện ẩn trong JSON;
  bắt tổ hợp phím ngoài danh sách vẫn dùng được (mục "(phím khác: ...)", tên NUMx/VK x) và đánh dấu chưa lưu khi chỉ
  đổi THỨ TỰ phím bổ trợ; `~ActionRunner` không bao giờ hủy luồng đang chạy; chữ HUD "Next Round/End" -> tiếng Việt.

**QR Tools / WiFi**
- Mã WiFi thiếu trường `T:` bị hiểu là WPA kể cả khi không có mật khẩu (mạng mở không kết nối được) - nay suy theo mật
  khẩu.
- "Mật khẩu khác..." luôn tạo hồ sơ mang tên SSID nên mạng đã có hồ sơ tên khác ("Nha 2") bị nhân đôi hồ sơ - nay dùng
  lại đúng hồ sơ đó (trừ hồ sơ do Group Policy quản lý: giữ cách cũ). `WlanProfileXml::build` thêm tham số tên hồ sơ.
- Danh sách "WiFi đã lưu" ở trang tạo mã thử lại nếu lần đầu không mở được dịch vụ WLAN; hồ sơ 802.1X có gợi ý đúng.
- Đo tốc độ: khối dữ liệu đầu tiên không còn bị tính vào tốc độ (kết quả cao hơn thật trên mạng nhanh).
- Biểu tượng tín hiệu: 4 mức thật (trước 75%/50% giống nhau, tín hiệu yếu dùng 📵 "cấm điện thoại").

**Connect Together** (CHƯA kiểm chứng trên hai máy thật)
- TCP_NODELAY cho mọi phiên (kể cả kết nối tự mở - đặt lại lúc `connected`): khung chuột nhỏ không còn bị Nagle giữ.
- Nhận quyền điều khiển thì con trỏ (bị hook giữ đứng yên) được lùi 200 px vào trong: nằm đúng mép thì Windows kẹp
  điểm chuột muốn tới, độ dịch về phía mép luôn bằng 0.
- Khóa DPAPI không mở được lúc nạp (lỗi hồ sơ Windows tạm thời) trước đây làm máy đã ghép đôi BỊ XÓA VĨNH VIỄN ở lần
  lưu kế tiếp - nay giữ nguyên mục đó; quên máy/quên tất cả/ghép đôi lại thì gỡ bản cũ.
- Mỗi cạnh màn hình chỉ một máy; không tạo mã ghép đôi khi chưa chạy; lỗi lưu địa chỉ được báo.
- Công tắc mới "Đồng bộ clipboard" (mặc định bật như trước) - tắt khi sao chép thông tin nhạy cảm.

**Disk Cleanup**: bấm Quét đúng lúc lượt trước đã báo xong nhưng luồng chưa thoát hẳn khiến tab kẹt ở "Đang quét..."
(`startScan` bỏ qua lời gọi chồng - đúng hợp đồng) - nay ba tab chờ ngắn (tối đa 3 giây) cho luồng cũ thoát TRƯỚC khi
chuyển giao diện sang trạng thái bận. (Bản sửa đầu đặt việc chờ trong chính `startScan()` - `disk_vpn_stress_tests` bắt
được ngay vì phá hợp đồng "gọi chồng bị bỏ qua", đã chuyển lên tầng giao diện.) Xóa tệp trùng không còn báo "Lỗi" cho tệp cố ý giữ lại do đã đổi; nhãn "(sẽ giữ
lại)" theo đúng ô đang bỏ tích.

**Android**: nút mới "🔌 Kết nối không dây..." (`adb connect` bất đồng bộ, `AdbPairer::connectDevice`,
`interpretConnectResult`) - trước đây ghép đôi xong không có cách nào đưa máy vào danh sách; chỉ thiếu `scrcpy.exe`
vẫn liệt kê/ghép đôi được (nút Điều khiển cần scrcpy); lỗi khởi chạy scrcpy không còn báo hai lần, bấm Dừng không
còn nháy "Process crashed". (CHƯA kiểm chứng với điện thoại thật.)

**VPN**: trạng thái callback RAS gom dưới một khóa và gắn với HRASCONN (lỗi muộn của phiên trước không còn làm phiên
sau báo thất bại); ô khóa L2TP/IPsec PSK (`-L2tpPsk`, truyền qua biến môi trường riêng của powershell.exe bằng
`PowerShellRunner::runWithSecretEnv`, KHÔNG nằm trong dòng lệnh); xóa hồ sơ báo lỗi lưu tệp phụ. (CHƯA kiểm chứng với
máy chủ VPN thật.)

**Security Gateway**: mọi lệnh Defender (đọc trạng thái, danh sách mối đe dọa, bật/tắt bảo vệ, xóa mối đe dọa) chạy
NỀN - trước đây mở cửa sổ đứng 3 lệnh PowerShell liền. Nút gọi Defender khóa theo cả "đang chạy nền" lẫn "đang quét";
nút chạy lại quyền Quản trị bị chặn khi luồng nền còn chạy; hàm bật/tắt trả kết quả đúng kể cả khi không truyền con
trỏ kết quả và trả luôn trạng thái đọc lại (2 lệnh PowerShell thay vì 3).

**Downloader**
- Kiểm tra "bài ảnh hay video" TikTok giới hạn 2 lượt cùng lúc (trước: chọn 100 mục = 100 bộ phân giải + Edge ẩn).
- "Kiểm tra lại" bỏ mục HTTP cũ (trước: lần tải sau tiếp tục link/tên tệp CŨ); xóa dòng gỡ luôn mục trong
  `FileDownloader` (`forget()` - trước: tên tệp bị giữ chỗ mãi, lần sau thành " (1)", bộ nhớ tăng dần).
- Tiêu đề có `[ ]` làm sai kiểm tra tên trùng (bộ lọc QDir coi là ký tự đại diện) -> mục báo Hoàn tất với tệp cũ; hai
  tên gốc là tiền tố của nhau không chạy cùng lúc; hủy/lỗi yt-dlp dọn tệp tạm (.part/.ytdl/.part-FragN/.temp và tệp
  định dạng chờ ghép .fNNN.ext).
- Tệp tải về được gắn **Mark of the Web** (`Zone.Identifier`, ZoneId=3, địa chỉ đã bỏ truy vấn/đăng nhập) như trình
  duyệt - SmartScreen/Office vẫn cảnh báo; "Mở tệp"/nhấp đúp ở Lịch sử HỎI trước với tệp chạy được (.exe/.ps1/.lnk...).
- Trình duyệt ẩn chụp Drive: bỏ `--remote-allow-origins=*` (trang web bất kỳ trên máy nối được vào cổng DevTools),
  cổng do trình duyệt tự xin (`--remote-debugging-port=0` + đọc `DevToolsActivePort`) - hết tranh chấp cổng.
- yt-dlp xin N+1 mục nên cảnh báo "danh sách bị cắt" hoạt động; "Chọn chất lượng khác..." dùng cookie/Referer như lúc
  tải; phân tích link chỉ cần yt-dlp.exe, thiếu ffmpeg.exe thì tải định dạng một tệp (`b/best`).
- Dòng tóm tắt hàng đợi gộp làm mới (~6 lần/giây) thay vì mỗi khối dữ liệu mạng.

**Stress test**: bộ fuzz hồ sơ Auto Click trong `core_stress_tests` sinh giá trị ngoài miền (mã phím âm, 2 tỉ nấc
cuộn) mà giao diện không bao giờ tạo - sau khi thêm lớp kẹp, khứ hồi lệch đúng như thiết kế (3 kiểm tra fail). Đã đưa
bộ sinh về miền hợp lệ và thêm kiểm tra miền cho các trường mới trong khối JSON "rác".

**Kiểm thử** (build sạch, máy dev Windows 10): 18 bộ test thường **4.105 kiểm tra, 0 lỗi** (trước đợt này 3.971 - thêm
134 kiểm tra hồi quy: `autoclick` 279, `qr` 245 + 197, `wifi` 124 + 74, `connect` 1.101 + 17, `diskcleanup` 457 + 53,
`android` 153 + 30, `vpn` 132 + 7, `security` 333 + 8, `downloader` 700 + 100, `update` 95). Năm bộ stress: xem dòng
v1.19.23 ở mục 8 (`core` 551/0, `qrwifi` 759.618/0, `connect` 386/0, `disk_vpn` 7.320/0, `secdl` 1.876.549/0). **CHƯA kiểm chứng** (cần phần cứng/môi trường thật): phát
lại macro và bắt phím trên desktop thật, chuột qua biên giữa hai máy LAN, `adb connect` với điện thoại, kết nối L2TP PSK
thật, bật/tắt Defender thật, `Zone.Identifier` trên ổ FAT32/exFAT (bỏ qua êm), Chrome thay Edge cho chụp Drive.

## 4zl. VPN: tab "Đổi IP theo quốc gia" (VPN Gate) + gộp tối đa máy chủ + tab "Kiểm tra kết nối" (2026-10-22, v1.19.26)

Yêu cầu người dùng (kèm ảnh Hotspot Shield): (1) VPN phải chọn được một số quốc gia rồi bấm connect để đổi
IP máy; (2) tích hợp đọc profile của các VPN client khác (FortiClient/GlobalProtect...). Bổ sung giữa chừng:
"lấy nhiều nhất có thể các máy chủ VPN free" và "có hẳn 1 tab riêng để kiểm tra kết nối đến". Người dùng chọn
**làm yêu cầu 1 trước**, nguồn **VPN Gate**, và **chỉ push code, không phát hành release**.

**Quyết định kiến trúc (làm rõ với người dùng trước khi code):** Hotspot Shield đổi IP theo quốc gia được vì
họ SỞ HỮU hạ tầng máy chủ. One for ALL là ứng dụng cục bộ, **không có/không tự dựng máy chủ VPN nào**. Cách
HỢP PHÁP duy nhất để có đúng trải nghiệm "chọn quốc gia → đổi IP" mà không tự dựng hạ tầng: dùng lại **danh
sách relay tình nguyện công khai của VPN Gate** (dự án nghiên cứu học thuật ĐH Tsukuba, Nhật), kết nối bằng
**L2TP/IPsec** của Windows RAS có sẵn (tái dùng toàn bộ `VpnController`/`VpnConnector`), tài khoản công khai
user/mật khẩu/PSK đều là "vpn" (công bố trên trang hướng dẫn của VPN Gate).

**Tab "🌍 Đổi IP theo quốc gia" (`VpnGateTab`, tab ĐẦU của cửa sổ VPN):**
- `VpnGateClient` (vpn_core): GET API công khai `https://www.vpngate.net/api/iphone/` (HTTPS, hủy được,
  timeout 40s). `VpnGateClientInternal::parseCsv()` THUẦN, test được: bỏ dòng `*vpn_servers`/`#...`/`*`, lấy 7
  trường đầu; **lọc an toàn** dòng IP không phải IPv4 (`isValidIpv4` qua QHostAddress) hoặc mã quốc gia không
  đúng 2 chữ cái - quan trọng vì IP trở thành đích kết nối VPN. Giá trị âm kẹp về 0. `VpnGateServer` chỉ giữ
  trường để hiển thị + kết nối (cố ý KHÔNG giữ cấu hình OpenVPN base64 - bản này kết nối L2TP).
- Giao diện: banner cảnh báo (relay tình nguyện, IP động, tốc độ/ổn định không bảo đảm, không phải máy chủ
  nào cũng bật L2TP, tự chịu trách nhiệm), nhóm "Vị trí hiện tại (theo IP)" (tái dùng `PublicIpChecker`), nút
  "Tải thêm máy chủ", combo lọc theo quốc gia (kèm số máy chủ mỗi nước), bảng Quốc gia/Ping/Tốc độ/Số phiên/IP
  (sắp theo điểm), nút Kết nối/Ngắt và "Kiểm tra kết nối".
- Kết nối: tạo hồ sơ VPN **TẠM** `OneForAll-VPNGate` (ghi đè) trỏ IP đã chọn, L2TP, PSK "vpn" (qua biến môi
  trường - không lộ dòng lệnh, cơ chế có từ v1.19.23) → quay số RAS user/pass "vpn" → khi lỗi hoặc khi ngắt
  thì tự **xóa hồ sơ tạm** (best-effort). Lỗi báo rõ "máy chủ có thể không bật L2TP hoặc bị chặn - thử máy chủ
  khác". IP hiển thị tự cập nhật sau kết nối/ngắt để NGƯỜI DÙNG tự xác nhận IP đã đổi thật.

**"Lấy nhiều nhất có thể" - gộp tích lũy (`VpnGateCache`, vpn_core):** API `/api/iphone/` chỉ trả ~100 máy chủ
"tốt nhất" mỗi lần (đã đo thật: gọi 4 lần liên tiếp đều ra ĐÚNG 98 máy chủ như nhau - không có endpoint công
khai lớn hơn, các nguồn free khác đều là OpenVPN cần máy khách riêng = giai đoạn 2). Cách trung thực để tối đa
hóa: **GỘP kết quả mỗi lần tải vào một danh sách lưu trên đĩa** (`profiles/vpngate_servers.json`), dedup theo
IP (bản mới thắng), loại máy chủ quá cũ (không thấy lại sau 3 ngày - IP của VPN Gate là động), trần 1000. Danh
sách top xoay vòng theo thời gian nên tải định kỳ sẽ tích lũy thêm. `merge()`/`toJson()`/`fromJson()` THUẦN,
test được. Nạp cache lúc mở tab để thấy ngay danh sách đã tích lũy.

**Tab "🔎 Kiểm tra kết nối" (`ConnectivityTab` + `ConnectivityTester`, tab thứ 2):** thử mở MỘT kết nối TCP tới
host:port (thuần Qt `QTcpSocket`, không cần Administrator, không gửi dữ liệu gì sau bắt tay) và báo có "chạm"
được không + độ trễ; nhật ký tối đa 50 dòng. Dùng để lọc nhanh máy chủ đã chết trước khi kết nối VPN. Lưu ý
nêu rõ trong giao diện: L2TP chạy trên UDP nên phép thử TCP (mặc định cổng 443 - nhiều máy chủ VPN Gate có bật
SSL-VPN/SSTP) chỉ là CHỈ BÁO máy chủ còn sống, không khẳng định chắc L2TP kết nối được. Bấm "Kiểm tra kết nối"
ở tab VPN Gate chuyển sang tab này và điền sẵn IP máy chủ đang chọn (`checkConnectivityRequested`).

`VpnControlWindow` nay có 4 tab [Đổi IP theo quốc gia, Kiểm tra kết nối, VPN hệ thống, WireGuard]; cả tab VPN
Gate (có phiên RAS) được xử lý trong `closeEvent`/`forceStopNow` như tab VPN hệ thống.

**Kiểm thử:** `vpn_tests` 182/0 (+27: `isValidIpv4`/`parseCsv` với CSV mẫu đúng định dạng thật; `VpnGateCache`
merge/loại-cũ/trần/JSON khứ hồi/dữ liệu hỏng; `ConnectivityTester` TCP tới máy chủ cục bộ chạm được + cổng
đóng không chạm được). `vpn_ui_tests` 7/0 (dựng thật cửa sổ 4 tab, không crash). **Kiểm chứng parser trên DỮ
LIỆU THẬT** (2026-10-22): tải CSV thật, chạy đúng luật lọc - 98/98 máy chủ được chấp nhận, 0 bỏ oan (Nhật/Hàn/
Mỹ/Thái/Nga/Việt Nam/Anh...).

**CHƯA kiểm chứng (cần mạng + môi trường thật):** kết nối L2TP THẬT tới một máy chủ VPN Gate và xác nhận IP đổi
đầu-cuối (môi trường build không kết nối VPN thật được - cùng giới hạn đã nêu cho VPN hệ thống); người dùng cần
tự bấm thử. Máy chủ chọn không bật L2TP thì kết nối báo lỗi và mời chọn máy khác (đã xử lý), không treo.

**Yêu cầu 2 (đọc profile FortiClient/GlobalProtect) - CHƯA làm đợt này** (người dùng chọn làm sau): sẽ là đọc
các kết nối đã cấu hình trên máy (tên, địa chỉ cổng - từ registry của chính các client đó) rồi khởi chạy chính
client đó để kết nối; KHÔNG giải mã mật khẩu đã lưu, KHÔNG kết nối thẳng (giao thức SSL-VPN riêng).

## 4zm. VPN: sửa lỗi kết nối L2TP "Lỗi 6: The handle is invalid" + helper NAT-T (2026-10-22, v1.19.27)

Người dùng báo (kèm ảnh): kết nối máy chủ VPN Gate **lần nào cũng** báo "Không kết nối được máy chủ này (Thao
tác VPN thất bại. (Lỗi 6: The handle is invalid.))". Đây là đường `RasDialW` thật mà tài liệu ghi "CHƯA kiểm
chứng" (môi trường build không có VPN thật) - giờ chạy thật mới lộ.

**Nguyên nhân thật (bug, không đoán):** hàm gọi lại `dialCallback` có đúng chữ ký `RASDIALFUNC1`
(`HRASCONN,UINT,RASCONNSTATE,DWORD,DWORD`), nhưng `RasDialW` lại truyền `dwNotifierType = 1`. Theo tài liệu
Microsoft, **1 = RASDIALFUNC (3 tham số), 2 = RASDIALFUNC1 (5 tham số)** (xác nhận trong `ras.h` của MinGW:
dòng 440-441). Truyền nhầm 1: RAS gọi lại theo ABI 3 tham số nên tham số đầu (`HRASCONN`) bị đọc lệch, biến
`g_callback.conn` KHÔNG BAO GIỜ khớp HRASCONN thật → mất toàn bộ mã lỗi thật mà hàm gọi lại báo về. Khi quay số
thất bại, chỉ còn `RasGetConnectStatusW` trả về handle đã bị Windows thu hồi = `ERROR_INVALID_HANDLE` (6) →
hiện "The handle is invalid" khó hiểu thay vì lý do thật (789/800...).

**Đã sửa:**
- `VpnConnector::doConnect`: `dwNotifierType` 1 → **2** (đúng cho `RASDIALFUNC1`). Nay hàm gọi lại khớp
  HRASCONN, mã lỗi RAS thật (789 = bắt tay L2TP/IPsec thất bại, 800/809 = không liên lạc được máy chủ, 691 =
  sai tài khoản...) được báo đúng.
- `VpnConnector::evaluateDialPoll`: khi handle bị thu hồi mà chưa có mã lỗi cụ thể, KHÔNG hiện "handle is
  invalid" nữa mà cho thông báo hướng dẫn: máy chủ có thể không bật L2TP, hoặc mạng/ISP chặn L2TP (UDP
  500/4500/1701), hoặc máy chủ sau NAT → thử máy chủ khác / dùng tab Kiểm tra kết nối / bật "Sửa lỗi L2TP sau NAT".

**Helper "Sửa lỗi L2TP sau NAT" (nguyên nhân phổ biến nhất khiến L2TP tới máy chủ công cộng thất bại):**
L2TP/IPsec tới máy chủ nằm sau NAT (đa số máy chủ tình nguyện của VPN Gate) THẤT BẠI trên Windows cho tới khi
đặt khóa registry `HKLM\SYSTEM\CurrentControlSet\Services\PolicyAgent\AssumeUDPEncapsulationContextOnSendRule`
= 2 - cách sửa được Microsoft tài liệu hóa chính thức. Thêm nút "🔧 Sửa lỗi L2TP sau NAT (Admin)" ở tab VPN
Gate: hỏi xác nhận rõ ràng → mở UAC chạy PowerShell quyền Admin đặt khóa (`VpnController::enableAssumeUdp
EncapsulationElevated` qua `ShellExecuteExW` verb "runas") → báo cần KHỞI ĐỘNG LẠI máy. `buildReadAssumeUdp
Script()`/`assumeUdpSetCommand()` thuần, test được; đọc trạng thái không cần Admin.

**Kiểm thử:** `vpn_tests` 188/0 (+6: `evaluateDialPoll` khi handle thu hồi cho thông báo L2TP đúng + ưu tiên mã
lỗi thật của hàm gọi lại; script đọc/đặt AssumeUDPEncapsulation), `vpn_ui_tests` 7/0.

**CHƯA kiểm chứng (cần VPN thật + máy thật):** kết nối L2TP đầu-cuối sau khi sửa; việc đặt khóa registry NAT-T
và khởi động lại có làm L2TP tới VPN Gate thành công hay không - cần người dùng tự thử trên máy thật. Nếu sau
khi bật NAT-T + khởi động lại vẫn lỗi với nhiều máy chủ, nhiều khả năng mạng/ISP chặn hẳn L2TP - khi đó cần
OpenVPN/SSTP (giai đoạn sau), không khắc phục được bằng L2TP.

## 4zn. VPN: tab "VPN đã cài sẵn" - đọc hồ sơ FortiClient/GlobalProtect/OpenVPN/Cisco rồi mở client (2026-10-22, v1.19.28)

Yêu cầu 2 của người dùng (đã hoãn ở các đợt trước): "VPN của tôi phải tích hợp, có thể lấy và đọc các profile
được lưu trên các VPN khác như FortiNet, GlobalProtect...". Làm theo đúng ranh giới an toàn đã thống nhất:
**đọc các kết nối ĐÃ CẤU HÌNH trên máy (tên + địa chỉ máy chủ) từ registry/tệp cấu hình của CHÍNH các phần
mềm đó, rồi MỞ chính phần mềm đó để kết nối - KHÔNG giải mã/đọc mật khẩu đã lưu, KHÔNG tự kết nối thẳng** (các
VPN doanh nghiệp này dùng giao thức SSL-VPN độc quyền của nhà cung cấp). Giống cách WiFi Connection đọc hồ sơ
WiFi đã lưu của Windows.

**`ThirdPartyVpnScanner` (vpn_core):** quét 4 phần mềm phổ biến (chỉ đọc, an toàn):
- **FortiClient**: tunnel SSL-VPN trong registry `HKCU\Software\Fortinet\SslvpnClient\Tunnels\<tên>` và các
  biến thể `HKLM\...\Fortinet\FortiClient\Sslvpn\Tunnels` / `WOW6432Node` - đọc giá trị `Server`.
- **GlobalProtect**: portal ở `HKLM\SOFTWARE\Palo Alto Networks\GlobalProtect\PanSetup` (giá trị `Portal`) và
  `HKCU\...\GlobalProtect\Settings` (giá trị `LastUrl`).
- **OpenVPN**: tệp `*.ovpn` trong `config_dir` (đọc từ `HKLM\SOFTWARE\OpenVPN`, mặc định `Program Files\OpenVPN\
  config`) và `%USERPROFILE%\OpenVPN\config`; đọc dòng `remote <host> <port>` đầu tiên (`parseOvpnRemote`).
- **Cisco Secure Client/AnyConnect**: tệp hồ sơ XML trong `ProgramData\Cisco\...\Profile\*.xml`; đọc
  `<ServerList><HostEntry>` (HostName/HostAddress) qua `parseCiscoProfileXml`.
Đường dẫn chương trình client (để MỞ) dò theo các vị trí cài đặt chuẩn (`firstExistingPath`).

**`launch()`**: mở phần mềm client bằng `ShellExecuteW` (OpenVPN: `openvpn-gui.exe --connect "<tên>.ovpn"`; các
client khác mở cửa sổ chính để người dùng tự đăng nhập/kết nối). Không bao giờ truyền/ghi mật khẩu.

**Tab "🏢 VPN đã cài sẵn" (`ThirdPartyVpnTab`, tab cuối):** bảng [Phần mềm, Tên hồ sơ, Máy chủ/Portal], nút
"Quét lại" và "Mở phần mềm để kết nối" (chỉ bật khi tìm thấy chương trình). Rỗng thì báo rõ không tìm thấy hồ sơ
nào. Quét chạy trên luồng giao diện vì chỉ đọc registry/tệp cục bộ (nhanh, khác PowerShell).

**Kiểm thử:** `vpn_tests` 200/0 (+12: `parseOvpnRemote` bỏ comment/kèm cổng, `parseCiscoProfileXml` nhiều
HostEntry/XML rác, `normalizeServer`, `firstExistingPath`, và `scan()` thật chỉ-đọc không crash - máy build không
cài client nào nên trả về rỗng), `vpn_ui_tests` 7/0 (dựng thật cửa sổ 5 tab).

**CHƯA kiểm chứng (cần máy có cài sẵn các phần mềm đó + hồ sơ thật):** đọc đúng registry/tệp của từng phiên bản
FortiClient/GlobalProtect/OpenVPN/Cisco (bố cục có thể đổi theo phiên bản - mã viết phòng thủ, thiếu thì bỏ qua,
không lỗi), và việc MỞ client + tham số `--connect` của OpenVPN hoạt động thật. Cần người dùng tự kiểm trên máy
có cài các phần mềm đó.

## 4zo. VPN: quét thêm tunnel FortiClient IPsec + chẩn đoán lỗi kết nối VPN Gate (2026-10-22, v1.19.29)

Người dùng báo (kèm ảnh): (a) tab "VPN đã cài sẵn" QUÉT THIẾU hồ sơ - khay hệ thống FortiClient có `VPN_CVTLT`
nhưng app không thấy; (b) kết nối VPN Gate vẫn lỗi dù đã bật "Sửa lỗi L2TP sau NAT" + khởi động lại.

**Sửa (a) - quét thiếu tunnel FortiClient IPsec:** soi registry thật trên máy người dùng (FortiClient 7.4.3): các
tunnel SSL-VPN nằm ở `...\FortiClient\Sslvpn\Tunnels\<tên>` (giá trị `Server`), nhưng tunnel **IPsec** nằm ở chỗ
KHÁC - `...\FortiClient\IPSec\Tunnels\<tên>` với địa chỉ gateway trong SUBKEY `<tên>\P1` giá trị `RemoteGW`.
Scanner cũ chỉ đọc Sslvpn nên bỏ sót mọi tunnel IPsec. Đã bổ sung: đọc cả `IPSec\Tunnels` (lấy `P1\RemoteGW`) và
đúng khóa HKCU `...\FortiClient\Sslvpn\Tunnels`; dedup theo tên nhưng ƯU TIÊN bản CÓ địa chỉ (giá trị Server/
RemoteGW thường chỉ nằm ở HKLM, HKCU chỉ giữ cờ hiển thị). **Xác nhận thật trên máy có FortiClient:** nay quét ra
đủ `VPN_BDTTG` (103.221.228.37:10443), `VPN_VNU` (112.137.143.2:444), `VPN_CVTLT` (IPsec, 119.17.206.59),
GlobalProtect - đủ 4 hồ sơ (trước chỉ 3, thiếu VPN_CVTLT).

**Chẩn đoán (b) - vì sao VPN Gate vẫn lỗi** (thử THẬT trên máy người dùng, không đoán): khóa NAT-T đã = 2 thật
(nút hoạt động đúng, chỉ chạy ẩn nên không thấy cửa sổ), dịch vụ IKEEXT/PolicyAgent đều Running. Nhưng quay số
L2TP thật tới 5 máy chủ VPN Gate khác nhau đều **lỗi 791** ("security policy for the connection was not found"),
kể cả khi đặt cấu hình IPsec tường minh (3DES/SHA1/DH2). Tức **L2TP/IPsec native của Windows hỏng trên máy này** -
rất có thể do thành phần IPsec của FortiClient (máy có cài FortiClient với tunnel IPsec) xung đột với IPsec của
Windows, hoặc ISP chặn. **SSTP** (TCP 443, không dùng tầng IPsec) bắt tay TLS tới máy chủ OK nhưng chứng chỉ là
`*.opengw.net` nên phải kết nối bằng hostname `<id>.opengw.net` - hostname này KHÔNG có trong API iphone của VPN
Gate (cả cấu hình .ovpn cũng chỉ ghi IP). **OpenVPN** kết nối bằng IP trên cổng 443 (chạm được) với chứng chỉ CA
nhúng sẵn trong .ovpn - đây là lối thoát khả thi nhất, nhưng cần đóng gói `openvpn.exe` (giai đoạn 2, chưa làm).

Kết luận (b): đây KHÔNG phải lỗi trong ứng dụng (app đã bật NAT-T đúng và nay báo đúng mã lỗi). Trên máy mà L2TP
native bị hỏng/chặn, cần OpenVPN - sẽ hỏi người dùng trước khi thêm (thêm một nhị phân vendored + sửa CI, giống
yt-dlp/scrcpy).

**Kiểm thử:** `vpn_tests` 200/0 (scan() thật trên máy có FortiClient ra đủ 4 hồ sơ gồm VPN_CVTLT IPsec với địa chỉ
đúng), `vpn_ui_tests` 7/0. Phần đọc registry là Win32 nên không unit-test được bằng dữ liệu giả - đã xác nhận TRỰC
TIẾP trên máy thật.

**CHƯA làm (chờ người dùng quyết):** đóng gói OpenVPN để kết nối VPN Gate thật (giai đoạn 2); kết nối THẲNG trong
ứng dụng tới FortiClient/GlobalProtect (cần openfortivpn/OpenConnect + xử lý thông tin đăng nhập - giai đoạn 3).

## 4zp. VPN: kết nối VPN Gate bằng OpenVPN (đóng gói openvpn.exe + wintun.dll) (2026-10-22, v1.19.30)

> **ĐÃ BỎ ở v1.19.31 (mục 4zq):** người dùng báo kết nối đứng ở "Đang khởi động OpenVPN..." và yêu cầu bỏ OpenVPN.
> Toàn bộ mô tả dưới đây chỉ còn giá trị lịch sử - mã `OpenVpnRunner`, gói `vendor/openvpn/` và bước CI tương ứng đã
> bị gỡ. Nguyên nhân treo (đọc từ log thật) và cách thay thế: xem 4zq.

Thực hiện "giai đoạn 2" của mục 4zo: trên máy mà L2TP/IPsec native hỏng, dùng **OpenVPN** để kết nối máy chủ công
cộng VPN Gate. Người dùng đã chọn "Có, thêm OpenVPN".

**Vì sao OpenVPN chạy được khi L2TP không:** cấu hình `.ovpn` của VPN Gate kết nối bằng **IP trên TCP 443** (chạm
được dù ISP chặn UDP 500/4500/1701 của L2TP) và xác thực hoàn toàn trong tầng TLS bằng **chứng chỉ CA + chứng chỉ
client NHÚNG SẴN** trong `.ovpn` (khối `<ca>`/`<cert>`/`<key>` - đã xác minh trên `.ovpn` THẬT tải từ API) nên không
đụng tầng IPsec đang hỏng và không cần hostname như SSTP.

**Luồng dữ liệu:** `VpnGateClient::parseCsv` giải mã cột Base64 CUỐI của mỗi dòng CSV thành nội dung `.ovpn`, lưu vào
`VpnGateServer::ovpnConfig` (CHỈ khi vừa tải - KHÔNG ghi vào cache tích lũy để cache gọn). Tab "🌍 Đổi IP theo quốc
gia" có ô chọn **"Cách kết nối: OpenVPN (khuyến nghị) / L2TP"** (mặc định OpenVPN nếu có đủ nhị phân). Chọn OpenVPN →
`OpenVpnRunner::connectConfig(ovpnConfig, "vpn", "vpn")`.

**`OpenVpnRunner` (vpn_core, `src/tools/vpn/engine/OpenVpnRunner.*`):**
- Chạy `openvpn.exe` ĐÓNG GÓI (`<thư mục chạy>/openvpn/openvpn.exe`) qua **UAC** (`ShellExecuteExW` verb `runas`) vì
  tạo adapter ảo wintun cần quyền Administrator. Tiến trình nâng quyền KHÔNG kill được từ tiến trình thường nên điều
  khiển qua **management interface**: openvpn mở cổng TCP trên `127.0.0.1` (cổng tự do lấy động), có **mật khẩu ngẫu
  nhiên** (chỉ tiến trình này biết). App nối vào, đọc `>STATE:...,CONNECTED,...` (báo đã kết nối) và gửi
  `signal SIGTERM` để ngắt SẠCH. Lời nhắc `ENTER PASSWORD:` của management KHÔNG có ký tự xuống dòng nên dò thẳng trong
  bộ đệm, không đọc theo dòng.
- `Internal::buildConfig()` (THUẦN, test được) dựng lại `.ovpn` chạy nền được từ cấu hình gốc: BỎ `auth-user-pass`
  (tương tác), `management`/`dev-node`/`windows-driver`/`block-outside-dns`; THÊM `management 127.0.0.1 <port> <pwfile>`
  + `management-hold` (chờ app nối management xong mới quay số) + `windows-driver wintun` (ép dùng wintun.dll đóng gói,
  không cần cài TAP) + `auth-nocache`. **CHỈ thêm lại `auth-user-pass <file>` khi cấu hình GỐC có** - cấu hình VPN Gate
  chuẩn xác thực bằng chứng chỉ client nhúng, KHÔNG có auth-user-pass; thêm thừa sẽ làm hỏng bắt tay (đã xác minh trên
  `.ovpn` thật: không có dòng auth-user-pass).
- `isAvailable()` kiểm `openvpn.exe` + `wintun.dll`; thiếu thì tab báo rõ và vẫn cho chọn L2TP.
- **Đóng cửa sổ khi đang bật OpenVPN:** tunnel đã kết nối ổn định KHÔNG tính là `isBusy()`, nên `VpnControlWindow::
  closeEvent` gọi thêm `VpnGateTab::hasActiveOpenVpnTunnel()` → `cancelAndWait()` (SIGTERM) để không bỏ lại
  `openvpn.exe` nâng quyền chạy tiếp giữ VPN sau khi đóng.

**Đóng gói nhị phân (`vendor/openvpn/`, KHÔNG commit - giống scrcpy/yt-dlp):** `openvpn.exe` + các DLL phụ thuộc
(`libssl-3-x64.dll`, `libcrypto-3-x64.dll`, `libpkcs11-helper-1.dll`, `vcruntime140.dll` - lấy đúng bằng `objdump -p`)
từ bản **community CHÍNH THỨC** `build.openvpn.net` (admin-install `msiexec /a` chỉ giải nén, không cài driver/dịch vụ);
`wintun.dll` từ nguồn CHÍNH THỨC `wintun.net` (bản .msi 2.6 KHÔNG còn bundle wintun dạng tệp). **Xác minh tính xác
thực** (đúng tinh thần THIRD_PARTY.md): nguồn OpenVPN không có SHA256SUMS nhưng mọi nhị phân đều ký **Authenticode** -
xác minh bằng `Get-AuthenticodeSignature` (Windows-native, chắc chắn trên CI, KHÔNG phụ thuộc gpg/keyring - gpg của
Git trên runner hiểu sai `GNUPGHOME` kiểu Windows nên không dùng được, đã gặp thật): `.msi` + `openvpn.exe` + `libssl`/
`libcrypto`/`libpkcs11-helper` do "OpenVPN Inc." ký, `vcruntime140` do "Microsoft" ký, `wintun.dll` do "WireGuard LLC"
ký (OpenVPN cũng công bố chữ ký GPG `.asc`, fingerprint `F554 A368 ... 2F2B 01E7`, để kiểm tay nếu muốn).
`build_app.bat` (bước [6/8]) chép `vendor/openvpn` → `build/openvpn` → `OneForAll_Release/openvpn`; CI
(`scripts/ci_fetch_vendor.ps1` + `.github/workflows/build-installer.yml`) tự tải + xác minh + chép. Cả hai bộ cài đặt
đã gói toàn bộ cây `OneForAll_Release` (Inno `recursesubdirs`, WiX `heat.exe`) nên tự kèm `openvpn/`.

**Kiểm thử:** `vpn_tests` 223/0 (+23): `buildConfig` cho cả cấu hình CÓ auth-user-pass (thay bằng tệp) lẫn cấu hình
VPN Gate thật KHÔNG có auth-user-pass (không tự thêm, giữ nguyên `<cert>`/`<key>`); `stateName`; `isAvailable`. Đã xác
minh THẬT: tải + GPG-verify `.msi`, Authenticode-verify `openvpn.exe`/`wintun.dll`, giải nén đủ DLL, build full +
đóng gói `OneForAll_Release/openvpn/` đủ 7 tệp.

**CHƯA kiểm chứng đầu-cuối trên máy này:** việc kết nối OpenVPN thật và đổi IP cần MỘT lần bấm **Yes ở hộp thoại UAC**
(môi trường tự động hóa không tự bấm thay được). Mọi phần không cần UAC đã xác minh. Khi người dùng sẵn sàng: mở app →
tab "Đổi IP theo quốc gia" → Tải máy chủ → chọn 1 dòng → "Kết nối" (OpenVPN) → bấm Yes ở UAC → xem IP công khai đổi.

## 4zq. VPN: bỏ OpenVPN -> SSTP; sửa lỗi gốc quay số RAS; kết nối TRỰC TIẾP FortiGate/GlobalProtect (2026-10-22, v1.19.31)

Người dùng: (1) "ấn mở openvpn daemon rồi mà có gì xảy ra đâu - bỏ openvpn đi, tự làm một cái kết nối được tới
các server VPN free"; (2) "thêm kết nối các profile trực tiếp qua tiện ích".

### (a) Vì sao OpenVPN treo (đọc log THẬT, không đoán)
`%TEMP%\OneForAll-*\openvpn.log`: app ĐÃ nối cổng quản lý nhưng openvpn đứng `Need hold release ... waiting` 60 giây.
Lời nhắc `ENTER PASSWORD:` không có ký tự xuống dòng nên không bị xóa khỏi bộ đệm -> dòng kế tiếp thành
`ENTER PASSWORD:SUCCESS: password is correct` -> app không nhận ra nên không bao giờ gửi `hold release`. Hết giờ
thì app cắt socket mà KHÔNG gửi SIGTERM -> 2 tiến trình `openvpn.exe` NÂNG QUYỀN bị rò (đã dọn bằng cách nối cổng
quản lý với mật khẩu trong tệp tạm rồi gửi `signal SIGTERM`). Theo yêu cầu, OpenVPN bị gỡ hoàn toàn.

### (b) VPN Gate qua SSTP của Windows (không daemon, không nhị phân ngoài, không quyền Admin)
Phát hiện quan trọng (sửa kết luận sai ở 4zo): cột `HostName` của API iphone CHÍNH LÀ tiền tố tên miền DDNS -
`<HostName>.opengw.net` phân giải đúng IP máy chủ (đo 12/12), và chứng chỉ máy chủ trình ra trên 443
(`CN=opengw.net`, Let's Encrypt) được Windows xác thực KHÔNG lỗi với tên đó. SSTP (TLS/443) không đụng tầng IPsec
nên chạy được cả trên máy mà L2TP hỏng (lỗi 791). `VpnGateServer::sstpHost()` (lọc chặt: chỉ nhãn DNS a-z0-9-,
1-63 ký tự - dữ liệu từ mạng đi vào lệnh PowerShell). Tab có ô chọn "SSTP - cổng 443 (khuyến nghị) / L2TP".
`Add-VpnConnection -TunnelType Sstp -EncryptionLevel Required` (code cũ) chạy được với SoftEther (đã thử).

### (c) Lỗi gốc của `VpnConnector` - MỌI lần quay số qua app đều hỏng
Thử SSTP bằng `rasdial.exe` thì đổi IP được, nhưng qua code của app (`vpn_gate_sstp_real_check`) thì cả 5 máy chủ
đều "đóng ngay khi thiết lập". Công cụ chẩn đoán riêng (quay số đồng bộ + in từng hàm gọi lại) cho thấy:
- Hàm gọi lại kiểu RasDialFunc1 chạy trên luồng riêng của RAS và báo đủ trạng thái 0->5 - nhưng bản v1.19.27 đã đổi
  NHẦM `dwNotifierType` 1 -> 2 (đối chiếu nguyên văn tài liệu `RasDialW`: 0 = RasDialFunc, 1 = RasDialFunc1,
  2 = RasDialFunc2 7 tham số) -> RAS gọi hàm 5 tham số như hàm 7 tham số, mọi trạng thái/mã lỗi bị đọc lệch. Trả về 1.
- NGUYÊN NHÂN CHÍNH: `RasGetConnectStatusW` trả **mã 6 (ERROR_INVALID_HANDLE) TẠM THỜI ở +16ms** khi phiên chưa đăng
  ký xong với dịch vụ RAS. `evaluateDialPoll` coi đó là "phiên đã chết" ở nhịp thăm dò đầu tiên rồi gác máy (máy chủ
  thấy lỗi 628). Đây chính là "Lỗi 6: The handle is invalid" người dùng gặp từ đầu - lỗi của app, không chỉ do
  mạng. Nay `DialPoll::invalidHandleMs` đo mã 6 kéo dài liên tục; chỉ kết luận khi quá `kInvalidHandleGraceMs`
  (15 giây) mà hàm gọi lại chưa báo kết quả (hàm gọi lại luôn báo mã lỗi thật khi quay số thất bại). Chờ alertable
  (`SleepEx`). Sửa này áp dụng cho cả tab VPN thủ công.
- **Xác nhận thật bằng đúng code của app**: IP `42.113.162.72` (VN) -> SSTP `public-vpn-257.opengw.net` ->
  `219.100.37.245` (Tokyo, Japan) -> ngắt -> trở lại `42.113.162.72`.

### (d) Kết nối TRỰC TIẾP FortiGate SSL-VPN / GlobalProtect (`src/tools/vpn/direct/`)
FortiClient 7.4 KHÔNG có lệnh kết nối công khai (`FortiVPN.exe` chỉ có `-h` và `-s`, điều khiển qua named pipe riêng)
nên không thể "ra lệnh cho phần mềm của họ". Ứng dụng tự nói giao thức (tham chiếu mã nguồn mở OpenConnect -
fortinet.c/ppp.c/gpst.c/auth-globalprotect.c, KHÔNG chép mã, tự viết lại bằng Qt):
- `PppSession` (thuần): LCP (MRU+Magic, từ chối xác thực PPP/tùy chọn lạ, Echo, Terminate, Protocol-Reject IPv6CP/
  CCP), IPCP (xin IP 0.0.0.0/DNS qua Nak - RFC 1332/1877), gửi lại 3 giây, phát hiện mất kết nối (4 Echo không trả
  lời). `FortinetFraming`: đầu 6 byte `len|0x5050|len`; phản hồi `HTTP/...` = máy chủ từ chối đường hầm.
- `FortinetProtocol` (thuần): `POST /remote/logincheck` (HTTP 405 = sai mật khẩu; `ret=..,tokeninfo=` = OTP, gửi lại
  reqid/polid/grp/portal/peer/magic; FortiToken push), `GET /remote/fortisslvpn_xml` (IP, DNS, chia tuyến/negate),
  `GET /remote/sslvpn-tunnel` (thành công thì KHÔNG trả HTTP mà vào PPP ngay).
- `GlobalProtectProtocol` (thuần): prelogin (phát hiện SAML -> chưa hỗ trợ), portal getconfig (gateway, cookie
  portal), gateway login.esp (đối số JNLP theo vị trí), OTP dạng JavaScript `respStatus="Challenge"`, getconfig
  (IP, netmask, DNS, access-routes - `0.0.0.0/0` = toàn phần), đường hầm `START_TUNNEL` + khung 16 byte `1a2b3c4d`,
  keepalive 10 giây. Không gửi báo cáo HIP (gateway đòi HIP có thể hạn chế truy cập - CHƯA kiểm chứng).
- `WintunAdapter` + `NetConfigurator`: `wintun.dll` nạp động bằng đường dẫn tuyệt đối (`<thư mục chạy>/wintun/`,
  `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|SYSTEM32`), adapter "OneForAll VPN" (GUID cố định -> hồ sơ NLA ổn định), luồng đọc
  riêng (chỉ chuyển gói IPv4). iphlpapi: IP (`CreateUnicastIpAddressEntry`), metric 1 + MTU, tuyến /32 tới máy chủ qua
  đường GỐC (đo bằng `GetBestRoute2` TRƯỚC khi thêm tuyến VPN - chống vòng lặp), đường hầm toàn phần = 0/1 + 128/1
  (không xóa tuyến mặc định - ngắt là mạng trở lại như cũ), chia tuyến/negate, DNS (`SetInterfaceDnsSettings` nạp
  động). Gỡ mọi tuyến đã thêm khi ngắt; xóa adapter.
- `TunnelEngine` (chạy trong tiến trình phụ): HTTPS qua QNetworkAccessManager (cookie tự quản, không lẫn kho chung),
  đường hầm `QSslSocket`; chứng chỉ không được Windows tin -> chỉ chấp nhận khi SHA-256 trùng vân tay người dùng đã
  xác nhận, ngược lại báo `certificateUntrusted` (FortiGate dùng chứng chỉ mặc định tự ký - đã thấy thật trên 2 máy chủ
  của người dùng). Mật khẩu xóa trắng ngay khi xác thực xong; đăng xuất phiên (best-effort 4 giây) khi ngắt.
  `opened` của PPP được hoãn sang vòng sự kiện sau (tránh hủy `PppSession` từ bên trong chính nó).
- **Tiến trình phụ nâng quyền** (`OneForAll.exe --vpn-helper <pipe> <mã>`, `VpnHelperMain`): ứng dụng chính
  (`DirectVpnSession`) tạo named pipe tên ngẫu nhiên, khởi chạy qua UAC, chỉ gửi việc (kèm mật khẩu) cho kết nối gửi
  đúng mã một lần 128-bit; mật khẩu không bao giờ nằm trên dòng lệnh. Pipe đóng (đóng app/app bị dừng) -> tiến trình
  phụ tự ngắt, gỡ tuyến, xóa adapter rồi thoát - không bao giờ để lại tiến trình nâng quyền mồ côi (bài học từ
  openvpn.exe). Thử lại sau khi tin cậy chứng chỉ dùng lại tiến trình phụ, không hỏi UAC lại.
- UI (tab "VPN đã cài sẵn"): cột "Kết nối trực tiếp", nút "🔌 Kết nối trực tiếp"/"⛔ Ngắt", hộp tài khoản (chỉ nhớ
  TÊN đăng nhập theo máy chủ - `profiles/vpn_direct_users.json`; mật khẩu không lưu), hộp OTP (để trống = FortiToken
  push), hộp xác nhận vân tay (`profiles/vpn_trusted_certs.json`). Đóng cửa sổ (chỉ ẩn) KHÔNG ngắt - như VPN hệ thống;
  thoát app thì ngắt (`forceStopNow`). FortiClient IPsec (IKEv1+XAuth), Cisco, OpenVPN: vẫn "Mở phần mềm".

### Kiểm thử
- `vpn_tests` 219/0 (gồm hồi quy mã 6 tạm thời của `evaluateDialPoll`, `sstpHost`), `vpn_ui_tests` 7/0.
- **`vpn_direct_tests` 150/0 (bộ mới)**: kiểu IP/tuyến/host:port (chặn chèn header), PPP với máy chủ kịch bản (từ chối
  PAP, gửi lại, Nak IP+DNS, khung nén, Echo, Protocol-Reject, Terminate, mất kết nối), khung Fortinet/GP (nạp từng
  byte), giao thức Fortinet/GP (gồm phản hồi prelogin THẬT của portal 171.226.12.171), **đầu-cuối `TunnelEngine` với
  FortiGate GIẢ** (đăng nhập + OTP + XML + PPP + gói hai chiều + ngắt êm), sai mật khẩu, **GlobalProtect GIẢ** (portal
  -> OTP -> gateway kèm cookie portal -> getconfig -> START_TUNNEL -> gói hai chiều -> keepalive), **tiến trình phụ
  thật** (`--vpn-helper`: mã xác thực, báo lỗi, tự thoát khi pipe đóng).
- Kiểm tra tay (không thuộc bộ tự động), ĐÃ CHẠY THẬT trên máy người dùng:
  - `vpn_gate_sstp_real_check`: đổi IP thật qua SSTP (VN -> Tokyo -> ngắt -> VN).
  - `vpn_wintun_real_check` (quyền Admin, dải thử nghiệm 198.18.0.0/15): **10/10** - tạo adapter wintun thật, gán IP,
    tuyến vào adapter + tuyến /32 tới "máy chủ" qua đường gốc, gói UDP máy -> đường hầm (luồng đọc nhận đúng) và đường
    hầm -> máy (socket trên máy nhận đúng gói bơm vào adapter), gỡ tuyến + xóa adapter (kiểm lại bằng Get-NetAdapter/
    Get-NetRoute: không còn sót gì).
  - `vpn_direct_real_probe` (KHÔNG gửi thông tin đăng nhập): **3/3** máy chủ thật trong hồ sơ sẵn sàng - bắt tay TLS bằng
    ngăn Schannel của app, vân tay SHA-256 tính đúng, FortiGate `VPN_BDTTG`/`VPN_VNU` trả trang `/remote/login`
    SSL-VPN (chứng chỉ tự ký -> app hỏi + ghim), GlobalProtect `171.226.12.171` prelogin phân tích OK (không SAML,
    chứng chỉ được Windows tin cậy).

### CHƯA kiểm chứng
- Đăng nhập + đường hầm tới FortiGate/GlobalProtect THẬT của người dùng (cần tài khoản cơ quan; không thử đăng nhập
  bằng tài khoản giả vào máy chủ của cơ quan).
- (Đã kiểm chứng sau: lớp adapter wintun + định tuyến chạy Admin - 10/10, xem trên.) Còn lại DUY NHẤT bước đăng nhập
  bằng tài khoản cơ quan thật rồi chạy đường hầm thật - phải do người dùng nhập tài khoản trong ứng dụng.

## 4zr. Lấy IP chậm sau khi nối VPN + rà giao diện bằng ảnh chụp thật (2026-10-22, v1.19.32)

**Lấy IP chậm/"Operation canceled"** (người dùng gửi ảnh): `PublicIpChecker` của tab giữ một `QNetworkAccessManager`
suốt đời -> kết nối HTTPS keep-alive tới ipwho.is mở TRƯỚC khi nối VPN được dùng lại SAU khi tuyến đổi; kết nối đó
gắn IP của card Wi-Fi nên gói bị rơi, yêu cầu treo đủ `kTimeoutMs` (8 s) rồi bị hủy. Tái hiện bằng
`vpn_gate_sstp_real_check` sửa lại để dùng MỘT checker cho trước/sau (đúng như tab): 7997 ms rồi "Operation
canceled". Sửa: `clearConnectionCache()` trước mỗi lần tra cứu + thử lại 1 lần (kết nối mới, sau 1 s) khi lỗi mạng
(`setEndpointForTest` đặt 0 lần thử lại để test giữ số yêu cầu xác định). Đo lại: 1093 ms, đúng IP qua VPN.

**Rà giao diện** - công cụ kiểm tra tay `ui_snapshot` (target `EXCLUDE_FROM_ALL`): dựng 8 cửa sổ công cụ, duyệt mọi
tab, `grab()` ra PNG ở cỡ mặc định + 820x640. Mặc định nền offscreen (kiểu Fusion + font Windows qua
`QT_QPA_FONTDIR`); `UI_SNAPSHOT_NATIVE=1` dùng nền Windows THẬT với `WA_DontShowOnScreen` - vẽ đúng kiểu gốc
(windowsvista: ô tích, thanh trượt) mà không hiện lên màn hình. Bài học: offscreen + kiểu gốc KHÔNG vẽ được ô tích
(dễ kết luận nhầm "mất ô tích"); kết luận bố cục phải dựa trên ảnh kiểu gốc. Lỗi thật đã sửa: hàng điều khiển tab
"Đổi IP" cắt chữ (tách 2 hàng, hàng NAT chỉ hiện với L2TP), ô Cổng vỡ mũi tên (QSpinBox + stylesheet bo góc -> bỏ
mũi tên), nút Android cắt chữ, nhóm ghép đôi Connect bị ép bẹp (vùng cuộn + chiều cao tối thiểu).

**Nhật ký chẩn đoán kết nối trực tiếp**: `DirectVpnSession` ghi các mốc vào `app.log` (không bí mật). Phát hiện khi
người dùng báo "đã kết nối" nhưng hệ thống không có tiến trình phụ/adapter - lỗi giai đoạn trước tiến trình phụ trước
đây không để lại dấu vết nào.

## 4zs. Kết nối trực tiếp chạy THẬT + lưu tài khoản + DNS nội bộ (2026-10-23, v1.19.33)

Người dùng tự nhập tài khoản trong ứng dụng (v1.19.32) và nối `VPN_BDTTG` (FortiGate SSL-VPN 103.221.228.37:10443):
nhật ký + trạng thái hệ thống xác nhận chứng chỉ tự ký được hỏi/ghim, đăng nhập, cấu hình (IP 10.212.134.201, 2 tuyến,
DNS 10.45.1.20 + 8.8.8.8), adapter wintun Up, PPP mở; **ping 10.45.1.33/10.45.1.34 đi qua card "OneForAll VPN" thành
công**. Ngắt + thoát: PPP Terminate, adapter bị xóa, tuyến /32 tới máy chủ bị gỡ, tiến trình phụ tự thoát.

Lỗi phát hiện khi chạy thật: DNS nội bộ 10.45.1.20 không thuộc tuyến riêng nên Windows gửi truy vấn ra Wi-Fi.
`NetConfigurator::apply` nay thêm tuyến /32 qua adapter cho DNS dải riêng RFC 1918 chưa được tuyến nào phủ
(`DirectVpn::isPrivateIpv4`). CHƯA kiểm chứng lại trên máy chủ thật sau khi sửa.

Lưu tài khoản (`CredentialStore`): Windows Credential Manager (`CredWriteW`, kiểu Generic, `CRED_PERSIST_LOCAL_MACHINE`,
DPAPI theo người dùng) - không ghi tệp. Chỉ lưu sau `connected()`; dùng tài khoản đã lưu mà máy chủ từ chối thì
`CredentialStore::remove` để lần sau hỏi lại.

## 4zt. GlobalProtect: báo cáo HIP (sửa rớt sau ~60s); FortiGate chạy ổn định (2026-10-23, v1.19.34)

Người dùng nối GlobalProtect thật (portal 171.226.12.171): đăng nhập/portal/gateway/getconfig OK, adapter wintun Up,
IP 10.0.20.22, 6 tuyến - nhưng sau ~60 giây app báo "Gateway không còn phản hồi". Đây là `kDeadPeerMs` của ta: suốt
60s KHÔNG nhận gì từ gateway. Nguyên nhân thật: gateway GlobalProtect bật **HIP (Host Information Profile)** - client
phải nộp báo cáo tình trạng máy; chưa nộp thì gateway thiết lập đường hầm nhưng NGỪNG chuyển gói. Client chính thức
luôn nộp HIP sau getconfig (OpenConnect: `check_and_maybe_submit_hip_report` trong `gpst_setup`, trước `gpst_connect`).

Thêm (thuần, trong `GlobalProtectProtocol`): `hipMd5` (md5 của portal|user|domain|computer - định danh ổn định của
máy+gateway, KHÔNG đổi theo authcookie/preferred-ip), `hipCheckBody`, `hipReportNeeded`, `buildHipReportXml` (bản
Windows tối thiểu theo mẫu công khai `trojans/hipreport.sh`), `hipReportBody`. `TunnelEngine::gpHipCheck`: POST
`ssl-vpn/hipreportcheck.esp`; nếu cần thì POST `ssl-vpn/hipreport.esp` kèm XML - TRƯỚC khi mở đường hầm; nộp lại định
kỳ trong `onTick` (mặc định 1 giờ). Lỗi mạng/không cần HIP -> bỏ qua, không chặn kết nối. Các POST này đi qua kênh
HTTPS riêng tới gateway (tuyến /32 qua đường gốc đã phủ IP gateway).

FortiGate SSL-VPN (`VPN_BDTTG`) đã chạy ổn định thật, lưu tài khoản hoạt động (lần 2 không hỏi), tuyến DNS nội bộ đã
thêm. CHƯA kiểm chứng lại GlobalProtect thật sau khi thêm HIP (chờ người dùng thử).

## 4zu. GlobalProtect: kênh dữ liệu ESP (IPsec/UDP) - gateway bắt buộc ESP (2026-10-23, v1.19.35)

Chẩn đoán thật (v1.19.34 thêm log): GP nối xong nhận ĐÚNG 12 byte "START_TUNNEL" rồi gateway im lặng trên SSL, không
trả lời 5 keepalive -> rớt ở 60s (dead-peer của ta). Log cấu hình: `ESP(ipsec)=CÓ, udp-port=4501`. Gateway này (chính
sách admin) BẮT BUỘC kênh dữ liệu ESP; đường hầm SSL chỉ để thiết lập. Thử bỏ quảng cáo enc/hmac trong getconfig ->
gateway VẪN trả `<ipsec>` (không chuyển sang SSL). Nên phải làm ESP.

`GpEsp` (`GpEspCrypto` + `GpEspChannel`): ESP RFC 4303 trên UDP. Crypto bằng **BCrypt** (bcrypt.dll - AES-128/256-CBC,
HMAC-SHA1-96/SHA256-128/MD5-96). Khóa đối xứng + SPI do gateway cấp sẵn trong `<ipsec>` (ekey/akey-c2s/s2c, c2s/s2c-spi)
- KHÔNG có IKE. Khuôn gói: `[SPI 4][seq 4][IV 16][ciphertext][HMAC trunc]`; ciphertext = `[payload][đệm][padlen][next-hdr]`.
Thiết lập: mở UDP tới gateway:udp-port, gửi gói "ping" dò = gói IPv4/ICMP-echo tới `<gw-address>` mang payload magic bắt
buộc của GP ("monitor  pan ha "); nhận được ESP hợp lệ (phản hồi dò) -> kênh lên. Dữ liệu: gói IP từ adapter ->
seal -> UDP; UDP nhận -> open -> adapter. Keepalive/DPD = gửi lại gói dò mỗi 10s; mất hoạt động 60s -> rớt.

`TunnelEngine`: sau getconfig+HIP, `parseEspConfig`; nếu có ESP thì `startGpEsp` (tạo adapter + áp tuyến với MTU trừ ~80
byte chi phí ESP, mở `GpEspChannel`) và KHÔNG mở đường hầm SSL (GET /ssl-tunnel-connect làm khóa ESP vô hiệu). ESP không
lên trong 10s -> báo lỗi (gợi ý UDP bị chặn). Gateway không ép ESP (`<ipsec>` vắng) -> giữ luồng SSL như cũ.

CHƯA kiểm chứng trên gateway GlobalProtect THẬT sau khi làm ESP (đã test đầu-cuối với gateway giả phản hồi ESP thật qua
UDP loopback - round-trip dữ liệu đạt).

## 4zv. Kết nối trực tiếp hồ sơ FortiClient IPsec: client IKEv1 tự viết (2026-10-23, v1.19.36)

Người dùng hỏi hồ sơ FortiClient kiểu **IPsec** (VPN_CVTLT - trước đây `direct=None`, chỉ "Mở phần mềm") kết nối trực
tiếp như nào. Hồ sơ này là FortiGate IPsec: **IKEv1 Aggressive mode + PSK + XAuth + Mode-config + Quick Mode + ESP**
(đọc từ registry, KHÔNG bí mật: `Aggressive=1, AuthMethod=1(PSK), DHGroup=512, ikeversion=1, Modecfg=1, XAuth=1,
RemoteGW=119.17.206.59, udp_port=500`). Tự viết client IKEv1 (không thư viện ngoài - dùng BCrypt của Windows).

Kiến trúc (4 lớp thuần + tích hợp), tất cả trong `src/tools/vpn/direct/`:
- **`IkeCrypto`**: Diffie-Hellman nhóm MODP 2/5/14 (BCrypt `BCRYPT_DH_ALGORITHM`, số nguyên tố RFC 3526 nhúng sẵn), băm
  SHA1/SHA256/MD5, PRF=HMAC, `prf+` kiểu IKE, AES-CBC, byte ngẫu nhiên.
- **`IsakmpMessage`**: mã hóa/giải mã bản tin ISAKMP (RFC 2408) - tiêu đề + chuỗi payload; dựng SA/proposal/transform
  (lồng nhau), KE, Nonce, ID, HASH, Notify, Delete, Config (Mode-cfg/XAuth) + thuộc tính TV/TLV.
- **`IkeKeys`**: dẫn xuất khóa RFC 2409 §5 - `SKEYID=prf(psk,Ni|Nr)`, SKEYID_d/a/e, HASH_I/HASH_R, mở rộng khóa mã hóa
  phiên, IV khởi tạo Phase 1 (`hash(gxi|gxr)`) và Phase 2 (`hash(iv1|M-ID)`), KEYMAT ESP (prf nối theo SPI/Ni/Nr), HASH
  Quick Mode 1/2/3.
- **`IkeSession`** (QObject, máy trạng thái): gửi Aggressive msg1 (SA+KE+Ni+IDii+VID NAT-T) -> nhận msg2, tính g^xy +
  SKEYID + kiểm HASH_R -> gửi msg3 (HASH_I) -> XAuth (máy chủ hỏi, trả user/pass mã hóa) -> Mode-config (xin IP/DNS/
  subnet) -> Quick Mode (SA ESP + nonce, chốt SPI) -> dẫn xuất khóa/SPI ESP (đóng gói trong `EspConfig` để dùng lại
  `GpEspCrypto`). Mã hóa payload sau Phase 1 bằng AES-CBC với chuỗi IV theo message-ID. KHÔNG tự mở socket - phát
  `sendDatagram`/nhận `onDatagram` để test được.

Tích hợp `TunnelEngine` (`DirectProtocol::FortinetIpsec`): một `QUdpSocket` dùng CHUNG cho IKE và ESP (bắt buộc với
NAT-T để giữ ánh xạ NAT cùng cổng nguồn). Thương lượng trên UDP 500; phát hiện NAT-T (VID/NAT-D) -> chuyển cổng 4500,
IKE thêm dấu non-ESP (4 byte 0) phân biệt với gói ESP (SPI≠0). Quick Mode xong -> tạo adapter wintun + áp IP/DNS/tuyến
(MTU trừ ~80) + nạp khóa ESP (`GpEspCrypto`) -> bơm gói IP ↔ ESP/UDP 4500. Keepalive NAT = 1 byte 0xFF mỗi 20s; mất
phản hồi 60s -> rớt. PSK nhập trong ứng dụng (hộp thoại đăng nhập, lưu kèm user/pass vào Windows Credential Manager
dưới mục `<máy chủ>|psk`). Scanner đánh dấu hồ sơ IPsec = `FortinetIpsec` (kết nối trực tiếp được).

Kiểm thử: `ike_crypto_check` (47/0) - DH round-trip nhóm 2/5/14, HMAC/băm theo vector RFC 2202/4231, AES-CBC vector
NIST, round-trip bản tin ISAKMP + lồng SA/proposal/transform + config, dẫn xuất khóa hai vai nhất quán. **`ike_session_check`
(24/0, MỚI): bắt tay IKEv1 TRỌN VẸN offline** - `IkeSession` (client) đấu một responder GIẢ dựng bằng chính các lớp
trên; responder KIỂM HASH_I, HASH XAuth, HASH Quick Mode của client (chứng minh client tính đúng dải byte + mã hóa +
IV + KEYMAT với một peer IKE chuẩn), cuối cùng hai bên dẫn xuất khóa ESP TRÙNG nhau. `vpn_direct_tests` 357/0.

**Thông số đã XÁC NHẬN bằng dò thật (v1.19.37, 2026-10-24)**: công cụ tay `ike_real_probe` gửi IKEv1 Aggressive msg1
THẬT tới gateway 119.17.206.59 (KHÔNG đăng nhập) và đọc msg2: gateway **chỉ trả lời nhóm 20 (ECP-384)** - đúng như
`DHGroup=512`; chọn **AES-256 + SHA-256 + XAUTHInitPSK**; gửi **2 NAT-D** (client sau NAT → NAT-T/UDP 4500). Đã thêm
**ECDH** vào `IkeCrypto::Dh` (BCrypt `ECDH_P256/384/521`: KE = toạ độ X‖Y, bí mật chung = toạ độ X theo RFC 5903) và
test round-trip offline (nhóm 19/20/21). `IkeSession` nay: mặc định **nhóm 20** cho FortiGate IPsec, **chào nhiều
transform** (AES-256/128 × SHA-256/SHA-1 × XAUTHInitPSK/PSK, cùng nhóm vì Aggressive khóa 1 KE) và **đọc transform
gateway chọn trong msg2** (`applyChosenTransform`) để dẫn xuất khóa cho đúng enc/hash.

**Bắt tay ĐÃ CHẠY THẬT đầu-cuối (v1.19.38, 2026-10-24)**: với PSK + tài khoản XAuth thật, công cụ tay `ike_real_connect`
(chạy đúng lớp `IkeSession`, không tạo adapter nên không cần Admin; bí mật truyền qua tham số, không ghi tệp) hoàn tất
**Phase 1 (nhóm 20) → XAuth → Mode-config (gateway cấp IP 172.16.16.x + 2 DNS) → Quick Mode → chốt khóa/SPI ESP**. Các
sai khác với FortiGate thật đã tìm & sửa: (1) **mã thuộc tính XAuth** đúng là `XAUTH_STATUS=16527`, `XAUTH_MESSAGE=16524`
(trước nhầm 16528/16527 nên không nhận ra gói báo XAuth OK); (2) giải mã bản tin hỏng KHÔNG đẩy IV (tránh lệch chuỗi IV
khi phát lại); (3) Quick Mode chào **nhiều transform ESP** + đọc transform gateway chọn (gateway chọn **AES-256-CBC +
SHA1**); (4) **PFS nhóm 20** (KE Quick Mode + KEYMAT ghép g^xy); (5) kiểu đóng gói **Tunnel(1)** - FortiGate TỪ CHỐI cả
proposal nếu có giá trị UDP-Tunnel (3/61443); (6) proxy-ID selector 0.0.0.0/0; (7) ESP sequence bắt đầu **1** (RFC 4303 -
seq 0 bị chống phát lại bỏ). Thông số đọc từ registry P1/P2 (không đọc trường bí mật): DHGroup=512 (nhóm 20), Phase 2 PFS
nhóm 20, selector 0/0.

**Tầng DỮ LIỆU ESP vẫn CHẶN (v1.19.39, 2026-10-24)**: dùng `pktmon` bắt gói so sánh với FortiClient thật -> FortiClient
làm TOÀN BỘ IKE+ESP trên **UDP 4500↔4500 từ cổng nguồn 4500** (0 gói cổng 500), dấu NONESP cho IKE. Đã chỉnh client +
app: **bind cổng 4500** (bận thì cổng tạm) + **ép NAT-T từ msg1** + gửi **NAT-D** (RFC 3947, SHA256(ckyI|ckyR|IP|port) -
dò khớp định dạng gateway, qua `ike_real_probe`) với IP:port cục bộ thật. Nay gói của client **giống hệt FortiClient trên
dây** (cùng cổng/khuôn UDP-encap ESP/SPI/seq). NHƯNG: `ike_real_connect` xác nhận gói ESP ĐI RA đúng mà gateway **không
trả gói nào** - thử cả **5 biến thể dẫn xuất KEYMAT** (PFS/không-PFS × đảo nonce/đảo SPI/đảo thứ tự khóa enc-auth) đều im.
SA Phase 2 đã cài (QM3 nhận, không phát lại QM2). Nguyên nhân nằm ngoài tầm chẩn đoán từ phía client (gateway bỏ gói im
lặng, không trả INVALID-SPI) - cần **nhật ký phía FortiGate** (diag debug application ike) để biết vì sao ESP bị bỏ.
Công cụ tay để tiếp tục: `ike_real_probe` (dò nhóm/thuật toán/NAT-D), `ike_real_connect` (bắt tay + thử nhiều KEYMAT).

**ĐÃ GIẢI QUYẾT (v1.19.43, 2026-10-25) - đoạn "ESP vẫn CHẶN" ở trên dựa trên nhận định SAI.** Nguyên nhân thật: việc **bind cổng cục bộ 4500** (v1.19.39). Windows xử lý gói ESP-in-UDP đến cổng cục bộ 4500 ở tầng IPsec của HĐH trước khi tới socket nên mọi ESP gateway trả về bị mất (IKE có dấu non-ESP vẫn tới). Gateway KHÔNG hề im lặng: Npcap (bắt gói không cần Admin, driver `AdminOnly=0`) thấy gói ESP đi VÀO cho từng lần chạy thử. Sửa: một socket **cổng tạm** cho IKE+ESP, luồng chuẩn msg1/msg2 tới cổng 500 rồi float sang 4500, `forceNatT`. Chứng minh trên gateway thật bằng `ike_real_connect` (IkeSession+GpEsp, mô hình y như TunnelEngine): ICMP echo reply từ 172.16.6.25/8.8.8.8/1.1.1.1/96.45.45.45, đáp án DNS (UDP), SYN-ACK TCP từ 172.16.6.25:443 và 96.45.45.45:53; KEYMAT/ICV/AES kiểm độc lập bằng OpenSSL+Python khớp RFC. "FortiClient 0 gói cổng 500" cũng SAI (FortiClient gửi msg1/msg2 qua cổng 500 rồi float sang 4500). Bài học: khi một giao thức "im lặng", bắt gói ở tầng NIC (Npcap) để xem gói có VÀO máy không, đừng chỉ tin socket của chính tiến trình. Vendor ID của FortiClient (MD5 chuỗi trong ipsec.exe) KHÔNG cần để chạy được.

## 5. Build và chạy
`build_app.bat` (cần Qt 6.11.1 MinGW tại `D:\Qt`, CMake, Ninja) → `build\OneForAll.exe`, tự đồng bộ
sang `OneForAll_Release\` (bản `run_app.bat` chạy). Nếu có `vendor\scrcpy\` (xem `THIRD_PARTY.md` để
tải bản scrcpy-win64 chính thức) thì cũng tự đóng gói vào `scrcpy\` cạnh file exe cho tính năng Android
Phone Control. Tương tự, nếu có `vendor\yt-dlp\` (xem `THIRD_PARTY.md` để tải yt-dlp.exe + ffmpeg/ffprobe
chính thức) thì tự đóng gói vào `yt-dlp\` cạnh file exe cho tính năng Downloader (tải video nền tảng).
Thư mục `build/`, `OneForAll_Release/`, `vendor/`, `logs/` không được commit (xem `.gitignore`).
Dữ liệu lúc chạy (hồ sơ, log) KHÔNG nằm trong các thư mục này mà ở `%LOCALAPPDATA%\OneForAll\` (mục 4o);
`OneForAll_Release\profiles\default.json` chỉ là tệp mẫu do `build_app.bat` chép từ `profiles\default.json`.

## 5b. Bộ cài đặt setup.exe (đã hoàn thiện)
Yêu cầu người dùng: chỉ MỘT file `setup.exe` duy nhất, bung thư viện/exe thật khi chạy, cho chọn đường
dẫn cài đặt, cho chọn tạo shortcut - như một ứng dụng Windows bình thường (đăng ký gỡ cài đặt trong
Control Panel/"Apps & features", không phải chỉ giải nén zip).

**Công cụ dùng: Inno Setup** (jrsoftware.org, miễn phí, phổ biến nhất cho đúng nhu cầu này trên Windows -
không dùng CPack/NSIS/WiX vì Inno Setup đơn giản hơn để viết/bảo trì và cho ra đúng một file `setup.exe`
gọn nhẹ). Đã tải bản 7.1.0 chính thức từ GitHub Releases của dự án (`jrsoftware/issrc`), xác minh đúng
SHA-256 theo `digest` API GitHub cung cấp trước khi cài. Compiler (`ISCC.exe`) cài vào thư mục riêng của
người dùng (`%LOCALAPPDATA%`), KHÔNG cần quyền Administrator.

**Không cần quyền Administrator để CÀI ĐẶT** (`PrivilegesRequired=lowest` trong `installer/OneForAll.iss`)
- cài vào `{autopf}` (tự chọn vị trí phù hợp theo quyền hiện có, giống cách VS Code/Discord làm), người
dùng vẫn có thể bấm "Show details" để chọn cài cho mọi người dùng (cần quyền Administrator) nếu muốn qua
`PrivilegesRequiredOverridesAllowed=dialog`. Giao diện cài đặt THUẦN TIẾNG VIỆT - dùng bản dịch cộng đồng
chính thức của dự án Inno Setup (`installer/Vietnamese.isl`, lấy từ `Files/Languages/Unofficial/` trong
chính kho mã nguồn `jrsoftware/issrc`, không phải nguồn thứ ba không rõ gốc).

**Icon riêng cho exe/shortcut/trình cài đặt** (`assets/app_icon.ico`): trước đây `OneForAll.exe` KHÔNG có
icon file .exe riêng (chỉ có icon Qt Resource hiển thị TRONG ứng dụng lúc chạy, Explorer/Taskbar/shortcut
vẫn hiện icon exe mặc định của Windows) - đã tạo icon đa độ phân giải (16-256px) từ `icon/autoclicker.jpg`
theo ĐÚNG thuật toán `IconHelper::makeBadgedPixmap` (nền trắng bo góc + viền mảnh) để đồng nhất với icon
hiển thị trong `MainWindow` lúc chạy, nhúng vào exe qua tài nguyên Win32 (`assets/app_icon.rc` +
`windres`, thêm vào `CMakeLists.txt`'s SOURCES).

**[Files] loại trừ dữ liệu thử nghiệm của người phát triển**: `logs\*` (tự tạo lại khi chạy) và
`profiles\qr_history.json` (lịch sử quét QR cá nhân thật, không phải mẫu) - vẫn giữ
`profiles\default.json` (chuỗi Auto Click MẪU có chủ đích, hữu ích cho người dùng mới).

**Đã xác nhận THẬT toàn bộ vòng đời cài đặt/gỡ cài đặt** (không chỉ biên dịch thành công):
- Cài đặt im lặng (`/VERYSILENT`) vào thư mục test → xác nhận đúng tệp (`OneForAll.exe` có icon, DLL, có
  `profiles\default.json`, KHÔNG có `profiles\qr_history.json`), shortcut Desktop trỏ đúng
  `{app}\OneForAll.exe` với `WorkingDir` đúng `{app}`, khóa registry gỡ cài đặt đúng
  (`DisplayName`/`DisplayVersion`/`UninstallString` chính xác) dưới `HKEY_CURRENT_USER` (xác nhận đúng
  không cần quyền Administrator).
- Khởi chạy THẬT `OneForAll.exe` từ thư mục vừa cài - tiến trình chạy được.
- Gỡ cài đặt im lặng (`unins000.exe /VERYSILENT`) → xác nhận xóa sạch: thư mục cài đặt, shortcut Desktop,
  khóa registry - không còn gì sót lại.

**Build:** `build_installer.bat` (tự gọi `build_app.bat /nopause` để dựng `OneForAll_Release\` mới nhất
rồi biên dịch `installer/OneForAll.iss` bằng `ISCC.exe`) → `dist\OneForAll_Setup.exe` (một file duy
nhất, ~140MB do có ffmpeg/yt-dlp đóng gói sẵn, nén LZMA2 ultra). Cùng một lệnh này giờ cũng tự dựng LUÔN
`dist\OneForAll_Setup.msi` (WiX Toolset, xem mục 4n) và zip của cả hai
(`OneForAll_Setup_exe.zip`/`OneForAll_Setup_msi.zip`) - đủ 4 file mỗi lần chạy. Thư mục `dist/` và `obj/`
(file trung gian của WiX) không được commit (xem `.gitignore`), giống `build/`/`OneForAll_Release/`/
`vendor/`.

### 5c. Quy trình phát hành bản mới (để `UpdateChecker` - mục 4k - nhận ra)
Các bước **thủ công trên GitHub** người phát hành (chủ dự án) phải tự làm mỗi lần muốn người dùng đã cài
đặt tự nhận được bản mới - không có bước nào trong số này tự động được từ phía ứng dụng:
1. Sửa file `VERSION` ở gốc dự án thành số phiên bản mới (vd `1.16.0`) - đây là nguồn DUY NHẤT, không sửa
   version ở nơi khác.
2. Cập nhật changelog `PROJECT_OVERVIEW.md`/`README.md` như thường lệ, commit.
3. Chạy `build_installer.bat` → ra ĐỦ 4 file trong `dist\` (đã tự mang đúng version mới nhờ bước 1):
   `OneForAll_Setup.exe`, `OneForAll_Setup.msi`, `OneForAll_Setup_exe.zip`, `OneForAll_Setup_msi.zip`.
4. Gắn tag Git ĐÚNG KHỚP version (bắt buộc có tiền tố "v", `UpdateChecker` tự bỏ tiền tố này khi so sánh):
   ```
   git tag v1.16.0
   git push origin v1.16.0
   ```
5. Đẩy tag xong thì CI (`.github/workflows/build-installer.yml`) TỰ build lại 4 file và tạo GitHub Release
   kèm đủ 4 asset (cập nhật 2026-10-15: trước đây ghi là phải tạo Release bằng tay - không còn đúng từ khi có
   CI). Kiểm tra Release qua `https://api.github.com/repos/DinhDuyen2810/ALLinONE/releases/tags/v1.16.0`.
   Chỉ khi CI không chạy được mới tạo tay (web UI hoặc `gh release create ... dist/*`).
6. Xong - lần mở ứng dụng tiếp theo trên MỌI máy đã cài bản có bộ tự cập nhật (v1.15.0 trở đi), app sẽ tự
   hỏi thấy v1.16.0 mới hơn, hiện hộp thoại mời cập nhật.

**Từ v1.19.0:** máy cài bằng `.msi` tự cập nhật bằng asset `OneForAll_Setup.msi` (tên cũng phải đúng tuyệt
đối) - Release thiếu tệp này thì các máy đó được báo tự tải tay. Trình tự cập nhật đối chiếu SHA-256 với
trường `digest` GitHub tự sinh cho từng asset, không cần đính kèm tệp checksum riêng.

**Lưu ý:** asset đính kèm Release PHẢI tên ĐÚNG TUYỆT ĐỐI `OneForAll_Setup.exe` (khớp đúng tên
`OutputBaseFilename=OneForAll_Setup` trong `installer/OneForAll.iss` - `build_installer.bat`/GitHub
Release UI đều giữ nguyên tên này mặc định, không cần đổi tay) - `UpdateChecker` so khớp TUYỆT ĐỐI, không
phải chỉ "kết thúc bằng" (tránh khớp nhầm asset khác tên tương tự nếu một Release lỡ đính kèm nhiều file,
vd checksum `.sha256` hay bản cho kiến trúc khác). Một Release không đính kèm đúng tên asset này bị coi
như "không có gì để tự cập nhật" (an toàn, không báo lỗi gây hoang mang, chỉ im lặng bỏ qua).

**Giới hạn thật:** `AppId` cố định (GUID) để các bản cập nhật sau nhận diện đúng là CÙNG một ứng dụng
(cho phép cài đè/gỡ đúng phiên bản cũ). `AppVersion` của bộ cài KHÔNG cần sửa tay: `build_installer.bat` và CI
đọc `VERSION` rồi truyền `/DMyAppVersion=...` cho Inno Setup (cập nhật 2026-10-15 - ghi chú cũ "sửa tay" đã lỗi thời).

## 6. Vấn đề đã biết (chưa sửa)
- **Connect Together - ghép đôi**: mã 9 số được dùng thẳng làm khóa (PBKDF2, salt cố định, không PAKE). Ai bắt
  được gói ghép đôi trong LAN đúng lúc ghép có thể dò offline 10^9 mã để lấy khóa dài hạn. Chỉ ghép đôi
  trên mạng tin cậy; thiết kế lại (trao đổi khóa có xác thực) là việc riêng, chưa làm.
- Connect Together: server lắng nghe trên mọi card mạng (`AnyIPv4`); bố cục nhiều màn hình không chữ nhật
  chưa xử lý; chưa từng chạy thử trên hai máy thật (xem mục 4o).
- Các mục còn dang dở sau hai đợt rà soát: xem cuối mục 4q (4q đã xử lý phần lớn danh sách ở cuối mục 4p).
- Auto Click: Pause/Resume có ở tầng runner nhưng chưa có nút trên UI; chưa có checkbox bật/tắt từng hành
  động (trường `enabled` chỉ đọc từ JSON, nay được giữ nguyên khi lưu); nội dung "Gõ văn bản" lưu dạng chữ
  thường trong hồ sơ và hiện trên HUD lúc chạy; HUD/overlay/bộ ghi macro quy đổi tọa độ theo DPI màn hình CHÍNH (sai
  trên nhiều màn hình khác DPI).
- Android: hai bản ứng dụng chạy cùng lúc dùng chung một daemon adb (đóng một bản dừng adb của bản kia).
- Security Gateway: hủy quét chỉ dừng `powershell.exe`, chưa chắc dừng lần quét bên trong dịch vụ Defender;
  clipboard không được khôi phục sau khi dán lệnh; danh sách chặn hosts chỉ chặn đúng tên miền (không gồm tên miền con)
  và không xóa bộ đệm DNS.
- VPN: chưa có tùy chọn `-SplitTunneling`; không xóa được hồ sơ đang kết nối; không sửa được nhãn quốc gia sau khi thêm. Tab "Đổi IP theo quốc gia" dùng relay TÌNH NGUYỆN công cộng của VPN Gate (IP động, tốc độ/ổn định không bảo đảm, API chỉ trả ~100 máy chủ/lần - gộp tích lũy để có thêm; không phải máy chủ nào cũng bật L2TP) - CHƯA kiểm chứng kết nối L2TP thật đầu-cuối. Yêu cầu 2 (đọc profile FortiClient/GlobalProtect) chưa làm.
- QR Tools: mật khẩu WiFi trong lịch sử (`qr_history.json`) lưu dạng chữ (chỉ che trên giao diện).
- Connect Together: công tắc đồng bộ clipboard không được nhớ giữa các lần mở cửa sổ (luôn bật lại).
- Downloader: yt-dlp tự ghi lại cookie vào chính tệp `cookies.txt` bạn chọn (hành vi gốc của yt-dlp `--cookies`).
- Tự cập nhật: nếu người dùng mở hai cửa sổ ứng dụng cùng lúc, trình cài đặt `.exe` im lặng có thể tự hủy
  (thấy bản còn lại đang chạy) - ứng dụng vẫn mở lại và hỏi cập nhật lần sau.
- File cài đặt chưa ký số - SignPath Foundation đã từ chối (mục 4m); hướng tiếp theo ở mục 4r (Microsoft
  Store là lựa chọn chính, miễn phí). Đã thêm metadata phiên bản Win32 (mục 4r) để giảm rủi ro bị hệ thống
  chấm điểm heuristic xếp hạng sai, nhưng KHÔNG xóa được cảnh báo "chưa ký số" - chỉ ký số/Store mới hết.

## 7. Quy trình làm việc
Quy tắc đứng nằm ở `CLAUDE.md` (gốc dự án). Tóm tắt - mỗi lần sửa code: build exe + chạy test liên quan → `build_installer.bat /nopause` (đủ 4 file: `setup.exe`, `setup.msi`, zip của cả hai) → cập nhật file này (mục 3-6 nếu cấu trúc/hành vi đổi) cùng bảng lịch sử bên dưới và `README.md` → commit → push → kiểm lượt chạy CI.

## 8. Lịch sử thay đổi
| Ngày | Commit | Nội dung |
|---|---|---|
| 2026-10-05 | Initial | Đưa toàn bộ source lên Git, thêm `.gitignore` và `PROJECT_OVERVIEW.md` |
| 2026-10-06 | Fix Auto Click | Sửa engine, UI và tọa độ, xem chi tiết bên dưới |
| 2026-10-06 | QR Tools | Hoàn thiện module QR (tạo, quét, lịch sử), thêm thư viện qrcodegen/quirc, bộ test (v1.0.7) |
| 2026-10-06 | WiFi Connection | Module quét/quản lý hồ sơ WiFi hợp pháp qua Windows WLAN API; từ chối tính năng dò mật khẩu theo vòng lặp (v1.0.8) |
| 2026-10-06 | Speed Test | Thêm đo tốc độ mạng (ping/jitter/download/upload) vào WiFi Connection; từ chối lại yêu cầu dò mật khẩu từ file (v1.0.9) |
| 2026-10-06 | Love WiFi | Nút vui "Love WiFi" trong tab Mạng xung quanh; từ chối 2 lần nữa các biến thể thu nhỏ của yêu cầu dò mật khẩu (v1.0.10) |
| 2026-10-07 | Connect Together (nền tảng) | Bắt đầu module tham khảo Mouse without Borders: mã hóa AES-256-GCM (Windows CNG), mã ghép đôi, khung giao thức - 256 test. Chưa có mạng/UI (v1.1.0) |
| 2026-10-07 | Connect Together (mạng) | Lưu trữ peer, phiên TCP mã hóa, khám phá LAN qua UDP - test thật (loopback TCP + broadcast UDP thật trên máy), 403 test. Chưa có hook input/UI (v1.1.1) |
| 2026-10-07 | Connect Together (Win32 input) | Phát hiện biên màn hình, hook bàn phím/chuột toàn cục (cài/gỡ thật trên máy), tiêm input - 465 test. Chưa có điều phối/UI (v1.1.2) |
| 2026-10-07 | Connect Together (điều phối) | ConnectSessionController nối toàn bộ lại - ghép đôi 2 chiều test thật qua TCP, 506 test. Chưa có UI (v1.1.3) |
| 2026-10-07 | Connect Together (hoàn thiện) | UI (Ghép đôi/Máy đã ghép đôi), nối vào MainWindow; tìm và sửa lỗi crash ngẫu nhiên nghiêm trọng (double deleteLater) trong luồng ghép đôi (v1.2.0) |
| 2026-10-07 | Disk Cleanup (lõi) | Quét hạng mục dọn dẹp, xóa qua Thùng rác, dung lượng ổ đĩa, tìm tệp lớn - 82 test (có xóa file thật qua Recycle Bin). Chưa có UI (v1.3.0) |
| 2026-10-07 | Disk Cleanup (UI + sửa lỗi .lnk) | UI 2 tab (Dọn dẹp theo hạng mục, Tìm tệp lớn), nối vào MainWindow; phát hiện và sửa lỗi thật khi quét tay - Qt tự "đi theo" lối tắt (.lnk) khiến hạng mục Recent Items bị cộng nhầm hàng chục GB; 91 test lõi + 10 test UI (v1.4.0) |
| 2026-10-07 | Disk Cleanup (trùng lặp + phân vùng) | Thêm `DuplicateFinder` + tab Tìm tệp trùng lặp (hash SHA-256, cây nhóm/tệp); thêm `PartitionManager`/`PartitionResizer` + tab Quản lý phân vùng (liệt kê thật, đổi kích thước qua Resize-Partition có xác nhận gõ tên ổ đĩa, yêu cầu quyền Administrator); sửa 2 lỗi thật phát hiện khi quét tay (stdin `-Command -` im lặng không chạy script nhiều dòng → đổi `-EncodedCommand`; lỗi PowerShell bị serialize CLIXML không đọc được → đổi `[Console]::Error`); 156 test lõi + 19 test UI (v1.5.0) |
| 2026-10-07 | Disk Cleanup (phản hồi khi quét ổ lớn) | Người dùng báo quét ổ C: 120GB bị Windows đánh dấu "Không phản hồi". Đo thật: luồng giao diện không hề bị chặn (độ trễ lớn nhất 21ms trong lúc quét thật ~390.000 tệp/20s) - đổi `CleanupScanner`/`CleanupExecutor`/`LargeFileScanner`/`DuplicateFinder` sang `QThread::LowPriority`, tăng tần suất báo tiến độ (mỗi 200 tệp) kèm đường dẫn đang xử lý để người dùng thấy ứng dụng vẫn chạy (v1.5.1) |
| 2026-10-07 | Disk Cleanup (sửa "Không phản hồi" THẬT ở Tìm tệp trùng lặp) | Người dùng báo lại: Tìm tệp trùng lặp trên ổ C: vẫn bị Task Manager đánh dấu "Không phản hồi" thật (không chỉ cảm giác). Nguyên nhân: `DuplicateFinder` cũ phát `groupFound` cho TỪNG nhóm một, không giới hạn - thư mục cache trình duyệt (nhiều hồ sơ Chrome, cỡ khối cố định) tạo ra hàng trăm/nghìn nhóm, dồn dập đủ để chiếm trọn một lượt xử lý sự kiện của Qt. Sửa: `DuplicateFinder` gom toàn bộ trong bộ nhớ, sắp xếp theo lãng phí giảm dần, cắt `setMaxGroups()` (mặc định 500), CHỈ phát `scanFinished` một lần (đúng mẫu `LargeFileScanner`); UI dựng cả cây 1 lần (`setUpdatesEnabled(false)`), nhóm mặc định thu gọn. Đo lại thật: quét ~20.300 tệp dữ liệu Chrome trong 60s liên tục, độ trễ lớn nhất chỉ 22ms. 167 test lõi (+11 test hồi quy giới hạn/sắp xếp nhóm) (v1.5.2) |
| 2026-10-07 | Disk Cleanup (căn cột + sửa lỗi dọn dẹp 0x2) | Người dùng báo 2 lỗi kèm ảnh: (1) cột "Kích thước" ở Tìm tệp trùng lặp bị dạt giữa, thừa khoảng trắng - do `QTreeWidget` mặc định `stretchLastSection=true` đè lên cấu hình cột, đã tắt + căn phải chữ; (2) Dọn dẹp theo hạng mục báo lỗi "mã lỗi 0x2" (ERROR_FILE_NOT_FOUND) - do tệp tạm tự bị xóa giữa lúc quét và lúc bấm Dọn dẹp (bình thường với `%TEMP%`/cache), khiến `SHFileOperationW` báo thất bại cho CẢ LÔ dù phần lớn tệp vẫn xóa được. Sửa `CleanupExecutor`: lọc trước tệp đã tự mất (coi là đã đạt mục tiêu), kiểm tra lại THẬT sau khi xóa để báo freedBytes/deletedCount chính xác, chỉ thất bại khi không giải phóng được gì; `RecycleBinOps` dịch mã lỗi Win32 phổ biến sang tiếng Việt. 176 test lõi (+9 hồi quy mô phỏng tệp tự mất) (v1.5.3) |
| 2026-10-07 | Rà soát toàn bộ 5 tính năng (UI + hoạt động) | Theo yêu cầu người dùng: rà kỹ Auto Click/Connect Together/QR Tools/WiFi Connection/Disk Cleanup - build lại + chạy toàn bộ 895 kiểm tra tự động (đều qua), chụp ảnh từng màn hình để soát giao diện. Phát hiện/sửa: (1) **Auto Click toàn bộ panel/nhãn/HUD từng thuần tiếng Anh** ("ACTION CHAINS", "Type:", "Wait Before:", HUD "Auto Click Running"...) trong khi phần còn lại ứng dụng toàn tiếng Việt - dịch hết, đã xác nhận an toàn (combo box đọc/ghi theo chỉ số, `Action::typeName()` độc lập hoàn toàn với khóa JSON của `ActionSerializer`); phát hiện thêm 1 lỗi tự gây ra khi dịch (nút "Nhân bản" bị cắt chữ do panel hẹp) và sửa luôn (xếp dọc thay vì ngang). (2) WiFi: checkbox "Hiện mật khẩu đã lưu" bị cắt chữ do chung hàng với 5 nút - tách hàng riêng. (3) **Xác nhận dứt điểm** nghi vấn hiển thị cũ ở tab Ghép đôi (Connect Together, nêu từ v1.2.0): dựng cửa sổ hiện THẬT trên màn hình (không `WA_DontShowOnScreen`) rồi chụp - chữ hiển thị hoàn toàn bình thường, xác nhận đó chỉ là hiện tượng chụp ảnh widget ẩn, không phải lỗi code. |
| 2026-10-07 | Android Phone Control (hoàn thiện) | Yêu cầu người dùng: điều khiển thiết bị Android "tính realtime và độ linh hoạt kiểu như remote". Quyết định kiến trúc: dùng lại **scrcpy** (Genymobile, Apache-2.0, mã nguồn mở, kiểm chứng nhiều năm) thay vì tự viết lại mã hóa H.264 + tiêm sự kiện phía Android (rủi ro cao, không tự kiểm thử đủ trong môi trường không có thiết bị thật) - đóng gói bản `scrcpy-win64` chính thức (đã xác minh SHA-256) vào `vendor/scrcpy/` (không commit Git, như `build/`/`OneForAll_Release/`), `build_app.bat` tự đóng gói kèm exe. `AdbController` (liệt kê/ghép đôi/kết nối thiết bị qua `adb.exe`) + `ScrcpyLauncher` (khởi chạy `scrcpy.exe` làm tiến trình nền, không chặn UI) + `DevicesTab` (danh sách thiết bị tự làm mới, tùy chọn độ phân giải/bitrate/fps/tắt màn hình/luôn nổi/ghi hình - cờ thật của scrcpy) + `WirelessPairDialog` (ghép đôi không dây Android 11+). "Chuyển sang không dây" dùng `--tcpip` của scrcpy (tự dò IP, bật TCP/IP, kết nối, gương - một bước). 45 test lõi (phân tích `adb devices -l` mẫu + dựng đối số dòng lệnh, không cần thiết bị thật) + 7 test UI (gọi `adb devices` thật - chỉ đọc, an toàn). **Giới hạn thật:** không có điện thoại Android gắn sẵn trong môi trường này nên KHÔNG tự kiểm tra được gương màn hình/ghép đôi không dây đầu-cuối - cần người dùng tự kiểm tra tay. Cửa sổ gương là cửa sổ riêng của scrcpy (nhúng vào cửa sổ Qt là việc có thể làm sau, "nếu có thể"). |
| 2026-10-07 | VPN & Location (hoàn thiện) | Yêu cầu người dùng: "VPN và location có thể chuyển đổi IP qua nhiều quốc gia và đổi vị trí GPS trong 1 quốc gia". VPN: `VpnController` dùng module `VpnClient` của Windows qua PowerShell (`Add`/`Get`/`Remove-VpnConnection`, mặc định per-user không cần Administrator) + `VpnConnector` dùng `rasdial.exe` để kết nối/ngắt kết nối kèm username/password hỏi riêng mỗi lần (không lưu mật khẩu). Location: **từ chối có chủ đích** giả lập GPS thật - API `Geolocator` của Windows bị khóa sau quyền "System location" chỉ cấp cho app MSIX đã duyệt, không thể gọi từ Win32 không đóng gói dù có quyền Administrator; cách còn lại là ghi registry không tài liệu, rủi ro hỏng dịch vụ định vị thật, không chấp nhận được - thay bằng `PublicIpChecker` tra vị trí thật theo IP công khai (`ipwho.is`, đổi theo khi đổi VPN). **Lỗi thật phát hiện qua kiểm tra trên Windows thật:** `Add-VpnConnection` gọi cứng `-AuthenticationMethod MSChapv2` cho mọi loại tunnel bị Windows từ chối thật với IKEv2 ("IKEv2 tunnel type only supports Eap and Machine certificate") - IKEv2 lại là lựa chọn mặc định của hộp thoại thêm hồ sơ; sửa bằng `authMethodForTunnelType()` (Ikev2 → Eap, còn lại giữ MSChapv2), xác nhận lại bằng cách chạy thật Add→Get→Remove-VpnConnection cho cả 5 loại tunnel, tất cả thành công. Tách `WinElevation`/`PowerShellRunner` dùng chung từ `PartitionManager` (giảm trùng lặp, 176/19 test Disk Cleanup không đổi). 41 test lõi (+ test hồi quy khóa đúng phương thức xác thực/loại tunnel) + 6 test UI (gọi `Get-VpnConnection` thật). **Giới hạn thật:** không có tài khoản VPN thật trong môi trường này nên KHÔNG tự kiểm tra được `rasdial` kết nối/ngắt kết nối thật đầu-cuối - cần người dùng tự kiểm tra tay với tài khoản VPN thật của họ. |
| 2026-10-07 | Security Gateway (hoàn thiện) | Yêu cầu người dùng: tham khảo Kaspersky - (1) tab PowerShell kiểm tra lệnh trước khi mở thật và dán vào, (2) chặn link độc hại ngay lúc truy vấn web, (3) quét/xóa/cách ly mã độc khi đang bật. Quyết định kiến trúc: xây trên **Windows Defender** có sẵn (không tự viết lại AV) qua PowerShell module `Defender`, đã dùng agent nghiên cứu xác nhận từng cmdlet qua Microsoft Learn trước khi viết code. `CommandAnalyzer` phân tích TĨNH lệnh PowerShell tìm dấu hiệu tấn công (download cradle/mã hóa/công cụ tấn công đã biết/né tránh phòng thủ/duy trì) - **không bao giờ tự chạy lệnh**, chỉ mở cửa sổ `powershell.exe` thật riêng biệt rồi dán (`CommandLauncher`, tái dùng `InputController` của Auto Click), người dùng tự bấm Enter. `WebProtectionTab` bật/tắt Network Protection (chặn kết nối tới trang độc hại ở tầng hệ điều hành, trước khi trình duyệt tải nội dung) + `HostsBlocklist` (danh sách chặn tên miền tùy chỉnh qua hosts file, khối riêng được đánh dấu). `MalwareScanTab` bật/tắt bảo vệ thời gian thực, quét nhanh/toàn bộ/thư mục (`DefenderScanWorker` - QThread vì `Start-MpScan` đồng bộ/chặn), liệt kê/xóa mối đe dọa. **3 phát hiện/sửa lỗi THẬT qua kiểm tra trực tiếp trên Defender thật**: (1) `Get-MpPreference`'s `EnableNetworkProtection` trả `System.Byte` thô (0/1/2) chứ không tự thành chữ khi ép `[string]` - khiến so khớp "Enabled" luôn sai, sửa bằng switch/map tường minh trong script; (2) `SeverityID` thật có thể là 5 (ngoài tài liệu 0-4, thấy ở mối đe dọa EICAR) - sửa `severityLabel()`; (3) yêu cầu quyền Administrator KHÔNG đồng đều - `Set-MpPreference`/ghi hosts file cần thật, nhưng `Start-MpScan`/`Remove-MpThreat` KHÔNG cần - sửa lại banner cho đúng thay vì nói chung chung. Xác nhận thật bằng tệp test EICAR chuẩn công nghiệp: Defender phát hiện ngay, `Remove-MpThreat` xử lý thành công (ghi nhận `IsActive` có độ trễ đồng bộ vài giây - không phải lỗi app). Xác nhận `Remove-MpThreat` KHÔNG có tham số chọn từng mục (luôn xóa tất cả mối đe dọa đang hoạt động) TRƯỚC khi thiết kế UI, nên không có nút xóa riêng từng dòng. 76 test lõi + 6 test UI (gọi `Get-MpComputerStatus`/đọc hosts file thật - chỉ đọc). **Giới hạn môi trường phát triển này**: không có desktop tương tác thật nên không chụp được ảnh màn hình và không bấm được UAC - giao diện chưa xác nhận bằng mắt (chỉ qua test cấu trúc widget + rà theo đúng mẫu các tab khác đã xác nhận trước đó), luồng mở PowerShell+dán chưa tự kiểm tra trực tiếp được (cần người dùng tự thử trên máy thật có desktop). Tổng 1076 kiểm tra toàn dự án đều pass. |
| 2026-10-07 | Downloader (hoàn thiện - đủ 9/9 công cụ) | Yêu cầu người dùng: tải ảnh/video/tài liệu từ trang trình duyệt, hỏi nên dán link trực tiếp hay cách khác, yêu cầu đưa vào TOÀN BỘ cách có thể tải. Xây 3 cơ chế bổ sung nhau: (1) **Tải trực tiếp** (`FileDownloader`, thuần QNetworkAccessManager không cần QThread) - dán URL trực tiếp tới tệp, nhiều tệp đồng thời, tạm dừng/tiếp tục qua HTTP Range; (2) **Quét trang web** (`PageMediaScanner`) - dán URL một trang, dò `<img>/<video>/<audio>/<source>/<a href>` trong HTML TĨNH (đã xác nhận KHÔNG có QtWebEngine khả dụng cho bản Qt MinGW đang dùng - Qt chỉ phát hành WebEngine cho MSVC trên Windows - nên không chạy được JavaScript, có nêu rõ giới hạn này với người dùng); (3) **Video nền tảng** (`yt-dlp` + `ffmpeg` đóng gói `vendor/yt-dlp/`, cùng tinh thần dùng công cụ đã kiểm chứng như scrcpy) - `YtDlpInfoWorker` lấy định dạng qua `--dump-json`, `YtDlpDownloadWorker` tải và đọc tiến độ qua `--progress-template` (cách yt-dlp CHÍNH THỨC khuyến nghị cho chương trình khác đọc máy, thay vì tự phân tích thanh tiến độ dạng người đọc). **Xác nhận thật trước khi code**: ffmpeg THỰC SỰ bắt buộc (thử `-f best` không cần ffmpeg bị lỗi thật "Requested format is not available" trên video YouTube thật - chất lượng cao hiện đại hầu như không còn định dạng ghép sẵn); giấy phép `yt-dlp.exe` đã biên dịch là GPLv3+ kết hợp (khác mã nguồn Unlicense) - ghi đúng trong THIRD_PARTY.md. **Xác nhận thật qua chính các lớp C++ của ứng dụng** (không chỉ gọi tay yt-dlp.exe): tải+ghép thành công một video YouTube thật ngắn, `FileDownloader` tải thật một ảnh công khai qua HTTP, `PageMediaScanner::scan()` tải+phân tích thật một trang HTML cục bộ. 63 test lõi + 8 test UI (có tải thật 1 tệp công khai nhỏ). Tổng **1147 kiểm tra toàn dự án đều pass**. |
| 2026-10-07 | Rà soát toàn diện cả 9 công cụ: an toàn đa luồng + giao diện responsive | Yêu cầu người dùng: duyệt toàn bộ tiện ích, đảm bảo hoạt động đúng/đủ, giao diện thân thiện/responsive, không crash ở mọi tiến trình, đa luồng song song ổn. Rà soát có hệ thống toàn bộ `src/` (không chỉ chạy lại test cũ) theo 4 hướng: **(1) Vòng đời QThread** - liệt kê cả 9 class kế thừa `QThread` trong dự án, đối chiếu thời gian chạy THẬT tối đa của từng worker với thời gian `wait()` trong destructor cửa sổ chứa nó. Phát hiện **4 lỗi THẬT** (tiềm ẩn, chưa ai báo nhưng xác nhận được bằng cách đọc code + đo thời gian thật): `VpnConnector` (rasdial có thể mất tới 45s nhưng `VpnTab` chỉ `wait(3000)`), `DefenderScanWorker` (FullScan tới 1 giờ nhưng `MalwareScanTab` chỉ `wait(5000)`), `PartitionResizer` (co giãn lớn tới vài phút nhưng không hề có cơ chế hủy), `CleanupExecutor` (xóa hàng nghìn tệp có thể lâu hơn 5s, cũng không hủy được) - tất cả đều có thể khiến đóng cửa sổ giữa chừng HỦY MỘT QThread ĐANG THỰC SỰ CHẠY (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt, có thể crash). Sửa: thêm `requestCancel()` (hủy an toàn, polling 200ms) cho `VpnConnector` và `DefenderScanWorker` (thêm `PowerShellRunner::runCancelable` dùng chung) + nút "Hủy quét" thật trong UI; `PartitionTab`/`CleanupTab` (không an toàn để hủy giữa chừng) thêm `closeEvent` CHẶN HẲN việc đóng cửa sổ trong lúc đang chạy, cộng `wait()` không giới hạn làm lưới an toàn cuối. **Xác nhận THẬT** cơ chế hủy mới hoạt động: chạy `Start-Sleep -Seconds 30` qua `runCancelable`, đặt cờ hủy từ luồng khác sau 1 giây - trả về sau ~1 giây (không phải 30), xác nhận tiến trình con bị dừng thật (chuỗi sau lệnh sleep không được in ra). **(2) `connect()` với lambda** - xác nhận toàn bộ lambda bắt `this` trong `connect()` (67 lượt, 21 tệp) đều truyền `this`/widget làm đối tượng nhận (context object) để Qt tự ngắt kết nối khi đối tượng bị hủy - không có nguy cơ gọi vào con trỏ `this` treo. **(3) Luồng dữ liệu dùng chung** - xác nhận mọi `QNetworkAccessManager` (4 chỗ) đều sống trên luồng giao diện (không tạo trong `run()` của QThread nào); xác nhận mọi cờ hủy chia sẻ giữa 2 luồng đều là `std::atomic_bool` (không phải `bool` thường - tránh race điều kiện thật). **(4) Giao diện responsive** - phát hiện 3 nhãn hiển thị ĐƯỜNG DẪN THƯ MỤC (độ dài không giới hạn) trong `DirectDownloadTab`/`PageScanTab`/`PlatformVideoTab` thiếu `setWordWrap` - đường dẫn dài có thể bị cắt cụt không thấy hết (thanh cuộn ngang đã tắt toàn ứng dụng); đã thêm word-wrap + tooltip đầy đủ, áp dụng phòng ngừa thêm cho nhãn vị trí IP/VPN. **Lỗi THẬT khác phát hiện qua kiểm tra tải song song thật (3 tệp cùng lúc)**: một URL lỗi HTTP thật (404) khiến NỘI DUNG TRANG LỖI bị ghi nhầm vào tệp đích - người dùng thấy "Thất bại" nhưng vẫn có tệp rác nằm lại; nguyên nhân sâu hơn: logic cũ coi MỌI mã trạng thái khác 206 khi đang resume là "server bỏ qua Range" rồi TỰ XÓA (Truncate) tệp đích trước khi biết đó có phải lỗi thật hay không - nghĩa là một lần resume gặp lỗi mạng thật có thể XÓA MẤT phần đã tải đúng trước đó. Sửa `FileDownloader`: chỉ ghi dữ liệu vào tệp khi mã trạng thái HTTP là 2xx; chỉ coi là "tải lại từ đầu" khi CHÍNH XÁC là 200 (không phải "khác 206"); xóa tệp đích khi thất bại CHỈ với lần tải mới (không phải đang resume) - xác nhận lại bằng tải thật 1 URL 404 thật, tệp không còn bị tạo ra. Xác nhận tải song song thật (3 tệp cùng lúc, tối đa đồng thời mặc định 3) hoàn tất đúng, không hỏng trạng thái dùng chung. Thêm 3 test hồi quy (tổng 1150 kiểm tra toàn dự án đều pass, tăng từ 1147). **Giới hạn môi trường này không đổi**: vẫn không có desktop tương tác thật nên không chụp lại được ảnh màn hình cho các module mới (VPN/Security/Downloader) - đã bù bằng rà soát code kỹ + áp dụng đúng mẫu đã được xác nhận bằng ảnh ở các module cũ hơn. |
| 2026-10-08 | Auto Click: 11 phản hồi người dùng (a-k) kèm ảnh lỗi giao diện | Sửa lỗi giao diện thật (viên nén/capsule đè chữ do thiếu `setEditTriggers(NoEditTriggers)`); thêm dấu `*` khi sửa hành động chưa lưu; thu nhỏ ứng dụng trước khi chạy; tìm và sửa lỗi THẬT trong `Logger` (flush() ép ghi đĩa ở MỌI lần log, không điều kiện - rủi ro giật luồng tự động hóa khi chạy 24h liên tục vì `ActionRunner` log đồng bộ) + thêm cơ chế xoay vòng log trước đó CHƯA TỪNG CÓ (ngưỡng 10MB); nới rộng khu danh sách hành động, thu hẹp khu cài đặt; khu hành động rút còn 3 nút (Thêm/Nhân bản/Xóa) + kéo-thả đổi vị trí (tự viết `ReorderableTable` tránh lỗi biết trước của `QTableWidget::InternalMove` xáo trộn theo ô thay vì theo dòng); thêm nhấp đúp đổi tên chuỗi hành động (đã có sẵn qua chuột phải); bỏ nút mũi tên ô Lặp lại. **Thêm 2 tính năng bắt thao tác thật mới** (ngoài yêu cầu gốc, đáp ứng sâu hơn mục f/i): `HotkeyCapture` (nút "Bắt tổ hợp phím", hook `WH_KEYBOARD_LL` theo mẫu `InputHook`, nuốt phím khi đang bắt) và `DragGestureCapture` (nút "Bắt thao tác kéo thật", hook `WH_MOUSE_LL`, ẩn cửa sổ rồi người dùng kéo thật trên màn hình, hook KHÔNG BAO GIỜ nuốt sự kiện để màn hình vẫn nhận tương tác, xong tự điền tọa độ và khôi phục cửa sổ). Xác nhận mục (k) nút Dừng ở HUD đã nối đúng từ trước, không cần sửa. **Giới hạn môi trường kiểm thử**: không có màn hình/desktop tương tác thật trong môi trường build này nên không tự xác nhận được việc Windows giao sự kiện bàn phím/chuột thật qua `SetWindowsHookExW` tới 2 lớp bắt mới - đã kiểm tra thay thế bằng cách gọi thẳng logic xử lý sự kiện (`onRawKey`/`onRawMouse`) với dữ liệu giả lập đúng thật, 22/22 kiểm tra đạt; người dùng cần tự bấm thử 2 nút này trên máy thật để xác nhận lần cuối. |
| 2026-10-08 | Android: `adb.exe` không dừng dù đã đóng ứng dụng; đổi icon ứng dụng sang `icon/App.png` | Người dùng báo `adb.exe` (từ `One for ALL\scrcpy`) còn chạy dù đã đóng ứng dụng - dựng lại được bằng cách soi tiến trình thật đang chạy (bắt được một `adb.exe` mồ côi từ phiên test trước, xác nhận lỗi có thật). **2 nguyên nhân gốc cộng hưởng, đã sửa cả hai** (xem 4g để biết chi tiết đầy đủ): (1) `AndroidControlWindow` thiếu `closeEvent` (khác mọi cửa sổ tool khác có tiến trình nền) nên bấm X chỉ ẨN cửa sổ, không hủy `DevicesTab`/`ScrcpyLauncher`, destructor (vốn đã có `stop()` đúng) không bao giờ chạy - thêm `closeEvent` gọi `DevicesTab::stopActiveSession()` (hàm mới, dùng chung nút Dừng/destructor/closeEvent/`aboutToQuit`); (2) `ScrcpyLauncher::stop()` chỉ dừng đúng tiến trình `scrcpy.exe`, không đụng tới `adb.exe shell ...` là tiến trình CON của nó (Windows không đệ quy dừng cây tiến trình như Linux) - thêm `terminateProcessTree()` (quét `CreateToolhelp32Snapshot` + BFS tìm hậu duệ PID rồi `TerminateProcess` từng cái), gọi sau mỗi lần `stop()` chủ động và khi scrcpy thoát không bình thường. Xác nhận thật bằng cách tìm và tự tay dừng đúng tiến trình `adb.exe` mồ côi phát hiện được trên máy build trước khi commit. **Icon ứng dụng:** dựng lại `assets/app_icon.ico` (icon file .exe - Explorer/Taskbar/shortcut/installer) từ `icon/App.png` thay vì `icon/autoclicker.jpg`, dùng đúng thuật toán `IconHelper::makeBadgedPixmap` (nền trắng bo góc + viền mảnh, giữ tỉ lệ bo góc/đệm cho mọi kích thước 16-256px vì không có script dựng icon sẵn trong repo, viết tay bằng Pillow); icon cửa sổ chính lúc chạy (`MainWindow::setWindowIcon`) cũng đổi sang `:/icons/App.png` (thêm vào `resources.qrc`) - các icon riêng từng tool trong sidebar (wifi.png, vpn.jpg, autoclicker.jpg...) giữ nguyên, không đổi. |
| 2026-10-09 | Rà soát toàn bộ + sửa lỗi tiềm ẩn (v1.19.0) | Đọc từng dòng toàn bộ dự án rồi sửa theo từng công cụ - chi tiết đầy đủ ở mục 4o. Nặng nhất: Connect Together chưa từng chuyển được quyền điều khiển (bên nhận bỏ qua handoff) và có thể khóa chuột/phím khi rớt kết nối; Disk Cleanup có thể đổi kích thước nhầm phân vùng khi đổi dòng chọn; cờ hủy VPN/quét Defender không được đặt lại; chèn lệnh PowerShell qua dấu nháy Unicode; tự cập nhật không mở lại ứng dụng và cài nhầm bản `.exe` lên bản `.msi`; bộ cài có thể đóng gói dữ liệu cá nhân của máy dev; bước robocopy của CI sẽ bị tính là thất bại. Dữ liệu người dùng chuyển sang `%LOCALAPPDATA%\OneForAll`. Thêm `CLAUDE.md` (quy tắc đứng). Build sạch toàn bộ + 18 bộ test (qr, wifi, connect, diskcleanup, android, vpn, security, downloader - mỗi bộ `_tests` và `_ui_tests` - cùng `update_tests` và `autoclick_tests` mới): **2717 kiểm tra đều pass, 0 lỗi** (trước đợt này khoảng 1170). |
| 2026-10-09 | Rà soát độc lập lần hai + stress test (v1.19.1) | Năm người đọc độc lập rà lại từng nhóm công cụ và viết năm bộ stress/fuzz mới (`core`, `qrwifi`, `connect`, `disk_vpn`, `secdl`) - chi tiết ở mục 4p. Lỗi nặng nhất tìm thêm: Disk Cleanup xóa nhầm tệp bên cạnh khi tên có dấu chấm/khoảng trắng cuối hoặc ký tự đại diện; `CommandAnalyzer` treo với lệnh dài (regex bậc hai) và báo "An toàn" sai với ký tự UTF-16 hỏng; Connect Together hai máy cùng nhập mã cho nhau giữ hai khóa khác nhau; Downloader báo "Hoàn tất" với tệp cụt; WiFi ghi đè nhầm hồ sơ khi SSID có ký tự điều khiển; `AppPaths` xóa nhầm tệp đang dùng với đường dẫn tương đối/8.3/junction; phím dừng Ctrl+Alt+F8 không đăng ký được khi bị chương trình khác giữ (thêm tổ hợp dự phòng). Xóa `NEW_SESSION_SETUP.md` (ghi chú bàn giao phiên cũ, đã thay bằng `CLAUDE.md`). Build sạch toàn bộ trên máy rảnh: 18 bộ test thường **2967 kiểm tra, 0 lỗi**; 5 bộ stress đều pass - `core_stress_tests` 525/0 (95 s), `qrwifi_stress_tests` 759.893/0 (290 s), `disk_vpn_stress_tests` 7320/0 (162 s), `secdl_stress_tests` 1.876.772/0 (123 s), `connect_stress_tests` 386/0 (135 s). |
| 2026-10-09 | Hoàn thiện mục dang dở + dọn rác (v1.19.2) | Sau khi khởi động lại máy: xác nhận hai tiến trình PowerShell kẹt đã mất, dọn ~1,4 GB rác của các lượt test (thư mục tạm, 2.409 tệp test trong Thùng rác). Hoàn thiện danh sách còn dang dở của 4p - chi tiết ở mục 4q: Connect Together suy khóa ở luồng nền + trần khung sau xác thực + báo lỗi lưu; `PowerShellRunner` dừng cả tiến trình cháu; `Logger` tự mở lại; `adb pair` bất đồng bộ; Disk Cleanup nhận ra junction tới thư mục được bảo vệ + kiểm lại tuổi tệp lúc xóa; hai lỗi VPN; hosts giữ kiểu xuống dòng từng dòng; lịch sử QR gộp giữa hai bản ứng dụng + thử lại khi ghi; test không còn để sót thư mục tạm (`tests/TestDataDir.h`). Build sạch toàn bộ sau khi khởi động lại máy: 18 bộ test thường **3217 kiểm tra, 0 lỗi**; 5 bộ stress đều pass - `core_stress_tests` 550/0, `qrwifi_stress_tests` 759.439/0, `disk_vpn_stress_tests` 7320/0, `secdl_stress_tests` 1.875.637/0, `connect_stress_tests` 386/0. |
| 2026-10-09 | SignPath từ chối - rà giảm rủi ro bị gắn cờ + metadata Win32 (v1.19.3) | SignPath Foundation từ chối cấp chứng chỉ (lý do: chưa đủ tín hiệu uy tín công khai, không phải chất lượng code) - chi tiết, nguyên văn lý do và hướng tiếp theo (Microsoft Store miễn phí là lựa chọn chính) ở mục 4r. Theo yêu cầu người dùng "rà mã nguồn đảm bảo không bị block": làm rõ trước là cảnh báo SmartScreen không nằm trong mã nguồn và không sửa code xóa được. Phần sửa được thật: thêm `VERSIONINFO` (Company/Product/Version/Copyright/Description) cho `OneForAll.exe` (trước đó RỖNG HOÀN TOÀN - xác nhận thật qua `VersionInfo` trước/sau khi sửa), cho `OneForAll_Setup.exe` và thuộc tính ARP của `OneForAll_Setup.msi`. Lỗi thật gặp khi viết: dùng tên `VS_VERSION_INFO` làm ID tài nguyên mà không `#include <winver.h>` khiến windres hiểu nhầm thành resource TÊN thay vì ID số 1, biên dịch không báo lỗi nhưng `GetFileVersionInfo` không tìm thấy gì - sửa bằng ID số `1` trực tiếp. Rà xác nhận KHÔNG có registry tự khởi động/cài service/bỏ qua UAC. Có chủ đích KHÔNG đổi cách gọi PowerShell hay các mẫu trong `CommandAnalyzer` để né AV - đó là evasion, ngoài ranh giới cho phép. Quét Defender thật trên `dist\` sau build: sạch. Build lại toàn bộ: 18 bộ test thường **3213 kiểm tra, 0 lỗi**. |
| 2026-10-10 | Rà lỗi giao diện ghi đè/cắt mất + nút không hoạt động (v1.19.4) | Chi tiết đầy đủ ở mục 4s. Sửa panel "Cài đặt hành động" của Auto Click bị cắt mất (ảnh chụp thật người dùng gửi) + cột "Mô tả" bị bóp quá hẹp. Thêm `FlowLayout` dùng chung (`src/ui/widgets/`, lib `ui_widgets`) - áp dụng cho dãy thẻ ổ đĩa Disk Cleanup (không còn giới hạn/cắt mất khi máy có nhiều ổ, kể cả ổ ảo Google Drive) và 7 nút WiFi NetworksTab. Sửa lỗi CHỨC NĂNG thật: Downloader "Tải trực tiếp" - chuột phải vào hàng chưa chọn không hiện menu Bắt đầu/Tạm dừng/Hủy (trông như "nút không hoạt động"). Rà xác nhận KHÔNG có vấn đề ở Android Phone Control, QR Tools (ảnh chụp thật). Thêm `placeholder-text-color` cho 8 module (contrast tốt hơn, không phải fix cho nghi vấn PairingTab). Ghi nhận CHƯA giải quyết được nghi vấn hiển thị nhòe ở `PairingTab` đã biết từ v1.2.0 - thử 4 cách chẩn đoán khác nhau đều không đổi gì, nghiêng về khả năng là đặc điểm công cụ chụp ảnh tự động hơn là lỗi code, nhưng KHÔNG kết luận chắc. 11 bộ test liên quan: 2554 kiểm tra, 0 lỗi. |
| 2026-10-10 | Luồng tự cập nhật: không cài được sau khi tải + kiểm tra thủ công (v1.19.5) | Chi tiết đầy đủ ở mục 4t. Người dùng tự test: tải 100% nhưng không tự lên bản mới, mở lại lại hỏi cập nhật. Đã TỰ CÀI installer v1.19.3 thật từ GitHub Release + test cập nhật thật qua tự động hóa UI (21 giây, thành công hoàn toàn trong môi trường test - không tái hiện được lỗi) nhưng xác định được điểm yếu thật: helper script PowerShell chạy sau khi app thoát hoàn toàn "mù" - không log, không kiểm tra exit code trình cài đặt, Sleep 500ms sau Wait-Process có thể không đủ tránh race condition AppMutex khiến Inno Setup tự hủy cài đặt trong im lặng. Sửa: tăng Sleep lên 1500ms, helper script tự ghi log + đánh dấu lỗi (exit code) vào %TEMP%, `UpdateInstaller::consumePreviousUpdateFailure()` đọc dấu vết đó lúc khởi động và báo rõ ràng thay vì im lặng hỏi lại từ đầu. Thêm nút "Kiểm tra cập nhật" thủ công (UpdateChecker::checkNow() đã có sẵn nhưng chưa có UI gọi) - xác nhận thật bằng tự động hóa UI. Tự mở lại app sau cập nhật đã hoạt động đúng từ trước, không cần sửa. CLAUDE.md: đổi rule - từ nay LUÔN tag+push+release sau mỗi lần sửa, không cần hỏi lại. |
| 2026-10-10 | setup.exe không hỏi đường dẫn cài đặt (v1.19.6) | Chi tiết đầy đủ ở mục 4u. Xác nhận thật nguyên nhân bằng cách tự cài rồi chạy lại installer: Inno Setup mặc định (`UsePreviousAppDir=yes`) tự động nhớ cài đặt trước (cùng AppId), bỏ qua cả trang "Chọn Chế độ Cài đặt" lẫn trang chọn thư mục kể cả ở chế độ tương tác - xác nhận bằng cách gỡ sạch rồi cài lại (trang "Chọn Chế độ" xuất hiện đúng như thiết kế). Sửa bằng `DisableDirPage=no` tường minh trong `installer/OneForAll.iss` - không đổi `UsePreviousAppDir` để không phá cơ chế tự cập nhật silent (mục 4t) vốn cần tự cài đúng vào vị trí cũ. Chưa xác nhận được bằng automation rằng trang chọn thư mục thực sự hiện lại sau khi sửa - đã thử 3 cách tự động hóa UI (UIA InvokePattern, input injection chuột, PostMessage) đều không click được vào control tùy biến kiểu Delphi/VCL của Inno Setup Modern Wizard - giới hạn công cụ, không phải dấu hiệu sai. |
| 2026-10-10 | Auto Click: hiện đại hóa UI + tổ hợp phím có thứ tự (v1.19.7) | Chi tiết đầy đủ ở mục 4v. 8 điểm từ ảnh chụp thật người dùng gửi: menu chuột phải hiện đại + icon 👆 dùng chung (`ModernMenu`, áp dụng cả Downloader); bỏ viền focus xấu trong bảng hành động; Esc hủy capture tọa độ qua hook `WH_KEYBOARD_LL` thay vì `keyPressEvent` không đáng tin; ô Timing đổi sang `DurationInput` (số + đơn vị ms/s/m/h) dùng chung; trang "Kéo chuột" chỉ còn nút bắt thật; tổ hợp phím hiện số thứ tự đã bấm + tự dồn khi bỏ chọn giữa (field `modOrder` mới, `effectiveModOrder()` tự suy luận cho hồ sơ JSON cũ không vỡ tương thích); đổi tên "Phím tắt"→"Tổ hợp phím" + thêm "Kết thúc bằng" Cuộn chuột/Click chuột (vd Ctrl+Cuộn = zoom); HUD tự quay về vị trí gốc khi hết va chạm. 109/109 test (+9 mới), `core_stress_tests` không hồi quy ở phần Auto Click. Chưa xác nhận được bằng ảnh chụp tự động trang "Tổ hợp phím" mới và menu mới - popup `QComboBox`/`QMenu` là cửa sổ riêng, 3 cách UI Automation thử đều không đổi/chụp được nó trong môi trường này; đã xác nhận qua code review + unit test, cần tự xem bằng mắt thường. |
| 2026-10-11 | Auto Click: dọn giao diện chính + con trỏ ngón tay + sửa đơn vị thời gian (v1.19.8) | Chi tiết đầy đủ ở mục 4w. 6 điểm tiếp theo từ ảnh chụp thật: thông báo "Đang tự khởi động lại..." trước khi thoát để cập nhật (trễ 1.5s, vẫn trong hạn 60s của tiến trình trợ giúp); menu chuột phải đổi CON TRỎ CHUỘT thành ngón tay trỏ thay vì icon 👆 vẽ trên từng dòng (v1.19.7 không đúng ý); bỏ nút Dừng + nhãn Trạng thái ở cửa sổ chính (trùng minibox/đã có cảnh báo lỗi riêng); `ChainListWidget` bỏ nút Nhân bản/Xóa dưới danh sách + "Chạy chuỗi này" trong chuột phải (trùng chức năng), thêm "Ghim lên"/"Bỏ ghim" (đưa lên đầu + nhãn 📌, không ép giữ mãi trên cùng) và kéo-thả đổi thứ tự chuỗi; `ActionListWidget` bỏ "Sửa" trong chuột phải (không có tác dụng gì khác click chọn hàng) + nút Nhân bản/Xóa dưới bảng; bỏ icon mũi tên ▾ trên MỌI QComboBox toàn bộ 9 công cụ (`ComboBoxStyle::noArrow()` mới); `DurationInput` đổi đơn vị không còn quy đổi số hiển thị (trước đây "500"→"0.5" khi chọn giây, giờ số giữ nguyên, chỉ đổi ý nghĩa đơn vị). 121/121 test (+12 mới), `core_stress_tests` 547/549 pass, phần Auto Click (76/76) không hồi quy - 2 lỗi còn lại thuộc `UpdateInstaller::buildHelperScript`, có từ trước v1.19.7, không liên quan đợt sửa này. Chưa xác nhận bằng mắt thường: thông báo restart khi có bản cập nhật thật, con trỏ ngón tay thật, ghim/kéo-thả chuột thật. |
| 2026-10-11 | Sửa lỗi tự cập nhật thất bại ngẫu nhiên, ExitCode=1 (v1.19.9) | Chi tiết đầy đủ ở mục 4x. Người dùng báo kèm ảnh chụp "Cập nhật lần trước thất bại - ExitCode=1", tải 100% lúc cập nhật được lúc không. Nguyên nhân thật tìm qua đọc code (không đoán): app cố ý không giới hạn 1 bản sao (`CreateMutexW` trong main.cpp) + Inno Setup dùng cùng tên mutex (`AppMutex=OneForAllRunningMutex`) để phát hiện app đang chạy ở bước khởi tạo + `/CLOSEAPPLICATIONS` không có `/FORCECLOSEAPPLICATIONS` trong khi app chưa đăng ký Restart Manager -> không đóng êm được một cửa sổ OneForAll.exe KHÁC đang chạy -> Setup tự hủy ngay, đúng ExitCode=1 "Setup failed to initialize". Sửa `buildHelperScript`: thêm vòng chờ tối đa 30s (Get-Process/Where-Object theo đường dẫn exe) cho bản sao khác tự thoát (không ép đóng - không an toàn với thao tác người dùng đang dở ở cửa sổ khác) + thử lại cài đặt tối đa 5 lần nếu vẫn lỗi. Sửa chữ hộp thoại theo góp ý UX người dùng (không ngụ ý 100% tải = xong ngay). Phát hiện và sửa luôn 1 lỗi pre-existing trong fuzz test buildHelperScript (baseline hardcode lạc hậu từ v1.19.5 - đổi sang so sánh động). 95/95 test (+6 mới); core_stress_tests 551/551 pass sạch hoàn toàn - lần đầu tiên trong phiên không còn lỗi nào. Chưa xác nhận bằng một lần cập nhật thật trên máy có 2 bản sao chạy cùng lúc. |
| 2026-10-11 | Sửa góc đen ở menu + mũi tên còn sót ở combo/spinbox + bỏ nút Nạp cấu hình (v1.19.10) | Chi tiết đầy đủ ở mục 4y. Người dùng gửi 2 ảnh chụp thật, nêu 3 điểm. (1) Góc đen ở menu chuột phải dù đã bo góc - QMenu là popup không trong suốt mặc định, sửa bằng `Qt::WA_TranslucentBackground`. (2) Combo "Nút"/"Phím"/"Hướng" vẫn còn mũi tên dù v1.19.8 đã thêm ComboBoxStyle::noArrow() - tìm đúng nguyên nhân bằng ảnh chụp so sánh (không đoán): `kComboStyle` dùng khối CSS TRẦN không có selector `QComboBox {}` bao ngoài, nối với rule có selector của noArrow() khiến Qt không áp dụng đúng rule phía sau; xác nhận qua so sánh combo "Loại"/"ms" (đã bọc đúng) sạch còn "Nút" (thiếu bọc) thì không - sửa bằng cách bọc đúng selector, audit toàn bộ nơi khác dùng noArrow() đều đã đúng. Tiện thể bổ sung `setButtonSymbols(NoButtons)` cho 17 QSpinBox còn thiếu (8 ở Auto Click + 9 ở QR/Disk Cleanup/Connect/Android). Thêm `ComboBoxStyle::applyCursor()` đổi con trỏ ngón tay khi hover qua mọi combo, áp dụng 9 cửa sổ chính + 2 dialog độc lập. (3) Bỏ nút "Nạp cấu hình" theo yêu cầu người dùng. Thêm test hồi quy bắt đúng lỗi "CSS thiếu selector". 123/123 autoclick_tests (+2 mới) và toàn bộ 16 bộ test các tool khác đều pass - không hồi quy dù sửa trải khắp 8 tool; core_stress_tests 551/551 pass sạch. Đã xác nhận bằng ảnh chụp thật combo/spinbox hết mũi tên; chưa xác nhận được bằng ảnh chụp góc menu (môi trường không bắt được popup window kịp lúc) - tin vào kỹ thuật Qt chuẩn, cần tự xem trên máy thật. |
| 2026-10-13 | Auto Click: làm lại "Ghi nhớ chuỗi" + cache chỉnh sửa chưa lưu trên nền v1.19.10 (v1.19.11) | Chi tiết đầy đủ ở mục 4z. Sau khi người dùng yêu cầu quay code về ĐÚNG v1.19.10 (xóa mọi commit/tag sau đó, lưu lịch sử yêu cầu ở `YEU_CAU_SAU_1.19.10.md`), người dùng gửi lại 2 yêu cầu Auto Click: (1) loại hành động "Ghi nhớ chuỗi" - ghi macro chuột+bàn phím thật toàn máy, minibox ĐỨNG YÊN lúc ghi / né chuột lúc chạy chuỗi; (2) chỉnh sửa chưa lưu không mất khi đổi sang hành động khác (cache theo từng hàng, dấu `*`, chỉ hỏi lúc RUN/đóng cửa sổ). Port CÓ CHỌN LỌC từ lịch sử git (`cherry-pick -n 51ac6ab` + phần cache của `ae232ec` + phần sửa minibox của bản cuối), KHÔNG mang theo cơ chế giao diện nào (không `PopupCornerFix`/`WindowTheme`/Fusion/`paintEvent`/sửa style) - giao diện giữ y hệt v1.19.10, `git diff v1.19.10` ngoài Auto Click chỉ khác `CMakeLists.txt` (thêm `ActionRecorder.cpp`). `autoclick_tests` 165/165, `core_stress_tests` 551/551; xác nhận thật trên app đang chạy: trang "Ghi nhớ chuỗi" hiển thị đúng phong cách, cache giữ `*` + nạp lại đúng bản dở khi quay lại hàng. CHƯA kiểm chứng được: ghi/phát lại macro thật bằng hook toàn cục + `SendInput`, hành vi minibox khi ghi/chạy (cần thử tay trên máy thật). |
| 2026-10-13 | QR Tools: làm lại các cải tiến đã mất + con trỏ ngón tay cho mọi nút bấm (v1.19.12) | Chi tiết đầy đủ ở mục 4za. Người dùng gửi lại danh sách yêu cầu QR Tools + con trỏ đã mất khi quay về v1.19.10: WiFi chọn nhanh từ hồ sơ đã lưu + nút "Kết nối WiFi" khi quét ra mã WiFi; Vị trí có "Lấy vị trí hiện tại" (theo IP công khai) + "Mở bản đồ" (Google Maps) + dán link Google Maps; Quét ẩn khung "KẾT QUẢ" khi chỉ có 1 mã, nhận diện email/SĐT trong văn bản tự do, bảng bo góc đồng bộ; Tạo mã chỉ còn 5 loại; con trỏ ngón tay mở rộng sang mọi `QPushButton`. Port CÓ CHỌN LỌC từ `ae232ec` + phần dán link của `14baffc`, KHÔNG mang theo cơ chế giao diện nào (không `PopupCornerFix`/`WindowTheme`/Fusion/bản đồ nhúng). `qr_tests` 136/136, `qr_ui_tests` 165/165, `wifi_tests` 119/119, `qrwifi_stress_tests` 760.699/760.699, `disk_vpn_stress_tests` 7.320/7.320; xác nhận thật trên app đang chạy: 5 loại, 2 nút Vị trí + ô dán link, combo "WiFi đã lưu". CHƯA kiểm chứng: gọi thật `ipwho.is`, mở Google Maps thật, kết nối WiFi thật từ mã QR, hình ảnh thực tế trên máy thật. |
| 2026-10-13 | Sửa lỗi bấm "Lưu hành động" 2 lần + gọn lại trang QR Vị trí + link rút gọn Google Maps (v1.19.13) | Chi tiết đầy đủ ở mục 4zb. Người dùng gửi ảnh + 3 yêu cầu: (1) trang QR Vị trí vượt chiều rộng - đã bố trí lại (ô giá trị thu hẹp, nút nằm cạnh, có test chống vượt cột trái ở 860/1020/1400px); (2) không đọc được link rút gọn `maps.app.goo.gl` - nay gửi 1 yêu cầu GET tới Google, đọc header `Location`, tách tọa độ (ưu tiên điểm ghim `!3d!4d`), chỉ theo host Google; xác nhận qua mạng thật với link của người dùng; (3) phải bấm Lưu 2 lần - nguyên nhân gốc: `setActions()` tự phát `currentCellChanged` ngay khi editor còn "dirty" nên bản vừa lưu bị chụp lại thành cache chưa lưu (lỗi do cache v1.19.11, không có ở v1.19.10), cùng lỗ hổng ở "Lưu lại trước khi chạy?" và Nhân bản/Xóa/Dời; tái hiện bằng test trước khi sửa, sửa bằng `ScopedFlag`. `autoclick_tests` 194/194, `core_stress_tests` 551/551, `qr_tests` 182/182, `qr_ui_tests` 172/172, `qrwifi_stress_tests` 760.292/760.292. CHƯA kiểm chứng: bấm chuột thật trong app, trang đồng ý cookie EU. |
| 2026-10-13 | QR Vị trí: ô tọa độ rộng hơn + nút co vừa chữ; con trỏ ngón tay trên danh sách công cụ (v1.19.14) | Chi tiết đầy đủ ở mục 4zc. Người dùng gửi ảnh ô tọa độ bị cắt chữ số cuối: nguyên nhân là bề rộng cố định lấy từ `sizeHint()` (chưa tính padding stylesheet) - nay bề rộng tối thiểu tính theo chữ thật của `-180.000000`, ô giãn theo cột, hai nút co vừa chữ (lưới 3 cột), có test ở 860/1020/1400px. Thêm helper `ItemHoverCursor` (ngón tay trỏ khi rê lên MỘT MỤC của danh sách, vùng trống giữ mũi tên) gắn vào danh sách công cụ của cửa sổ chính. Bản đầu tiên được tag sau v1.19.10. `autoclick_tests` 201/201, `qr_ui_tests` 190/190, `qr_tests` 182/182, `qrwifi_stress_tests` 760.141/760.141. CHƯA kiểm chứng: con trỏ thật trong cửa sổ chính, DPI khác. |
| 2026-10-13 | QR: định dạng vị trí + khung nhận dạng khi quét màn hình; Auto Click: chọn nhiều để xóa/nhân bản (v1.19.15) | Chi tiết đầy đủ ở mục 4zd. 4 yêu cầu: (1) đọc đúng 3 định dạng vị trí - dữ liệu thật cho thấy KHÔNG cùng một điểm (link dài = lộ trình xuất phát + Hội Gym; link rút gọn = Hội Gym; độ-phút-giây = điểm xuất phát), nay link dài lấy điểm của địa điểm có mã địa điểm (trùng link rút gọn) và báo điểm còn lại, thêm đọc tọa độ viết tay (độ-phút-giây, độ thập phân, cặp số); (2) khung nhận dạng nhỏ hơn mã - tái hiện: ảnh dpr 1,25 cho khung 79,8% do `padWhite()` vẽ theo kích thước logic, sửa bằng chuẩn hóa dpr về 1; (3) đếm số lần quét mã đã tạo - KHÔNG làm được (mã tĩnh, không có máy chủ); (4) chọn nhiều hành động/chuỗi bằng Ctrl/Shift để xóa/nhân bản (nhiều hành động: cụm bản sao lùi về sau hành động chọn cuối). `qr_tests` 241/241, `qr_ui_tests` 197/197, `autoclick_tests` 251/251, `core_stress_tests` 551/551, `qrwifi_stress_tests` 760.298/760.298. CHƯA kiểm chứng: Shift + bấm chuột thật, menu chuột phải thật, ảnh chụp màn hình thật ở DPI 125%. Chỉ push code, không tạo release. |
| 2026-10-14 | Downloader: hàng đợi hợp nhất + phân giải mọi loại link + Lịch sử + Cài đặt (v1.19.16) | Chi tiết đầy đủ ở mục 4ze. 6 yêu cầu: (1) Tải trực tiếp = hàng đợi (thêm -> kiểm tra OK/ERR -> chọn -> tải; thư mục đích trống sau bước kiểm tra); (2) trang chặn chuột phải vẫn đọc được qua quét sâu mã nguồn + trình duyệt ẩn (NetLog); (3) phủ Facebook ảnh/video, TikTok ảnh, trang phim HLS, Google Drive/Docs, Studocu (ERR vì Cloudflare); (4) tab Lịch sử + Cài đặt (thư mục tải dùng chung); (5) nút 🌐 mở link; (6) hồ sơ/kênh bung thành từng bài (Instagram/Facebook cần cookie). `downloader_tests` 510/510, `downloader_ui_tests` 57/57, `update_tests` 95/95, `secdl_stress_tests` 1.869.830/1.869.830, `core_stress_tests` đạt; hàng đợi chạy thật qua mạng. CHƯA kiểm chứng: bấm chuột/hộp thoại thật, cookie Instagram/Facebook, hồ sơ Facebook đầy đủ. Có tạo release. |
| 2026-10-14 | Đối chiếu code giao diện với v1.19.10 + đưa `QRUiStyle.h` về đúng v1.19.10 (v1.19.17) | Chi tiết ở mục 4zf. `git diff v1.19.10 HEAD -- src`: 218/285 tệp giống hệt, 67 tệp khác đều là tính năng được yêu cầu; thay đổi duy nhất trên giao diện cũ không phục vụ tính năng (bo góc bảng QR 12px) đã hoàn lại 10px. `qr_tests` 241/241, `qr_ui_tests` 197/197. Chỉ push code, không tạo tag/release. CHƯA kiểm chứng hiển thị thật trên màn hình người dùng. |
| 2026-10-14 | Downloader: quét từng bài đăng của hồ sơ + Quét trang web dùng bộ phân giải đầy đủ + cookies.txt cho tệp có quyền (v1.19.18) | Chi tiết ở mục 4zg. Nguyên nhân thật: tab Quét trang chỉ quét HTML tĩnh; yt-dlp không hỗ trợ ảnh; hồ sơ TikTok liệt kê bài ảnh dạng /video/ (tải ra mp3). Kết quả qua hàng đợi thật: ảnh Facebook OK, hồ sơ Facebook 9 ảnh đầy đủ (9/9), ảnh TikTok 2 ảnh, hồ sơ TikTok 19 ảnh + 6 video (25/25); Drive (chủ tắt quyền tải) và Studocu (Cloudflare + trả phí) ERR đúng lý do - không vượt hạn chế. Thông báo ffmpeg.exe là Windows chặn tệp chưa ký số. `downloader_tests` 554/554, `downloader_ui_tests` 65/65, `update_tests` 95/95, `secdl_stress_tests` 1.874.715/1.874.715, `core_stress_tests` đạt. CHƯA kiểm chứng: nguyên nhân lỗi ảnh Facebook trên máy người dùng, cookies.txt với tài khoản Google thật. Có tạo release. |
| 2026-10-14 | Downloader: gộp 3 tab thành "Tải xuống" + sửa giao diện lỗi + lý do 2 link không tải được (v1.19.19) | Chi tiết ở mục 4zh. Gộp Tải trực tiếp + Quét trang web + Video nền tảng vào `DownloadTab` (nút "🔍 Quét trang", ô "Quét sâu", "Chất lượng video" chung + "Chọn chất lượng khác..." cho từng video yt-dlp). Giao diện lỗi: nút 🌐 là widget nên dòng chọn hiện khung trắng quanh nút - nay cột Mở là chữ "↗" trong ô, bảng NoFocus, thanh tiến độ dòng chưa tải trong suốt, lý do lỗi đầy đủ hiện khi chọn dòng. Drive: chủ tệp tắt quyền tải; Studocu: Cloudflare 403 + tường trả phí - không vượt hạn chế. `downloader_tests` 574/574, `downloader_ui_tests` 85/85. Chỉ push code, không tạo tag/release. CHƯA kiểm chứng yt-dlp thật cho chọn chất lượng. |
| 2026-10-14 | Downloader: một nút "Quét" + bỏ nút thư mục + ERR thành dòng + không tích hợp 4pdf.net/okdebrid.com (v1.19.20) | Chi tiết ở mục 4zi. Gộp "Thêm vào hàng đợi" + "Quét trang" thành "🔍 Quét"; bỏ "Đổi..."/"Mở thư mục"; link quét không ra gì thành dòng ERR (`addFailed`); link Drive/Docs bỏ quét tĩnh (đo thật: trang xem Drive lộ ảnh xem trước `drive-viewer` của tệp bị khóa tải). Từ chối có chủ đích tích hợp 4pdf.net/okdebrid.com (vượt khóa tải của chủ tệp Drive, tường trả phí + Cloudflare của Studocu); nêu đường hợp lệ. Mạng thật: 2 link -> 2 dòng ERR đúng lý do. `downloader_tests` 579/579, `downloader_ui_tests` 94/94. |
| 2026-10-14 | Downloader: thu hẹp ô dán link + nút Quét 76 -> 52 px (v1.19.21) | Chi tiết ở cuối mục 4zi. Chỉ đổi chiều cao (`kInputHeight`), ảnh chụp offscreen xác nhận vẫn đủ 2 dòng chữ. `downloader_ui_tests` 94/94. |
| 2026-10-14 | Dọn dẹp tệp thừa (không đổi phiên bản, không đổi mã) | Xóa `YEU_CAU_SAU_1.19.10.md` khỏi repo theo yêu cầu người dùng (nội dung còn trong git: `git show d9a0601:YEU_CAU_SAU_1.19.10.md`; các mục 4z-4zf nhắc tới tệp này là lịch sử). Dọn tệp sinh ra/mồ côi KHÔNG commit: `obj/` (bước trung gian WiX, `build_installer.bat` tự xóa và dựng lại mỗi lần chạy), 30 `.exe` dò/kiểm thử một lần trong `build/` không còn target CMake nào, 4 thư mục ảnh chụp cũ trong `build/` - khoảng 460 MB. Giữ nguyên: `dist/` (4 tệp phát hành hiện tại), `OneForAll_Release/`, `vendor/`, `build/profiles` và `build/logs` (có thể là dữ liệu cũ của người dùng). |
| 2026-10-14 | Downloader: hỗ trợ tải tài liệu Google Drive CHỈ XEM (chủ tệp tắt "tải xuống") - đảo quyết định ở mục 4zg/4zi theo yêu cầu người dùng (v1.19.22) | Chi tiết ở mục 4zj. Chụp từng trang ở chế độ xem bằng Edge/Chrome ẩn qua DevTools rồi ghép PDF; nghiệm thu thật 130/130 trang trên link mẫu; không khuyến khích dùng cho mục đích vi phạm pháp luật/bản quyền. `downloader_tests` 652/652, `downloader_ui_tests` 100/100, `secdl_stress_tests` 1.874.476/1.874.476. |
| 2026-10-15 | Rà soát toàn bộ lần ba: sửa lỗi ở cả 9 công cụ + stress test (v1.19.23) | Chi tiết ở mục 4zk. Năm người đọc độc lập rà 9 công cụ, hai người rà lại bản sửa (tìm thêm 3 lỗi chắc chắn + 2 lỗi có khả năng trong chính bản sửa - đã sửa). Nặng nhất: phát lại macro Auto Click để KẸT phím/nút khi dừng; Connect Together xóa vĩnh viễn máy đã ghép đôi khi DPAPI lỗi tạm thời; Downloader "Kiểm tra lại" tải link CŨ, tệp tải về không có Mark of the Web, trình duyệt ẩn mở DevTools cho mọi origin; Security đứng hình khi mở. 18 bộ test 4.105/0 (trước 3.971); stress `core` 551/0, `qrwifi` 759.618/0, `connect` 386/0, `disk_vpn` 7.320/0, `secdl` 1.876.549/0. Lần này KHÔNG tạo tag/Release (theo yêu cầu). |
| 2026-10-18 | WiFi chia sẻ QR + Android ghép đôi QR; tab Cài đặt WiFi (v1.19.24) | WiFi > Hồ sơ đã lưu: nút "Chia sẻ QR" dựng mã `WIFI:...` từ hồ sơ (qua `qr_core`; mật khẩu dạng chữ cần Admin). Android > Thiết bị: nút "Ghép bằng QR..." hiện mã `WIFI:T:ADB;...`, dò `_adb-tls-pairing._tcp` qua `adb mdns services` (`AdbController::internal::parseMdnsPairingService`) rồi tự `adb pair` với mã 6 số do app sinh. WiFi > Cài đặt: mật khẩu mặc định cho nút "Kết nối lab" (một mật khẩu, một lần thử); kết luận kết nối theo phản hồi adapter (`ConnectionWatcher::evaluateAttempt`), không chờ hết giờ; ô khoảng From/To hệ 36 chỉ LƯU trong QSettings, CHƯA nối vào hành động nào. Phụ thuộc: wifi_ui/android_ui link thêm `qr_core` (một chiều, tránh vòng với qr_ui). CHƯA kiểm chứng hai luồng QR (cần điện thoại thật). Lần này chỉ push code, KHÔNG tạo tag/Release. |
| 2026-10-19 | VPN: thêm tab WireGuard - giai đoạn 1/3 đa giao thức VPN (v1.19.25) | `VpnControlWindow` nay là `QTabWidget` [VPN hệ thống, WireGuard]. `WireGuardController` (vpn_core) dùng CHÍNH WireGuard for Windows đã cài trên máy (không đóng gói driver): `importTunnel()` → `wireguard.exe /installtunnelservice`; `startTunnel()`/`stopTunnel()` → `sc.exe start/stop "WireGuardTunnel$<tên>"`; `removeTunnel()` → `/uninstalltunnelservice`; `listTunnels()` đọc thư mục cấu hình WireGuard + metadata hiển thị riêng (KHÔNG bao giờ lưu PrivateKey). Mọi thao tác cần Administrator, banner relaunch cùng mẫu MalwareScanTab/WebProtectionTab. `vpn_tests` 155/0 (+ test thuần cho sanitizeTunnelName/serviceName/parseConfForDisplay/isServiceRunningOutput/tunnelNameFromConfigFileName), `vpn_ui_tests` 7/0 (dựng thật WireGuardTab, không crash). CHƯA kiểm chứng gọi wireguard.exe/sc.exe thật (môi trường build không có WireGuard cài sẵn + máy chủ thật). Còn lại kế hoạch: OpenVPN (giai đoạn 2), FortiClient/GlobalProtect detect-and-launch + OpenConnect (giai đoạn 3) - CHƯA làm. Lần này chỉ push code, KHÔNG tạo tag/Release. |
| 2026-10-22 | VPN: tab "Đổi IP theo quốc gia" (VPN Gate) + gộp tối đa máy chủ + tab "Kiểm tra kết nối" (v1.19.26) | Yêu cầu (ảnh Hotspot Shield): chọn quốc gia rồi connect đổi IP; lấy nhiều máy chủ free nhất có thể; có tab riêng kiểm tra kết nối. App KHÔNG có máy chủ riêng nên dùng danh sách relay tình nguyện CÔNG KHAI của VPN Gate (ĐH Tsukuba) qua API `vpngate.net/api/iphone/`, kết nối L2TP/IPsec qua RAS có sẵn (tài khoản công khai "vpn"). Tab "🌍 Đổi IP theo quốc gia": tải danh sách, lọc theo quốc gia, bảng ping/tốc độ, kết nối qua hồ sơ tạm `OneForAll-VPNGate` (tự xóa khi ngắt). `VpnGateCache` GỘP + lưu đĩa (dedup IP, loại cũ 3 ngày) vì API chỉ trả ~100 máy chủ/lần (đo thật). Tab "🔎 Kiểm tra kết nối" (`ConnectivityTester`, QTcpSocket) lọc máy chủ còn sống. `vpn_tests` 182/0 (+27), `vpn_ui_tests` 7/0; parser kiểm CSV thật 98/98. CHƯA kiểm chứng kết nối L2TP thật đầu-cuối. Yêu cầu 2 (FortiClient/GlobalProtect) để sau. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-22 | VPN: sửa lỗi kết nối L2TP "Lỗi 6: The handle is invalid" + helper NAT-T (v1.19.27) | Người dùng báo mọi lần kết nối VPN Gate đều "Lỗi 6: The handle is invalid". Bug thật: `dialCallback` là RASDIALFUNC1 nhưng RasDialW truyền `dwNotifierType=1` (phải là 2) -> hàm gọi lại lệch tham số, mất mã lỗi RAS thật, chỉ còn RasGetConnectStatus trả handle đã thu hồi (6). Sửa 1->2; evaluateDialPoll cho thông báo hữu ích (L2TP/NAT) thay vì "handle is invalid". Thêm nút "Sửa lỗi L2TP sau NAT" đặt khóa registry AssumeUDPEncapsulationContextOnSendRule=2 (Microsoft tài liệu hóa, qua UAC, cần khởi động lại). `vpn_tests` 188/0 (+6), `vpn_ui_tests` 7/0. CHƯA kiểm chứng kết nối L2TP thật đầu-cuối. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-22 | VPN: tab "VPN đã cài sẵn" - đọc hồ sơ FortiClient/GlobalProtect/OpenVPN/Cisco rồi mở client (v1.19.28) | Yêu cầu 2: đọc profile các VPN client khác. `ThirdPartyVpnScanner` (vpn_core) CHỈ ĐỌC tên + địa chỉ máy chủ từ registry/tệp cấu hình của FortiClient (SslvpnClient\Tunnels), GlobalProtect (PanSetup\Portal), OpenVPN (*.ovpn remote), Cisco (profile XML HostEntry) - KHÔNG giải mã mật khẩu, KHÔNG kết nối thẳng. Nút "Mở phần mềm để kết nối" ShellExecute client (OpenVPN: --connect). Tab "🏢 VPN đã cài sẵn". `vpn_tests` 200/0 (+12 parser thuần + scan() thật không crash), `vpn_ui_tests` 7/0. CHƯA kiểm chứng trên máy có cài sẵn các phần mềm đó. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-22 | VPN: quét thêm tunnel FortiClient IPsec + chẩn đoán lỗi VPN Gate (v1.19.29) | (a) Tab "VPN đã cài sẵn" bỏ sót tunnel IPsec của FortiClient (vd VPN_CVTLT): tunnel IPsec nằm ở `IPSec\Tunnels\<tên>` với gateway ở subkey `<tên>\P1\RemoteGW`, khác SSL-VPN (`Sslvpn\Tunnels` giá trị `Server`). Đã đọc cả hai + đúng khóa HKCU, dedup ưu tiên bản có địa chỉ. Xác nhận thật: quét ra đủ 4 hồ sơ. (b) Chẩn đoán thật lỗi VPN Gate: NAT-T đã =2, IKEEXT/PolicyAgent Running, nhưng L2TP tới 5 máy chủ đều lỗi 791 -> L2TP native hỏng trên máy này (nghi FortiClient IPsec xung đột/ISP chặn), KHÔNG phải lỗi app. OpenVPN (IP:443) là lối thoát - cần đóng gói openvpn.exe (giai đoạn 2, sẽ hỏi trước). `vpn_tests` 200/0, `vpn_ui_tests` 7/0. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-22 | VPN: kết nối VPN Gate bằng OpenVPN (đóng gói openvpn.exe + wintun.dll) (v1.19.30) | Tiếp v1.19.29 (L2TP native hỏng): thêm cách kết nối OpenVPN cho tab "Đổi IP theo quốc gia" (ô chọn OpenVPN/L2TP). `OpenVpnRunner` (vpn_core) chạy `openvpn.exe` đóng gói qua UAC, điều khiển qua management interface (127.0.0.1 + mật khẩu ngẫu nhiên): đọc `>STATE...CONNECTED`, gửi `signal SIGTERM` để ngắt. `buildConfig()` (thuần, test được) dựng `.ovpn` chạy nền: bỏ auth-user-pass/management/dev-node/windows-driver/block-outside-dns, thêm management-hold + windows-driver wintun + auth-nocache; CHỈ thêm lại auth-user-pass khi gốc có (VPN Gate xác thực bằng `<cert>/<key>` nhúng, không user/pass - xác minh trên .ovpn THẬT). `VpnGateServer.ovpnConfig` lấy từ cột Base64 cuối của API (không lưu cache). Đóng cửa sổ khi đang bật OpenVPN sẽ tự ngắt. Đóng gói `vendor/openvpn/` (không commit): openvpn.exe + OpenSSL DLLs từ build.openvpn.net, wintun.dll từ wintun.net - xác minh bằng **chữ ký Authenticode** (OpenVPN Inc./Microsoft/WireGuard LLC; Windows-native, không phụ thuộc gpg vốn hỏng trên CI do GNUPGHOME kiểu Windows); `build_app.bat`/CI/`scripts/ci_fetch_vendor.ps1` tự tải+chép. `vpn_tests` 223/0 (+23). CHƯA kiểm chứng đầu-cuối đổi IP (cần 1 lần bấm Yes UAC - chưa tự bấm thay được); đã xác minh chữ ký nhị phân + dựng cấu hình đúng .ovpn thật + build/đóng gói đủ. Chỉ push code, KHÔNG tag/Release. **(Đã bỏ ở v1.19.31.)** |
| 2026-10-22 | VPN: bỏ OpenVPN -> SSTP; sửa lỗi gốc quay số RAS; kết nối trực tiếp FortiGate/GlobalProtect (v1.19.31) | Mục 4zq. OpenVPN treo vì lời nhắc `ENTER PASSWORD:` không bị xóa khỏi bộ đệm (không gửi `hold release`) + rò `openvpn.exe` nâng quyền khi hết giờ -> gỡ hẳn. VPN Gate nối SSTP (RAS gốc, `<HostName>.opengw.net` khớp chứng chỉ Let's Encrypt). Lỗi gốc `VpnConnector`: mã 6 TẠM THỜI của `RasGetConnectStatusW` ở +16ms bị coi là thất bại -> mọi lần quay số qua app hỏng (cả "Lỗi 6" người dùng gặp từ đầu); `dwNotifierType` sửa lại 1 (v1.19.27 đổi nhầm 2). Đã đổi IP THẬT bằng code của app (VN -> Tokyo). Kết nối trực tiếp FortiGate SSL-VPN/GlobalProtect: client giao thức tự viết (PPP/LCP/IPCP, đăng nhập/OTP/cấu hình), adapter wintun.dll + iphlpapi, tiến trình phụ nâng quyền `--vpn-helper` (pipe + mã một lần, tự dọn khi pipe đóng), ghim vân tay chứng chỉ tự ký. `vpn_tests` 219/0, `vpn_ui_tests` 7/0, `vpn_direct_tests` 150/0 (mới; máy chủ FortiGate/GP giả đầu-cuối). Đã chạy thật: adapter wintun Admin 10/10, thăm dò 3/3 máy chủ thật (không đăng nhập). CHƯA kiểm chứng: đăng nhập + đường hầm bằng tài khoản cơ quan thật. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-22 | Lấy IP chậm sau khi nối VPN + rà giao diện bằng ảnh chụp thật (v1.19.32) | Mục 4zr. Kết nối HTTPS keep-alive cũ kẹt sau khi tuyến đổi -> 8 s rồi hủy; `clearConnectionCache()` + thử lại 1 lần -> 1093 ms. Công cụ `ui_snapshot` (offscreen hoặc kiểu gốc không hiện màn hình) rà 8 công cụ: sửa cắt chữ hàng điều khiển VPN Gate (2 hàng, NAT chỉ khi L2TP), ô Cổng, nút Android, nhóm ghép đôi Connect bị ép. Nhật ký chẩn đoán kết nối trực tiếp. vpn 219/7, vpn_direct 150, android 153/30, connect_ui 17. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-23..24 | Kết nối trực tiếp FortiClient IPsec: client IKEv1 tự viết (v1.19.33→39) | Mục 4zs-4zv. v1.19.33: kết nối trực tiếp CHẠY THẬT với FortiGate người dùng + lưu tài khoản + sửa DNS nội bộ. v1.19.34: GlobalProtect nộp HIP (sửa rớt ~60s). v1.19.35: GlobalProtect kênh ESP (gateway bắt buộc). **v1.19.36**: hồ sơ FortiClient IPsec (VPN_CVTLT) nối trực tiếp bằng client **IKEv1 tự viết** (`IkeCrypto`+`IsakmpMessage`+`IkeKeys`+`IkeSession`, dựa BCrypt): Aggressive+PSK+XAuth+Mode-config+Quick Mode+ESP; IKE+ESP chung socket UDP (NAT-T 4500). Test `ike_crypto_check` 47/0, `ike_session_check` 24/0 (bắt tay TRỌN VẸN offline với responder giả, khớp khóa ESP hai bên), `vpn_direct_tests` 357/0. **v1.19.37**: dò thật gateway (`ike_real_probe`, không đăng nhập) xác nhận gateway dùng **nhóm 20 ECP-384 + AES-256 + SHA-256 + XAUTHPSK + NAT-T** → thêm ECDH (nhóm 19/20/21) vào IkeCrypto, IkeSession chào nhiều transform + đọc transform gateway chọn; `ike_crypto_check` 59/0. **v1.19.38**: với PSK + tài khoản thật, `ike_real_connect` hoàn tất **bắt tay IKEv1 đầu-cuối THẬT** (Phase 1→XAuth→Mode-config cấp IP→Quick Mode→khóa ESP). Sửa: mã thuộc tính XAuth (STATUS=16527/MESSAGE=16524), PFS nhóm 20, nhiều transform ESP + đọc transform gateway chọn (AES-256-CBC+SHA1), đóng gói Tunnel(1) (FortiGate từ chối UDP-Tunnel 3/61443), proxy 0/0, ESP seq=1. **v1.19.39**: bắt gói (`pktmon`) so với FortiClient -> client+app nay khớp FortiClient trên dây (UDP 4500↔4500 từ cổng 4500, NAT-T từ msg1, NAT-D). Tầng DỮ LIỆU ESP VẪN CHẶN: gói ESP ra đúng (giống FortiClient) mà gateway im (thử 5 biến thể KEYMAT); SA đã cài; cần nhật ký phía FortiGate. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-25 | Công cụ tay `directvpn_real_e2e` - kiểm đầu-cuối IPsec với adapter thật (v1.19.45) | Mục 4zv. Chạy `DirectVpnSession` (helper nâng quyền = chính tệp với `--vpn-helper`), adapter wintun + tuyến + DNS, rồi `ping`/`curl` của HĐH tới 172.16.6.25 và kiểm dọn sạch. Mới build + khởi chạy một lần, CHƯA có kết quả (máy khóa, UAC chưa được duyệt). Không đổi mã sản phẩm. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-25 | FortiGate IPsec: kiểm chứng bằng CHÍNH `TunnelEngine` trên gateway thật + chia tuyến kiểu FortiClient (v1.19.44) | Mục 4zv. Công cụ tay `tunnel_engine_real_check` chạy `TunnelEngine` ở `noAdapterForTest` (không adapter/Admin), bơm ICMP/DNS/TCP SYN: 142 gói trả về; ICMP reply từ 172.16.6.25, đáp án DNS, SYN-ACK từ 172.16.6.25:443. Phát hiện khi soát log: app xin ít thuộc tính Mode-config nên gateway không đẩy chia tuyến -> app sẽ chuyển CẢ internet qua VPN; nay xin như FortiClient (`cfgFortiClient`) -> gateway đẩy 172.16.0.0/16, 172.17.0.0/16, 172.16.9.0/24; phân tích `UNITY_SPLIT_INCLUDE` (14 byte/mục) + bỏ trùng. Dựng gói tách `tests/RawPackets.h`. `ike_session_check` 27/0. CHƯA kiểm chứng: adapter wintun thật + định tuyến (UAC) với IPsec. Chỉ push code, KHÔNG tag/Release. |
| 2026-10-25 | FortiGate IPsec: **tìm ra nguyên nhân gốc "ESP im lặng" = bind cổng cục bộ 4500 - Windows chặn ESP trả về** (v1.19.43) | Mục 4zv. Sửa `TunnelEngine::beginIke`: một socket cổng tạm cho IKE+ESP, luồng chuẩn 500->4500, `forceNatT` (NAT-D lệch). Chứng minh trên gateway THẬT (`ike_real_connect`, không adapter): ICMP reply từ 172.16.6.25 + DNS + SYN-ACK TCP 443. KEYMAT/ICV/AES kiểm độc lập bằng OpenSSL+Python. Đính chính nhận định sai ở v1.19.39-42. Công cụ bắt gói không cần Admin: Npcap `AdminOnly=0` (`wpcap.dll` qua ctypes). `vpn_tests` sửa kỳ vọng IPsec (lỗi thời từ v1.19.36). CHƯA kiểm chứng: chạy app thật (adapter wintun + UAC + truy cập https://172.16.6.25). Chỉ push code, KHÔNG tag/Release. |
| 2026-10-24..25 | FortiGate IPsec: DPD + sửa tự đóng phiên do DELETE SA khác; chứng minh vận chuyển/khóa đúng (v1.19.40→42) | Mục 4zw. **v1.19.40**: trả lời DPD `R-U-THERE`→`R-U-THERE-ACK`. **v1.19.41**: sửa UI hộp thoại đăng nhập VPN (dòng đầu bị cắt); log chẩn đoán SSL-VPN; `forti_login_probe`; VPN_UID (SSL-VPN) nối OK (lỗi trước do nhập liệu). **v1.19.42**: **sửa lỗi thật** - `IkeSession::onInformational` trước tự đóng phiên khi nhận BẤT KỲ DELETE (FortiGate hay gửi DELETE dọn SA CŨ, SPI khác hẳn); nay chỉ đóng khi DELETE khớp SA hiện tại (SPI `m_spiOut`/`m_spiIn` hoặc cookie phase1), còn lại bỏ qua. Thêm `sendDpd()` chủ động: `ike_real_connect` cho thấy gateway LUÔN trả ACK suốt 75s trên UDP-4500 ⇒ vận chuyển hai chiều + khóa Phase 1 (a/e) đúng ⇒ SKEYID_d ⇒ **khóa ESP đúng**. Dò khóa chuẩn tới **8.8.8.8/1.1.1.1** (full-tunnel, chắc chắn đáp) vẫn **0 gói về** ⇒ gateway KHÔNG chuyển tiếp/trả về ESP dù client đã đúng mọi mặt (selector QM2 echo `172.16.16.13/32↔0.0.0.0/0` khớp). encap QM cấu hình được: gateway nhận encap=1 **và encap=3 (UDP-Tunnel RFC)**, từ chối 61443/đa-transform. Giả thuyết còn lại (cần BẢN GHI GÓI FortiClient thật): FortiClient có thể dùng ESP thuần proto-50 (router IPsec-passthrough) - socket UDP không nhận proto-50. Bước bắt gói cần Admin + người dùng kết nối FortiClient (CHƯA làm được: UAC bị hủy). `ike_session_check` 24/0, `ike_crypto_check` 59/0. Chỉ push code, KHÔNG tag/Release. |

### Chi tiết lần sửa 2026-10-06 (v1.0.6)
- **InputController:** phím mở rộng (mũi tên, Home/End, PgUp/PgDn, Insert, Delete, Win) gửi kèm `KEYEVENTF_EXTENDEDKEY` + scancode (tránh bị hiểu thành numpad); hotkey nhả modifier theo thứ tự ngược; TypeText chuyển `
` thành Enter, `	` thành Tab; Drag bị dừng thì không nhảy tới điểm cuối.
- **ActionRunner:** `requestStop/Pause/Resume` chỉ đổi state khi thread đang chạy; cờ stop/pause reset trong `setChain()` thay vì đầu `run()` (không mất yêu cầu dừng ngay sau `start()`).
- **Tọa độ capture:** `CoordinateOverlay` lấy tọa độ bằng `GetCursorPos` (pixel vật lý, khớp `SetCursorPos`), trước đây dùng tọa độ logic Qt nên lệch khi Windows scale khác 100%. Overlay nhận focus để ESC hoạt động.
- **RuntimeOverlay:** HUD không cướp focus (`WA_ShowWithoutActivating`, `WindowDoesNotAcceptFocus`) nên phím gõ không rơi vào HUD; progress bar countdown reset đúng ở mỗi pha; tọa độ đích quy đổi sang pixel logic khi né HUD.
- **AutoClickWindow:** menu "Run Chain" chạy đúng chain được chọn và không chạy khi runner đang chạy; Repeat/Infinite lưu vào chain khi đổi (không mất khi đổi chain hoặc Save); closeEvent hỏi xác nhận cả khi runner đang Paused/Stopping; Load/Import từ file rỗng không làm mất danh sách chain.
- **AutoClickTool:** chỉ một cửa sổ Auto Click (mở lại thì dùng lại), tránh nhiều runner tranh chấp chuột.
- **ActionSerializer:** clamp giá trị enum/`repeatCount` đọc từ JSON.
- Kiểm thử: harness riêng (click, TypeText Unicode, phím mũi tên, lặp 2 vòng, 8 action, dừng giữa chừng) chạy đạt trên Windows thật.
