# One for ALL - quy tắc làm việc cho Claude Code

Ứng dụng desktop Windows (C++20, Qt 6 MinGW, CMake + Ninja) gồm 9 công cụ. Kiến trúc và lịch sử quyết định
kỹ thuật: `PROJECT_OVERVIEW.md`. Changelog theo phiên bản: `README.md` mục 6. Thư viện bên thứ ba:
`THIRD_PARTY.md`.

## Quy tắc đứng - áp dụng cho MỌI lần sửa

### 1. Sau mỗi lần sửa code: build đủ → commit → push

Không coi một lần sửa là xong cho tới khi đã làm đủ, theo đúng thứ tự:

1. **Build exe + chạy test liên quan.** `build_app.bat /nopause` ra `build\OneForAll.exe`; build và chạy
   các target `*_tests` / `*_ui_tests` của phần vừa sửa (xem mục "Lệnh" bên dưới). Test fail thì sửa
   trước, không commit.
2. **Build đủ 4 file phát hành** bằng MỘT lệnh: `build_installer.bat /nopause` → trong `dist\`:
   - `OneForAll_Setup.exe` (Inno Setup)
   - `OneForAll_Setup.msi` (WiX Toolset)
   - `OneForAll_Setup_exe.zip` (zip của file .exe)
   - `OneForAll_Setup_msi.zip` (zip của file .msi)
3. **Cập nhật tài liệu đi kèm**: tăng `VERSION` (nguồn DUY NHẤT cho số phiên bản), thêm dòng changelog
   vào `README.md` mục 6 và `PROJECT_OVERVIEW.md` mục 8, sửa mọi chỗ mô tả hành vi đã đổi (kể cả
   `PRIVACY.md`/`THIRD_PARTY.md` nếu liên quan).
4. **Commit** với message mô tả rõ thay đổi.
5. **Push** lên `origin main`, rồi kiểm tra lượt chạy GitHub Actions mới nhất có PASS không (máy này
   không có `gh`; dùng API công khai:
   `https://api.github.com/repos/DinhDuyen2810/ALLinONE/actions/runs?per_page=3`). CI fail thì đọc log
   thật rồi sửa, không đoán.

`dist\`, `obj\`, `build\`, `OneForAll_Release\`, `vendor\`, `logs\` không commit (xem `.gitignore`).

Gắn tag `vX.Y.Z` / tạo GitHub Release KHÔNG nằm trong quy tắc này: đẩy tag làm CI tự tạo Release và mọi
máy đã cài sẽ được mời cập nhật - chỉ làm khi người dùng yêu cầu rõ.

### 2. Xác nhận thật, không đoán

Báo "đã sửa" chỉ khi đã build và chạy được. Điều gì không kiểm chứng được trên máy này (cần 2 máy LAN,
điện thoại Android, tài khoản VPN, hộp thoại UAC...) thì ghi rõ là CHƯA kiểm chứng, trong cả câu trả lời
lẫn tài liệu.

### 3. WiFi Connection: chỉ quản lý kết nối hợp pháp

Xem mật khẩu đã lưu trên chính máy này, kết nối bằng ĐÚNG MỘT mật khẩu người dùng nhập. KHÔNG thêm bất
kỳ cơ chế thử nhiều mật khẩu / dò mật khẩu WiFi nào, dưới bất kỳ biến thể nào.

### 4. Không tự ký số (code-signing)

Không ký `.exe`/`.msi`, không thêm chứng chỉ vào kho tin cậy của máy. Việc ký chỉ do chủ dự án làm, hoặc
qua SignPath trong CI khi chứng chỉ được cấp.

### 5. Không thao tác hệ thống thật khi kiểm thử

Test tự động không được: đổi kích thước phân vùng, sửa file hosts thật, `Set-MpPreference`, kết nối VPN
thật, đổi cấu hình WiFi của máy, tiêm chuột/phím thật, xóa gì ngoài thư mục tạm do test tự tạo. Test đụng
tới dữ liệu người dùng phải gọi `AppPaths::setDataDirOverride(<thư mục tạm>)` ngay đầu `main`.

## Lệnh

```bat
:: Build nhanh + đóng gói bản chạy thử (OneForAll_Release\)
build_app.bat /nopause
run_app.bat

:: Build + chạy một bộ test (PATH cần Qt/MinGW/CMake/Ninja như đầu build_app.bat)
set "PATH=D:\Qt\Tools\mingw1310_64\bin;D:\Qt\Tools\CMake_64\bin;D:\Qt\Tools\Ninja;D:\Qt\6.11.1\mingw_64\bin;%PATH%"
cmake --build build --target connect_tests connect_ui_tests
build\connect_tests.exe

:: Đủ 4 file phát hành
build_installer.bat /nopause
```

Các bộ test: `qr`, `wifi`, `connect`, `diskcleanup`, `android`, `vpn`, `security`, `downloader` (mỗi bộ có
`_tests` và `_ui_tests`), `autoclick_tests`, `update_tests`. Chúng là target `EXCLUDE_FROM_ALL`, phải gọi tên.

## Kiến trúc cần nhớ khi sửa

- Mỗi công cụ là một `ITool` (`src/core/Tool.h`) đăng ký trong `MainWindow::registerTools()`; cửa sổ công
  cụ không có cha, giữ qua `QPointer`, đóng chỉ ẨN và được tái dùng - mọi thứ cần dừng khi đóng phải làm
  trong `closeEvent`, mọi thứ cần dừng khi thoát app phải làm trong `stopBackgroundWorkForQuit()`.
- Mỗi công cụ tách `*_core` (không widget, test được) và `*_ui`. Phần dùng chung nằm trong lib
  `core_shared` (`AppPaths`, `Logger`, `WinElevation`, `PowerShellRunner`), `input_core`
  (`InputController`), `winprocess_core`. Không liệt kê trùng một file `.cpp` trong hai target.
- Dữ liệu người dùng (hồ sơ JSON, log) nằm ở `%LOCALAPPDATA%\OneForAll\` qua `AppPaths` - không dùng
  đường dẫn tương đối `profiles/...` hay `logs/...`.
- Chuỗi không tự kiểm soát đưa vào script PowerShell phải qua `PowerShellRunner::quoteLiteral()`. Tiến
  trình hệ thống gọi bằng đường dẫn tuyệt đối (`AppPaths::systemExecutable`).
- Cờ hủy dùng chung giữa hai luồng là `std::atomic_bool` và phải được đặt lại mỗi lần bắt đầu thao tác mới.
