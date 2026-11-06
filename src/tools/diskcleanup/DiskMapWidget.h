#pragma once

#include <QList>
#include <QRect>
#include <QWidget>

#include "engine/DiskLayout.h"
#include "engine/PartitionManager.h"

/// Sơ đồ đĩa kiểu "Disk Management" của Windows: mỗi đĩa là một hàng gồm nhãn đĩa + dải khối phân vùng/vùng chưa
/// phân bổ rộng theo tỉ lệ dung lượng (xem DiskLayout.h). Chỉ VẼ và thu thập thao tác chuột/phím - KHÔNG bao giờ
/// đụng tới đĩa; mọi thay đổi thật vẫn đi qua PartitionTab (tra khoảng cho phép -> gõ xác nhận -> PartitionResizer).
///
/// Chế độ đổi kích thước (setResizeContext): phân vùng được chọn hiện TAY NẮM ở mép phải. Windows chỉ đổi kích
/// thước bằng mép phải, nên kéo tay nắm sang TRÁI = thu nhỏ (phần giải phóng nằm bên phải phân vùng), sang PHẢI
/// = mở rộng (chỉ lấy được từ vùng chưa phân bổ liền kề bên phải). Hướng có thể bị KHÓA (DiskLayout::ResizeDirection)
/// để không kéo nhầm. Kích thước xem trước (previewSize) luôn nằm trong khoảng Windows cho phép đã áp hướng khóa.
class DiskMapWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DiskMapWidget(QWidget* parent = nullptr);

    void setData(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions);

    /// Chọn (tô sáng) một phân vùng; (-1,-1) = bỏ chọn. KHÔNG phát tín hiệu.
    void setSelection(int diskNumber, int partitionNumber);

    /// Bật chế độ đổi kích thước cho phân vùng (diskNumber, partitionNumber) với khoảng Windows cho phép + hướng
    /// đã khóa. Không tìm thấy phân vùng/khoảng không hợp lệ -> tắt chế độ. Kích thước xem trước = hiện tại.
    void setResizeContext(int diskNumber, int partitionNumber, const PartitionManager::SupportedSizeRange& range,
                          DiskLayout::ResizeDirection direction);
    void clearResizeContext();
    void setDirection(DiskLayout::ResizeDirection direction);
    /// Đặt kích thước xem trước (kẹp vào khoảng cho phép). KHÔNG phát tín hiệu - dành cho ô nhập số đồng bộ sang.
    void setPreviewSize(qint64 bytes);

    bool isResizeActive() const { return m_resizeActive; }
    qint64 previewSize() const { return m_previewBytes; }
    qint64 currentSize() const { return m_currentBytes; }
    DiskLayout::SizeBounds bounds() const { return m_bounds; }
    /// Vùng chưa phân bổ liền kề sau/trước phân vùng đang ở chế độ đổi kích thước (0 nếu không có).
    qint64 freeBytesAfterResizeTarget() const;
    qint64 freeBytesBeforeResizeTarget() const;
    int rowCount() const { return m_rows.size(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // ---- Chỉ dành cho bộ test UI ----
    QRect handleRectForTest() const;
    QRect blockRectForTest(int diskNumber, int partitionNumber) const;
    QRect unallocatedRectForTest(int diskNumber, int ordinal) const; // ordinal-th (từ 0) vùng chưa phân bổ của đĩa

signals:
    /// Người dùng bấm vào một khối PHÂN VÙNG (không phát cho vùng chưa phân bổ).
    void partitionClicked(int diskNumber, int partitionNumber);
    /// Người dùng kéo tay nắm/bấm phím làm đổi kích thước xem trước. KHÔNG phát khi code gọi setPreviewSize().
    void previewSizeChanged(qint64 bytes);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct Block
    {
        QRect rect;
        DiskLayout::Segment seg;
    };
    struct RowGeom
    {
        int rowIndex{0};
        QRect labelRect;
        QRect barRect;
        QList<Block> blocks;
        double bytesPerPx{0.0};
    };

    QList<RowGeom> computeGeometry() const;
    int partitionIndexOf(int diskNumber, int partitionNumber) const;
    QRect handleRect(const QList<RowGeom>& geom) const;
    /// Kẹp 'bytes' vào khoảng cho phép (đã áp hướng khóa) rồi đặt làm kích thước xem trước. 'snapToMiB': làm tròn
    /// xuống 1 MiB (kéo chuột). Chỉ phát previewSizeChanged khi 'emitSignal' và giá trị thật sự đổi.
    void applyPreview(qint64 bytes, bool emitSignal, bool snapToMiB);
    QString blockToolTip(const Block& block, int diskNumber) const;
    void paintBlock(QPainter& p, const Block& block, int diskNumber) const;
    void paintRowLabel(QPainter& p, const RowGeom& row) const;
    void paintLegend(QPainter& p, int y) const;
    int legendTop() const;

    QList<PartitionManager::PartitionInfo> m_partitions;
    QList<DiskLayout::DiskRow> m_rows;

    int m_selDisk{-1};
    int m_selPartition{-1};

    bool m_resizeActive{false};
    int m_resizeDisk{-1};
    int m_resizePartitionIndex{-1}; // chỉ số trong m_partitions
    PartitionManager::SupportedSizeRange m_range;
    DiskLayout::ResizeDirection m_direction{DiskLayout::ResizeDirection::Free};
    DiskLayout::SizeBounds m_bounds;
    qint64 m_currentBytes{0};
    qint64 m_previewBytes{0};

    bool m_dragging{false};
    int m_dragStartX{0};
    qint64 m_dragStartBytes{0};
    double m_dragBytesPerPx{0.0};
};
