# Thiết lập phiên làm việc mới tại D:\ALLinONE

File này dành cho việc MỞ MỘT PHIÊN CLAUDE CODE MỚI với working directory là `D:\ALLinONE` (phiên tạo ra
các thay đổi gần nhất lại đang gắn với một thư mục/repo Git khác, nên không tự push lên GitHub được - xem
mục "Trạng thái hiện tại" bên dưới). Đọc file này để nắm đủ ngữ cảnh build/release/Git mà không cần đọc
lại toàn bộ lịch sử hội thoại cũ. Chi tiết TỪNG TÍNH NĂNG/kiến trúc nằm ở `PROJECT_OVERVIEW.md` (file này
KHÔNG lặp lại nội dung đó).

## 1. Dự án là gì

"One for ALL" - ứng dụng desktop Windows (C++20/Qt 6.11.1 MinGW, CMake+Ninja) gồm 9 công cụ (Auto Click,
QR Tools, WiFi Connection, Đo tốc độ mạng, Connect Together, Disk Cleanup, Android Phone Control, VPN &
Location, Security Gateway, Downloader). Mã nguồn + lịch sử đầy đủ: GitHub `DinhDuyen2810/ALLinONE`.

## 2. Trạng thái hiện tại (tại thời điểm viết file này)

- **Remote Git ĐÃ cấu hình sẵn** trong `D:\ALLinONE\.git\config`:
  ```
  [remote "origin"]
      url = https://github.com/DinhDuyen2810/ALLinONE.git
  [branch "main"]
      remote = origin
      merge = refs/heads/main
  ```
  → Không cần `git remote add` gì cả, chỉ cần `git push`/`git pull` bình thường.
- **Có 1 commit LOCAL CHƯA PUSH** trên nhánh `main`: `5ad4320` - "Thêm bộ cài đặt .msi (WiX Toolset) + zip
  cho cả .exe/.msi". Lý do chưa push: phiên làm việc tạo ra commit này bị hệ thống phân quyền chặn lệnh
  `git push` (và cả `git remote -v`) vì phiên đó đang gắn với MỘT THƯ MỤC/REPO KHÁC (`C:\Users\HP\Documents\
  Mã độc`), không phải `D:\ALLinONE` - bị phân loại "ngoài phạm vi phiên làm việc", không phải lỗi thật.
  **Việc đầu tiên cần làm ở phiên mới (đang đúng working directory `D:\ALLinONE` nên sẽ không bị chặn)**:
  ```
  cd D:\ALLinONE
  git status          # xác nhận: ahead of origin/main by 1 commit, working tree clean
  git push origin main
  ```
- File `VERSION` hiện tại: `1.18.0`.
- `.github/workflows/build-installer.yml` đã được SỬA để CI tự dựng đủ 4 file release (trước đó chỉ dựng
  `.exe`) nhưng **CHƯA được xác nhận chạy thật** - vì cần push commit trên lên thì GitHub Actions mới chạy
  lại trên nhánh `main`. Sau khi push, nên vào tab "Actions" của repo (hoặc `gh run list`/`gh run view`
  nếu có GitHub CLI) kiểm tra lượt chạy mới nhất có PASS đủ cả bước build `.msi` không - đây là bước
  "xác nhận thật" còn thiếu, đúng kỷ luật của dự án (xem mục 7).
- Đơn xin chứng chỉ ký số miễn phí **SignPath Foundation** đã nộp, đang chờ email phản hồi (không có gì
  cần làm thêm phía code cho việc này, chỉ chờ).

## 3. Yêu cầu môi trường (trên máy dev Windows hiện tại)

| Công cụ | Đường dẫn trên máy dev | Dùng cho |
|---|---|---|
| Qt 6.11.1 MinGW | `D:\Qt\6.11.1\mingw_64` | Biên dịch ứng dụng |
| MinGW 13.1.0 | `D:\Qt\Tools\mingw1310_64\bin` | Toolchain C++ |
| CMake | `D:\Qt\Tools\CMake_64\bin` | Cấu hình build |
| Ninja | `D:\Qt\Tools\Ninja` | Build system |
| Inno Setup 6/7 | `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe` | Biên dịch `OneForAll_Setup.exe` |
| WiX Toolset v3.14.1 | `D:\WixToolset` (hoặc `%LOCALAPPDATA%\WixToolset`, hoặc biến môi trường `WIX_TOOLSET_BIN`) | Biên dịch `OneForAll_Setup.msi` |

`build_app.bat` tự set PATH tới Qt/MinGW/CMake/Ninja (xem dòng `set "PATH=D:\Qt\Tools\mingw1310_64\bin;...`
ở đầu file) - không cần tự cấu hình PATH hệ thống. `build_installer.bat` tự tìm WiX theo thứ tự ưu tiên
`WIX_TOOLSET_BIN` → `%LOCALAPPDATA%\WixToolset` → `D:\WixToolset`, báo lỗi rõ kèm link tải nếu không thấy.

WiX Toolset KHÔNG có trình cài đặt - tải trực tiếp `wix314-binaries.zip` từ
`https://github.com/wixtoolset/wix3/releases` rồi giải nén vào một trong 3 vị trí trên (xem
`THIRD_PARTY.md` để biết lý do không có checksum chính thức, đã chấp nhận rủi ro giống BtbN/FFmpeg).

## 4. Quy trình build

### 4a. Build nhanh để chạy thử (không cần trình cài đặt)
```
D:\ALLinONE\build_app.bat /nopause
D:\ALLinONE\run_app.bat
```
Ra `build\OneForAll.exe` + đồng bộ sang `OneForAll_Release\` (bản `run_app.bat` chạy).

### 4b. Build ĐỦ 4 file phát hành (bắt buộc mỗi lần sửa code - xem mục 7)
```
D:\ALLinONE\build_installer.bat /nopause
```
Một lệnh duy nhất, 5 bước, ra đủ 4 file trong `dist\`:
1. `OneForAll_Setup.exe` (Inno Setup, tự giải nén khi chạy)
2. `OneForAll_Setup.msi` (WiX/Windows Installer, đi qua `msiexec.exe` có sẵn của Windows - lối thoát cho
   máy bị Smart App Control chặn `.exe` tự giải nén, xem `PROJECT_OVERVIEW.md` mục 4m/4n)
3. `OneForAll_Setup_exe.zip` (zip của file #1)
4. `OneForAll_Setup_msi.zip` (zip của file #2)

`dist\` và `obj\` (file trung gian của WiX: `heat.exe`/`candle.exe`) KHÔNG commit vào Git (xem
`.gitignore`) - luôn phải tự build lại, không lấy từ Git.

**Đã xác nhận thật** (không chỉ biên dịch thành công) cả `.exe` và `.msi`: cài đặt im lặng, khởi chạy exe
vừa cài, gỡ cài đặt im lặng - xóa sạch hoàn toàn cả hai dạng. Chi tiết đầy đủ ở `PROJECT_OVERVIEW.md`
mục 4n.

## 5. Quy trình phát hành bản mới (để tính năng tự cập nhật trong app nhận ra)

Xem đầy đủ, chi tiết tại `PROJECT_OVERVIEW.md` mục 5c. Tóm tắt:
1. Sửa file `VERSION` ở gốc dự án (nguồn DUY NHẤT cho số phiên bản).
2. Cập nhật changelog `PROJECT_OVERVIEW.md`/`README.md`, commit.
3. `build_installer.bat /nopause` → ra đủ 4 file trong `dist\`.
4. `git tag vX.Y.Z && git push origin vX.Y.Z` (tag PHẢI có tiền tố "v", khớp đúng VERSION).
5. Tạo GitHub Release cho tag đó, đính kèm **ĐỦ 4 FILE** trong `dist\` làm asset (không chỉ riêng `.exe`
   như các bản release cũ trước v1.18.0).
6. `UpdateChecker` trong app so khớp TUYỆT ĐỐI tên asset `OneForAll_Setup.exe` (bỏ qua asset khác tên) -
   file đó vẫn phải có mặt trong Release để tính năng tự cập nhật hoạt động, dù giờ có thêm 3 file khác.

## 6. CI/CD (GitHub Actions)

`.github/workflows/build-installer.yml` - chạy trên máy ảo Windows sạch của GitHub khi: đẩy lên `main`,
đẩy tag `v*` (tự tạo GitHub Release kèm đủ 4 file), hoặc bấm chạy tay (`workflow_dispatch`). Pipeline: cài
Qt 6.10.3 MinGW (ghim version - xem lý do ở `PROJECT_OVERVIEW.md` mục 4m) → Ninja → Inno Setup + WiX Toolset
(qua Chocolatey) → tải/xác minh vendor (`scripts/ci_fetch_vendor.ps1`) → cấu hình CMake → biên dịch →
`windeployqt` → dựng `OneForAll_Release` → biên dịch `.exe` (ISCC) → biên dịch `.msi` (heat+candle+light) →
nén 2 zip → tải lên artifact + (nếu là tag) tạo Release.

**Việc cần làm ở phiên mới**: sau khi push commit `5ad4320`, vào GitHub Actions xác nhận lượt chạy mới
PASS đủ - đặc biệt các bước WiX mới thêm (chưa từng chạy thật trên CI, chỉ mới xác nhận thật trên máy dev
cục bộ). Nếu lỗi, xem log thật qua `gh run view <id> --log` (hoặc web UI) rồi sửa - đúng kỷ luật "xác nhận
thật" của dự án, không đoán.

## 7. Các quy tắc đứng (standing rules) - PHẢI tuân thủ ở MỌI thay đổi từ nay

1. **Mỗi lần sửa code xong**: build lại (`build_installer.bat /nopause` - đủ 4 file) → commit → push →
   cập nhật MỌI file liên quan (kể cả chỗ ghi tay như số version tĩnh trong README) - không bỏ sót bước
   nào, không commit mà chưa build/test.
2. **WiFi Connection** chỉ dùng để quản lý kết nối WiFi hợp pháp (xem mật khẩu đã lưu, kết nối mạng đã
   biết) - KHÔNG thêm tính năng bẻ khóa/brute-force mật khẩu WiFi của người khác.
3. **KHÔNG tự ký số (code-signing) bất kỳ file `.exe`/`.msi` nào** - hệ thống phân quyền của Claude Code sẽ
   tự chặn (phân loại "Security Weaken") nếu cố làm, kể cả qua cách khác/tool khác/lượt sau. Việc ký số
   CHỈ người dùng (chủ máy) tự làm được, hoặc chờ chứng chỉ SignPath được cấp rồi tích hợp vào CI (xem
   mục 2 - đang chờ phản hồi).

## 8. File tham khảo khác trong repo

- `PROJECT_OVERVIEW.md` - mô tả đầy đủ kiến trúc + từng tính năng + lịch sử quyết định kỹ thuật (mục 1-6).
- `README.md` - giới thiệu dự án + bảng changelog chi tiết từng phiên bản (mục 6).
- `THIRD_PARTY.md` - mọi thư viện/công cụ bên thứ ba, giấy phép, nguồn tải, cách xác minh.
- `PRIVACY.md` - chính sách quyền riêng tư (không thu thập dữ liệu, liệt kê rõ kết nối mạng ra ngoài).
- `LICENSE` - MIT (mã nguồn của dự án, không áp dụng cho thư viện bên thứ ba vendored).
