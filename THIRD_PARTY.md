# Thư viện và nguồn tham khảo

Mã nguồn CỦA DỰ ÁN NÀY (thư mục `src/` trừ `src/third_party/`, cùng các file cấu hình/build ở gốc dự án)
phát hành theo giấy phép MIT - xem file `LICENSE` ở gốc dự án. Các thư viện/công cụ bên thứ ba dùng/đóng
gói kèm theo liệt kê dưới đây giữ nguyên giấy phép GỐC của chính chúng.

## Công cụ BUILD-TIME (không đóng gói/phân phối cùng ứng dụng, chỉ cần khi tự dựng bộ cài đặt)
| Công cụ | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **Inno Setup** (Jordan Russell/Martijn Laan) | Biên dịch `installer/OneForAll.iss` thành `dist\OneForAll_Setup.exe` (một file cài đặt duy nhất) qua `build_installer.bat` | Tùy chỉnh (miễn phí cho mọi mục đích, kể cả thương mại) | https://jrsoftware.org/isinfo.php |
| **Vietnamese.isl** (memecoder, bản dịch cộng đồng) | Giao diện bộ cài đặt thuần tiếng Việt | Cùng giấy phép Inno Setup | https://github.com/jrsoftware/issrc/blob/main/Files/Languages/Unofficial/Vietnamese.isl (vendor một bản tại `installer/Vietnamese.isl`) |

Ghi chú: Inno Setup CHỈ là công cụ phát triển (giống Qt SDK/CMake/Ninja) - không đóng gói/phân phối cùng
`OneForAll.exe`, người dùng cuối không cần cài. Tải bản 7.1.0 trực tiếp từ GitHub Releases của dự án
(`jrsoftware/issrc`), đã xác minh đúng SHA-256 theo `digest` mà GitHub Releases API trả về trước khi cài.
`Vietnamese.isl` lấy từ chính kho mã nguồn chính thức của Inno Setup (thư mục dành cho bản dịch cộng
đồng chưa đóng gói sẵn), không phải nguồn thứ ba không rõ gốc.

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
| **yt-dlp** | Tải video/âm thanh từ YouTube/Facebook/TikTok và hơn 1000 nền tảng khác - dùng cho tab "Video nền tảng" của Downloader | Mã nguồn: Unlicense (public domain). **Bản thực thi `yt-dlp.exe` đã biên dịch (PyInstaller) là GPLv3+ dạng kết hợp** (chứa mã khởi động PyInstaller thuộc GPLv3+) - đã xác nhận qua README chính thức của dự án, không phải Unlicense thuần như mã nguồn | https://github.com/yt-dlp/yt-dlp |
| **FFmpeg** (`ffmpeg.exe`/`ffprobe.exe`, bản dựng tĩnh BtbN/FFmpeg-Builds) | Ghép luồng video+âm thanh tải riêng thành 1 tệp hoàn chỉnh - YouTube chất lượng cao hiện đại hầu hết không còn định dạng ghép sẵn, **bắt buộc cần ffmpeg** (đã xác nhận thật: thử `-f best` không có ffmpeg báo lỗi "Requested format is not available" trên video thật) | GPL (bản dựng "gpl", gồm codec GPL) | https://github.com/BtbN/FFmpeg-Builds |

Ghi chú tích hợp:
- Tải `yt-dlp.exe` từ GitHub Releases của yt-dlp (đã xác minh SHA-256 theo `SHA2-256SUMS` đính kèm bản phát hành) và `ffmpeg.exe`/`ffprobe.exe` từ bản dựng `ffmpeg-n*-win64-gpl-*.zip` của BtbN/FFmpeg-Builds (đã xác minh SHA-256 theo `checksums.sha256` đính kèm) vào `vendor/yt-dlp/` ở gốc dự án (file `YtDlpController::ytDlpExecutablePath()`/`ffmpegDirectoryPath()` tìm `<thư mục chạy exe>/yt-dlp/yt-dlp.exe` và truyền `--ffmpeg-location <thư mục đó>` cho yt-dlp).
- **Không commit** `vendor/` vào Git (xem `.gitignore`) - cùng lý do như `vendor/scrcpy/`. `build_app.bat` tự sao chép `vendor/yt-dlp/*` vào `OneForAll_Release/yt-dlp/` khi đóng gói.
- Vì sao dùng yt-dlp có sẵn thay vì tự viết: trích xuất video từ hàng nghìn trang web khác nhau (mỗi trang một cách mã hóa/phân phối luồng riêng, thay đổi liên tục để chống tải) là bài toán cực lớn đã được yt-dlp (fork tích cực bảo trì của youtube-dl, cập nhật gần như hàng tuần) giải quyết - tự viết lại không khả thi và sẽ lỗi thời rất nhanh.
- Đã xác nhận thật (không chỉ tin theo tài liệu): chạy `yt-dlp --dump-json`/tải thật một video ngắn công khai ("Me at the zoo", video YouTube đầu tiên) qua chính các lớp C++ của ứng dụng (`YtDlpInfoWorker`/`YtDlpDownloadWorker`) - lấy đúng thông tin định dạng, tải và ghép video+âm thanh thành công bằng ffmpeg bundled, tiến độ `--progress-template` đọc được chính xác.

## Đã cân nhắc nhưng không dùng
- **ZXing-cpp** (Apache-2.0, https://github.com/zxing-cpp/zxing-cpp): bộ đọc mạnh hơn, hỗ trợ nhiều loại mã vạch, nhưng lớn và cần thêm cấu hình build; có thể thay thế quirc sau nếu cần quét mã vạch 1D.
- **libqrencode** (LGPL): giấy phép ràng buộc hơn.

## Tài liệu định dạng nội dung
- ISO/IEC 18004: chuẩn mã QR.
- ZXing wiki "Barcode Contents": định dạng `WIFI:`, `SMSTO:`, `geo:`, `MECARD:`, `MATMSG:`.
- RFC 6068 (`mailto:`), RFC 3966 (`tel:`), RFC 5870 (`geo:`), RFC 2426 / vCard 3.0.
- Qt Multimedia (`QCamera`, `QMediaCaptureSession`, `QVideoSink`): quét bằng camera.
