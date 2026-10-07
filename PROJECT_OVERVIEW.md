# One For ALL - Mô tả toàn bộ dự án

> File này mô tả tổng thể dự án và **được cập nhật sau mỗi lần sửa code** (xem mục "Lịch sử thay đổi" cuối file).
> Tài liệu kiến trúc chi tiết và changelog tính năng nằm ở [README.md](README.md); đặc tả thiết kế ở [OneForAll_AutoClick_Design.md](OneForAll_AutoClick_Design.md).

## 1. Dự án là gì
Ứng dụng desktop Windows (C++20, Qt 6.11 MinGW, CMake + Ninja) dạng launcher gồm 9 công cụ. Hiện **Auto Click**, **QR Tools**, **WiFi Connection**, **Connect Together** (tham khảo Mouse without Borders) và **Disk Cleanup** (tham khảo CCleaner/BleachBit/Windows Disk Cleanup) hoàn chỉnh; 4 tool còn lại (Android Phone Control, Downloader, Security Gateway, VPN & Location) là `PlaceholderTool` (màn hình "đang phát triển").

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
| `src/third_party/` | qrcodegen (MIT), quirc (ISC), xem `THIRD_PARTY.md` |
| `tests/` | `qr_tests`/`qr_ui_tests`, `wifi_tests`/`wifi_ui_tests`, `connect_tests`/`connect_ui_tests` (506 + 2, ghép đôi 2 chiều thật qua TCP, ổn định qua 40+ lần chạy liên tiếp), `diskcleanup_tests`/`diskcleanup_ui_tests` (156 + 19 kiểm tra, có xóa file thật qua Thùng rác, liệt kê phân vùng thật) |
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
  bằng `DuplicateFinder` (nhóm theo kích thước rồi hash SHA-256 nội dung để xác nhận trùng thật); hiển
  thị theo cây (nhóm → từng tệp có checkbox), mặc định GIỮ LẠI bản đầu mỗi nhóm, tick sẵn các bản còn
  lại để xóa vào Thùng rác.
- **Quản lý phân vùng** (`PartitionTab`): liệt kê phân vùng thật (`PartitionManager::listPartitions()`,
  chỉ đọc, không cần quyền Administrator) với dung lượng/còn trống/loại/cờ khởi động-hệ thống; banner
  cảnh báo rủi ro mất dữ liệu luôn hiển thị; nếu chưa chạy với quyền Administrator thì hiện banner +
  nút "Chạy lại với quyền Quản trị..." (ShellExecuteW verb "runas" - UAC chuẩn, không âm thầm); chọn 1
  phân vùng → "Tra kích thước có thể đổi tới..." gọi `Get-PartitionSupportedSize` (cần quyền
  Administrator) → nếu được, hiện khoảng Min-Max và ô nhập kích thước mới (GB); nút "Đổi kích thước"
  CHỈ bật sau khi gõ đúng tên ổ đĩa (hoặc `DISKx-PARTy` nếu không có tên ổ) vào ô xác nhận riêng, cộng
  thêm 1 hộp thoại cảnh báo Yes/No nữa trước khi thực sự chạy `Resize-Partition` qua `PartitionResizer`
  (QThread riêng, không chặn UI, co giãn ổ lớn có thể mất vài phút).

**Đã xong, đã test (156 kiểm tra lõi + 19 kiểm tra UI):**
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
- `CleanupExecutor`: xóa trên QThread riêng, test xóa file thật thành công qua Thùng rác.
- `DiskSpaceInfo`: đọc dung lượng ổ đĩa thật (QStorageInfo, chỉ đọc, an toàn) - xác nhận trên máy này.
- `LargeFileScanner`: tìm tệp lớn theo ngưỡng, sắp xếp giảm dần, giới hạn số kết quả - test bằng tệp giả.
- `DuplicateFinder`: nhóm theo kích thước thật của writeFile (toàn ký tự giống nhau), xác nhận tệp cùng
  kích thước nhưng khác nội dung KHÔNG bị coi là trùng (bước hash hoạt động đúng), ngưỡng tối thiểu lọc
  đúng, `wastedBytes()` tính đúng, dừng giữa chừng/thư mục không tồn tại không crash. Cũng loại symlink
  khỏi quét (`QDir::NoSymLinks`) - test hồi quy tương tự lỗi `.lnk` của `CleanupScanner` ở trên.
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

## 5. Build và chạy
`build_app.bat` (cần Qt 6.11.1 MinGW tại `D:\Qt`, CMake, Ninja) → `build\OneForAll.exe`. `run_app.bat` chạy `OneForAll_Release\OneForAll.exe`. Thư mục `build/`, `OneForAll_Release/`, `logs/` không được commit (xem `.gitignore`).

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
