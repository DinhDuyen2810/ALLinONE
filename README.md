# ONE FOR ALL - BỘ CÔNG CỤ ĐA NĂNG DESKTOP

> **Phiên bản:** 1.0.5  
> **Ngôn ngữ:** C++20  
> **GUI Framework:** Qt 6.11.1 (MinGW 64-bit)  
> **Hệ điều hành:** Windows 10 / Windows 11  
> **Build System:** CMake + Ninja  

---

## 1. Tổng quan dự án

`One for ALL` là nền tảng ứng dụng desktop dạng **modular suite**, quy tụ nhiều công cụ tiện ích độc lập vào một giao diện điều phối duy nhất. Mỗi công cụ hoạt động độc lập trong cửa sổ riêng (`QWidget* createWindow()`), không gây ảnh hưởng lẫn nhau và dễ dàng mở rộng thêm các module mới trong tương lai.

### Danh mục các module:
1. **Auto Click** *(Đã hoàn thiện giai đoạn 1)*: Tự động hóa chuỗi thao tác chuột & bàn phím chuyên nghiệp.
2. **Connect Together** *(Đã hoàn thiện)*: Dùng chung 1 chuột/bàn phím điều khiển nhiều máy qua mạng LAN (tham khảo Mouse without Borders) - ghép đôi bằng mã bảo mật, kênh truyền mã hóa AES-256-GCM, đồng bộ clipboard.
3. **QR Tools** *(Đã hoàn thiện)*: Tạo & quét mã QR đa năng (text/URL/WiFi/Email/SMS/Phone/Geo/vCard, màu, logo, PNG/JPG/SVG; quét từ ảnh/clipboard/màn hình/camera; lịch sử).
4. **Disk Cleanup** *(Đã hoàn thiện)*: Dọn dẹp tệp rác hệ thống theo hạng mục (tệp tạm, cache, Prefetch, tệp gần đây...) qua Thùng rác, xem dung lượng từng ổ đĩa, tìm & xóa tệp lớn, tìm & xóa tệp trùng lặp (hash nội dung), quản lý/đổi kích thước phân vùng đĩa (qua Resize-Partition của Windows).
5. **Android Phone Control** *(Đã hoàn thiện)*: Gương và điều khiển màn hình Android thời gian thực (chạm/phím/chuột) qua USB hoặc Wi-Fi, dựa trên adb + scrcpy; độ phân giải/bitrate/fps tùy chỉnh, ghép đôi không dây, ghi hình phiên điều khiển.
6. **Downloader** *(Dự kiến)*: Tải media đa luồng.
7. **Security Gateway** *(Dự kiến)*: Giám sát kết nối mạng an toàn.
8. **VPN & Location** *(Dự kiến)*: Quản lý mạng riêng ảo & định vị.
9. **WiFi Connection** *(Đã hoàn thiện)*: Quét mạng xung quanh, quản lý hồ sơ & mật khẩu WiFi đã lưu trên máy, kết nối bằng mật khẩu nhập trực tiếp (không có tính năng dò mật khẩu), đo tốc độ mạng thực tế (ping/jitter/download/upload).

---

## 2. Kiến trúc hệ thống (Architecture)

### 2.1. Kiến trúc Shell & Module mở rộng (Plugin/Tool Architecture)
Ứng dụng sử dụng mô hình **Inversion of Control (IoC)** và **Factory Pattern** thông qua interface `ITool`:

```text
               +----------------------------------+
               |           MainWindow             |
               +-----------------+----------------+
                                 |
                                 v
               +----------------------------------+
               |          ToolManager             |
               +-----------------+----------------+
                                 |
           +---------------------+---------------------+
           |                     |                     |
           v                     v                     v
     AutoClickTool       ConnectTogetherTool        QRTool ...
   (createWindow())        (createWindow())      (createWindow())
           |
           v
   [AutoClickWindow]
 (Cửa sổ độc lập)
```

- **`ITool`**: Interface định nghĩa danh tính, tên, icon, trạng thái sẵn sàng và phương thức tạo cửa sổ `createWindow()`.
- **`ToolManager`**: Quản lý đăng ký, tra cứu và duyệt danh sách các tool khả dụng.
- **`MainWindow`**: Sidebar điều hướng hiện đại, hiển thị chi tiết thông tin và khởi chạy từng tool thành các cửa sổ độc lập mà không nhồi nhét UI vào cùng một khung nhìn.

---

### 2.2. Kiến trúc Module Auto Click

Auto Click được xây dựng theo mô hình **Đa Tầng Phân Lập (Layered Architecture)**, chia tách hoàn toàn giữa Giao diện người dùng (UI), Dữ liệu (Model), Động cơ thực thi (Engine), và Giao tiếp hệ điều hành (Win32 Layer):

```text
+-------------------------------------------------------------------------------+
|                            TẦNG GIAO DIỆN (UI LAYER)                          |
|                                                                               |
|  +---------------------+   +---------------------+   +---------------------+  |
|  |   ChainListWidget   |   |   ActionListWidget  |   | ActionEditorWidget  |  |
|  |  (Danh sách chuỗi)  |   | (Bảng các hành động)|   |  (Biên tập tham số) |  |
|  +----------+----------+   +----------+----------+   +----------+----------+  |
|             \                         |                        /              |
|              +------------------------+-----------------------+               |
|                                       v                                       |
|                               AutoClickWindow                                 |
+---------------------------------------+---------------------------------------+
                                        | (Signals / Slots)
                                        v
+-------------------------------------------------------------------------------+
|                            TẦNG DỮ LIỆU (DATA MODEL)                          |
|                                                                               |
|  - Action: Kiểu, Tọa độ X/Y, Timing (waitBefore, waitAfter, duration), Key    |
|  - ActionChain: Danh sách Action, Số lần lặp (Repeat / Infinite), Clone deep  |
|  - ActionSerializer: Lưu trữ và nạp cấu hình JSON (profiles/)                 |
+---------------------------------------+---------------------------------------+
                                        | (Chạy trên QThread riêng)
                                        v
+-------------------------------------------------------------------------------+
|                            TẦNG ĐỘNG CƠ (ENGINE LAYER)                        |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  | ActionRunner (Kế thừa QThread)                                          |  |
|  |  - Quản lý vòng lặp Repeat & Trạng thái (Running, Paused, Stopping...)  |  |
|  |  - Quản lý Countdown đếm ngược từng 50ms cho WaitBefore/After/Duration  |  |
|  |  - std::atomic_bool stopRequested (Dừng an toàn tức thì không treo)    |  |
|  +------------------------------------+------------------------------------+  |
|                                       |                                       |
|                                       v                                       |
|  +-------------------------------------------------------------------------+  |
|  | InputController                                                         |  |
|  |  - Lớp duy nhất giao tiếp với Windows SendInput / SetCursorPos          |  |
|  |  - Hỗ trợ Drag nội suy mượt mà, Hold chuột, Unicode Type Text, Hotkey   |  |
|  +------------------------------------+------------------------------------+  |
+---------------------------------------|---------------------------------------+
                                        |
       +--------------------------------+--------------------------------+
       |                                                                 |
       v                                                                 v
+---------------+                                              +-------------------+
|  Win32 API    |                                              |  RuntimeOverlay   |
| (SendInput)   |                                              |  (HUD Widget)     |
|       |       |                                              |  - Đếm ngược      |
|       v       |                                              |  - Tự tránh chuột |
| Target Window |                                              +-------------------+
+---------------+
```

---

### 2.3. Năm Quy Tắc Thiết Kế Cốt Lõi (Core Design Rules)

1. **Rule 1 – UI không trực tiếp điều khiển thiết bị:** UI chỉ gửi dữ liệu model sang `ActionRunner`, việc điều khiển chuột/phím phải đi qua `InputController`.
2. **Rule 2 – Model hoàn toàn độc lập với UI:** Cấu trúc `Action` và `ActionChain` không phụ thuộc vào `QWidget` hay bất kỳ thành phần đồ họa nào.
3. **Rule 3 – Win32 API được cô lập triệt để:** Các hàm Win32 như `SendInput`, `SetCursorPos`, `SetProcessDpiAwarenessContext` chỉ nằm ở `InputController`, `MouseCapture` và `main.cpp`.
4. **Rule 4 – Chuẩn hóa thời gian bằng `std::chrono::milliseconds`:** Không dùng kiểu số nguyên tùy tiện rải rác.
5. **Rule 5 – ActionRunner không bao giờ chạy trên GUI Thread:** Luôn chạy trên `QThread` tách biệt với cờ nguyên tử `std::atomic_bool` để giao diện không bị đơ giật trong lúc chờ độ trễ (Sleep/Wait).

---

## 3. Cấu trúc thư mục dự án

```text
D:\ALLinONE\
├── CMakeLists.txt              # Cấu hình biên dịch CMake C++20 & Qt6
├── README.md                   # Tài liệu kiến trúc & nhật ký cập nhật
├── OneForAll.lnk               # Shortcut khởi chạy trực tiếp ứng dụng
├── run_app.bat                 # Script khởi chạy nhanh One for ALL
├── build_app.bat               # Script tự động cấu hình, biên dịch và đóng gói exe
│
├── OneForAll_Release\          # Thư mục bản Release độc lập (chạy trực tiếp không cần cài đặt)
│   ├── OneForAll.exe           # File thực thi chính
│   ├── Qt6Core.dll, Qt6Gui.dll, Qt6Widgets.dll ...
│   ├── libstdc++-6.dll, libgcc_s_seh-1.dll, libwinpthread-1.dll
│   ├── platforms\qwindows.dll  # Plugin hiển thị Windows
│   ├── profiles\               # Cấu hình JSON đi kèm
│   └── logs\                   # Thư mục log
│
├── assets\
│   └── resources.qrc           # File tài nguyên Qt Resource biên dịch icon
│
├── icon\                       # Bộ icon các công cụ
│   ├── autoclicker.jpg
│   ├── QR.jpg
│   ├── cleaner.png
│   ├── con_device.png
│   ├── download.png
│   ├── gateway.png
│   ├── pcTOphone.png
│   ├── vpn.jpg
│   └── wifi.png
│
├── profiles\                   # Thư mục lưu cấu hình chuỗi hành động JSON
│   └── default.json
│
├── logs\                       # Thư mục ghi log hệ thống
│   ├── app.log
│   └── autoclick.log
│
└── src\
    ├── main.cpp                # Điểm khởi chạy & thiết lập Per-Monitor DPI v2
    │
    ├── core\                   # Tầng lõi ứng dụng
    │   ├── Tool.h              # Interface ITool
    │   ├── ToolManager.h       # Quản lý đăng ký tool
    │   ├── ToolManager.cpp
    │   ├── IconHelper.h        # Tiện ích bo tròn & tạo badge nền trắng cho icon
    │   ├── Logger.h            # Hệ thống log đa luồng
    │   └── Logger.cpp
    │
    ├── ui\                     # Giao diện chính điều phối
    │   ├── MainWindow.h
    │   └── MainWindow.cpp
    │
    └── tools\
        └── autoclick\          # Module Auto Click
            ├── AutoClickTool.h
            ├── AutoClickTool.cpp
            ├── AutoClickWindow.h
            ├── AutoClickWindow.cpp
            │
            ├── model\          # Data Model
            │   ├── Action.h
            │   ├── Action.cpp
            │   ├── ActionChain.h
            │   └── ActionChain.cpp
            │
            ├── engine\         # Runtime Engine & Input
            │   ├── InputController.h
            │   ├── InputController.cpp
            │   ├── ActionRunner.h
            │   └── ActionRunner.cpp
            │
            ├── capture\        # Tương tác bắt tọa độ màn hình
            │   ├── CoordinateOverlay.h
            │   ├── CoordinateOverlay.cpp
            │   ├── MouseCapture.h
            │   └── MouseCapture.cpp
            │
            ├── overlay\        # HUD hiển thị trạng thái runtime
            │   ├── RuntimeOverlay.h
            │   └── RuntimeOverlay.cpp
            │
            ├── storage\        # Lưu trữ profile JSON
            │   ├── ActionSerializer.h
            │   └── ActionSerializer.cpp
            │
            └── widgets\        # Các thành phần giao diện 3 cột
                ├── ChainListWidget.h
                ├── ChainListWidget.cpp
                ├── ActionListWidget.h
                ├── ActionListWidget.cpp
                ├── ActionEditorWidget.h
                └── ActionEditorWidget.cpp
```

---

## 4. Hướng dẫn Khởi Chạy & Biên Dịch (Run & Build Guide)

### 4.1. Khởi chạy trực tiếp (Không cần cài đặt thêm)
Bản build hoàn chỉnh đã được đóng gói đầy đủ trong thư mục `OneForAll_Release\`. Bạn có thể chạy ngay bằng các cách sau:
- **Cách 1:** Click đúp vào file shortcut **`OneForAll.lnk`** ngay tại thư mục gốc `D:\ALLinONE\`.
- **Cách 2:** Click đúp vào file **`run_app.bat`** tại thư mục gốc.
- **Cách 3:** Vào thư mục `D:\ALLinONE\OneForAll_Release\` và click đúp vào file **`OneForAll.exe`**.

### 4.2. Môi trường yêu cầu (Khi muốn tự build lại mã nguồn)
- **Qt:** Qt 6.11.1 MinGW 64-bit (`D:\Qt\6.11.1\mingw_64`)
- **Compiler:** GCC/G++ 13.1.0 64-bit (`D:\Qt\Tools\mingw1310_64\bin`)
- **CMake:** 3.30+ (`D:\Qt\Tools\CMake_64\bin`)
- **Ninja:** 1.12+ (`D:\Qt\Tools\Ninja`)

### 4.3. Tự động build lại 1-click
Chỉ cần chạy file **`build_app.bat`**, script sẽ tự động:
1. Cấu hình CMake với Qt 6.11.1.
2. Biên dịch bằng Ninja.
3. Đóng gói đầy đủ các runtime DLLs bằng `windeployqt`.

---

## 5. Hướng dẫn sử dụng Auto Click (User Guide)

1. **Khởi động ứng dụng:** Mở `OneForAll.exe`, chọn mục **Auto Click** trên sidebar bên trái và bấm **Mở Cửa Sổ Auto Click**.
2. **Khu vực 1 - Action Chains (Bên trái):**
   - Bấm `+ Add` để thêm chuỗi mới, `Clone` để nhân bản, `Delete` để xóa.
   - Nhấp chuột phải vào từng chuỗi để đổi tên hoặc chạy nhanh.
3. **Khu vực 2 - Action List (Ở giữa):**
   - Hiển thị bảng hành động chi tiết (`#`, `Loại`, `Mô tả`, `Chờ trước`, `Chờ sau`, `Thời lượng`).
   - Hỗ trợ các nút: `+ Add Action`, `Clone`, `Before` (chèn trước), `After` (chèn sau), `▲ Up` / `▼ Down` (di chuyển thứ tự), `Delete`.
4. **Khu vực 3 - Action Settings (Bên phải):**
   - Cho phép chọn loại: **Mouse Click, Mouse Drag, Mouse Hold, Type Text, Hotkey, Key Press, Scroll**.
   - Bấm nút **🎯 Capture** để lấy tọa độ màn hình trực tiếp: Cửa sổ ứng dụng sẽ tự động ẩn đi, di chuyển chuột đến vị trí mong muốn và click để chọn (hoặc nhấn `ESC` để hủy).
   - Thiết lập thời gian: `Wait Before` (thời gian chờ trước), `Wait After` (thời gian chờ sau), `Duration` (thời lượng kéo thả/giữ).
   - Bấm **✓ Lưu hành động (Apply)** để áp dụng thay đổi vào danh sách.
5. **Thực thi (Run & Stop):**
   - Nhập số lần lặp tại ô `Repeat` (hoặc tích chọn `Infinite` để lặp vô hạn).
   - Bấm **▶ Bắt đầu chạy (RUN)**.
   - Một **Runtime HUD Overlay** sẽ hiển thị trên màn hình: cho biết vòng lặp hiện tại, hành động đang làm, hành động kế tiếp và thanh đếm ngược thời gian thực.
   - HUD Overlay áp dụng thuật toán tự động đổi góc màn hình để **không che khuất tọa độ mà chuột sắp thao tác**.
   - Bấm **■ Dừng lại (STOP)** tại cửa sổ chính hoặc trực tiếp trên nút Dừng của Overlay để hủy thực thi an toàn bất cứ lúc nào.
6. **Lưu & Xuất cấu hình:**
   - Bấm `💾 Lưu cấu hình (Save)` để ghi nhớ toàn bộ chain vào `profiles/default.json`.
   - Bấm `📤 Xuất file` hoặc `📥 Nhập file` để chia sẻ profile JSON sang máy khác.

---

## 6. Nhật ký cập nhật kiến trúc & tính năng (Changelog)

> [!IMPORTANT]
> **Quy định bắt buộc:** Mọi sửa đổi, bổ sung tính năng hoặc điều chỉnh cấu trúc mã nguồn trong các bước tiếp theo đều phải được cập nhật và ghi nhận chi tiết vào bảng nhật ký bên dưới.

| Ngày cập nhật | Phiên bản | Tác giả / Tác vụ | Nội dung thay đổi kiến trúc & tính năng | Các file liên quan |
|---|---|---|---|---|
| **2026-09-21** | **v1.0.0** | Khởi tạo dự án (Phase 1 & MVP Auto Click) | - Xây dựng kiến trúc đa module `One for ALL` (`ITool`, `ToolManager`, `MainWindow`).<br>- Tích hợp bộ icon từ thư mục `icon/` vào Qt Resource (`assets/resources.qrc`).<br>- Triển khai đầy đủ module `Auto Click` với 3 khu vực UI: Chains, Action List, Settings Editor.<br>- Xây dựng `InputController` cô lập Win32 SendInput/SetCursorPos.<br>- Xây dựng `ActionRunner` chạy trên `QThread` với atomic cancel và live countdown tick.<br>- Xây dựng `CoordinateOverlay` & `MouseCapture` lấy tọa độ toàn desktop.<br>- Xây dựng `RuntimeOverlay` HUD tự động tính toán vị trí tránh va chạm chuột.<br>- Xây dựng `ActionSerializer` hỗ trợ lưu/đọc cấu hình JSON.<br>- Thiết lập Per-Monitor DPI Awareness v2.<br>- Cung cấp script tự động build và đóng gói `build_app.bat`. | `CMakeLists.txt`<br>`src/main.cpp`<br>`src/core/*`<br>`src/ui/*`<br>`src/tools/autoclick/*`<br>`assets/resources.qrc`<br>`README.md` |
| **2026-09-21** | **v1.0.1** | Đóng gói bản Release độc lập (Standalone Package) | - Tạo gói phân phối độc lập hoàn chỉnh tại thư mục `OneForAll_Release/`.<br>- Tích hợp đầy đủ các DLL Qt6 (`Qt6Core`, `Qt6Gui`, `Qt6Widgets`...) và MinGW runtime (`libstdc++-6`, `libgcc_s_seh-1`...).<br>- Tạo shortcut khởi chạy trực tiếp `OneForAll.lnk` tại thư mục gốc.<br>- Cập nhật `run_app.bat` trỏ vào bản Release. | `OneForAll_Release/*`<br>`OneForAll.lnk`<br>`run_app.bat`<br>`README.md` |
| **2026-09-22** | **v1.0.2** | Cấu hình Windows GUI Subsystem (Triệt tiêu cửa sổ Shell) | - Thiết lập cờ `WIN32` trong `CMakeLists.txt` (`add_executable(OneForAll WIN32 ...)`).<br>- Chuyển đổi subsystem từ Console sang Windows GUI thuần túy, loại bỏ hoàn toàn cửa sổ dòng lệnh đen khi khởi chạy `OneForAll.exe`.<br>- Cập nhật file thực thi mới vào gói phát hành `OneForAll_Release/`. | `CMakeLists.txt`<br>`OneForAll_Release/OneForAll.exe`<br>`README.md` |
| **2026-09-22** | **v1.0.3** | Hiện đại hóa UI & Hỗ trợ badge nền trắng cho Icon | - Xây dựng lớp tiện ích `IconHelper` (`src/core/IconHelper.h`) tự động vẽ nền trắng bo tròn góc (antialiased rounded white badge) cho tất cả các icon, giải quyết triệt để vấn đề các icon PNG trong suốt bị chìm vào theme tối (Dark mode).<br>- Hiện đại hóa toàn diện giao diện `MainWindow`: Card hiển thị nổi với bo góc 18px, hiệu ứng hover mượt mà trên sidebar (bo góc 10px, chiều cao 52px), nút bấm dạng pill hiện đại (bo góc 10px).<br>- Hiện đại hóa giao diện `AutoClickWindow` và các widget con: Bo tròn góc mượt mà cho bảng hành động (`ActionListWidget`), danh sách chuỗi (`ChainListWidget`), khung tham số (`ActionEditorWidget`), HUD (`RuntimeOverlay` bo góc 16px) và màn hình chụp tọa độ (`CoordinateOverlay`).<br>- Đồng bộ lại bản build Release không có cửa sổ shell. | `src/core/IconHelper.h`<br>`src/core/Tool.h`<br>`src/core/ToolManager.h/.cpp`<br>`src/tools/autoclick/AutoClickTool.h/.cpp`<br>`src/ui/MainWindow.cpp`<br>`src/tools/autoclick/AutoClickWindow.cpp`<br>`src/tools/autoclick/widgets/*`<br>`src/tools/autoclick/overlay/RuntimeOverlay.cpp`<br>`src/tools/autoclick/capture/CoordinateOverlay.cpp`<br>`CMakeLists.txt`<br>`OneForAll_Release/OneForAll.exe`<br>`README.md` |
| **2026-09-22** | **v1.0.4** | Chuyển đổi toàn diện sang Giao diện Sáng (Light Theme) | - Thiết kế lại toàn bộ hệ thống màu theo phong cách **Light Theme hiện đại** (tương tự macOS & Windows 11 Fluent Light): nền chính xám sáng `#f6f8fa`, các thẻ card & bảng biểu màu trắng tinh khiết `#ffffff`, viền mảnh tinh tế `#d0d7de`, chữ màu than tối `#1f2328` sắc nét dễ đọc.<br>- Tối ưu hóa `IconHelper`: Bổ sung đường viền mảnh khử răng cưa bao quanh badge icon, giúp các icon nổi bật rõ ràng trên nền sáng.<br>- Chuyển đổi toàn bộ màu sắc trong `MainWindow`, `PlaceholderTool`, `AutoClickWindow`, `ChainListWidget`, `ActionListWidget`, `ActionEditorWidget`, `RuntimeOverlay` và `CoordinateOverlay` sang tone sáng đồng bộ.<br>- Đóng gói bản Release mới vào `OneForAll_Release/OneForAll.exe`. | `src/core/IconHelper.h`<br>`src/ui/MainWindow.cpp`<br>`src/core/ToolManager.cpp`<br>`src/tools/autoclick/AutoClickWindow.cpp`<br>`src/tools/autoclick/widgets/*`<br>`src/tools/autoclick/overlay/RuntimeOverlay.cpp`<br>`src/tools/autoclick/capture/CoordinateOverlay.cpp`<br>`OneForAll_Release/OneForAll.exe`<br>`README.md` |
| **2026-09-24** | **v1.0.5** | Triệt tiêu cuộn ngang & Tối ưu hóa cuộn dọc (Responsive Layout) | - **Triệt tiêu hoàn toàn thanh cuộn ngang**: Thiết lập `setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff)` và ẩn thanh cuộn ngang thông qua stylesheet toàn cục `QScrollBar:horizontal { height: 0px; background: transparent; }`.<br>- **Tối ưu hóa Bảng hành động (`ActionListWidget`)**: Bảng tự động co giãn cột mô tả (`QHeaderView::Stretch`), thu gọn văn bản tự động thêm dấu `...` (`Qt::ElideRight`), hiển thị tooltip đầy đủ khi rê chuột, sắp xếp cụm 7 nút điều khiển thành 2 hàng nhỏ gọn (4 nút + 3 nút) giúp loại bỏ giới hạn chiều rộng tối thiểu.<br>- **Hỗ trợ cuộn dọc chuyên biệt cho Cấu hình (`ActionEditorWidget`)**: Tích hợp `QScrollArea` cuộn dọc mượt mà cho khối cài đặt tham số hành động và thời gian, giữ nút *Lưu hành động* cố định ở đáy bảng điều khiển.<br>- **Cố định giới hạn co giãn**: Thêm `setMinimumWidth` cho từng cột trong `QSplitter` và `setMinimumSize` cho cửa sổ chính, ngăn chặn tình trạng bị ép biến dạng giao diện.<br>- Cập nhật bản build Release mới nhất vào `OneForAll_Release/OneForAll.exe`. | `src/ui/MainWindow.cpp`<br>`src/tools/autoclick/AutoClickWindow.cpp`<br>`src/tools/autoclick/widgets/ChainListWidget.cpp`<br>`src/tools/autoclick/widgets/ActionListWidget.cpp`<br>`src/tools/autoclick/widgets/ActionEditorWidget.cpp`<br>`OneForAll_Release/OneForAll.exe`<br>`README.md` |
| **2026-10-06** | **v1.0.6** | Sửa lỗi độ chính xác Auto Click | - Phím mở rộng/hotkey/TypeText xuống dòng đúng; tọa độ capture khớp pixel vật lý (đúng khi scale DPI); HUD không cướp focus; state runner an toàn; Repeat lưu vào chain; một cửa sổ Auto Click duy nhất; clamp enum khi đọc JSON. Chi tiết xem `PROJECT_OVERVIEW.md`. | `src/tools/autoclick/*`<br>`src/ui/MainWindow.cpp`<br>`PROJECT_OVERVIEW.md` |
| **2026-10-06** | **v1.0.7** | Hoàn thiện module QR Tools | - Lõi QR: mã hóa (Nayuki qrcodegen, MIT) + giải mã (quirc, ISC, đã nâng giới hạn vùng để đọc mã version cao) + payload builder/parser.<br>- UI 3 tab Tạo / Quét / Lịch sử; quét bằng file, clipboard, kéo-thả, chụp màn hình, camera.<br>- Bộ test `qr_tests` (87 kiểm tra) và `qr_ui_tests` (46 kiểm tra). Xem `THIRD_PARTY.md`. | `src/tools/qr/*`<br>`src/third_party/*`<br>`tests/*`<br>`CMakeLists.txt`<br>`src/ui/MainWindow.cpp`<br>`THIRD_PARTY.md` |
| **2026-10-06** | **v1.0.8** | Hoàn thiện module WiFi Connection | - `WlanController` cô lập Windows WLAN API (wlanapi.dll): quét mạng, liệt kê/đọc/xóa/xuất/nhập hồ sơ, kết nối (tạo hồ sơ + `WlanConnect`), đọc trạng thái kết nối hiện tại.<br>- `WlanProfileXml` dựng/phân tích XML hồ sơ WLAN (Open/WEP/WPA/WPA2/WPA3-Personal), test độc lập không cần phần cứng.<br>- UI 2 tab (Mạng xung quanh, Hồ sơ đã lưu) + hộp thoại kết nối; xem mật khẩu đã lưu cần quyền Administrator.<br>- **Có chủ đích KHÔNG triển khai** tính năng thử nhiều mật khẩu/dò mật khẩu theo vòng lặp do người dùng yêu cầu ban đầu — đây là brute-force vào mạng, chỉ làm phần quản lý hợp pháp theo xác nhận của người dùng.<br>- Phát hiện và sửa 1 lỗi khi test trên phần cứng thật: hồ sơ không xin plaintext key vẫn báo `hasPassword=true` với dữ liệu đã mã hóa DPAPI (sửa bằng cách kiểm tra `<protected>`).<br>- Bộ test `wifi_tests` (54 kiểm tra) và `wifi_ui_tests` (khói UI, xác nhận trên WiFi adapter thật). | `src/tools/wifi/*`<br>`tests/wifi_tests.cpp`<br>`tests/wifi_ui_tests.cpp`<br>`CMakeLists.txt`<br>`src/ui/MainWindow.cpp`<br>`PROJECT_OVERVIEW.md` |
| **2026-10-06** | **v1.0.9** | Thêm đo tốc độ mạng vào WiFi Connection | - `SpeedTestRunner` (Qt Network, không Win32): ping/jitter (8 mẫu tuần tự), tốc độ tải xuống/tải lên qua endpoint công khai của Cloudflare, mỗi pha giới hạn 9s kể từ byte đầu tiên.<br>- Tab "Đo tốc độ mạng" mới trong WiFi Connection, đối chiếu với tốc độ liên kết PHY của WiFi.<br>- Sửa 1 lỗi: bấm Dừng lúc đang đo ping báo nhầm lỗi mạng.<br>- Đã chạy kiểm thử thật trên Internet (không mock): ping/jitter/download/upload và nút Dừng giữa chừng đều hoạt động đúng.<br>- Thêm `Qt6::Network`, cập nhật đóng gói Release (`Qt6Network.dll`).<br>- **Từ chối** yêu cầu thêm "thử mật khẩu từ 1 file có sẵn" (dò mật khẩu WiFi) - cùng lý do đã từ chối ở v1.0.8, xem `PROJECT_OVERVIEW.md`. | `src/tools/wifi/engine/SpeedTestRunner.*`<br>`src/tools/wifi/SpeedTestTab.*`<br>`src/tools/wifi/WifiWindow.*`<br>`src/tools/wifi/NetworksTab.cpp`<br>`CMakeLists.txt`<br>`tests/wifi_ui_tests.cpp` |
| **2026-10-06** | **v1.0.10** | Nút vui "Love WiFi" | - Thêm nút "💜 Love WiFi" trong tab Mạng xung quanh (WiFi Connection): chọn một mạng, bấm nút, hiện hộp thoại vui `Yes, "<SSID>" love you too!`. Không liên quan mật khẩu/kết nối, chỉ là UI vui.<br>- **Từ chối thêm 2 lần** các biến thể thu nhỏ dần của yêu cầu dò mật khẩu WiFi (2 mật khẩu cố định → 1 mật khẩu cố định → hộp thoại giả báo "quét thành công, mật khẩu là X") - giữ nguyên lập trường đã nêu ở v1.0.8/v1.0.9. | `src/tools/wifi/NetworksTab.*` |
| **2026-10-07** | **v1.1.0** | Connect Together: nền tảng mã hóa/giao thức/mã ghép đôi | - Tham khảo Mouse without Borders: dùng chung 1 chuột/bàn phím điều khiển nhiều máy qua LAN. Mô hình an toàn như bản gốc: ghép đôi bằng mã bảo mật 9 số hiển thị trên màn hình (hết hạn 5 phút), chỉ hoạt động trong LAN, không chế độ ẩn.<br>- `CryptoSession`: AES-256-GCM qua Windows CNG (bcrypt.dll). `PairingCode`, `ProtocolMessage` (khung nhị phân). 256 test. | `src/tools/connect/engine/{PairingCode,ProtocolMessage,CryptoSession}.*`<br>`tests/connect_tests.cpp` |
| **2026-10-07** | **v1.1.1 - v1.1.2** | Connect Together: mạng + Win32 input | - `PeerStore`, `NetworkSession` (TCP mã hóa, test qua loopback thật), `PeerDiscovery` (UDP broadcast, test thật trên LAN máy dev).<br>- `EdgeDetector` (toán học chạm biên), `InputHook` (hook toàn cục WH_MOUSE_LL/WH_KEYBOARD_LL, cài/gỡ thật), `InputInjector` (SendInput/SetCursorPos). 465 test. | `src/tools/connect/engine/{PeerStore,NetworkSession,PeerDiscovery,EdgeDetector,InputHook,InputInjector}.*` |
| **2026-10-07** | **v1.2.0** | Connect Together: hoàn thiện + sửa crash nghiêm trọng | - `ConnectSessionController` điều phối toàn bộ; UI 2 tab (Ghép đôi, Máy đã ghép đôi) + banner trạng thái + log; nối vào `MainWindow`.<br>- **Tìm và sửa lỗi crash ngẫu nhiên (SIGSEGV ~70-100% lần chạy)**: 3 chỗ gọi `session->deleteLater()` trùng với handler `disconnected` tập trung, gây double-delete use-after-free ngẫu nhiên. Điều tra bằng gdb + checkpoint nhị phân + cô lập từng thành phần. Sửa xong, test lại 40+ lần liên tiếp, 0 lỗi.<br>- Sửa thêm: race lý thuyết trong `InputHook::uninstall()`, và lỗi UB thật trong test (thiếu `QGuiApplication` khi gọi `QGuiApplication::clipboard()`).<br>- Còn 1 nghi vấn hiển thị nhỏ trong tab Ghép đôi (ô nhập IP/Cổng/Mã) chỉ thấy qua ảnh chụp tự động, chưa xác nhận có thật trong app chạy thật hay không - cần tự kiểm tra mắt thường. | `src/tools/connect/engine/ConnectSessionController.cpp`<br>`src/tools/connect/engine/InputHook.cpp`<br>`src/tools/connect/{ConnectWindow,PairingTab,PeersTab,ConnectTool,ConnectUiStyle}.*`<br>`src/ui/MainWindow.cpp`<br>`tests/connect_tests.cpp`<br>`tests/connect_ui_tests.cpp` |
| **2026-10-07** | **v1.3.0** | Disk Cleanup: lõi engine | - Tham khảo CCleaner/BleachBit/Windows Disk Cleanup. `CategoryRegistry` (15 hạng mục rác đã biết, phân giải đường dẫn qua `CleanupEnvironment` test được bằng môi trường giả), `CleanupScanner` (quét QThread riêng), `RecycleBinOps` (lớp duy nhất gọi Shell API Thùng rác), `CleanupExecutor` (xóa QThread riêng, mặc định qua Thùng rác), `DiskSpaceInfo` (dung lượng ổ đĩa, QStorageInfo), `LargeFileScanner` (tìm tệp lớn theo ngưỡng).<br>- 82 test (có xóa file thật qua Recycle Bin, an toàn/khôi phục được). Chưa có UI. | `src/tools/diskcleanup/model/*`<br>`src/tools/diskcleanup/engine/*`<br>`tests/diskcleanup_tests.cpp`<br>`CMakeLists.txt` |
| **2026-10-07** | **v1.4.0** | Disk Cleanup: hoàn thiện UI + sửa lỗi cộng nhầm dung lượng | - UI 2 tab trong `DiskCleanupWindow`: **Dọn dẹp theo hạng mục** (`CleanupTab` - tổng quan dung lượng ổ đĩa, bảng hạng mục tick được, Quét/Dọn dẹp qua `CleanupScanner`/`CleanupExecutor`, "Xóa vĩnh viễn" mặc định tắt) và **Tìm tệp lớn** (`LargeFilesTab` - chọn ổ/thư mục, ngưỡng kích thước, mở thư mục chứa hoặc xóa vào Thùng rác). Nối `DiskCleanupTool` vào `MainWindow`.<br>- **Phát hiện và sửa lỗi thật khi tự quét tay trên máy build**: Qt trên Windows tự động "đi theo" tệp lối tắt (`.lnk`) khi hỏi `isDir()`/liệt kê thư mục - hạng mục "Danh sách tệp gần đây" (toàn lối tắt) từng báo nhầm **12.76 GB** trong khi thư mục thật chỉ ~4.3 MB, do một lối tắt trỏ tới Downloads/ổ D: bị cộng cả kích thước thư mục đích. Sửa bằng `GetFileAttributesExW` (đọc thẳng thuộc tính NTFS, không qua lớp diễn giải Shell Link của Qt) cho mọi symlink/lối tắt, và loại symlink khỏi đệ quy tính kích thước thư mục con. Thêm test hồi quy dựng lối tắt thật bằng `QFile::link()`.<br>- 91 test lõi (82 cũ + 9 mới, gồm test hồi quy lối tắt) + 10 test UI "khói" (không quét thật `%TEMP%`/`%WINDIR%` trong test tự động, chỉ quét tay). | `src/tools/diskcleanup/engine/CleanupScanner.*`<br>`src/tools/diskcleanup/{DiskUiStyle.h,CleanupTab,LargeFilesTab,DiskCleanupWindow,DiskCleanupTool}.*`<br>`tests/diskcleanup_tests.cpp`<br>`tests/diskcleanup_ui_tests.cpp`<br>`src/ui/MainWindow.cpp`<br>`CMakeLists.txt` |
| **2026-10-07** | **v1.5.0** | Disk Cleanup: tìm tệp trùng lặp + quản lý phân vùng | - **Tìm tệp trùng lặp** (`DuplicateFinder` + `DuplicateFilesTab`): quét 1 thư mục, nhóm theo kích thước rồi hash SHA-256 nội dung để xác nhận trùng thật (không chỉ trùng kích thước ngẫu nhiên); hiển thị theo cây (nhóm → từng tệp có checkbox), mặc định giữ lại bản đầu mỗi nhóm, xóa các bản còn lại vào Thùng rác.<br>- **Quản lý phân vùng** (`PartitionManager`/`PartitionResizer` + `PartitionTab`): liệt kê phân vùng thật qua PowerShell (`Get-Partition`/`Get-Volume`, chỉ đọc, không cần quyền Administrator); đổi kích thước qua `Resize-Partition` - CÙNG cơ chế an toàn mà Disk Management của Windows dùng, luôn tra `Get-PartitionSupportedSize` trước (cần quyền Administrator - có banner + nút "Chạy lại với quyền Quản trị" qua UAC chuẩn), yêu cầu gõ đúng tên ổ đĩa để xác nhận + hộp thoại cảnh báo trước khi thực hiện, chạy trên QThread riêng không chặn UI.<br>- **Sửa 2 lỗi thật phát hiện khi quét/tra cứu tay trên máy build**: (1) truyền script PowerShell nhiều dòng qua stdin với `-Command -` khiến powershell.exe thoát mã 0 nhưng im lặng không chạy gì - đổi sang `-EncodedCommand` (Base64 UTF-16LE, cách Microsoft khuyến nghị); (2) lỗi PowerShell bị tự động serialize thành CLIXML không đọc được khi stderr bị redirect qua QProcess - đổi sang `[Console]::Error.WriteLine()`.<br>- 156 test lõi (91 cũ + 65 mới, gồm test JSON thuần bằng dữ liệu mẫu, không gọi PowerShell) + 19 test UI "khói" (liệt kê phân vùng thật vì chỉ đọc/an toàn; **không bao giờ** test đổi kích thước phân vùng thật). | `src/tools/diskcleanup/engine/{DuplicateFinder,PartitionManager,PartitionResizer}.*`<br>`src/tools/diskcleanup/{DuplicateFilesTab,PartitionTab,DiskCleanupWindow}.*`<br>`tests/diskcleanup_tests.cpp`<br>`tests/diskcleanup_ui_tests.cpp`<br>`CMakeLists.txt` |
| **2026-10-07** | **v1.5.1** | Disk Cleanup: hết "Không phản hồi" khi quét ổ lớn | - Người dùng báo quét ổ C: (120GB) bị Windows đánh dấu "Không phản hồi". Đo thật bằng cách bấm Quét rồi theo dõi vòng lặp sự kiện của luồng giao diện trong 20 giây khi luồng nền xử lý ~390.000 tệp thật: độ trễ lớn nhất giữa 2 lần xử lý sự kiện chỉ 21ms - luồng giao diện CHƯA BAO GIỜ bị chặn, kiến trúc QThread nền vốn đã đúng từ đầu.<br>- "Không phản hồi" chỉ là CẢM GIÁC do màn hình đứng yên hàng phút không có gì thay đổi (không có cập nhật trực quan trong lúc quét), không phải treo thật.<br>- Vẫn cải thiện cho chắc chắn: `CleanupScanner`/`CleanupExecutor`/`LargeFileScanner`/`DuplicateFinder` chạy với `QThread::LowPriority` (nhường CPU cho giao diện, phòng máy yếu/ít lõi); `LargeFileScanner`/`DuplicateFinder` báo tiến độ dày hơn (mỗi 200 tệp thay vì 1024/256) kèm đường dẫn tệp đang xử lý (rút gọn giữa chuỗi) - người dùng THẤY ứng dụng đang chạy liên tục thay vì đoán.<br>- 156 test lõi + 19 test UI vẫn pass (không đổi hành vi test, chỉ đổi mức ưu tiên luồng + tần suất/nội dung tín hiệu tiến độ). | `src/tools/diskcleanup/CleanupTab.cpp`<br>`src/tools/diskcleanup/LargeFilesTab.*`<br>`src/tools/diskcleanup/DuplicateFilesTab.*`<br>`src/tools/diskcleanup/engine/LargeFileScanner.*`<br>`src/tools/diskcleanup/engine/DuplicateFinder.*` |
| **2026-10-07** | **v1.5.2** | Disk Cleanup: sửa "Không phản hồi" THẬT ở Tìm tệp trùng lặp | - Người dùng báo lại kèm ảnh chụp Task Manager: Tìm tệp trùng lặp trên ổ C: vẫn bị đánh dấu "Không phản hồi" THẬT (không chỉ cảm giác như lần trước). Tìm đúng nguyên nhân: `DuplicateFinder` cũ phát tín hiệu `groupFound` cho TỪNG NHÓM MỘT, không giới hạn số lượng - một thư mục kiểu cache trình duyệt (nhiều hồ sơ Chrome, nhiều loại cache cùng dùng cỡ khối cố định) có thể tạo ra hàng trăm/nghìn "nhóm trùng lặp", mỗi tín hiệu khiến luồng giao diện phải dựng thêm mục cây + tính lại layout - dồn dập đủ nhiều chiếm trọn một lượt xử lý sự kiện của Qt, đủ lâu để Windows đánh dấu treo thật.<br>- Sửa bằng cách đổi kiến trúc: `DuplicateFinder` gom toàn bộ kết quả trong bộ nhớ, sắp xếp theo dung lượng lãng phí giảm dần, cắt theo `setMaxGroups()` (mặc định 500) rồi CHỈ PHÁT `scanFinished` MỘT LẦN DUY NHẤT - đúng mẫu `LargeFileScanner` đã được đo thực tế không gây treo. UI dựng cả cây trong 1 lần (`setUpdatesEnabled(false)` khi dựng hàng loạt), các nhóm mặc định THU GỌN thay vì mở sẵn.<br>- Đo lại thật bằng cách quét toàn bộ thư mục dữ liệu Chrome (~20.300 tệp, nhiều hồ sơ) trong 60 giây liên tục: độ trễ lớn nhất giữa 2 lần xử lý sự kiện chỉ 22ms suốt quá trình - xác nhận hết hẳn nguy cơ "Không phản hồi". Quét nội dung cache trình duyệt vẫn CHẬM về bản chất (hash SHA-256 hàng chục nghìn tệp cần thời gian thật) nhưng không còn treo giao diện.<br>- 167 test lõi (+11 test hồi quy: 20 nhóm giả, `setMaxGroups(5)` xác nhận cắt đúng 5 nhóm lãng phí nhiều nhất, sắp xếp đúng, tổng lãng phí/số nhóm vẫn phản ánh đủ cả 20 nhóm thật) + 19 test UI. | `src/tools/diskcleanup/engine/DuplicateFinder.*`<br>`src/tools/diskcleanup/DuplicateFilesTab.*`<br>`tests/diskcleanup_tests.cpp` |
| **2026-10-07** | **v1.5.3** | Disk Cleanup: căn lại cột + sửa lỗi dọn dẹp "mã lỗi 0x2" | - Người dùng báo 2 lỗi kèm ảnh chụp: (1) cột "Kích thước" ở tab Tìm tệp trùng lặp bị dạt vào giữa, để lại khoảng trắng lớn bên phải - nguyên nhân là `QTreeWidget` (khác `QTableWidget` dùng ở các tab khác) mặc định `stretchLastSection=true`, tự kéo giãn cột cuối đè lên cấu hình `Stretch` đã đặt cho cột 0; đã tắt `stretchLastSection` và căn phải chữ trong cột "Kích thước" để khớp các bảng khác trong ứng dụng.<br>- (2) Dọn dẹp theo hạng mục báo lỗi đỏ "Không dọn dẹp được: Thao tác xóa thất bại (mã lỗi 0x2)" dù phần lớn tệp hoàn toàn xóa được. Nguyên nhân: `%TEMP%`/cache thay đổi liên tục - một tệp đã bị Windows/ứng dụng khác tự xóa giữa lúc quét và lúc bấm "Dọn dẹp" (chuyện bình thường), khiến `SHFileOperationW` báo `ERROR_FILE_NOT_FOUND` (0x2) cho CẢ LÔ dù các tệp khác vẫn xóa thành công.<br>- Sửa `CleanupExecutor`: lọc trước những tệp đã tự biến mất (coi là đã đạt mục tiêu dọn dẹp, không đưa vào yêu cầu xóa), và sau khi xóa thì kiểm tra lại THẬT xem tệp nào còn tồn tại hay không (không tin mù quáng vào mã trả về của `SHFileOperationW`, vốn không cho biết chính xác mục nào lỗi khi có lỗi giữa chừng) để báo cáo `freedBytes`/`deletedCount` chính xác - chỉ báo thất bại thật khi không giải phóng được gì cả. `RecycleBinOps` cũng dịch các mã lỗi Win32 hay gặp (0x2, 0x5, 0x20...) sang tiếng Việt dễ hiểu thay vì hiện hex thô.<br>- 176 test lõi (+9 test hồi quy mô phỏng đúng tình huống: 1 tệp "biến mất" giữa danh sách 3 tệp, xác nhận 2 tệp còn lại vẫn xóa được và toàn bộ thao tác vẫn báo thành công) + 19 test UI. | `src/tools/diskcleanup/DuplicateFilesTab.cpp`<br>`src/tools/diskcleanup/CleanupTab.cpp`<br>`src/tools/diskcleanup/engine/CleanupExecutor.*`<br>`src/tools/diskcleanup/engine/RecycleBinOps.cpp`<br>`tests/diskcleanup_tests.cpp` |
| **2026-10-07** | **v1.6.0** | Rà soát toàn diện 5 tính năng: UI + hoạt động | - Theo yêu cầu người dùng: rà kỹ cả 5 tính năng đã hoàn thiện (Auto Click, Connect Together, QR Tools, WiFi Connection, Disk Cleanup). Build lại toàn bộ + chạy 895 kiểm tra tự động (qr/wifi/connect/diskcleanup × core+UI) - tất cả đều qua; chụp ảnh từng màn hình chính để soát giao diện bằng mắt.<br>- **Auto Click - dịch toàn bộ UI sang tiếng Việt**: panel "ACTION CHAINS"/"ACTION LIST"/"ACTION SETTINGS", nhãn form (Type/Button/Position/Wait Before/Wait After/Duration/Amount...), nút (Add/Clone/Before/After/Up/Down/Delete), HUD lúc chạy ("Auto Click Running" → "Auto Click đang chạy", Round/Action/Next/Countdown), và nhãn pha đếm ngược ("Wait Before"/"Wait After") - toàn bộ từng thuần tiếng Anh, lạc tông với phần còn lại của ứng dụng. Đã xác nhận AN TOÀN trước khi sửa: combo box đọc/ghi theo chỉ số (`currentIndex()`, không theo text), và `Action::typeName()/description()` (chỉ hiển thị) độc lập hoàn toàn với `ActionSerializer`'s `actionTypeToString()` (khóa PascalCase riêng để lưu JSON) - dịch chữ không ảnh hưởng hồ sơ đã lưu. Tên phím (ENTER/TAB/ESC/mũi tên, Ctrl/Alt/Shift/Win) giữ nguyên tiếng Anh (quy ước bàn phím toàn cầu).<br>- Phát hiện và sửa luôn 1 lỗi TỰ GÂY RA khi dịch: 3 nút "+ Thêm/Nhân bản/Xóa" dưới panel Chuỗi hành động (panel hẹp) bị cắt chữ ở giữa khi xếp ngang (chữ tiếng Việt dài hơn - QPushButton tự căn giữa, không tự rút gọn như QLabel) - đổi sang xếp dọc.<br>- **WiFi**: checkbox "Hiện mật khẩu đã lưu (cần quyền Administrator)" từng chung hàng với 5 nút, bị co nhỏ hơn chữ cần thiết nên hiển thị cắt cụt - tách ra hàng riêng phía trên.<br>- **Xác nhận dứt điểm nghi vấn cũ (nêu từ v1.2.0, chưa từng xác minh)**: ô nhập IP/Cổng/Mã ở tab Ghép đôi (Connect Together) trông "nhòe/chồng chữ" trong ảnh chụp tự động. Dựng cửa sổ hiện THẬT trên màn hình (không dùng `WA_DontShowOnScreen`) rồi chụp lại - chữ hiển thị hoàn toàn rõ ràng, bình thường. Kết luận: đó là hiện tượng riêng của việc chụp ảnh (`grab()`) một widget đang ẩn ngoài màn hình, không phải lỗi trong code ứng dụng - không cần sửa gì. | `src/tools/autoclick/model/Action.cpp`<br>`src/tools/autoclick/engine/ActionRunner.cpp`<br>`src/tools/autoclick/overlay/RuntimeOverlay.cpp`<br>`src/tools/autoclick/widgets/{ChainListWidget,ActionListWidget,ActionEditorWidget}.cpp`<br>`src/tools/autoclick/storage/ActionSerializer.cpp`<br>`src/tools/wifi/ProfilesTab.cpp` |
| **2026-10-07** | **v1.7.0** | Android Phone Control: hoàn thiện (gương + điều khiển thời gian thực) | - Yêu cầu người dùng: điều khiển thiết bị Android, "tính realtime và độ linh hoạt kiểu như remote". Quyết định kiến trúc: dùng lại **scrcpy** (Genymobile, Apache-2.0, mã nguồn mở, kiểm chứng rộng rãi nhiều năm) thay vì tự viết lại mã hóa H.264 + tiêm sự kiện phía Android (`MediaProjection`/`MediaCodec`/`InputManager` phản chiếu) - bài toán phức tạp, rủi ro cao, và không thể tự kiểm thử đầy đủ trong môi trường này (không có thiết bị Android thật gắn sẵn). Đóng gói bản phát hành `scrcpy-win64` chính thức (đã xác minh checksum SHA-256 theo `SHA256SUMS.txt` đính kèm) vào `vendor/scrcpy/` - không commit Git (như `build/`/`OneForAll_Release/`), `build_app.bat` tự đóng gói kèm exe khi build.<br>- `AdbController`: lớp duy nhất gọi `adb.exe` - liệt kê thiết bị (`adb devices -l`), ghép đôi gỡ lỗi không dây Android 11+ (`adb pair`), kết nối/ngắt kết nối không dây.<br>- `ScrcpyLauncher`: khởi chạy `scrcpy.exe` làm tiến trình NỀN không chặn UI (khác các worker QThread dùng ở module khác - đây là tiến trình tương tác sống lâu, không phải việc "chạy xong rồi báo kết quả"); cửa sổ gương là cửa sổ riêng của scrcpy.<br>- **Độ linh hoạt kiểu remote thật sự** (đúng yêu cầu): độ phân giải tối đa, bitrate, giới hạn fps, giữ màn hình sáng, tắt màn hình điện thoại lúc điều khiển, luôn nổi trên cùng, toàn màn hình, bật/tắt âm thanh, ghi lại phiên điều khiển ra .mp4 - toàn bộ là cờ dòng lệnh THẬT của scrcpy, không phải mô phỏng.<br>- "Chuyển sang không dây" dùng cờ `--tcpip` của scrcpy (tự dò IP thiết bị đang cắm USB, bật TCP/IP, kết nối, rồi gương - một bước duy nhất); thiết bị chưa ghép đôi thì dùng `WirelessPairDialog` (nhập IP:Cổng ghép đôi + mã 6 số, API ghép đôi không dây chính thức Android 11+). Danh sách thiết bị tự làm mới mỗi 3 giây.<br>- 45 test lõi (phân tích `adb devices -l` mẫu dựng sẵn + dựng đối số dòng lệnh scrcpy cho mọi tổ hợp tùy chọn - không cần thiết bị/tiến trình thật) + 7 test UI (gọi `AdbController::listDevices()` THẬT trên máy build - chỉ đọc, an toàn, xác nhận plumbing adb hoạt động).<br>- **Giới hạn thật, nêu rõ với người dùng**: không có điện thoại Android gắn sẵn trong môi trường phát triển này, nên KHÔNG tự kiểm tra được gương màn hình/ghép đôi không dây đầu-cuối trên thiết bị thật - cần tự kiểm tra tay. Nhúng cửa sổ gương trực tiếp vào cửa sổ Qt (thay vì cửa sổ riêng của scrcpy) là hướng mở rộng khả thi sau này nếu cần. | `src/tools/android/**`<br>`vendor/scrcpy/` (không commit)<br>`tests/android_tests.cpp`<br>`tests/android_ui_tests.cpp`<br>`src/ui/MainWindow.cpp`<br>`build_app.bat`<br>`THIRD_PARTY.md`<br>`CMakeLists.txt` |

---
*Dự án được phát triển theo tài liệu đặc tả thiết kế [OneForAll_AutoClick_Design.md](file:///d:/ALLinONE/OneForAll_AutoClick_Design.md).*
