#pragma once

#include <QList>
#include <QString>

#include "PartitionManager.h"

/**
 * @brief Lõi THUẦN (không PowerShell, không Qt Widgets, không đụng đĩa) cho sơ đồ đĩa kiểu Disk Management:
 * dựng danh sách đoạn "phân vùng / vùng chưa phân bổ" theo vị trí trên đĩa, dự báo bố cục sau khi đổi kích
 * thước, tính độ rộng khối để vẽ và luật HƯỚNG thay đổi (thu nhỏ = kéo sang trái, mở rộng = kéo sang phải).
 *
 * Nguyên tắc quan trọng: Resize-Partition của Windows chỉ dịch chuyển MÉP PHẢI (điểm kết thúc) của phân
 * vùng - mép TRÁI (vị trí bắt đầu) đứng yên. Vì vậy:
 *  - thu nhỏ: phần giải phóng luôn nằm BÊN PHẢI phân vùng (nối vào vùng chưa phân bổ ngay sau nó);
 *  - mở rộng: chỉ lấy được từ vùng chưa phân bổ LIỀN KỀ BÊN PHẢI.
 * Mọi con số "tối thiểu/tối đa an toàn" vẫn do Windows quyết định (Get-PartitionSupportedSize) - lớp này chỉ
 * dùng để VẼ và để người dùng KHÓA hướng thay đổi, không thay thế phép tính an toàn đó.
 */
namespace DiskLayout
{
/// Khe hở nhỏ hơn mức này giữa hai phân vùng (căn lề 1 MiB, vùng đệm bảng GPT...) không phải "vùng chưa phân bổ".
constexpr qint64 kMinGapBytes = 4LL * 1024 * 1024;

enum class SegmentKind
{
    Partition,
    Unallocated
};

struct Segment
{
    SegmentKind kind{SegmentKind::Unallocated};
    qint64 offsetBytes{0};
    qint64 sizeBytes{0};
    int partitionIndex{-1}; // chỉ số trong danh sách phân vùng đưa vào build(); -1 với vùng chưa phân bổ
};

struct DiskRow
{
    PartitionManager::DiskInfo disk;
    QList<Segment> segments;
    /// true khi thiếu Offset của ít nhất một phân vùng: các phân vùng được xếp tuần tự theo số thứ tự, KHÔNG
    /// dựng vùng chưa phân bổ giữa chúng (không biết vị trí thật).
    bool approximate{false};
};

/// Dựng sơ đồ cho mọi đĩa (kể cả đĩa không có phân vùng nào = một vùng chưa phân bổ duy nhất). Nếu listDisks()
/// lỗi (disks rỗng) vẫn dựng được từ riêng các phân vùng (dung lượng đĩa = điểm kết thúc phân vùng cuối).
QList<DiskRow> build(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions);

/// Số byte vùng chưa phân bổ nằm LIỀN KỀ ngay sau / ngay trước phân vùng; 0 nếu liền sát phân vùng khác hay mép đĩa.
qint64 freeBytesAfter(const QList<Segment>& segments, int partitionIndex);
qint64 freeBytesBefore(const QList<Segment>& segments, int partitionIndex);

/// Bố cục SAU khi đổi phân vùng 'partitionIndex' thành 'newSizeBytes' - chỉ mép phải dịch chuyển. Thu nhỏ: phần
/// giải phóng nối vào (hoặc tạo) vùng chưa phân bổ ngay sau. Mở rộng: lấy từ vùng chưa phân bổ liền kề phía
/// sau, TỐI ĐA bằng vùng đó (không bao giờ nuốt phân vùng khác).
QList<Segment> projectResize(const QList<Segment>& segments, int partitionIndex, qint64 newSizeBytes);

/// Độ rộng (pixel) từng khối theo tỉ lệ dung lượng, khối nào quá nhỏ được nâng lên tối thiểu 'minPx' (các khối
/// còn lại co lại bù) - giống Disk Management để phân vùng 100 MB vẫn nhìn thấy được. Tổng đúng bằng 'totalPx'.
QList<int> computeBlockWidths(const QList<qint64>& sizes, int totalPx, int minPx);

/// Số byte ứng với 1 pixel ở các khối KHÔNG bị nâng độ rộng tối thiểu (dùng đổi quãng kéo chuột ra byte).
double bytesPerPixel(const QList<qint64>& sizes, const QList<int>& widths, int minPx);

// ---- Hướng thay đổi ----
enum class ResizeDirection
{
    Free,       // kéo cả hai hướng (mặc định)
    ShrinkOnly, // chỉ thu nhỏ: kéo mép phải sang TRÁI
    GrowOnly    // chỉ mở rộng: kéo mép phải sang PHẢI
};

struct SizeBounds
{
    qint64 minBytes{0};
    qint64 maxBytes{0};
};

/// Khoảng kích thước được phép kéo tới sau khi áp HƯỚNG đã khóa lên khoảng Windows cho phép. {0,0} nếu khoảng
/// Windows không hợp lệ.
SizeBounds boundsForDirection(qint64 currentBytes, const PartitionManager::SupportedSizeRange& range, ResizeDirection direction);

/// Rỗng nếu kích thước mới (GB, đúng độ chính xác 2 chữ số của ô nhập) không vi phạm hướng đã khóa; ngược lại là
/// lý do. Kích thước trùng kích thước hiện tại ở độ chính xác của ô nhập luôn hợp lệ (việc "không có gì để đổi"
/// do lớp kiểm tra khác báo).
QString directionBlockReason(double newSizeGb, qint64 currentBytes, ResizeDirection direction);

/// Mô tả dễ hiểu thay đổi sắp thực hiện: thu nhỏ/mở rộng bao nhiêu, vùng giải phóng/lấy thêm nằm ở phía nào, cảnh
/// báo khi không có vùng trống liền kề bên phải. 'name' vd "D:".
QString describeResize(const QString& name, qint64 currentBytes, qint64 newBytes, qint64 freeAfterBytes, qint64 freeBeforeBytes);

QString directionName(ResizeDirection direction);

/// "120.66 GB", "522.0 MB"... (cùng cách ghi với DiskUi::formatBytes, đặt ở đây để lõi không phụ thuộc giao diện).
QString formatSize(qint64 bytes);
} // namespace DiskLayout
