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
    bytes = std::clamp(bytes, m_bounds.minBytes, m_bounds.maxBytes);
    if (bytes == m_previewBytes)
        return;
    m_previewBytes = bytes;
    update();
    if (emitSignal)
        emit previewSizeChanged(bytes);
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
        if (m_resizeActive && m_rows[r].disk.number == m_resizeDisk && m_previewBytes != m_currentBytes)
            segs = DiskLayout::projectResize(segs, m_resizePartitionIndex, m_previewBytes);

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

QRect DiskMapWidget::handleRectForTest() const
{
    return handleRect(computeGeometry());
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
            if (m_previewBytes < m_currentBytes && bi + 1 < row.blocks.size() && row.blocks[bi + 1].seg.kind == DiskLayout::SegmentKind::Unallocated)
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
            if (diff == 0) tag += "  (hiện tại)";
            else tag += QString("  (%1%2)").arg(QString(diff > 0 ? "+" : "−"), DiskLayout::formatSize(std::llabs(diff)));
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
            m_dragStartX = pos.x();
            m_dragStartBytes = m_previewBytes;
            setFocus(Qt::MouseFocusReason);
            update();
            event->accept();
            return;
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
        for (const auto& row : geom)
            for (const auto& b : row.blocks)
                if (b.rect.contains(he->pos()))
                {
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
