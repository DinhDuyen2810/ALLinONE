# One For ALL - Mô tả toàn bộ dự án

> File này mô tả tổng thể dự án và **được cập nhật sau mỗi lần sửa code** (xem mục "Lịch sử thay đổi" cuối file).
> Tài liệu kiến trúc chi tiết và changelog tính năng nằm ở [README.md](README.md); đặc tả thiết kế ở [OneForAll_AutoClick_Design.md](OneForAll_AutoClick_Design.md).

## 1. Dự án là gì
Ứng dụng desktop Windows (C++20, Qt 6.11 MinGW, CMake + Ninja) dạng launcher gồm 9 công cụ. Hiện **Auto Click**, **QR Tools** và **WiFi Connection** hoàn chỉnh; 6 tool còn lại (Connect Together, Disk Cleanup, Android Phone Control, Downloader, Security Gateway, VPN & Location, WiFi Connection) là `PlaceholderTool` (màn hình "đang phát triển").

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
| `src/tools/wifi/WifiTool.*` | `ITool` của WiFi (một cửa sổ duy nhất) |
| `src/third_party/` | qrcodegen (MIT), quirc (ISC), xem `THIRD_PARTY.md` |
| `tests/` | `qr_tests`/`qr_ui_tests`, `wifi_tests` (lõi XML WLAN, không GUI), `wifi_ui_tests` (khói UI WiFi + SpeedTestRunner, đã chạy thật trên Internet) |
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
