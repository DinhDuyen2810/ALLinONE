# One For ALL - Mô tả toàn bộ dự án

> File này mô tả tổng thể dự án và **được cập nhật sau mỗi lần sửa code** (xem mục "Lịch sử thay đổi" cuối file).
> Tài liệu kiến trúc chi tiết và changelog tính năng nằm ở [README.md](README.md); đặc tả thiết kế ở [OneForAll_AutoClick_Design.md](OneForAll_AutoClick_Design.md).

## 1. Dự án là gì
Ứng dụng desktop Windows (C++20, Qt 6.11 MinGW, CMake + Ninja) dạng launcher gồm 9 công cụ. Hiện **Auto Click**, **QR Tools**, **WiFi Connection**, **Connect Together** (tham khảo Mouse without Borders), **Disk Cleanup** (tham khảo CCleaner/BleachBit/Windows Disk Cleanup), **Android Phone Control** (dựa trên scrcpy), **VPN & Location** (VPN gốc Windows + vị trí theo IP) và **Security Gateway** (tham khảo Kaspersky, xây trên Windows Defender có sẵn) hoàn chỉnh; 1 tool còn lại (Downloader) là `PlaceholderTool` (màn hình "đang phát triển").

## 2. Luồng khởi động
`main.cpp` đặt Per-Monitor DPI v2 → `QApplication` → `Logger` ghi `logs/app.log` → `MainWindow`.
`MainWindow::registerTools()` đăng ký tool vào singleton `ToolManager`, đổ lên sidebar. Bấm "Mở cửa sổ" gọi `ITool::createWindow()` tạo cửa sổ độc lập.

## 3. Cấu trúc mã nguồn
| Thư mục / file | Vai trò |
|---|---|
| `src/main.cpp` | Điểm vào, DPI awareness |
| `src/core/Tool.h` | Interface `ITool` (id, name, description, icon, `createWindow()`) |
| `src/core/ToolManager.*` | Registry tool + `PlaceholderTool` |
| `src/core/Logger.*` | Log thread-safe ra `logs/app.log`, `logs/autoclick.log` |
| `src/core/IconHelper.h` | Vẽ icon badge nền trắng bo góc |
| `src/ui/MainWindow.*` | Sidebar + thẻ thông tin + nút mở tool |
| `src/tools/autoclick/model/` | `Action`, `ActionChain` (struct thuần, không phụ thuộc UI) |
| `src/tools/autoclick/storage/` | `ActionSerializer`: chain ↔ JSON (`profiles/default.json`) |
| `src/tools/autoclick/engine/` | `ActionRunner` (QThread) và `InputController` (Win32 `SendInput`/`SetCursorPos`) |
| `src/tools/autoclick/capture/` | `CoordinateOverlay`, `MouseCapture`: bắt tọa độ toàn desktop |
| `src/tools/autoclick/overlay/` | `RuntimeOverlay`: HUD trạng thái khi chạy |
| `src/tools/autoclick/widgets/` | `ChainListWidget`, `ActionListWidget`, `ActionEditorWidget` |
| `src/tools/autoclick/AutoClick*.{h,cpp}` | `AutoClickTool` (ITool) và `AutoClickWindow` (điều phối UI 3 cột) |
| `src/tools/qr/QRCodec.*` | Lõi QR: mã hóa (qrcodegen), render ảnh/SVG, giải mã (quirc) |
| `src/tools/qr/QRPayload.*` | Tạo và phân tích nội dung: Text, URL, WiFi, Email, SMS, Phone, Geo, vCard/MECARD |
| `src/tools/qr/QRWindow.*`, `QRGenerateTab.*`, `QRScanTab.*`, `QRHistoryTab.*` | UI QR Tools: 3 tab Tạo / Quét / Lịch sử |
| `src/tools/qr/QRHistoryStore.*` | Lịch sử tạo/quét, lưu `profiles/qr_history.json` (tối đa 300 mục) |
| `src/tools/qr/QRImageView.*`, `ScreenSnipOverlay.*` | Khung xem ảnh kéo-thả có viền mã; chụp vùng màn hình |
| `src/tools/qr/QRTool.*` | `ITool` của QR (một cửa sổ duy nhất) |
| `src/tools/wifi/model/` | `WifiNetwork`, `WifiProfile`, `WifiSecurity` (struct/enum thuần) |
| `src/tools/wifi/engine/WlanProfileXml.*` | Dựng/phân tích XML hồ sơ WLAN (thuần Qt, test được không cần phần cứng) |
| `src/tools/wifi/engine/WlanController.*` | Lớp duy nhất gọi Windows WLAN API (wlanapi.dll): quét, hồ sơ, kết nối, trạng thái |
| `src/tools/wifi/WifiWindow.*`, `NetworksTab.*`, `ProfilesTab.*`, `ConnectDialog.*` | UI: tab Mạng xung quanh / Hồ sơ đã lưu, hộp thoại kết nối |
| `src/tools/wifi/engine/SpeedTestRunner.*` | Đo tốc độ mạng qua HTTP (ping/jitter/download/upload), thuần Qt Network |
| `src/tools/wifi/SpeedTestTab.*` | Tab "Đo tốc độ mạng" |
| `src/tools/connect/model/` | `ScreenSide`, `PeerInfo` (struct/enum thuần) |
| `src/tools/connect/engine/PairingCode.*` | Mã ghép đôi 9 số, sinh/kiểm tra/định dạng (thuần Qt) |
| `src/tools/connect/engine/ProtocolMessage.*` | Khung thông điệp nhị phân giữa 2 máy (thuần Qt, QDataStream) |
| `src/tools/connect/engine/CryptoSession.*` | AES-256-GCM qua Windows CNG (bcrypt.dll), suy khóa PBKDF2 |
| `src/tools/connect/model/PairedPeer.h` | Máy đã ghép đôi, lưu lâu dài (id, tên, khóa dài hạn, vị trí) |
| `src/tools/connect/engine/PeerStore.*` | Lưu/nạp danh sách peer đã ghép đôi, `profiles/connect_peers.json` |
| `src/tools/connect/engine/NetworkSession.*` | Phiên TCP đóng khung + mã hóa đầu-cuối (CryptoSession) |
| `src/tools/connect/engine/PeerDiscovery.*` | Khám phá máy trong LAN qua UDP broadcast (chỉ thông tin công khai) |
| `src/tools/connect/engine/EdgeDetector.*` | Toán học thuần phát hiện chuột chạm biên màn hình để chuyển quyền điều khiển |
| `src/tools/connect/engine/InputInjector.*` | Tiêm chuột/phím nhận được lên máy này (SendInput/SetCursorPos) |
| `src/tools/connect/engine/InputHook.*` | Hook bàn phím/chuột toàn cục (WH_MOUSE_LL/WH_KEYBOARD_LL), chỉ "nuốt" input khi đang chủ động chia sẻ |
| `src/tools/connect/engine/LocalIdentityStore.*` | Định danh ổn định của máy này, `profiles/connect_identity.json` |
| `src/tools/connect/engine/ConnectSessionController.*` | Điều phối toàn bộ: ghép đôi, kết nối lại, biên màn hình, hook/injector, clipboard |
| `src/tools/connect/ConnectWindow.*`, `PairingTab.*`, `PeersTab.*` | UI: tab Ghép đôi / Máy đã ghép đôi, banner trạng thái, log |
| `src/tools/connect/ConnectTool.*` | `ITool` của Connect Together (một cửa sổ duy nhất) |
| `src/tools/diskcleanup/model/` | `CleanupCategory`, `CleanupItem` (struct/enum thuần) |
| `src/tools/diskcleanup/engine/CategoryRegistry.*` | Danh sách hạng mục dọn dẹp đã biết + phân giải đường dẫn thật trên máy (thuần Qt, test được) |
| `src/tools/diskcleanup/engine/CleanupScanner.*` | Quét hạng mục trên QThread riêng, chỉ đọc (không xóa) |
| `src/tools/diskcleanup/engine/RecycleBinOps.*` | Lớp duy nhất gọi Shell API Thùng rác (SHFileOperationW/SHQueryRecycleBinW/SHEmptyRecycleBinW) |
| `src/tools/diskcleanup/engine/CleanupExecutor.*` | Thực hiện xóa (qua Thùng rác mặc định) trên QThread riêng |
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
| `src/tools/android/engine/AdbController.*` | Lớp duy nhất gọi `adb.exe` đóng gói kèm: liệt kê/ghép đôi/kết nối thiết bị |
| `src/tools/android/engine/ScrcpyLauncher.*` | Khởi chạy/quản lý tiến trình `scrcpy.exe` (gương + điều khiển màn hình thời gian thực) |
| `src/tools/android/AndroidUiStyle.h` | Style Light Theme dùng chung cho UI Android (giống các `*UiStyle.h` khác) |
| `src/tools/android/WirelessPairDialog.*` | Hộp thoại ghép đôi gỡ lỗi không dây (Android 11+): nhập IP:Cổng + mã 6 số |
| `src/tools/android/DevicesTab.*` | Danh sách thiết bị, tùy chọn điều khiển (độ phân giải/bitrate/fps/ghi hình...), nút Điều khiển |
| `src/tools/android/AndroidControlWindow.*` | Cửa sổ Android Phone Control |
| `src/tools/android/AndroidControlTool.*` | `ITool` của Android Phone Control (một cửa sổ duy nhất) |
| `src/core/WinElevation.*` | Kiểm tra/khởi chạy lại với quyền Administrator (Win32, dùng chung Disk Cleanup + VPN) |
| `src/core/PowerShellRunner.*` | Chạy script PowerShell qua `-EncodedCommand` (Base64 UTF-16LE), trả stdout/stderr/mã lỗi (dùng chung) |
| `src/tools/vpn/model/VpnProfile.h` | `VpnProfile`, `VpnTunnelType` (struct/enum thuần, không có trường mật khẩu) |
| `src/tools/vpn/engine/VpnController.*` | Thêm/liệt kê/xóa hồ sơ VPN qua PowerShell (`Add`/`Get`/`Remove-VpnConnection`), lưu nhãn quốc gia riêng |
| `src/tools/vpn/engine/VpnConnector.*` | Kết nối/ngắt kết nối VPN qua `rasdial.exe` trên QThread riêng (nhận username/password lúc chạy) |
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
| `src/third_party/` | qrcodegen (MIT), quirc (ISC), xem `THIRD_PARTY.md` |
| `vendor/scrcpy/` | scrcpy + adb đóng gói (Apache-2.0, KHÔNG commit Git) - xem `THIRD_PARTY.md` |
| `tests/` | `qr_tests`/`qr_ui_tests`, `wifi_tests`/`wifi_ui_tests`, `connect_tests`/`connect_ui_tests` (506 + 2, ghép đôi 2 chiều thật qua TCP, ổn định qua 40+ lần chạy liên tiếp), `diskcleanup_tests`/`diskcleanup_ui_tests` (176 + 19 kiểm tra, có xóa file thật qua Thùng rác, liệt kê phân vùng thật), `android_tests`/`android_ui_tests` (45 + 7 kiểm tra, có gọi `adb devices` thật - chỉ đọc), `vpn_tests`/`vpn_ui_tests` (41 + 6 kiểm tra, có gọi `Get-VpnConnection` thật - chỉ đọc), `security_tests`/`security_ui_tests` (76 + 6 kiểm tra, có gọi `Get-MpComputerStatus`/đọc hosts file thật - chỉ đọc) |
| `assets/resources.qrc`, `icon/` | Icon nhúng vào exe |
| `profiles/default.json` | Profile mặc định (chain mẫu) |
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

## 5. Build và chạy
`build_app.bat` (cần Qt 6.11.1 MinGW tại `D:\Qt`, CMake, Ninja) → `build\OneForAll.exe`, tự đồng bộ
sang `OneForAll_Release\` (bản `run_app.bat` chạy). Nếu có `vendor\scrcpy\` (xem `THIRD_PARTY.md` để
tải bản scrcpy-win64 chính thức) thì cũng tự đóng gói vào `scrcpy\` cạnh file exe cho tính năng Android
Phone Control. Thư mục `build/`, `OneForAll_Release/`, `vendor/`, `logs/` không được commit (xem `.gitignore`).

## 6. Vấn đề đã biết (chưa sửa)
- Pause/Resume đã an toàn ở tầng runner nhưng chưa có nút trên UI.
- Chưa có hotkey dừng toàn cục (chỉ nút Stop trên cửa sổ và HUD).
- Profile lưu text (kể cả mật khẩu trong TypeText) dạng plain text.
- Không có checkbox bật/tắt từng action trên UI (field `enabled` chỉ đọc từ JSON).
- Việc quy đổi tọa độ cho HUD giả định mọi màn hình cùng DPR với màn hình chính.

## 7. Quy trình làm việc
Mỗi lần sửa code: commit riêng với message mô tả rõ, và cập nhật file này (mục 3-6 nếu cấu trúc/hành vi đổi) cùng bảng lịch sử bên dưới.

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
