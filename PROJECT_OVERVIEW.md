# One For ALL - Mô tả toàn bộ dự án

> File này mô tả tổng thể dự án và **được cập nhật sau mỗi lần sửa code** (xem mục "Lịch sử thay đổi" cuối file).
> Tài liệu kiến trúc chi tiết và changelog tính năng nằm ở [README.md](README.md); đặc tả thiết kế ở [OneForAll_AutoClick_Design.md](OneForAll_AutoClick_Design.md).

## 1. Dự án là gì
Ứng dụng desktop Windows (C++20, Qt 6.11 MinGW, CMake + Ninja) dạng launcher gồm 9 công cụ. Hiện chỉ **Auto Click** hoàn chỉnh; 8 tool còn lại (Connect Together, QR Tools, Disk Cleanup, Android Phone Control, Downloader, Security Gateway, VPN & Location, WiFi Connection) là `PlaceholderTool` (màn hình "đang phát triển").

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

## 5. Build và chạy
`build_app.bat` (cần Qt 6.11.1 MinGW tại `D:\Qt`, CMake, Ninja) → `build\OneForAll.exe`. `run_app.bat` chạy `OneForAll_Release\OneForAll.exe`. Thư mục `build/`, `OneForAll_Release/`, `logs/` không được commit (xem `.gitignore`).

## 6. Vấn đề đã biết (chưa sửa)
- Pause chưa có UI; `requestPause()` ghi đè state từ thread GUI.
- `requestStop()` luôn đặt state `Stopping`, kể cả khi runner không chạy.
- `closeEvent` chỉ hỏi xác nhận khi state `Running`; `wait(2000)` chạy trên GUI thread.
- `AutoClickWindow` không có `WA_DeleteOnClose` nên mỗi lần mở không được giải phóng.
- Profile lưu text (kể cả mật khẩu trong TypeText) dạng plain text.

## 7. Quy trình làm việc
Mỗi lần sửa code: commit riêng với message mô tả rõ, và cập nhật file này (mục 3-6 nếu cấu trúc/hành vi đổi) cùng bảng lịch sử bên dưới.

## 8. Lịch sử thay đổi
| Ngày | Commit | Nội dung |
|---|---|---|
| 2026-10-05 | Initial | Đưa toàn bộ source lên Git, thêm `.gitignore` và `PROJECT_OVERVIEW.md` |
