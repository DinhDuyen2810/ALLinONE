# Thư viện và nguồn tham khảo

Mã nguồn CỦA DỰ ÁN NÀY (thư mục `src/` trừ `src/third_party/`, cùng các file cấu hình/build ở gốc dự án)
phát hành theo giấy phép MIT - xem file `LICENSE` ở gốc dự án. Các thư viện/công cụ bên thứ ba dùng/đóng
gói kèm theo liệt kê dưới đây giữ nguyên giấy phép GỐC của chính chúng.

## Công cụ BUILD-TIME (không đóng gói/phân phối cùng ứng dụng, chỉ cần khi tự dựng bộ cài đặt)
| Công cụ | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **Inno Setup** (Jordan Russell/Martijn Laan) | Biên dịch `installer/OneForAll.iss` thành `dist\OneForAll_Setup.exe` (một file cài đặt duy nhất) qua `build_installer.bat` | Tùy chỉnh (miễn phí cho mọi mục đích, kể cả thương mại) | https://jrsoftware.org/isinfo.php |
| **Vietnamese.isl** (memecoder, bản dịch cộng đồng) | Giao diện bộ cài đặt thuần tiếng Việt | Cùng giấy phép Inno Setup | https://github.com/jrsoftware/issrc/blob/main/Files/Languages/Unofficial/Vietnamese.isl (vendor một bản tại `installer/Vietnamese.isl`) |
| **WiX Toolset v3** (`candle.exe`/`heat.exe`/`light.exe`, .NET Foundation) | Biên dịch `installer/Product.wxs` (+ `Harvested.wxs` do `heat.exe` tự sinh) thành `dist\OneForAll_Setup.msi` qua `build_installer.bat` - bản cài đặt thay thế cho `OneForAll_Setup.exe`, đi qua dịch vụ Windows Installer có sẵn (`msiexec.exe`) thay vì tự giải nén ra thư mục tạm (xem PROJECT_OVERVIEW.md mục 4n/4o) | MS-RL | https://github.com/wixtoolset/wix3 |

Ghi chú: Inno Setup và WiX Toolset CHỈ là công cụ phát triển (giống Qt SDK/CMake/Ninja) - không đóng gói/
phân phối cùng `OneForAll.exe`, người dùng cuối không cần cài. Tải bản Inno Setup 7.1.0 trực tiếp từ
GitHub Releases của dự án (`jrsoftware/issrc`), đã xác minh đúng SHA-256 theo `digest` mà GitHub Releases
API trả về trước khi cài. `Vietnamese.isl` lấy từ chính kho mã nguồn chính thức của Inno Setup (thư mục
dành cho bản dịch cộng đồng chưa đóng gói sẵn), không phải nguồn thứ ba không rõ gốc. WiX Toolset v3.14.1
tải trực tiếp từ bản phát hành chính thức `wixtoolset/wix3` trên GitHub (`wix314-binaries.zip`) - KHÔNG có
checksum chính thức đính kèm bản phát hành này (đã kiểm tra qua GitHub Releases API, trường `digest` trả
về rỗng) - chấp nhận cùng mức rủi ro đã áp dụng cho BtbN/FFmpeg-Builds (không có nguồn xác minh thay thế,
vẫn tải trực tiếp từ kho chính thức của chính dự án đó).

## Thư viện được nhúng (vendored) trong `src/third_party/` (mã nguồn, đi kèm trong Git)
| Thư viện | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **QR Code generator library** (Nayuki) | Mã hóa QR (v1-40, 4 mức sửa lỗi, tự chọn mask/mode) | MIT | https://github.com/nayuki/QR-Code-generator (bản C++) |
| **quirc** (Daniel Beer) | Giải mã QR từ ảnh | ISC | https://github.com/dlbeer/quirc |

Ghi chú tích hợp:
- quirc mặc định chỉ chứa 254 vùng liên thông (pixel 8-bit), nên mã QR mật độ cao (version >= ~26) không được tìm thấy. Dự án biên dịch với `QUIRC_MAX_REGIONS=65534` (xem `CMakeLists.txt`); bộ test `qr_tests` kiểm tra mọi version tới 40.
- quirc không đọc được mã đảo màu, nên `QRCodec::decode` thử thêm lượt đảo màu, phóng to và thu nhỏ ảnh.

## Công cụ nhị phân được đóng gói (vendored) trong `vendor/scrcpy/` (KHÔNG commit vào Git - xem dưới)
| Công cụ | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **scrcpy** (Genymobile) | Gương + điều khiển màn hình Android thời gian thực (mã hóa/giải mã H.264, tiêm sự kiện chạm/phím) - dùng cho tính năng Android Phone Control | Apache-2.0 | https://github.com/Genymobile/scrcpy |
| **adb** (Android Debug Bridge, Android Platform Tools) | Liệt kê/ghép đôi/kết nối thiết bị Android qua USB và Wi-Fi - đóng gói sẵn trong bản phát hành Windows của scrcpy | Apache-2.0 | https://developer.android.com/tools/releases/platform-tools |

Ghi chú tích hợp:
- Tham khảo gói phát hành chính thức `scrcpy-win64-vX.Y.zip` từ GitHub Releases của dự án scrcpy (đã xác minh checksum SHA-256 theo `SHA256SUMS.txt` đính kèm bản phát hành trước khi dùng). Giải nén vào `vendor/scrcpy/` ở gốc dự án (file `AdbController::adbExecutablePath()`/`scrcpyExecutablePath()` tìm `<thư mục chạy exe>/scrcpy/adb.exe` và `.../scrcpy.exe`).
- **Không commit** `vendor/` vào Git (xem `.gitignore`) - tương tự `build/`/`OneForAll_Release/`, đây là tài nguyên triển khai (deployment), không phải mã nguồn. `build_app.bat` tự sao chép `vendor/scrcpy/*` vào `OneForAll_Release/scrcpy/` khi đóng gói.
- Vì sao dùng scrcpy có sẵn thay vì tự viết: mã hóa màn hình Android thành H.264 thời gian thực (qua `MediaProjection`+`MediaCodec` phía Android) và tiêm sự kiện chạm/phím độ trễ thấp (qua `InputManager` phản chiếu - không có API công khai) là bài toán Android-side phức tạp, đã được scrcpy giải quyết và kiểm chứng qua nhiều năm sử dụng thực tế rộng rãi - tự viết lại sẽ tốn công sức lớn, rủi ro cao, và không thể kiểm thử đầy đủ trong môi trường phát triển này (không có thiết bị Android thật gắn sẵn).

## Công cụ nhị phân được đóng gói (vendored) trong `vendor/yt-dlp/` (KHÔNG commit vào Git - xem dưới)
| Công cụ | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **yt-dlp** | Tải video/âm thanh từ YouTube/Facebook/TikTok và hơn 1000 nền tảng khác - dùng cho tải video nền tảng trong tab "Tải xuống" của Downloader | Mã nguồn: Unlicense (public domain). **Bản thực thi `yt-dlp.exe` đã biên dịch (PyInstaller) là GPLv3+ dạng kết hợp** (chứa mã khởi động PyInstaller thuộc GPLv3+) - đã xác nhận qua README chính thức của dự án, không phải Unlicense thuần như mã nguồn | https://github.com/yt-dlp/yt-dlp |
| **FFmpeg** (`ffmpeg.exe`/`ffprobe.exe`, bản dựng tĩnh BtbN/FFmpeg-Builds) | Ghép luồng video+âm thanh tải riêng thành 1 tệp hoàn chỉnh - YouTube chất lượng cao hiện đại hầu hết không còn định dạng ghép sẵn, **bắt buộc cần ffmpeg** (đã xác nhận thật: thử `-f best` không có ffmpeg báo lỗi "Requested format is not available" trên video thật) | GPL (bản dựng "gpl", gồm codec GPL) | https://github.com/BtbN/FFmpeg-Builds |

Ghi chú tích hợp:
- Tải `yt-dlp.exe` từ GitHub Releases của yt-dlp (đã xác minh SHA-256 theo `SHA2-256SUMS` đính kèm bản phát hành) và `ffmpeg.exe`/`ffprobe.exe` từ bản dựng win64-gpl của BtbN/FFmpeg-Builds (xác minh SHA-256 theo `checksums.sha256` đính kèm bản phát hành - `scripts/ci_fetch_vendor.ps1` làm đúng bước này trong CI từ bản 1.19.0; trước đó script CI tải ffmpeg mà không kiểm checksum) vào `vendor/yt-dlp/` ở gốc dự án (file `YtDlpController::ytDlpExecutablePath()`/`ffmpegDirectoryPath()` tìm `<thư mục chạy exe>/yt-dlp/yt-dlp.exe` và truyền `--ffmpeg-location <thư mục đó>` cho yt-dlp).
- **Không commit** `vendor/` vào Git (xem `.gitignore`) - cùng lý do như `vendor/scrcpy/`. `build_app.bat` tự sao chép `vendor/yt-dlp/*` vào `OneForAll_Release/yt-dlp/` khi đóng gói.
- Vì sao dùng yt-dlp có sẵn thay vì tự viết: trích xuất video từ hàng nghìn trang web khác nhau (mỗi trang một cách mã hóa/phân phối luồng riêng, thay đổi liên tục để chống tải) là bài toán cực lớn đã được yt-dlp (fork tích cực bảo trì của youtube-dl, cập nhật gần như hàng tuần) giải quyết - tự viết lại không khả thi và sẽ lỗi thời rất nhanh.
- Đã xác nhận thật (không chỉ tin theo tài liệu): chạy `yt-dlp --dump-json`/tải thật một video ngắn công khai ("Me at the zoo", video YouTube đầu tiên) qua chính các lớp C++ của ứng dụng (`YtDlpInfoWorker`/`YtDlpDownloadWorker`) - lấy đúng thông tin định dạng, tải và ghép video+âm thanh thành công bằng ffmpeg bundled, tiến độ `--progress-template` đọc được chính xác.
- **Trình duyệt ẩn (từ v1.19.16)**: tính năng quét sâu của Downloader chạy Microsoft Edge hoặc Google Chrome ĐÃ CÓ sẵn trên máy người dùng (`--headless=new`, hồ sơ tạm riêng) - ứng dụng KHÔNG đóng gói/phân phối trình duyệt nào, không thêm thư viện mới; không có Edge/Chrome thì bước này tự bỏ qua.

## Thư viện nhị phân được đóng gói (vendored) trong `vendor/wintun/` (KHÔNG commit vào Git - xem dưới)

| Thành phần | Dùng để làm gì | Giấy phép | Nguồn |
|---|---|---|---|
| **wintun.dll** 0.14.1 (WireGuard LLC) | Driver adapter mạng ảo TUN tầng 3 cho kết nối VPN TRỰC TIẾP tới FortiGate SSL-VPN / GlobalProtect trong tab "VPN đã cài sẵn" (từ v1.19.31). Ứng dụng nạp DLL này trong tiến trình phụ nâng quyền, tự viết phần giao thức VPN (xem `src/tools/vpn/direct/`) | "Prebuilt Binaries License" của WireGuard LLC (đã đọc nguyên văn): được phân phối lại KÈM phần mềm khác **chỉ dùng DLL qua API của `wintun.h`** ("Permitted API"), không sửa/dịch ngược DLL, giữ thông báo bản quyền, không dùng tên WireGuard/Wintun để quảng bá. Ứng dụng nạp động và chỉ gọi các hàm của `wintun.h` - đúng điều kiện; tệp `LICENSE.txt` đi kèm trong `wintun/` | https://www.wintun.net/ |

- Tải `wintun-0.14.1.zip` từ nguồn CHÍNH THỨC `wintun.net`, lấy `wintun/bin/amd64/wintun.dll` + `LICENSE.txt`. Zip không kèm `SHA256SUMS`, nhưng DLL được ký **Authenticode** bởi "WireGuard LLC" - xác minh bằng `Get-AuthenticodeSignature` (phải = `Valid`, chủ thể WireGuard; đã xác nhận THẬT). Đặt vào `vendor/wintun/` ở gốc dự án (`WintunAdapter::dllPath()` = `<thư mục chạy exe>/wintun/wintun.dll`, nạp bằng đường dẫn tuyệt đối).
- **Không commit** `vendor/` vào Git (xem `.gitignore`). `build_app.bat` (bước [6/8]) chép `vendor/wintun/*` vào `OneForAll_Release/wintun/`; `scripts/ci_fetch_vendor.ps1` tự tải + xác minh Authenticode trong CI (không dùng gpg - gpg của Git trên runner hiểu sai `GNUPGHOME` kiểu Windows, đã gặp thật).
- **Đã gỡ ở v1.19.31**: bộ `vendor/openvpn/` (OpenVPN community `openvpn.exe` + OpenSSL DLL) của v1.19.30 - người dùng yêu cầu bỏ OpenVPN; VPN Gate nay nối bằng SSTP có sẵn của Windows.

## Tham chiếu giao thức (đọc đặc tả, KHÔNG chép/nhúng mã)

| Nguồn | Dùng để làm gì | Giấy phép | Ghi chú |
|---|---|---|---|
| **OpenConnect** (`fortinet.c`, `ppp.c`, `gpst.c`, `auth-globalprotect.c`) | Đặc tả thực tế của giao thức FortiGate SSL-VPN (đăng nhập, OTP, XML cấu hình, khung PPP 6 byte) và GlobalProtect (prelogin/portal/gateway, JNLP, khung gói 16 byte) - Fortinet/Palo Alto không công bố giao thức | LGPL-2.1 | https://gitlab.com/openconnect/openconnect - ứng dụng TỰ VIẾT lại bằng Qt/C++ theo hành vi đã mô tả, không sao chép mã nguồn OpenConnect vào dự án |
| RFC 1661 (PPP/LCP), RFC 1332 (IPCP), RFC 1877 (DNS qua IPCP) | Phiên PPP trong đường hầm FortiGate (`PppSession`) | Tiêu chuẩn IETF | |

## Nguồn dữ liệu trực tuyến (không đóng gói mã nguồn/nhị phân, chỉ gọi API công khai khi dùng tính năng)

| Nguồn | Dùng ở đâu | Giấy phép/điều khoản | Ghi chú |
|---|---|---|---|
| **VPN Gate** (dự án nghiên cứu, ĐH Tsukuba, Nhật Bản) | Nguồn DỮ LIỆU: danh sách máy chủ VPN công cộng miễn phí (tab "Đổi IP theo quốc gia") | API công khai, dành cho phần mềm khách bên thứ ba | https://www.vpngate.net/ - KHÔNG đóng gói mã/nhị phân nào của VPN Gate; ứng dụng chỉ TẢI danh sách máy chủ qua API công khai `www.vpngate.net/api/iphone/` rồi kết nối bằng VPN có sẵn của Windows: **SSTP** tới tên miền DDNS `<HostName>.opengw.net` của chính máy chủ (mặc định từ v1.19.31; chứng chỉ Let's Encrypt `*.opengw.net`) hoặc **L2TP/IPsec** tới IP máy chủ |

## Đã cân nhắc nhưng không dùng
- **ZXing-cpp** (Apache-2.0, https://github.com/zxing-cpp/zxing-cpp): bộ đọc mạnh hơn, hỗ trợ nhiều loại mã vạch, nhưng lớn và cần thêm cấu hình build; có thể thay thế quirc sau nếu cần quét mã vạch 1D.
- **libqrencode** (LGPL): giấy phép ràng buộc hơn.

## Tài liệu định dạng nội dung
- ISO/IEC 18004: chuẩn mã QR.
- ZXing wiki "Barcode Contents": định dạng `WIFI:`, `SMSTO:`, `geo:`, `MECARD:`, `MATMSG:`.
- RFC 6068 (`mailto:`), RFC 3966 (`tel:`), RFC 5870 (`geo:`), RFC 2426 / vCard 3.0.
- Qt Multimedia (`QCamera`, `QMediaCaptureSession`, `QVideoSink`): quét bằng camera.
