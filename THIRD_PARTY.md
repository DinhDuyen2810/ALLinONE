# Thư viện và nguồn tham khảo

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

## Đã cân nhắc nhưng không dùng
- **ZXing-cpp** (Apache-2.0, https://github.com/zxing-cpp/zxing-cpp): bộ đọc mạnh hơn, hỗ trợ nhiều loại mã vạch, nhưng lớn và cần thêm cấu hình build; có thể thay thế quirc sau nếu cần quét mã vạch 1D.
- **libqrencode** (LGPL): giấy phép ràng buộc hơn.

## Tài liệu định dạng nội dung
- ISO/IEC 18004: chuẩn mã QR.
- ZXing wiki "Barcode Contents": định dạng `WIFI:`, `SMSTO:`, `geo:`, `MECARD:`, `MATMSG:`.
- RFC 6068 (`mailto:`), RFC 3966 (`tel:`), RFC 5870 (`geo:`), RFC 2426 / vCard 3.0.
- Qt Multimedia (`QCamera`, `QMediaCaptureSession`, `QVideoSink`): quét bằng camera.
