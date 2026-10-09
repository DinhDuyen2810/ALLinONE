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
| `src/tools/qr/QRPayload.*` | Tạo và phân tích nội dung: Text, URL, WiFi, Email, SMS, Phone, Geo, vCard/MECARD |
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
| `src/tools/downloader/{DirectDownloadTab,PageScanTab,PlatformVideoTab,DownloaderWindow}.*` | UI 3 tab Downloader |
| `src/tools/downloader/DownloaderTool.*` | `ITool` của Downloader (một cửa sổ duy nhất) |
| `src/third_party/` | qrcodegen (MIT), quirc (ISC), xem `THIRD_PARTY.md` |
| `vendor/scrcpy/` | scrcpy + adb đóng gói (Apache-2.0, KHÔNG commit Git) - xem `THIRD_PARTY.md` |
| `vendor/yt-dlp/` | yt-dlp + ffmpeg/ffprobe đóng gói (Unlicense/GPLv3+ kết hợp + GPL, KHÔNG commit Git) - xem `THIRD_PARTY.md` |
| `tests/` | `qr_tests`/`qr_ui_tests`, `wifi_tests`/`wifi_ui_tests`, `connect_tests`/`connect_ui_tests` (506 + 2, ghép đôi 2 chiều thật qua TCP, ổn định qua 40+ lần chạy liên tiếp), `diskcleanup_tests`/`diskcleanup_ui_tests` (176 + 19 kiểm tra, có xóa file thật qua Thùng rác, liệt kê phân vùng thật), `android_tests`/`android_ui_tests` (45 + 7 kiểm tra, có gọi `adb devices` thật - chỉ đọc), `vpn_tests`/`vpn_ui_tests` (41 + 6 kiểm tra, có gọi `Get-VpnConnection` thật - chỉ đọc), `security_tests`/`security_ui_tests` (76 + 6 kiểm tra, có gọi `Get-MpComputerStatus`/đọc hosts file thật - chỉ đọc), `downloader_tests`/`downloader_ui_tests` (63 + 8 kiểm tra, có tải thật một tệp công khai nhỏ qua HTTP thật) |
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
- **Tạo mã:** 8 loại nội dung (Văn bản, URL, WiFi, Email, SMS, Điện thoại, Vị trí, vCard). Tùy chọn: mức sửa lỗi L/M/Q/H, kích thước ảnh, viền trắng, màu mã/nền, ô bo tròn, logo giữa mã (tự dùng mức H). Xem trước trực tiếp, cảnh báo độ tương phản, tự giải mã lại ảnh vừa tạo để bảo đảm quét được. Lưu PNG/JPG/BMP/SVG, sao chép vào clipboard.
- **Quét mã:** từ file ảnh, clipboard (Ctrl+V), kéo-thả, chụp vùng màn hình, camera. Nhận nhiều mã trong một ảnh, vẽ viền quanh mã, phân tích nội dung (URL, WiFi, Email, vCard...), nút Sao chép / Mở (có hỏi xác nhận) / Lưu .txt / Tạo lại mã.
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

**Việc còn lại - thuộc về người dùng (chủ dự án), không thể tự động hóa**: nộp đơn xin SignPath (biểu
mẫu tại signpath.org/apply, hoặc OSS Request Form gửi email - cần xác minh danh tính người duy trì dự án,
không phải việc một công cụ tự động làm thay được). Thông tin đã chuẩn bị sẵn: Project Name "One for
ALL", Repository URL/Homepage `https://github.com/DinhDuyen2810/ALLinONE`, Download URL trang Releases,
Privacy Policy URL `PRIVACY.md` (mới thêm, trung thực đối chiếu đúng hành vi code), Maintainer Type
"Individual", Build System "GitHub Actions" (`.github/workflows/build-installer.yml`). **Lưu ý trung
thực**: SignPath có yêu cầu "uy tín nhất định" cho dự án - đây là dự án MỚI, có thể bị từ chối ở lần nộp
đầu, không phải kết quả chắc chắn dù đã đáp ứng đủ điều kiện kỹ thuật.

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

## 5. Build và chạy
`build_app.bat` (cần Qt 6.11.1 MinGW tại `D:\Qt`, CMake, Ninja) → `build\OneForAll.exe`, tự đồng bộ
sang `OneForAll_Release\` (bản `run_app.bat` chạy). Nếu có `vendor\scrcpy\` (xem `THIRD_PARTY.md` để
tải bản scrcpy-win64 chính thức) thì cũng tự đóng gói vào `scrcpy\` cạnh file exe cho tính năng Android
Phone Control. Tương tự, nếu có `vendor\yt-dlp\` (xem `THIRD_PARTY.md` để tải yt-dlp.exe + ffmpeg/ffprobe
chính thức) thì tự đóng gói vào `yt-dlp\` cạnh file exe cho tính năng Downloader (tab Video nền tảng).
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
5. Tạo GitHub Release cho tag vừa đẩy (trang GitHub của repo → Releases → "Draft a new release" → chọn
   tag `v1.16.0` → đính kèm CẢ 4 FILE trong `dist\` làm asset → Publish). Có thể làm qua
   `gh release create v1.16.0 dist/OneForAll_Setup.exe dist/OneForAll_Setup.msi dist/OneForAll_Setup_exe.zip dist/OneForAll_Setup_msi.zip --notes "..."`
   nếu đã cài GitHub CLI, nhưng web UI không cần cài gì thêm.
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
(cho phép cài đè/gỡ đúng phiên bản cũ) - `AppVersion` trong `installer/OneForAll.iss` cần tự cập nhật
bằng tay mỗi khi đổi phiên bản (hiện khớp `README.md`), không tự động đồng bộ từ CMake.

## 6. Vấn đề đã biết (chưa sửa)
- **Connect Together - ghép đôi**: mã 9 số được dùng thẳng làm khóa (PBKDF2, salt cố định, không PAKE). Ai bắt
  được gói ghép đôi trong LAN đúng lúc ghép có thể dò offline 10^9 mã để lấy khóa dài hạn. Chỉ ghép đôi
  trên mạng tin cậy; thiết kế lại (trao đổi khóa có xác thực) là việc riêng, chưa làm.
- Connect Together: server lắng nghe trên mọi card mạng (`AnyIPv4`); bố cục nhiều màn hình không chữ nhật
  chưa xử lý; chưa từng chạy thử trên hai máy thật (xem mục 4o).
- Auto Click: Pause/Resume có ở tầng runner nhưng chưa có nút trên UI; chưa có checkbox bật/tắt từng hành
  động (trường `enabled` chỉ đọc từ JSON, nay được giữ nguyên khi lưu); nội dung "Gõ văn bản" lưu dạng chữ
  thường trong hồ sơ; HUD né sai trên nhiều màn hình khác DPI.
- Android: `adb pair` vẫn chạy đồng bộ trên luồng giao diện (có thể đứng tới ~25 giây); sau khi ghép đôi
  không dây chưa có bước `adb connect`; hai bản ứng dụng chạy cùng lúc dùng chung một daemon adb.
- Security Gateway: các lệnh Defender ngắn vẫn chạy đồng bộ trên luồng giao diện (đứng 1-3 giây khi mở cửa sổ
  hoặc bật/tắt); hủy quét chỉ dừng `powershell.exe`, chưa chắc dừng lần quét bên trong dịch vụ Defender;
  clipboard không được khôi phục sau khi dán lệnh.
- VPN: chưa có tùy chọn `-L2tpPsk`/`-SplitTunneling`; không xóa được hồ sơ đang kết nối.
- WiFi: hồ sơ mới luôn đặt tên theo SSID kể cả khi hồ sơ cũ có tên khác.
- Tự cập nhật: nếu người dùng mở hai cửa sổ ứng dụng cùng lúc, trình cài đặt `.exe` im lặng có thể tự hủy
  (thấy bản còn lại đang chạy) - ứng dụng vẫn mở lại và hỏi cập nhật lần sau.
- File cài đặt chưa ký số (đang chờ SignPath, mục 4m).

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
