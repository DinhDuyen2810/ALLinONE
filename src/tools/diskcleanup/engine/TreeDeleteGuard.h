#pragma once

#include <QString>

/**
 * @brief Lớp chặn xóa nhầm cho tab "Cây thư mục": mọi đường dẫn người dùng chọn xóa PHẢI qua đây trước khi giao cho
 * Windows Shell (RecycleBinOps). Tab này cho xóa MỌI thư mục/tệp thấy trong cây - kể cả cả nhánh nhiều GB - nên khác tab
 * "Dọn dẹp" (chỉ xóa các hạng mục đã biết là an toàn) ở chỗ phải tự chặn những thứ KHÔNG BAO GIỜ được xóa từ đây.
 *
 * Chặn hẳn (blockReason không rỗng): gốc ổ đĩa, chính thư mục gốc đang xem (và cha của nó), thư mục Windows và mọi thứ
 * bên trong, thư mục hồ sơ người dùng/Program Files/ProgramData... (chính chúng, không phải nội dung bên trong), các mục
 * đặc biệt ở gốc ổ (System Volume Information, $Recycle.Bin, pagefile.sys, hiberfil.sys, Recovery, Boot, EFI...), thư mục
 * chứa chính ứng dụng này, và đường dẫn mà Windows có thể hiểu thành đối tượng khác (FsSafety::unsafeShellPathReason).
 * Cảnh báo (warning không rỗng, vẫn cho xóa nếu người dùng xác nhận): nằm trong Program Files/ProgramData - xóa tay có thể
 * làm hỏng chương trình đã cài.
 */
namespace TreeDeleteGuard
{
struct Verdict
{
    QString blockReason; // không rỗng = KHÔNG được xóa
    QString warning;     // không rỗng = hỏi kỹ hơn khi xác nhận
};

/// 'path' đã qua QDir::cleanPath (dấu '/'); 'scanRoot' là thư mục gốc của cây đang xem; 'isDir' = mục là thư mục.
Verdict check(const QString& path, const QString& scanRoot, bool isDir);
} // namespace TreeDeleteGuard
