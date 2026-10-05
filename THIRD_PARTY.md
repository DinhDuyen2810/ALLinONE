# Thư viện và nguồn tham khảo (QR Tools)

## Thư viện được nhúng (vendored) trong `src/third_party/`
| Thư viện | Dùng để | Giấy phép | Nguồn |
|---|---|---|---|
| **QR Code generator library** (Nayuki) | Mã hóa QR (v1-40, 4 mức sửa lỗi, tự chọn mask/mode) | MIT | https://github.com/nayuki/QR-Code-generator (bản C++) |
| **quirc** (Daniel Beer) | Giải mã QR từ ảnh | ISC | https://github.com/dlbeer/quirc |

Ghi chú tích hợp:
- quirc mặc định chỉ chứa 254 vùng liên thông (pixel 8-bit), nên mã QR mật độ cao (version >= ~26) không được tìm thấy. Dự án biên dịch với `QUIRC_MAX_REGIONS=65534` (xem `CMakeLists.txt`); bộ test `qr_tests` kiểm tra mọi version tới 40.
- quirc không đọc được mã đảo màu, nên `QRCodec::decode` thử thêm lượt đảo màu, phóng to và thu nhỏ ảnh.

## Đã cân nhắc nhưng không dùng
- **ZXing-cpp** (Apache-2.0, https://github.com/zxing-cpp/zxing-cpp): bộ đọc mạnh hơn, hỗ trợ nhiều loại mã vạch, nhưng lớn và cần thêm cấu hình build; có thể thay thế quirc sau nếu cần quét mã vạch 1D.
- **libqrencode** (LGPL): giấy phép ràng buộc hơn.

## Tài liệu định dạng nội dung
- ISO/IEC 18004: chuẩn mã QR.
- ZXing wiki "Barcode Contents": định dạng `WIFI:`, `SMSTO:`, `geo:`, `MECARD:`, `MATMSG:`.
- RFC 6068 (`mailto:`), RFC 3966 (`tel:`), RFC 5870 (`geo:`), RFC 2426 / vCard 3.0.
- Qt Multimedia (`QCamera`, `QMediaCaptureSession`, `QVideoSink`): quét bằng camera.
