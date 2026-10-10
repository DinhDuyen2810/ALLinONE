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
   thật rồi sửa, không đoán. Nếu có sửa `.github/workflows/*.yml` thì kiểm cú pháp TRƯỚC khi push
   (`python -c "import yaml,io; yaml.safe_load(io.open('.github/workflows/build-installer.yml',encoding='utf-8'))"`) -
   tệp workflow sai cú pháp cho ra lượt chạy "failure" với 0 job và không có log nào để đọc.
6. **Gắn tag `vX.Y.Z` (đúng số trong `VERSION`) rồi push tag** để CI tự tạo GitHub Release kèm đủ 4 file -
   LUÔN làm sau khi bước 5 xác nhận CI PASS, không cần hỏi lại mỗi lần (người dùng đã xác nhận muốn vậy
   theo quy trình chuẩn, v1.19.5). `git tag vX.Y.Z <commit>` rồi `git push origin vX.Y.Z` - việc push tag
   có thể bị hệ thống phân quyền tự động chặn ("[Create Public Surface]"); đó là rào chắn có chủ đích,
   KHÔNG tìm cách lách qua - báo lại người dùng rõ ràng (tag đã tạo cục bộ, chỉ còn bước push) để họ tự
   push hoặc xác nhận lại. Sau khi tag lên, đợi CI build-installer cho tag chạy xong rồi xác nhận GitHub
   Release có đủ 4 asset đúng tên qua
   `https://api.github.com/repos/DinhDuyen2810/ALLinONE/releases/tags/vX.Y.Z` - không coi là xong nếu
   chưa xác nhận được.

`dist\`, `obj\`, `build\`, `OneForAll_Release\`, `vendor\`, `logs\` không commit (xem `.gitignore`).

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

### 5. Giữ đầy đủ metadata phiên bản Win32 (VERSIONINFO) khi phát hành

`assets/app_icon.rc` (cho `OneForAll.exe`), `installer/OneForAll.iss` (cho `OneForAll_Setup.exe`),
`installer/Product.wxs` (cho `OneForAll_Setup.msi`) đều có khối VERSIONINFO/properties đầy đủ
(CompanyName/FileDescription/ProductName/LegalCopyright/...) - KHÔNG xóa hay để rỗng lại khi sửa các file
này. Lý do và bằng chứng thật: `PROJECT_OVERVIEW.md` mục 4r. Đây là thực hành Microsoft khuyến nghị để
cải thiện uy tín SmartScreen, nhưng KHÔNG đảm bảo tắt hẳn cảnh báo trên mọi máy - đặc biệt CHƯA kiểm chứng
trên máy Windows 11 có Smart App Control ở chế độ "On". Nếu tăng `VERSION`, nhớ `OBJECT_DEPENDS` trong
`CMakeLists.txt` đã khai cho `app_icon.rc` - không tự dò lại, Ninja không phát hiện `#include` trong `.rc`.

### 6. Không thao tác hệ thống thật khi kiểm thử

Test tự động không được: đổi kích thước phân vùng, sửa file hosts thật, `Set-MpPreference`, kết nối VPN
thật, đổi cấu hình WiFi của máy, tiêm chuột/phím thật, xóa gì ngoài thư mục tạm do test tự tạo. Test đụng
tới dữ liệu người dùng phải dùng `TestDataDir` (`tests/TestDataDir.h`) ngay đầu `main`: nó tạo thư mục tạm, gọi
`AppPaths::setDataDirOverride()` và đóng tệp log trước khi xóa - `QTemporaryDir` trần sẽ để sót thư mục trong
`%TEMP%` vì `Logger` giữ tệp log mở trong đó.

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

Năm bộ stress/fuzz (mỗi bộ 2-5 phút, chạy khi máy RẢNH - chúng có ngưỡng thời gian): `core_stress_tests`,
`qrwifi_stress_tests`, `connect_stress_tests`, `disk_vpn_stress_tests`, `secdl_stress_tests`. Chạy lại bộ của
phần vừa sửa trước khi commit. Bộ có dựng widget phải chọn nền tảng qua `tests/OffscreenPlatform.h`, KHÔNG
ép thẳng `QT_QPA_PLATFORM=offscreen` (thư mục `build\` đã windeployqt không có plugin đó - Qt sẽ bật hộp
thoại lỗi lên màn hình người dùng và treo).

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
