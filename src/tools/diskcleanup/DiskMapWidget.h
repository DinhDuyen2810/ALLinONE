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
///
/// Chế độ DỜI (setMoveEnabled(true), từ v1.25.0): ngoài tay nắm mép phải còn có TAY NẮM MÉP TRÁI (kéo sang trái = mở rộng về bên trái,
/// sang phải = thu nhỏ từ bên trái) và KÉO CẢ KHỐI để dời phân vùng (giữ kích thước) sang chỗ trống liền kề hoặc sang một vùng chưa phân bổ
/// KHÁC (đổi thứ tự). Windows không làm được việc này - ứng dụng tự chép dữ liệu (xem PartitionMover); widget chỉ vẽ xem trước + báo hình học mới
/// (previewGeometryChanged), mọi thao tác thật vẫn đi qua PartitionTab (lập kế hoạch -> gõ xác nhận -> chạy).
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

    /// Bật/tắt chế độ dời (tay nắm mép trái + kéo khối). Tắt thì 'disabledReason' được hiện trong gợi ý khi rê chuột lên khối. Mặc định TẮT.
    void setMoveEnabled(bool enabled, const QString& disabledReason = QString());
    bool isMoveEnabled() const { return m_moveEnabled; }
    /// Vị trí bắt đầu hiện tại / xem trước của phân vùng đang ở chế độ đổi kích thước (byte từ đầu đĩa).
    qint64 currentStart() const { return m_curStart; }
    qint64 previewStart() const { return m_previewStart; }
    /// Đặt hình học xem trước (vị trí + kích thước). KHÔNG phát tín hiệu - cho ô nhập đồng bộ sang. Hình học không hợp lệ bị bỏ qua.
    void setPreviewGeometry(qint64 startBytes, qint64 sizeBytes);
    /// Tổng dung lượng đĩa chứa phân vùng đang đổi (0 nếu không rõ).
    qint64 diskSizeOfTarget() const;
    /// Khe cho phép của phân vùng đang đổi: chỗ cũ ∪ vùng chưa phân bổ liền kề hai bên ({0,0} nếu chưa ở chế độ đổi kích thước).
    DiskLayout::Span targetSpan() const;
    DiskLayout::SizeBounds bounds() const { return m_bounds; }
    /// Vùng chưa phân bổ liền kề sau/trước phân vùng đang ở chế độ đổi kích thước (0 nếu không có).
    qint64 freeBytesAfterResizeTarget() const;
    qint64 freeBytesBeforeResizeTarget() const;
    int rowCount() const { return m_rows.size(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // ---- Chỉ dành cho bộ test UI ----
    QRect handleRectForTest() const;
    QRect leftHandleRectForTest() const;
    QRect blockRectForTest(int diskNumber, int partitionNumber) const;
    QRect unallocatedRectForTest(int diskNumber, int ordinal) const; // ordinal-th (từ 0) vùng chưa phân bổ của đĩa

signals:
    /// Người dùng bấm vào một khối PHÂN VÙNG (không phát cho vùng chưa phân bổ).
    void partitionClicked(int diskNumber, int partitionNumber);
    /// Người dùng kéo tay nắm/bấm phím làm đổi kích thước xem trước. KHÔNG phát khi code gọi setPreviewSize().
    void previewSizeChanged(qint64 bytes);
    /// Người dùng kéo tay nắm mép trái/khối (hoặc mép phải khi đang ở vị trí đã dời). KHÔNG phát khi code gọi setPreviewGeometry().
    void previewGeometryChanged(qint64 startBytes, qint64 sizeBytes);

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

    struct MapPiece
    {
        int x0{0}, x1{0};
        qint64 b0{0}, b1{0};
    };
    enum class DragKind
    {
        None,
        RightEdge,
        LeftEdge,
        Body
    };

    QList<RowGeom> computeGeometry() const;
    int partitionIndexOf(int diskNumber, int partitionNumber) const;
    QRect handleRect(const QList<RowGeom>& geom) const;
    QRect leftHandleRect(const QList<RowGeom>& geom) const;
    const RowGeom* targetRow(const QList<RowGeom>& geom) const;
    QList<MapPiece> buildMap(const RowGeom& row) const;
    static qint64 bytesAtX(const QList<MapPiece>& map, int x);
    int xAtByte(const RowGeom& row, qint64 byte) const;
    const QList<DiskLayout::Segment>* targetSegments() const;
    /// Đưa hình học đề xuất qua kẹp + kiểm tra kế hoạch (DiskLayout::planChange) rồi áp. Hình học không hợp lệ bị bỏ (giữ cái cũ).
    void applyGeometry(qint64 start, qint64 size, bool emitSignal);
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

    bool m_moveEnabled{false};
    QString m_moveDisabledReason;
    qint64 m_curStart{0};
    qint64 m_previewStart{0};

    bool m_dragging{false};
    DragKind m_dragKind{DragKind::None};
    int m_dragStartX{0};
    qint64 m_dragStartBytes{0};
    double m_dragBytesPerPx{0.0};
    QList<MapPiece> m_dragMap;   // bản đồ x -> byte ĐÓNG BĂNG lúc bấm (mép trái/khối)
    qint64 m_dragGrabBytes{0};   // (byte) từ điểm bấm tới mép trái (kéo khối) hoặc chênh của tay nắm
    qint64 m_dragFixedEnd{0};    // kéo mép trái: mép phải giữ nguyên
};
