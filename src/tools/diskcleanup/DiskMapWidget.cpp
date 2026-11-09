#include "DiskMapWidget.h"

#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QStringList>
#include <QToolTip>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace
{
constexpr int kMargin = 6;
constexpr int kLabelW = 108;
constexpr int kBarH = 80;     // chiều cao dải khối
constexpr int kTagH = 20;     // vùng ngay dưới dải: hiện kích thước xem trước
constexpr int kRowH = kBarH + kTagH;
constexpr int kRowGap = 8;
constexpr int kHeaderH = 12;  // dải màu trên đầu mỗi khối (như Disk Management)
constexpr int kMinBlockPx = 36;
constexpr int kBlockGap = 2;
constexpr int kLegendH = 26;
constexpr int kHandleW = 9;
constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024LL * 1024 * 1024;

const QColor kNavy(0x1b, 0x3a, 0x9c);
const QColor kRecoveryBlue(0x4b, 0x6c, 0xb7);
const QColor kReservedGray(0x6b, 0x72, 0x80);
const QColor kShrinkOrange(0xd9, 0x6a, 0x0b);
const QColor kGrowGreen(0x1a, 0x7f, 0x37);
const QColor kMoveBlue(0x09, 0x69, 0xda);
const QColor kInk(0x1f, 0x23, 0x28);
const QColor kMuted(0x57, 0x60, 0x6a);

bool isEfi(const PartitionManager::PartitionInfo& p) { return p.type.compare("System", Qt::CaseInsensitive) == 0; }
bool isReserved(const PartitionManager::PartitionInfo& p) { return p.type.compare("Reserved", Qt::CaseInsensitive) == 0; }
bool isRecovery(const PartitionManager::PartitionInfo& p) { return p.type.compare("Recovery", Qt::CaseInsensitive) == 0; }

QString partitionTitle(const PartitionManager::PartitionInfo& p)
{
    if (isEfi(p)) return "EFI (hệ thống)";
    if (isReserved(p)) return "MSR (dành riêng)";
    QString title = p.label.isEmpty() ? (isRecovery(p) ? QString("Phục hồi") : QString("Phân vùng %1").arg(p.partitionNumber)) : p.label;
    if (!p.driveLetter.isEmpty())
        title += " (" + p.driveLetter + ":)";
    return title;
}

QString partitionFlags(const PartitionManager::PartitionInfo& p)
{
    QStringList flags;
    if (p.isBoot) flags << "Khởi động";
    if (p.isSystem) flags << "Hệ thống";
    if (p.isActive) flags << "Hoạt động";
    return flags.join(", ");
}

QColor headerColor(const PartitionManager::PartitionInfo& p)
{
    if (isRecovery(p)) return kRecoveryBlue;
    if (isReserved(p)) return kReservedGray;
    return kNavy;
}

QString diskStyleText(const PartitionManager::DiskInfo& d)
{
    if (d.partitionStyle.compare("RAW", Qt::CaseInsensitive) == 0) return "Chưa khởi tạo";
    if (d.partitionStyle.isEmpty() || d.partitionStyle.compare("Unknown", Qt::CaseInsensitive) == 0) return "Cơ bản";
    return "Cơ bản · " + d.partitionStyle.toUpper();
}
} // namespace

DiskMapWidget::DiskMapWidget(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void DiskMapWidget::setData(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions)
{
    m_partitions = partitions;
    m_rows = DiskLayout::build(disks, partitions);
    m_dragging = false;
    m_dragKind = DragKind::None;
    // Dữ liệu mới: mọi trạng thái đổi kích thước/chọn cũ không còn đáng tin (chỉ số phân vùng đã đổi).
    m_resizeActive = false;
    m_selDisk = m_selPartition = -1;
    updateGeometry();
    update();
}

int DiskMapWidget::partitionIndexOf(int diskNumber, int partitionNumber) const
{
    for (int i = 0; i < m_partitions.size(); ++i)
        if (m_partitions[i].diskNumber == diskNumber && m_partitions[i].partitionNumber == partitionNumber)
            return i;
    return -1;
}

void DiskMapWidget::setSelection(int diskNumber, int partitionNumber)
{
    if (m_selDisk == diskNumber && m_selPartition == partitionNumber)
        return;
    m_selDisk = diskNumber;
    m_selPartition = partitionNumber;
    update();
}

void DiskMapWidget::setResizeContext(int diskNumber, int partitionNumber, const PartitionManager::SupportedSizeRange& range,
                                     DiskLayout::ResizeDirection direction)
{
    m_resizeActive = false;
    m_dragging = false;
    m_dragKind = DragKind::None;
    const int idx = partitionIndexOf(diskNumber, partitionNumber);
    if (idx < 0 || !range.ok)
    {
        update();
        return;
    }
    m_resizeDisk = diskNumber;
    m_resizePartitionIndex = idx;
    m_range = range;
    m_direction = direction;
    m_currentBytes = m_partitions[idx].sizeBytes;
    m_curStart = m_partitions[idx].offsetBytes;
    m_previewStart = m_curStart;
    m_bounds = DiskLayout::boundsForDirection(m_currentBytes, m_range, m_direction);
    m_previewBytes = m_currentBytes;
    m_resizeActive = true;
    setSelection(diskNumber, partitionNumber);
    update();
}

void DiskMapWidget::clearResizeContext()
{
    m_resizeActive = false;
    m_dragging = false;
    m_dragKind = DragKind::None;
    unsetCursor();
    update();
}

void DiskMapWidget::setDirection(DiskLayout::ResizeDirection direction)
{
    m_direction = direction;
    if (!m_resizeActive)
        return;
    m_bounds = DiskLayout::boundsForDirection(m_currentBytes, m_range, m_direction);
    applyPreview(m_previewBytes, false, false);
    update();
}

void DiskMapWidget::setPreviewSize(qint64 bytes)
{
    if (m_resizeActive)
        applyPreview(bytes, false, false);
}

void DiskMapWidget::applyPreview(qint64 bytes, bool emitSignal, bool snapToMiB)
{
    if (!m_resizeActive)
        return;
    if (snapToMiB)
        bytes = (bytes / kMiB) * kMiB; // kéo chuột: làm tròn xuống 1 MiB cho số đẹp; hai đầu khoảng vẫn chính xác nhờ kẹp ở dưới
    qint64 maxBytes = m_bounds.maxBytes;
    if (m_previewStart != m_curStart)
    {
        // Đã dời mép trái: giới hạn bên phải là hết vùng trống liền kề CỦA CHỖ CŨ (không còn là mức Windows báo cho vị trí cũ).
        if (const auto* segs = targetSegments())
        {
            const DiskLayout::Span sp = DiskLayout::adjacentSpan(*segs, m_resizePartitionIndex);
            maxBytes = std::max(m_bounds.minBytes, sp.hi - m_previewStart);
            if (m_direction == DiskLayout::ResizeDirection::ShrinkOnly)
                maxBytes = std::min(maxBytes, m_currentBytes);
        }
    }
    bytes = std::clamp(bytes, m_bounds.minBytes, maxBytes);
    if (bytes == m_previewBytes)
        return;
    m_previewBytes = bytes;
    update();
    if (emitSignal)
    {
        emit previewSizeChanged(bytes);
        emit previewGeometryChanged(m_previewStart, bytes);
    }
}

qint64 DiskMapWidget::freeBytesAfterResizeTarget() const
{
    if (!m_resizeActive)
        return 0;
    for (const auto& row : m_rows)
        if (row.disk.number == m_resizeDisk)
            return DiskLayout::freeBytesAfter(row.segments, m_resizePartitionIndex);
    return 0;
}

qint64 DiskMapWidget::freeBytesBeforeResizeTarget() const
{
    if (!m_resizeActive)
        return 0;
    for (const auto& row : m_rows)
        if (row.disk.number == m_resizeDisk)
            return DiskLayout::freeBytesBefore(row.segments, m_resizePartitionIndex);
    return 0;
}

void DiskMapWidget::setMoveEnabled(bool enabled, const QString& disabledReason)
{
    m_moveEnabled = enabled;
    m_moveDisabledReason = enabled ? QString() : disabledReason;
    if (!enabled && m_dragging && m_dragKind != DragKind::RightEdge)
    {
        m_dragging = false;
        m_dragKind = DragKind::None;
    }
    update();
}

const QList<DiskLayout::Segment>* DiskMapWidget::targetSegments() const
{
    if (!m_resizeActive)
        return nullptr;
    for (const auto& row : m_rows)
        if (row.disk.number == m_resizeDisk)
            return &row.segments;
    return nullptr;
}

qint64 DiskMapWidget::diskSizeOfTarget() const
{
    for (const auto& row : m_rows)
        if (row.disk.number == m_resizeDisk)
            return row.disk.sizeBytes;
    return 0;
}

DiskLayout::Span DiskMapWidget::targetSpan() const
{
    const auto* segs = targetSegments();
    return segs ? DiskLayout::adjacentSpan(*segs, m_resizePartitionIndex) : DiskLayout::Span{};
}

void DiskMapWidget::setPreviewGeometry(qint64 startBytes, qint64 sizeBytes)
{
    if (!m_resizeActive || m_curStart < 0)
        return;
    if (startBytes == m_curStart)
    {
        m_previewStart = m_curStart;
        applyPreview(sizeBytes, false, false);
        update();
        return;
    }
    const auto* segs = targetSegments();
    if (!segs)
        return;
    const auto plan = DiskLayout::planChange(*segs, m_resizePartitionIndex, startBytes, sizeBytes, sizeBytes < m_currentBytes ? m_range.minBytes : 0, diskSizeOfTarget());
    if (!plan.blockReason.isEmpty())
        return;
    m_previewStart = startBytes;
    m_previewBytes = sizeBytes;
    update();
}

void DiskMapWidget::applyGeometry(qint64 start, qint64 size, bool emitSignal)
{
    if (!m_resizeActive || m_curStart < 0)
        return;
    const auto* segs = targetSegments();
    if (!segs)
        return;
    if (start != m_curStart)
        start = ((start + kMiB / 2) / kMiB) * kMiB; // vị trí MỚI luôn căn 1 MiB (vị trí cũ giữ nguyên dù chưa căn)
    if (start == m_previewStart && size == m_previewBytes)
        return;
    if (!(start == m_curStart && size == m_currentBytes))
    {
        const auto plan = DiskLayout::planChange(*segs, m_resizePartitionIndex, start, size, size < m_currentBytes ? m_range.minBytes : 0, diskSizeOfTarget());
        if (!plan.blockReason.isEmpty())
            return; // hình học không hợp lệ: giữ nguyên xem trước hiện tại
    }
    m_previewStart = start;
    m_previewBytes = size;
    update();
    if (emitSignal)
    {
        emit previewGeometryChanged(start, size);
        emit previewSizeChanged(size);
    }
}

const DiskMapWidget::RowGeom* DiskMapWidget::targetRow(const QList<RowGeom>& geom) const
{
    for (const auto& row : geom)
        if (m_rows[row.rowIndex].disk.number == m_resizeDisk)
            return &row;
    return nullptr;
}

QList<DiskMapWidget::MapPiece> DiskMapWidget::buildMap(const RowGeom& row) const
{
    QList<MapPiece> map;
    for (const auto& b : row.blocks)
        map << MapPiece{b.rect.left(), b.rect.right() + 1, b.seg.offsetBytes, b.seg.offsetBytes + b.seg.sizeBytes};
    return map;
}

qint64 DiskMapWidget::bytesAtX(const QList<MapPiece>& map, int x)
{
    if (map.isEmpty())
        return 0;
    if (x <= map.first().x0)
        return map.first().b0;
    for (int i = 0; i < map.size(); ++i)
    {
        const MapPiece& m = map[i];
        if (x < m.x1)
        {
            if (x < m.x0)
                return m.b0; // khe giữa hai khối
            const double f = m.x1 > m.x0 ? double(x - m.x0) / double(m.x1 - m.x0) : 0.0;
            return m.b0 + static_cast<qint64>(std::llround(f * double(m.b1 - m.b0)));
        }
    }
    return map.last().b1;
}

int DiskMapWidget::xAtByte(const RowGeom& row, qint64 byte) const
{
    int best = row.barRect.left();
    for (const auto& b : row.blocks)
    {
        const qint64 b0 = b.seg.offsetBytes, b1 = b0 + b.seg.sizeBytes;
        if (byte >= b0 && byte <= b1 && b1 > b0)
            return b.rect.left() + static_cast<int>(std::llround(double(byte - b0) / double(b1 - b0) * double(b.rect.width())));
        if (byte > b1)
            best = b.rect.right();
    }
    return best;
}

QSize DiskMapWidget::sizeHint() const
{
    return QSize(760, legendTop() + kLegendH + kMargin);
}

QSize DiskMapWidget::minimumSizeHint() const
{
    return QSize(420, legendTop() + kLegendH + kMargin);
}

int DiskMapWidget::legendTop() const
{
    return kMargin + qMax(1, int(m_rows.size())) * (kRowH + kRowGap);
}

QList<DiskMapWidget::RowGeom> DiskMapWidget::computeGeometry() const
{
    QList<RowGeom> out;
    const int barX = kMargin + kLabelW + 8;
    const int barW = qMax(60, width() - barX - kMargin - kHandleW / 2);
    for (int r = 0; r < m_rows.size(); ++r)
    {
        RowGeom g;
        g.rowIndex = r;
        const int y = kMargin + r * (kRowH + kRowGap);
        g.labelRect = QRect(kMargin, y, kLabelW, kBarH);
        g.barRect = QRect(barX, y, barW, kBarH);

        QList<DiskLayout::Segment> segs = m_rows[r].segments;
        if (m_resizeActive && m_rows[r].disk.number == m_resizeDisk && (m_previewBytes != m_currentBytes || m_previewStart != m_curStart))
            segs = (m_previewStart == m_curStart) ? DiskLayout::projectResize(segs, m_resizePartitionIndex, m_previewBytes)
                                                  : DiskLayout::projectGeometry(segs, m_resizePartitionIndex, m_previewStart, m_previewBytes);

        QList<qint64> sizes;
        for (const auto& s : segs)
            sizes << s.sizeBytes;
        const int avail = qMax(1, barW - qMax(0, int(segs.size()) - 1) * kBlockGap);
        const QList<int> widths = DiskLayout::computeBlockWidths(sizes, avail, kMinBlockPx);
        g.bytesPerPx = DiskLayout::bytesPerPixel(sizes, widths, kMinBlockPx);

        int x = barX;
        for (int i = 0; i < segs.size(); ++i)
        {
            g.blocks << Block{QRect(x, y, widths[i], kBarH), segs[i]};
            x += widths[i] + kBlockGap;
        }
        out << g;
    }
    return out;
}

QRect DiskMapWidget::handleRect(const QList<RowGeom>& geom) const
{
    if (!m_resizeActive)
        return {};
    for (const auto& row : geom)
    {
        if (m_rows[row.rowIndex].disk.number != m_resizeDisk)
            continue;
        for (const auto& b : row.blocks)
            if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == m_resizePartitionIndex)
                return QRect(b.rect.right() - kHandleW / 2, b.rect.top() + 3, kHandleW, b.rect.height() - 6);
    }
    return {};
}

QRect DiskMapWidget::leftHandleRect(const QList<RowGeom>& geom) const
{
    if (!m_resizeActive || !m_moveEnabled || m_curStart < 0)
        return {};
    for (const auto& row : geom)
    {
        if (m_rows[row.rowIndex].disk.number != m_resizeDisk)
            continue;
        for (const auto& b : row.blocks)
            if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == m_resizePartitionIndex)
                return QRect(b.rect.left() - kHandleW / 2, b.rect.top() + 3, kHandleW, b.rect.height() - 6);
    }
    return {};
}

QRect DiskMapWidget::handleRectForTest() const
{
    return handleRect(computeGeometry());
}

QRect DiskMapWidget::leftHandleRectForTest() const
{
    return leftHandleRect(computeGeometry());
}

QRect DiskMapWidget::blockRectForTest(int diskNumber, int partitionNumber) const
{
    const int idx = partitionIndexOf(diskNumber, partitionNumber);
    for (const auto& row : computeGeometry())
        for (const auto& b : row.blocks)
            if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == idx && idx >= 0)
                return b.rect;
    return {};
}

QRect DiskMapWidget::unallocatedRectForTest(int diskNumber, int ordinal) const
{
    for (const auto& row : computeGeometry())
    {
        if (m_rows[row.rowIndex].disk.number != diskNumber)
            continue;
        int seen = 0;
        for (const auto& b : row.blocks)
            if (b.seg.kind == DiskLayout::SegmentKind::Unallocated && seen++ == ordinal)
                return b.rect;
    }
    return {};
}

// ------------------------------------------------------------------------------------------------ vẽ

void DiskMapWidget::paintRowLabel(QPainter& p, const RowGeom& row) const
{
    const auto& disk = m_rows[row.rowIndex].disk;
    p.setPen(QPen(QColor("#d0d7de"), 1));
    p.setBrush(QColor("#eef1f5"));
    p.drawRoundedRect(row.labelRect.adjusted(0, 0, -1, -1), 6, 6);

    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));

    QRect r = row.labelRect.adjusted(8, 6, -6, -4);
    p.setPen(kInk);
    p.setFont(bold);
    p.drawText(r, Qt::AlignLeft | Qt::AlignTop, QString("Đĩa %1").arg(disk.number));
    p.setFont(small);
    p.setPen(kMuted);
    const int lh = QFontMetrics(small).height() + 1;
    int y = r.top() + QFontMetrics(bold).height() + 2;
    const QStringList lines = {diskStyleText(disk), disk.sizeBytes > 0 ? DiskLayout::formatSize(disk.sizeBytes) : QString("—"),
                               disk.isOffline ? QString("Ngoại tuyến") : (disk.isReadOnly ? QString("Chỉ đọc") : QString("Trực tuyến"))};
    for (const QString& line : lines)
    {
        p.drawText(QRect(r.left(), y, r.width(), lh), Qt::AlignLeft | Qt::AlignTop,
                   QFontMetrics(small).elidedText(line, Qt::ElideRight, r.width()));
        y += lh;
    }
}

void DiskMapWidget::paintBlock(QPainter& p, const Block& b, int diskNumber) const
{
    Q_UNUSED(diskNumber);
    const bool free = b.seg.kind == DiskLayout::SegmentKind::Unallocated;
    const PartitionManager::PartitionInfo* part = (!free && b.seg.partitionIndex >= 0 && b.seg.partitionIndex < m_partitions.size())
                                                      ? &m_partitions[b.seg.partitionIndex] : nullptr;
    const bool selected = part && part->diskNumber == m_selDisk && part->partitionNumber == m_selPartition;
    const QRect r = b.rect;

    p.setPen(QPen(selected ? QColor("#0969da") : QColor("#6e7781"), selected ? 2 : 1));
    p.setBrush(selected ? QColor("#dbeafe") : QColor("#ffffff"));
    p.drawRoundedRect(r.adjusted(0, 0, -1, -1), 3, 3);

    if (part && (isEfi(*part) || isReserved(*part))) // thân gạch chéo như Disk Management
    {
        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(QColor(110, 120, 135, 120), Qt::BDiagPattern));
        p.drawRect(QRect(r.left() + 1, r.top() + kHeaderH, r.width() - 2, r.height() - kHeaderH - 1));
    }
    const QColor header = free ? QColor(0, 0, 0) : (part ? headerColor(*part) : kNavy);
    p.fillRect(QRect(r.left() + 1, r.top() + 1, r.width() - 2, kHeaderH - 1), header);

    QFont bold = font();
    bold.setBold(true);
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    const QRect textRect = r.adjusted(5, kHeaderH + 4, -4, -3);
    if (textRect.width() < 14)
        return;
    p.save();
    p.setClipRect(textRect);
    p.setPen(kInk);

    QStringList lines;
    if (free)
        lines << DiskLayout::formatSize(b.seg.sizeBytes) << QString("Chưa phân bổ");
    else if (part)
    {
        lines << partitionTitle(*part);
        QString second = DiskLayout::formatSize(part->sizeBytes);
        if (part->hasFileSystem())
            second += " " + part->fileSystem;
        lines << second;
        const QString flags = partitionFlags(*part);
        if (!flags.isEmpty())
            lines << flags;
    }
    int y = textRect.top();
    for (int i = 0; i < lines.size(); ++i)
    {
        const QFont& f = (i == 0) ? bold : small;
        p.setFont(f);
        p.setPen(i == 0 ? kInk : kMuted);
        const QFontMetrics fm(f);
        p.drawText(QRect(textRect.left(), y, textRect.width(), fm.height()), Qt::AlignLeft | Qt::AlignTop,
                   fm.elidedText(lines[i], Qt::ElideRight, textRect.width()));
        y += fm.height() + 1;
        if (y > textRect.bottom())
            break;
    }
    p.restore();
}

void DiskMapWidget::paintLegend(QPainter& p, int y) const
{
    struct Item { QColor color; Qt::BrushStyle pattern; QString text; };
    QList<Item> items = {
        {QColor(0, 0, 0), Qt::SolidPattern, "Chưa phân bổ"},
        {kNavy, Qt::SolidPattern, "Phân vùng chính"},
        {QColor(110, 120, 135), Qt::BDiagPattern, "Hệ thống/Dành riêng"},
    };
    if (m_resizeActive)
    {
        items << Item{kShrinkOrange, Qt::BDiagPattern, "Sẽ giải phóng (bên phải)"};
        items << Item{kGrowGreen, Qt::BDiagPattern, "Sẽ lấy thêm (từ vùng trống bên phải)"};
        if (m_moveEnabled)
            items << Item{kMoveBlue, Qt::BDiagPattern, "Sẽ dời (chỗ cũ nét đứt)"};
    }
    QFont small = font();
    small.setPointSizeF(qMax(7.5, font().pointSizeF() - 1.0));
    p.setFont(small);
    const QFontMetrics fm(small);
    int x = kMargin + 2;
    for (const auto& it : items)
    {
        const QRect sw(x, y + 7, 14, 10);
        p.setPen(QPen(QColor("#6e7781"), 1));
        p.setBrush(Qt::white);
        p.drawRect(sw);
        p.setPen(Qt::NoPen);
        if (it.pattern == Qt::SolidPattern)
        {
            p.setBrush(it.color);
            p.drawRect(sw.adjusted(1, 1, 0, 0));
        }
        else
        {
            // Nền nhạt cùng màu rồi gạch chéo đậm: nếu chỉ gạch thì vạch 1px bị khử răng cưa nhìn như màu xám chung.
            p.setBrush(QColor(it.color.red(), it.color.green(), it.color.blue(), 70));
            p.drawRect(sw.adjusted(1, 1, 0, 0));
            p.setBrush(QBrush(it.color, it.pattern));
            p.drawRect(sw.adjusted(1, 1, 0, 0));
        }
        p.setPen(kMuted);
        p.drawText(QRect(x + 18, y + 3, fm.horizontalAdvance(it.text) + 4, 18), Qt::AlignLeft | Qt::AlignVCenter, it.text);
        x += 18 + fm.horizontalAdvance(it.text) + 18;
    }
}

void DiskMapWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(font());

    p.setPen(QPen(QColor("#d0d7de"), 1));
    p.setBrush(QColor("#ffffff"));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);

    if (m_rows.isEmpty())
    {
        p.setPen(kMuted);
        p.drawText(QRect(0, 0, width(), legendTop()), Qt::AlignCenter, "Chưa có dữ liệu đĩa (bấm \"Làm mới\").");
        return;
    }

    const QList<RowGeom> geom = computeGeometry();
    for (const auto& row : geom)
    {
        paintRowLabel(p, row);
        for (const auto& b : row.blocks)
            paintBlock(p, b, m_rows[row.rowIndex].disk.number);
    }

    // ---- Lớp xem trước thay đổi + tay nắm ----
    if (m_resizeActive)
    {
        for (const auto& row : geom)
        {
            if (m_rows[row.rowIndex].disk.number != m_resizeDisk)
                continue;
            int bi = -1;
            for (int i = 0; i < row.blocks.size(); ++i)
                if (row.blocks[i].seg.kind == DiskLayout::SegmentKind::Partition && row.blocks[i].seg.partitionIndex == m_resizePartitionIndex)
                    bi = i;
            if (bi < 0)
                break;
            const Block& pb = row.blocks[bi];
            int ghostX = pb.rect.right() + 1;
            QColor tagColor = kMuted;
            const bool moved = m_previewStart != m_curStart;
            if (moved)
            {
                // Đã dời/đổi mép trái: tô khối mới + vẽ nét đứt quanh CHỖ CŨ (nay là vùng trống) để thấy phân vùng đi đâu.
                tagColor = (m_previewStart < m_curStart) ? kGrowGreen : kShrinkOrange;
                if (m_previewBytes == m_currentBytes)
                    tagColor = kMoveBlue;
                p.setPen(Qt::NoPen);
                p.fillRect(QRect(pb.rect.left() + 1, pb.rect.top() + kHeaderH, pb.rect.width() - 2, pb.rect.height() - kHeaderH - 1),
                           QColor(tagColor.red(), tagColor.green(), tagColor.blue(), 40));
                const int ox0 = xAtByte(row, m_curStart), ox1 = xAtByte(row, m_curStart + m_currentBytes);
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(kMuted, 1, Qt::DashLine));
                p.drawRect(QRect(ox0, row.barRect.top() + 1, std::max(2, ox1 - ox0), row.barRect.height() - 3));
            }
            else if (m_previewBytes < m_currentBytes && bi + 1 < row.blocks.size() && row.blocks[bi + 1].seg.kind == DiskLayout::SegmentKind::Unallocated)
            {
                const Block& fb = row.blocks[bi + 1];
                const qint64 freed = m_currentBytes - m_previewBytes;
                const int fw = fb.seg.sizeBytes > 0 ? int(std::min<double>(fb.rect.width(), double(freed) / double(fb.seg.sizeBytes) * fb.rect.width())) : 0;
                const QRect over(fb.rect.left(), fb.rect.top() + kHeaderH, qMax(1, fw), fb.rect.height() - kHeaderH - 1);
                p.setPen(Qt::NoPen);
                p.fillRect(over, QColor(kShrinkOrange.red(), kShrinkOrange.green(), kShrinkOrange.blue(), 45));
                p.setBrush(QBrush(kShrinkOrange, Qt::BDiagPattern));
                p.drawRect(over);
                ghostX = fb.rect.left() + fw;
                tagColor = kShrinkOrange;
            }
            else if (m_previewBytes > m_currentBytes)
            {
                const qint64 taken = m_previewBytes - m_currentBytes;
                const qint64 total = std::max<qint64>(1, pb.seg.sizeBytes);
                const int tw = int(std::min<double>(pb.rect.width(), double(taken) / double(total) * pb.rect.width()));
                const QRect over(pb.rect.right() - tw, pb.rect.top() + kHeaderH, qMax(1, tw), pb.rect.height() - kHeaderH - 1);
                p.setPen(Qt::NoPen);
                p.fillRect(over, QColor(kGrowGreen.red(), kGrowGreen.green(), kGrowGreen.blue(), 45));
                p.setBrush(QBrush(kGrowGreen, Qt::BDiagPattern));
                p.drawRect(over);
                ghostX = pb.rect.right() - tw;
                tagColor = kGrowGreen;
            }
            if (m_previewBytes != m_currentBytes) // vạch đứt = mép phải CŨ
            {
                p.setPen(QPen(kMuted, 1, Qt::DashLine));
                p.drawLine(ghostX, row.barRect.top() - 2, ghostX, row.barRect.bottom() + 2);
            }

            // Thẻ kích thước dưới khối
            const qint64 diff = m_previewBytes - m_currentBytes;
            QString tag = DiskLayout::formatSize(m_previewBytes);
            if (diff == 0 && !moved) tag += "  (hiện tại)";
            else if (diff != 0) tag += QString("  (%1%2)").arg(QString(diff > 0 ? "+" : "−"), DiskLayout::formatSize(std::llabs(diff)));
            if (moved)
                tag = QString("bắt đầu %1 → %2 (%3%4)  ·  ").arg(DiskLayout::formatSize(m_curStart), DiskLayout::formatSize(m_previewStart),
                                                                 QString(m_previewStart > m_curStart ? "+" : "−"),
                                                                 DiskLayout::formatSize(std::llabs(m_previewStart - m_curStart))) + tag;
            QFont tagFont = font();
            tagFont.setBold(true);
            p.setFont(tagFont);
            const QFontMetrics tfm(tagFont);
            const int tw = tfm.horizontalAdvance(tag) + 14;
            QRect tagRect(0, row.barRect.bottom() + 3, tw, kTagH - 4);
            tagRect.moveLeft(std::clamp(pb.rect.center().x() - tw / 2, row.barRect.left(), qMax(row.barRect.left(), row.barRect.right() - tw)));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(tagColor.red(), tagColor.green(), tagColor.blue(), 28));
            p.drawRoundedRect(tagRect, 8, 8);
            p.setPen(tagColor);
            p.drawText(tagRect, Qt::AlignCenter, tag);
            p.setFont(font());
        }

        const QRect h = handleRect(geom);
        if (!h.isNull())
        {
            const bool canShrink = m_previewBytes > m_bounds.minBytes;
            const bool canGrow = m_previewBytes < m_bounds.maxBytes;
            // Mũi tên đặt thấp (gần đáy khối) để không đè lên chữ ở nửa trên của khối bên cạnh.
            const int cy = h.bottom() - 12;
            auto triangle = [&](int baseX, int dir, const QColor& c, bool enabled) {
                QPolygonF poly; // đáy ở sát tay nắm, ĐỈNH hướng ra xa tay nắm
                poly << QPointF(baseX + dir * 9, cy) << QPointF(baseX, cy - 7) << QPointF(baseX, cy + 7);
                p.setPen(Qt::NoPen);
                p.setBrush(enabled ? c : QColor("#c4cad1"));
                p.drawPolygon(poly);
            };
            triangle(h.left() - 1, -1, kShrinkOrange, canShrink);   // ◀ thu nhỏ (kéo sang trái)
            triangle(h.right() + 2, +1, kGrowGreen, canGrow);       // ▶ mở rộng (kéo sang phải)
            p.setPen(Qt::NoPen);
            p.setBrush(m_dragging ? QColor("#0854b0") : QColor("#0969da"));
            p.drawRoundedRect(h, 3, 3);
            p.setPen(QPen(Qt::white, 1));
            p.drawLine(h.center().x() - 1, h.top() + 8, h.center().x() - 1, h.bottom() - 8);
            p.drawLine(h.center().x() + 1, h.top() + 8, h.center().x() + 1, h.bottom() - 8);
            if (hasFocus())
            {
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QColor("#0969da"), 1, Qt::DashLine));
                p.drawRoundedRect(h.adjusted(-3, -3, 3, 3), 4, 4);
            }
        }

        const QRect lh = leftHandleRect(geom);
        if (!lh.isNull())
        {
            const auto* segs = targetSegments();
            const DiskLayout::Span sp = segs ? DiskLayout::adjacentSpan(*segs, m_resizePartitionIndex) : DiskLayout::Span{};
            const qint64 end = m_previewStart + m_previewBytes;
            const bool canGrowLeft = m_previewStart > sp.lo && m_direction != DiskLayout::ResizeDirection::ShrinkOnly;
            const bool canShrinkLeft = end - m_previewStart > m_bounds.minBytes && m_direction != DiskLayout::ResizeDirection::GrowOnly;
            const int cy = lh.bottom() - 12;
            auto triangle = [&](int baseX, int dir, const QColor& c, bool enabled) {
                QPolygonF poly;
                poly << QPointF(baseX + dir * 9, cy) << QPointF(baseX, cy - 7) << QPointF(baseX, cy + 7);
                p.setPen(Qt::NoPen);
                p.setBrush(enabled ? c : QColor("#c4cad1"));
                p.drawPolygon(poly);
            };
            triangle(lh.left() - 1, -1, kGrowGreen, canGrowLeft);     // ◀ mở rộng về bên trái
            triangle(lh.right() + 2, +1, kShrinkOrange, canShrinkLeft); // ▶ thu nhỏ từ bên trái
            p.setPen(Qt::NoPen);
            p.setBrush(m_dragging && m_dragKind == DragKind::LeftEdge ? QColor("#0854b0") : QColor("#0969da"));
            p.drawRoundedRect(lh, 3, 3);
            p.setPen(QPen(Qt::white, 1));
            p.drawLine(lh.center().x() - 1, lh.top() + 8, lh.center().x() - 1, lh.bottom() - 8);
            p.drawLine(lh.center().x() + 1, lh.top() + 8, lh.center().x() + 1, lh.bottom() - 8);
        }
    }

    paintLegend(p, legendTop());
}

// ------------------------------------------------------------------------------------------------ chuột/phím

void DiskMapWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
    {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const QList<RowGeom> geom = computeGeometry();

    if (m_resizeActive)
    {
        const QRect h = handleRect(geom).adjusted(-6, 0, 6, 0);
        if (!h.isNull() && h.contains(pos))
        {
            for (const auto& row : geom)
                if (m_rows[row.rowIndex].disk.number == m_resizeDisk)
                    m_dragBytesPerPx = row.bytesPerPx;
            m_dragging = m_dragBytesPerPx > 0.0;
            m_dragKind = m_dragging ? DragKind::RightEdge : DragKind::None;
            m_dragStartX = pos.x();
            m_dragStartBytes = m_previewBytes;
            setFocus(Qt::MouseFocusReason);
            update();
            event->accept();
            return;
        }
        const RowGeom* row = targetRow(geom);
        const QRect lh = leftHandleRect(geom).adjusted(-6, 0, 6, 0);
        if (row && !lh.isNull() && lh.contains(pos))
        {
            m_dragMap = buildMap(*row);
            m_dragKind = DragKind::LeftEdge;
            m_dragFixedEnd = m_previewStart + m_previewBytes;
            m_dragGrabBytes = m_previewStart - bytesAtX(m_dragMap, pos.x());
            m_dragging = true;
            setFocus(Qt::MouseFocusReason);
            update();
            event->accept();
            return;
        }
        // Kéo cả khối (chỉ khi cho phép dời, hướng "Cả hai", chưa đổi kích thước): bấm vào thân khối của phân vùng đang chọn.
        if (row && m_moveEnabled && m_curStart >= 0 && m_direction == DiskLayout::ResizeDirection::Free && m_previewBytes == m_currentBytes)
        {
            for (const auto& b : row->blocks)
            {
                if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == m_resizePartitionIndex && b.rect.contains(pos))
                {
                    m_dragMap = buildMap(*row);
                    m_dragKind = DragKind::Body;
                    m_dragGrabBytes = bytesAtX(m_dragMap, pos.x()) - m_previewStart;
                    m_dragStartX = pos.x();
                    m_dragging = true;
                    setFocus(Qt::MouseFocusReason);
                    update();
                    event->accept();
                    return;
                }
            }
        }
    }
    for (const auto& row : geom)
    {
        for (const auto& b : row.blocks)
        {
            if (b.seg.kind != DiskLayout::SegmentKind::Partition || !b.rect.contains(pos))
                continue;
            if (b.seg.partitionIndex < 0 || b.seg.partitionIndex >= m_partitions.size())
                continue;
            const auto& part = m_partitions[b.seg.partitionIndex];
            setSelection(part.diskNumber, part.partitionNumber);
            emit partitionClicked(part.diskNumber, part.partitionNumber);
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void DiskMapWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if (m_dragging && m_dragKind == DragKind::LeftEdge)
    {
        // Mép phải giữ nguyên; mép trái đi theo chuột trong [đầu vùng trống liền kề trái, mép phải - kích thước tối thiểu].
        if (const auto* segs = targetSegments())
        {
            const DiskLayout::Span sp = DiskLayout::adjacentSpan(*segs, m_resizePartitionIndex);
            qint64 lo = sp.lo;
            qint64 hi = m_dragFixedEnd - std::max<qint64>(m_bounds.minBytes, kMiB);
            if (m_direction == DiskLayout::ResizeDirection::ShrinkOnly)
                lo = std::max(lo, m_curStart);
            if (m_direction == DiskLayout::ResizeDirection::GrowOnly)
                hi = std::min(hi, m_curStart);
            if (hi >= lo)
            {
                qint64 start = std::clamp(bytesAtX(m_dragMap, pos.x()) + m_dragGrabBytes, lo, hi);
                if (std::llabs(start - m_curStart) < kMiB / 2)
                    start = m_curStart; // gần chỗ cũ = chưa đổi (giữ đúng giá trị cũ dù chưa căn lề)
                applyGeometry(start, m_dragFixedEnd - ((start == m_curStart) ? m_curStart : ((start + kMiB / 2) / kMiB) * kMiB), true);
            }
        }
        event->accept();
        return;
    }
    if (m_dragging && m_dragKind == DragKind::Body)
    {
        if (const auto* segs = targetSegments())
        {
            const qint64 desired = bytesAtX(m_dragMap, pos.x()) - m_dragGrabBytes;
            const qint64 snapped = DiskLayout::snapMoveStart(*segs, m_resizePartitionIndex, desired, diskSizeOfTarget());
            if (snapped >= 0)
                applyGeometry(snapped, m_currentBytes, true);
        }
        event->accept();
        return;
    }
    if (m_dragging)
    {
        const double dx = pos.x() - m_dragStartX;
        applyPreview(m_dragStartBytes + static_cast<qint64>(std::llround(dx * m_dragBytesPerPx)), true, true);
        event->accept();
        return;
    }
    const QList<RowGeom> geom = computeGeometry();
    const QRect h = handleRect(geom).adjusted(-6, 0, 6, 0);
    if (m_resizeActive && !h.isNull() && h.contains(pos))
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }
    if (m_resizeActive && !leftHandleRect(geom).isNull() && leftHandleRect(geom).adjusted(-6, 0, 6, 0).contains(pos))
    {
        setCursor(Qt::SizeHorCursor);
        return;
    }
    if (const RowGeom* trow = targetRow(geom))
        if (m_resizeActive && m_moveEnabled && m_direction == DiskLayout::ResizeDirection::Free && m_previewBytes == m_currentBytes)
            for (const auto& b : trow->blocks)
                if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == m_resizePartitionIndex && b.rect.contains(pos))
                {
                    setCursor(Qt::SizeAllCursor);
                    return;
                }
    for (const auto& row : geom)
        for (const auto& b : row.blocks)
            if (b.seg.kind == DiskLayout::SegmentKind::Partition && b.rect.contains(pos))
            {
                setCursor(Qt::PointingHandCursor);
                return;
            }
    unsetCursor();
}

void DiskMapWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging && event->button() == Qt::LeftButton)
    {
        m_dragging = false;
        m_dragKind = DragKind::None;
        update();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void DiskMapWidget::keyPressEvent(QKeyEvent* event)
{
    if (!m_resizeActive)
    {
        QWidget::keyPressEvent(event);
        return;
    }
    qint64 step = kGiB;
    if (event->modifiers() & Qt::ShiftModifier) step = 100 * kMiB;
    if (event->modifiers() & Qt::ControlModifier) step = 10 * kGiB;
    switch (event->key())
    {
    case Qt::Key_Left:   applyPreview(m_previewBytes - step, true, false); break;
    case Qt::Key_Right:  applyPreview(m_previewBytes + step, true, false); break;
    case Qt::Key_Home:   applyPreview(m_bounds.minBytes, true, false); break;
    case Qt::Key_End:    applyPreview(m_bounds.maxBytes, true, false); break;
    case Qt::Key_Escape: applyPreview(m_currentBytes, true, false); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

QString DiskMapWidget::blockToolTip(const Block& b, int diskNumber) const
{
    if (b.seg.kind == DiskLayout::SegmentKind::Unallocated)
        return QString("Đĩa %1 - vùng CHƯA PHÂN BỔ\n%2\nBắt đầu tại %3").arg(diskNumber).arg(DiskLayout::formatSize(b.seg.sizeBytes), DiskLayout::formatSize(b.seg.offsetBytes));
    if (b.seg.partitionIndex < 0 || b.seg.partitionIndex >= m_partitions.size())
        return {};
    const auto& p = m_partitions[b.seg.partitionIndex];
    QString t = QString("Đĩa %1 - phân vùng %2 (%3)\n%4").arg(diskNumber).arg(p.partitionNumber).arg(p.type, partitionTitle(p));
    t += "\nDung lượng " + DiskLayout::formatSize(p.sizeBytes);
    if (p.hasFileSystem()) t += " · " + p.fileSystem;
    if (p.freeBytes >= 0) t += "\nCòn trống " + DiskLayout::formatSize(p.freeBytes);
    if (p.offsetBytes >= 0) t += "\nBắt đầu tại " + DiskLayout::formatSize(p.offsetBytes);
    const QString flags = partitionFlags(p);
    if (!flags.isEmpty()) t += "\n" + flags;
    return t;
}

bool DiskMapWidget::event(QEvent* e)
{
    if (e->type() == QEvent::ToolTip)
    {
        auto* he = static_cast<QHelpEvent*>(e);
        const QList<RowGeom> geom = computeGeometry();
        const QRect h = handleRect(geom).adjusted(-6, 0, 6, 0);
        if (m_resizeActive && !h.isNull() && h.contains(he->pos()))
        {
            QToolTip::showText(he->globalPos(),
                               "Kéo sang TRÁI = thu nhỏ (phần giải phóng nằm bên phải phân vùng).\n"
                               "Kéo sang PHẢI = mở rộng (lấy từ vùng chưa phân bổ liền kề bên phải).\n"
                               "Phím ← → : 1 GB (Shift 100 MB, Ctrl 10 GB) · Home/End: nhỏ nhất/lớn nhất · Esc: về hiện tại.",
                               this);
            return true;
        }
        const QRect lhit = leftHandleRect(geom).adjusted(-6, 0, 6, 0);
        if (m_resizeActive && !lhit.isNull() && lhit.contains(he->pos()))
        {
            QToolTip::showText(he->globalPos(),
                               "Kéo mép TRÁI sang TRÁI = mở rộng về bên trái (lấy từ vùng chưa phân bổ bên trái).\n"
                               "Kéo sang PHẢI = thu nhỏ từ bên trái.\n"
                               "Ứng dụng sẽ CHÉP dữ liệu phân vùng sang vị trí mới (có thể mất vài phút tới hàng chục phút) - xem mô tả bên dưới trước khi xác nhận.",
                               this);
            return true;
        }
        for (const auto& row : geom)
            for (const auto& b : row.blocks)
                if (b.rect.contains(he->pos()))
                {
                    if (m_resizeActive && b.seg.kind == DiskLayout::SegmentKind::Partition && b.seg.partitionIndex == m_resizePartitionIndex)
                    {
                        if (m_moveEnabled)
                            QToolTip::showText(he->globalPos(),
                                               blockToolTip(b, m_rows[row.rowIndex].disk.number) +
                                                   "\n\nKéo cả khối sang trái/phải để DỜI phân vùng (kể cả sang vùng chưa phân bổ khác để đổi thứ tự).",
                                               this);
                        else if (!m_moveDisabledReason.isEmpty())
                            QToolTip::showText(he->globalPos(),
                                               blockToolTip(b, m_rows[row.rowIndex].disk.number) + "\n\nKhông dời được: " + m_moveDisabledReason, this);
                        else
                            QToolTip::showText(he->globalPos(), blockToolTip(b, m_rows[row.rowIndex].disk.number), this);
                        return true;
                    }
                    const QString t = blockToolTip(b, m_rows[row.rowIndex].disk.number);
                    if (!t.isEmpty())
                    {
                        QToolTip::showText(he->globalPos(), t, this);
                        return true;
                    }
                }
        QToolTip::hideText();
        e->ignore();
        return true;
    }
    return QWidget::event(e);
}
