#pragma once

#include <QByteArray>

#include <map>

/// Ghép các ảnh JPEG thành một tệp PDF (mỗi ảnh = một trang phủ kín) - thuần Qt Core, không thư viện PDF, không mạng.
/// Dùng khi một nguồn chỉ cho xem từng trang dưới dạng ảnh (xem DriveViewerCapture). Bytes JPEG được đưa NGUYÊN VẸN vào
/// PDF (bộ lọc DCTDecode) nên không giải mã/nén lại và nhanh, nhẹ bộ nhớ.
namespace ImagePdf
{
struct Page
{
    QByteArray jpeg; ///< Nguyên tệp JPEG (bắt đầu bằng FF D8)
    int width{0};    ///< Kích thước ảnh theo pixel
    int height{0};
};

/// `pages`: khóa = SỐ TRANG (1..N). std::map tự sắp tăng dần nên thứ tự trang trong PDF luôn đúng dù ảnh được chụp lộn xộn.
/// Trang rỗng/kích thước <= 0 bị bỏ qua. Trả mảng rỗng nếu không có trang hợp lệ nào.
/// Kích thước trang PDF = pixel * 0,75 (96 dpi -> 72 điểm/inch). Số thực LUÔN dùng dấu chấm (không phụ thuộc locale máy -
/// một PDF có "540,00" là PDF hỏng).
QByteArray build(const std::map<int, Page>& pages);
} // namespace ImagePdf
