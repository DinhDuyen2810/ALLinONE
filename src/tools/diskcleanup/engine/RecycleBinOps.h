#pragma once

#include <QString>
#include <QStringList>

/**
 * @brief Lớp duy nhất giao tiếp với Windows Shell API cho Thùng rác (Recycle Bin) - cô lập Win32
 * theo đúng nguyên tắc của dự án (SHFileOperationW, SHQueryRecycleBinW, SHEmptyRecycleBinW).
 */
namespace RecycleBinOps
{
struct Status
{
    qint64 itemCount{0};
    qint64 totalBytes{0};
};

/// Chuyển các đường dẫn (file hoặc thư mục) vào Thùng rác - có thể khôi phục lại được. Đây là cách
/// xóa MẶC ĐỊNH và được khuyến nghị của ứng dụng.
bool moveToRecycleBin(const QStringList& paths, QString* error = nullptr);

/// Xóa vĩnh viễn, KHÔNG qua Thùng rác - không thể khôi phục. Chỉ dùng khi người dùng chủ động chọn.
bool permanentlyDelete(const QStringList& paths, QString* error = nullptr);

/// Tổng dung lượng + số mục hiện có trong Thùng rác, gộp tất cả các ổ đĩa.
Status queryStatus();

/// Làm trống toàn bộ Thùng rác (mọi ổ đĩa).
bool empty(QString* error = nullptr);
}
