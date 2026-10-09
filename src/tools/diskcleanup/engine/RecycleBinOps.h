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
///
/// CHÚ Ý: Shell của Windows TỰ chuyển sang XÓA VĨNH VIỄN khi một mục không vào Thùng rác được (lớn hơn
/// dung lượng tối đa của Thùng rác trên ổ đó, Thùng rác bị tắt, ổ mạng/ổ tháo rời không có Thùng rác)
/// - và với FOF_NOCONFIRMATION thì làm việc đó KHÔNG HỎI. Nơi gọi PHẢI lọc trước bằng
/// notRecyclableReason(); hàm này còn đặt thêm FOF_WANTNUKEWARNING làm lớp chặn thứ hai (Windows hỏi
/// lại trước khi hủy hẳn thay vì âm thầm làm).
bool moveToRecycleBin(const QStringList& paths, QString* error = nullptr);

/// Lý do mục này (kích thước sizeBytes) KHÔNG thể đưa vào Thùng rác một cách khôi phục được; rỗng nếu
/// được hoặc không xác định được (khi đó FOF_WANTNUKEWARNING là lớp chặn còn lại). Đọc đúng cấu hình
/// Thùng rác mà Shell dùng: HKCU\...\Explorer\BitBucket\Volume\{GUID ổ} (MaxCapacity tính bằng MB,
/// NukeOnDelete) + loại ổ đĩa.
QString notRecyclableReason(const QString& path, qint64 sizeBytes);

/// Xóa vĩnh viễn, KHÔNG qua Thùng rác - không thể khôi phục. Chỉ dùng khi người dùng chủ động chọn.
bool permanentlyDelete(const QStringList& paths, QString* error = nullptr);

/// Tổng dung lượng + số mục hiện có trong Thùng rác, gộp tất cả các ổ đĩa.
Status queryStatus();

/// Làm trống toàn bộ Thùng rác (mọi ổ đĩa).
bool empty(QString* error = nullptr);

// ---- Lõi thuần của notRecyclableReason() - tách riêng để kiểm thử bằng dữ liệu dựng sẵn ----
namespace internal
{
enum class DriveKind { Fixed, Removable, Network, Other };
/// maxCapacityMb < 0 = không đọc được cấu hình (không kết luận gì về dung lượng).
QString recycleVerdict(DriveKind kind, bool nukeOnDelete, qint64 maxCapacityMb, qint64 sizeBytes);
} // namespace internal
}
