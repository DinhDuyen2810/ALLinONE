#include "DiskLayout.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace DiskLayout
{
namespace
{
constexpr double kGb = 1024.0 * 1024.0 * 1024.0;

bool isFreeSegment(const QList<Segment>& segments, int index)
{
    return index >= 0 && index < segments.size() && segments[index].kind == SegmentKind::Unallocated;
}

int indexOfPartition(const QList<Segment>& segments, int partitionIndex)
{
    for (int i = 0; i < segments.size(); ++i)
        if (segments[i].kind == SegmentKind::Partition && segments[i].partitionIndex == partitionIndex)
            return i;
    return -1;
}
} // namespace

QString formatSize(qint64 bytes)
{
    constexpr double KB = 1024.0, MB = KB * 1024, GB = MB * 1024;
    const double b = static_cast<double>(bytes);
    if (b >= GB) return QString::number(b / GB, 'f', 2) + " GB";
    if (b >= MB) return QString::number(b / MB, 'f', 1) + " MB";
    if (b >= KB) return QString::number(b / KB, 'f', 0) + " KB";
    return QString::number(bytes) + " B";
}

QList<DiskRow> build(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions)
{
    // Gom mọi số đĩa xuất hiện ở một trong hai danh sách (Get-Disk có thể lỗi riêng, Get-Partition vẫn đủ để vẽ).
    QList<int> numbers;
    QSet<int> seen;
    for (const auto& d : disks)
        if (d.number >= 0 && !seen.contains(d.number)) { seen.insert(d.number); numbers << d.number; }
    for (const auto& p : partitions)
        if (p.diskNumber >= 0 && !seen.contains(p.diskNumber)) { seen.insert(p.diskNumber); numbers << p.diskNumber; }
    std::sort(numbers.begin(), numbers.end());

    QList<DiskRow> rows;
    for (const int number : numbers)
    {
        DiskRow row;
        row.disk.number = number;
        for (const auto& d : disks)
            if (d.number == number) { row.disk = d; break; }

        QList<int> idx;
        bool allOffsets = true;
        for (int i = 0; i < partitions.size(); ++i)
        {
            if (partitions[i].diskNumber != number)
                continue;
            idx << i;
            if (partitions[i].offsetBytes < 0)
                allOffsets = false;
        }
        row.approximate = !allOffsets;
        std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
            return allOffsets ? partitions[a].offsetBytes < partitions[b].offsetBytes
                              : partitions[a].partitionNumber < partitions[b].partitionNumber;
        });

        qint64 cursor = 0;
        for (const int i : idx)
        {
            const auto& p = partitions[i];
            const qint64 size = std::max<qint64>(0, p.sizeBytes);
            // Không biết Offset: xếp liền nhau. Biết: chống chồng lấn bằng cách không cho lùi quá con trỏ hiện tại.
            const qint64 start = allOffsets ? std::max(p.offsetBytes, cursor) : cursor;
            if (allOffsets && start - cursor >= kMinGapBytes)
                row.segments << Segment{SegmentKind::Unallocated, cursor, start - cursor, -1};
            row.segments << Segment{SegmentKind::Partition, start, size, i};
            cursor = start + size;
        }

        const qint64 diskSize = row.disk.sizeBytes > 0 ? row.disk.sizeBytes : cursor;
        if (row.disk.sizeBytes <= 0)
            row.disk.sizeBytes = diskSize;
        if (allOffsets && diskSize - cursor >= kMinGapBytes)
            row.segments << Segment{SegmentKind::Unallocated, cursor, diskSize - cursor, -1};
        rows << row;
    }
    return rows;
}

qint64 freeBytesAfter(const QList<Segment>& segments, int partitionIndex)
{
    const int i = indexOfPartition(segments, partitionIndex);
    return (i >= 0 && isFreeSegment(segments, i + 1)) ? segments[i + 1].sizeBytes : 0;
}

qint64 freeBytesBefore(const QList<Segment>& segments, int partitionIndex)
{
    const int i = indexOfPartition(segments, partitionIndex);
    return (i > 0 && isFreeSegment(segments, i - 1)) ? segments[i - 1].sizeBytes : 0;
}

QList<Segment> projectResize(const QList<Segment>& segments, int partitionIndex, qint64 newSizeBytes)
{
    QList<Segment> out = segments;
    const int i = indexOfPartition(out, partitionIndex);
    if (i < 0)
        return out;

    const qint64 oldSize = out[i].sizeBytes;
    const qint64 target = std::max<qint64>(0, newSizeBytes);
    const qint64 delta = target - oldSize;
    if (delta == 0)
        return out;

    if (delta < 0)
    {
        const qint64 freed = -delta;
        out[i].sizeBytes = target;
        if (isFreeSegment(out, i + 1))
        {
            out[i + 1].offsetBytes -= freed;
            out[i + 1].sizeBytes += freed;
        }
        else
        {
            out.insert(i + 1, Segment{SegmentKind::Unallocated, out[i].offsetBytes + target, freed, -1});
        }
        return out;
    }

    const qint64 available = isFreeSegment(out, i + 1) ? out[i + 1].sizeBytes : 0;
    const qint64 grow = std::min(delta, available);
    if (grow <= 0)
        return out;
    out[i].sizeBytes = oldSize + grow;
    out[i + 1].offsetBytes += grow;
    out[i + 1].sizeBytes -= grow;
    if (out[i + 1].sizeBytes <= 0)
        out.removeAt(i + 1);
    return out;
}

QList<int> computeBlockWidths(const QList<qint64>& sizes, int totalPx, int minPx)
{
    const int n = sizes.size();
    QList<int> widths(n, 0);
    if (n == 0 || totalPx <= 0)
        return widths;

    qint64 totalBytes = 0;
    for (const qint64 s : sizes)
        totalBytes += std::max<qint64>(0, s);
    // Không đủ chỗ cho mọi khối ở độ rộng tối thiểu, hoặc không có dung lượng để chia tỉ lệ: chia đều.
    if (totalBytes <= 0 || totalPx <= n * minPx)
    {
        for (int i = 0; i < n; ++i)
            widths[i] = totalPx / n;
        widths[0] += totalPx - (totalPx / n) * n;
        return widths;
    }

    QList<bool> fixed(n, false);
    for (int round = 0; round <= n; ++round)
    {
        int fixedCount = 0;
        qint64 freeBytes = 0;
        for (int i = 0; i < n; ++i)
        {
            if (fixed[i]) ++fixedCount;
            else freeBytes += std::max<qint64>(0, sizes[i]);
        }
        const double freePx = static_cast<double>(totalPx - fixedCount * minPx);
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            if (fixed[i])
                continue;
            const double w = freeBytes > 0 ? static_cast<double>(std::max<qint64>(0, sizes[i])) / static_cast<double>(freeBytes) * freePx : 0.0;
            if (w < minPx)
            {
                fixed[i] = true;
                changed = true;
            }
        }
        if (!changed)
        {
            int used = 0;
            int largest = -1;
            for (int i = 0; i < n; ++i)
            {
                if (fixed[i])
                    widths[i] = minPx;
                else
                {
                    widths[i] = static_cast<int>(static_cast<double>(std::max<qint64>(0, sizes[i])) / static_cast<double>(freeBytes) * freePx);
                    if (largest < 0 || sizes[i] > sizes[largest])
                        largest = i;
                }
                used += widths[i];
            }
            if (largest < 0) // mọi khối đều ở mức tối thiểu
                largest = 0;
            widths[largest] += totalPx - used; // phần dư do làm tròn xuống dồn vào khối lớn nhất
            return widths;
        }
    }
    return widths; // không tới được (vòng lặp luôn kết thúc trong n+1 lượt), giữ để trình biên dịch yên tâm
}

double bytesPerPixel(const QList<qint64>& sizes, const QList<int>& widths, int minPx)
{
    qint64 bytes = 0;
    qint64 px = 0;
    for (int i = 0; i < sizes.size() && i < widths.size(); ++i)
    {
        if (widths[i] > minPx) // khối bị nâng độ rộng tối thiểu không còn đúng tỉ lệ
        {
            bytes += std::max<qint64>(0, sizes[i]);
            px += widths[i];
        }
    }
    if (px <= 0) // mọi khối đều ở mức tối thiểu: lấy tỉ lệ chung
    {
        for (int i = 0; i < sizes.size() && i < widths.size(); ++i)
        {
            bytes += std::max<qint64>(0, sizes[i]);
            px += widths[i];
        }
    }
    return px > 0 ? static_cast<double>(bytes) / static_cast<double>(px) : 0.0;
}

SizeBounds boundsForDirection(qint64 currentBytes, const PartitionManager::SupportedSizeRange& range, ResizeDirection direction)
{
    if (!range.ok || range.minBytes < 0 || range.maxBytes < range.minBytes)
        return {};
    const qint64 current = std::clamp(currentBytes, range.minBytes, range.maxBytes);
    switch (direction)
    {
    case ResizeDirection::ShrinkOnly: return {range.minBytes, current};
    case ResizeDirection::GrowOnly:   return {current, range.maxBytes};
    case ResizeDirection::Free:       break;
    }
    return {range.minBytes, range.maxBytes};
}

QString directionBlockReason(double newSizeGb, qint64 currentBytes, ResizeDirection direction)
{
    if (direction == ResizeDirection::Free)
        return {};
    if (!std::isfinite(newSizeGb) || std::fabs(newSizeGb) > 1e12)
        return "Kích thước mới không hợp lệ.";
    const long long now100 = std::llround(static_cast<double>(currentBytes) / kGb * 100.0);
    const long long next100 = std::llround(newSizeGb * 100.0);
    if (next100 == now100)
        return {};
    if (direction == ResizeDirection::ShrinkOnly && next100 > now100)
        return "Đang khóa hướng \"Chỉ thu nhỏ\" (kéo sang trái) nhưng kích thước mới LỚN hơn hiện tại. "
               "Đổi hướng sang \"Mở rộng\" hoặc \"Cả hai\" nếu thật sự muốn mở rộng.";
    if (direction == ResizeDirection::GrowOnly && next100 < now100)
        return "Đang khóa hướng \"Chỉ mở rộng\" (kéo sang phải) nhưng kích thước mới NHỎ hơn hiện tại. "
               "Đổi hướng sang \"Thu nhỏ\" hoặc \"Cả hai\" nếu thật sự muốn thu nhỏ.";
    return {};
}

QString directionName(ResizeDirection direction)
{
    switch (direction)
    {
    case ResizeDirection::ShrinkOnly: return "Chỉ thu nhỏ (kéo sang trái)";
    case ResizeDirection::GrowOnly:   return "Chỉ mở rộng (kéo sang phải)";
    case ResizeDirection::Free:       break;
    }
    return "Cả hai hướng";
}

QString describeResize(const QString& name, qint64 currentBytes, qint64 newBytes, qint64 freeAfterBytes, qint64 freeBeforeBytes)
{
    constexpr qint64 kNoChangeBytes = 1024 * 1024;
    const qint64 diff = newBytes - currentBytes;
    if (diff > -kNoChangeBytes && diff < kNoChangeBytes)
        return "Chưa thay đổi kích thước - kéo mép phải của phân vùng sang trái (thu nhỏ) hoặc sang phải (mở rộng), "
               "hoặc nhập kích thước mới.";

    if (diff < 0)
        return QString("◀ THU NHỎ %1 từ %2 xuống %3 (giảm %4). Phần giải phóng nằm BÊN PHẢI %1, thành vùng chưa phân bổ; "
                       "mép TRÁI giữ nguyên vị trí.")
            .arg(name, formatSize(currentBytes), formatSize(newBytes), formatSize(-diff));

    QString text = QString("MỞ RỘNG %1 từ %2 lên %3 (tăng %4) ▶. Lấy từ vùng chưa phân bổ LIỀN KỀ BÊN PHẢI; mép TRÁI giữ nguyên vị trí.")
                       .arg(name, formatSize(currentBytes), formatSize(newBytes), formatSize(diff));
    if (freeAfterBytes <= 0)
    {
        text += " ⚠ Hiện KHÔNG có vùng chưa phân bổ liền kề bên phải";
        text += freeBeforeBytes > 0
                    ? QString(" (có %1 trống BÊN TRÁI: Windows chỉ đổi bằng mép phải, nhưng với phân vùng dữ liệu NTFS có thể dùng chỗ đó bằng cách KÉO MÉP TRÁI hoặc DỜI phân vùng - xem ghi chú bên dưới).")
                          .arg(formatSize(freeBeforeBytes))
                    : QString(".");
    }
    else if (diff > freeAfterBytes + kNoChangeBytes)
    {
        text += QString(" ⚠ Vượt quá vùng trống liền kề bên phải (chỉ có %1).").arg(formatSize(freeAfterBytes));
    }
    return text;
}

// ---- Dời phân vùng / đổi mép TRÁI ----

namespace
{
qint64 alignUp(qint64 v, qint64 a) { return a > 0 ? ((v + a - 1) / a) * a : v; }
bool sameSegments(const QList<Segment>& a, const QList<Segment>& b)
{
    if (a.size() != b.size())
        return false;
    for (int k = 0; k < a.size(); ++k)
        if (a[k].kind != b[k].kind || a[k].offsetBytes != b[k].offsetBytes || a[k].sizeBytes != b[k].sizeBytes || a[k].partitionIndex != b[k].partitionIndex)
            return false;
    return true;
}
qint64 alignDown(qint64 v, qint64 a) { return a > 0 ? (v / a) * a : v; }
} // namespace

Span adjacentSpan(const QList<Segment>& segments, int partitionIndex)
{
    const int i = indexOfPartition(segments, partitionIndex);
    if (i < 0)
        return {};
    Span sp{segments[i].offsetBytes, segments[i].offsetBytes + segments[i].sizeBytes};
    if (isFreeSegment(segments, i - 1))
        sp.lo = segments[i - 1].offsetBytes;
    if (isFreeSegment(segments, i + 1))
        sp.hi = segments[i + 1].offsetBytes + segments[i + 1].sizeBytes;
    return sp;
}

QList<Segment> projectGeometry(const QList<Segment>& segments, int partitionIndex, qint64 newStart, qint64 newSize)
{
    const int i = indexOfPartition(segments, partitionIndex);
    if (i < 0 || newSize <= 0 || newStart < 0)
        return segments;
    const Span sp = adjacentSpan(segments, partitionIndex);
    const int first = isFreeSegment(segments, i - 1) ? i - 1 : i;
    const int last = isFreeSegment(segments, i + 1) ? i + 1 : i;

    // 1) Gỡ phân vùng ra: chỗ cũ + vùng trống liền kề thành MỘT vùng trống.
    QList<Segment> tmp;
    for (int k = 0; k < first; ++k)
        tmp << segments[k];
    tmp << Segment{SegmentKind::Unallocated, sp.lo, sp.hi - sp.lo, -1};
    for (int k = last + 1; k < segments.size(); ++k)
        tmp << segments[k];

    // 2) Tìm vùng trống chứa hình học mới rồi cắt ra.
    const qint64 newEnd = newStart + newSize;
    for (int k = 0; k < tmp.size(); ++k)
    {
        const Segment& f = tmp[k];
        if (f.kind != SegmentKind::Unallocated || newStart < f.offsetBytes || newEnd > f.offsetBytes + f.sizeBytes)
            continue;
        QList<Segment> out;
        for (int m = 0; m < k; ++m)
            out << tmp[m];
        auto addFree = [&](qint64 offset, qint64 size) {
            if (size >= kMinGapBytes)
                out << Segment{SegmentKind::Unallocated, offset, size, -1};
        };
        addFree(f.offsetBytes, newStart - f.offsetBytes);
        out << Segment{SegmentKind::Partition, newStart, newSize, partitionIndex};
        addFree(newEnd, f.offsetBytes + f.sizeBytes - newEnd);
        for (int m = k + 1; m < tmp.size(); ++m)
            out << tmp[m];
        return out;
    }
    return segments; // không nằm gọn trong vùng trống nào
}

qint64 snapMoveStart(const QList<Segment>& segments, int partitionIndex, qint64 desiredStart, qint64 diskSize, qint64 alignBytes)
{
    const int i = indexOfPartition(segments, partitionIndex);
    if (i < 0)
        return -1;
    const qint64 size = segments[i].sizeBytes;
    const qint64 ownStart = segments[i].offsetBytes;
    const Span own = adjacentSpan(segments, partitionIndex);
    const qint64 usableLo = std::max<qint64>(alignBytes, 0);
    const qint64 usableHi = diskSize > 0 ? alignDown(diskSize - alignBytes, alignBytes) : std::numeric_limits<qint64>::max() / 4;

    std::vector<std::pair<qint64, qint64>> spans; // (không đặt tên slots: là macro của Qt)
    spans.emplace_back(own.lo, own.hi);
    for (int k = 0; k < segments.size(); ++k)
    {
        if (segments[k].kind != SegmentKind::Unallocated)
            continue;
        if (k == i - 1 || k == i + 1) // đã nằm trong khe của chính nó
            continue;
        spans.emplace_back(segments[k].offsetBytes, segments[k].offsetBytes + segments[k].sizeBytes);
    }

    qint64 best = -1;
    qint64 bestDist = std::numeric_limits<qint64>::max();
    for (size_t s = 0; s < spans.size(); ++s)
    {
        const qint64 lo = std::max(spans[s].first, usableLo);
        const qint64 hi = std::min(spans[s].second, usableHi);
        const qint64 low = alignUp(lo, alignBytes);
        const qint64 high = alignDown(hi - size, alignBytes);
        const bool isOwn = (s == 0);
        // Gần chỗ cũ trong khe của chính nó = không đổi gì (kể cả khi chỗ cũ không căn lề 1 MiB).
        if (isOwn && std::llabs(desiredStart - ownStart) < alignBytes / 2 && ownStart >= own.lo && ownStart + size <= own.hi)
            return ownStart;
        if (high < low)
            continue;
        const qint64 nearest = alignBytes > 0 ? ((desiredStart + alignBytes / 2) / alignBytes) * alignBytes : desiredStart;
        const qint64 cand = std::clamp(nearest, low, high);
        const qint64 dist = std::llabs(cand - desiredStart);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = cand;
        }
    }
    return best;
}

bool ChangePlan::needsMove() const
{
    for (const PlanStep& s : steps)
        if (s.kind == StepKind::MoveData)
            return true;
    return false;
}

qint64 ChangePlan::copyBytes() const
{
    for (const PlanStep& s : steps)
        if (s.kind == StepKind::MoveData)
            return s.after.size;
    return 0;
}

ChangePlan planChange(const QList<Segment>& segments, int partitionIndex, qint64 newStart, qint64 newSize, qint64 minSizeBytes, qint64 diskSize,
                      qint64 alignBytes)
{
    ChangePlan plan;
    const int i = indexOfPartition(segments, partitionIndex);
    if (i < 0)
    {
        plan.blockReason = QStringLiteral("Không tìm thấy phân vùng trong sơ đồ.");
        return plan;
    }
    const Geometry cur{segments[i].offsetBytes, segments[i].sizeBytes};
    plan.before = cur;
    plan.target = Geometry{newStart, newSize};
    if (newSize <= 0 || newStart < 0)
    {
        plan.blockReason = QStringLiteral("Vị trí/kích thước mới không hợp lệ.");
        return plan;
    }
    const qint64 newEnd = newStart + newSize;
    const bool moved = newStart != cur.start;
    if (!moved && newSize == cur.size)
    {
        plan.blockReason = QStringLiteral("Vị trí và kích thước mới trùng hiện tại - không có gì để đổi.");
        return plan;
    }
    if (diskSize > 0 && newEnd > diskSize - alignBytes)
    {
        plan.blockReason = QStringLiteral("Vượt quá cuối đĩa (phải chừa lại %1 cuối đĩa cho bảng phân vùng).").arg(formatSize(alignBytes));
        return plan;
    }
    if (moved)
    {
        if (newStart < alignBytes)
        {
            plan.blockReason = QStringLiteral("Vị trí bắt đầu mới quá sát đầu đĩa (phải từ %1 trở đi).").arg(formatSize(alignBytes));
            return plan;
        }
        if (newStart % alignBytes != 0 || (newSize != cur.size && newEnd % alignBytes != 0))
        {
            plan.blockReason = QStringLiteral("Vị trí/kích thước mới phải căn theo bội số của %1.").arg(formatSize(alignBytes));
            return plan;
        }
    }
    // Không được chồng lên phân vùng khác (vùng trống có thể ở bất kỳ phía nào - nhưng phải thật sự trống).
    for (int k = 0; k < segments.size(); ++k)
    {
        if (k == i || segments[k].kind != SegmentKind::Partition)
            continue;
        if (newStart < segments[k].offsetBytes + segments[k].sizeBytes && newEnd > segments[k].offsetBytes)
        {
            plan.blockReason = QStringLiteral("Vị trí mới chồng lên một phân vùng khác - chỉ dùng được vùng CHƯA PHÂN BỔ.");
            return plan;
        }
    }
    // Phải nằm gọn trong (chỗ cũ ∪ vùng trống): chặn cả trường hợp vùng bị bỏ qua vì khe hở nhỏ (< kMinGapBytes).
    if (sameSegments(projectGeometry(segments, partitionIndex, newStart, newSize), segments))
    {
        plan.blockReason = QStringLiteral("Vị trí mới không nằm gọn trong một vùng chưa phân bổ.");
        return plan;
    }
    if (newSize < cur.size && minSizeBytes > 0 && newSize < minSizeBytes)
    {
        plan.blockReason = QStringLiteral("Nhỏ hơn mức Windows cho phép thu nhỏ (%1).").arg(formatSize(minSizeBytes));
        return plan;
    }

    if (!moved)
    {
        plan.steps << PlanStep{newSize < cur.size ? StepKind::ShrinkRight : StepKind::GrowRight, Geometry{cur.start, newSize}};
        return plan;
    }
    if (newSize < cur.size)
    {
        plan.steps << PlanStep{StepKind::ShrinkRight, Geometry{cur.start, newSize}};
        plan.steps << PlanStep{StepKind::MoveData, Geometry{newStart, newSize}};
    }
    else if (newSize == cur.size)
    {
        plan.steps << PlanStep{StepKind::MoveData, Geometry{newStart, newSize}};
    }
    else
    {
        plan.steps << PlanStep{StepKind::MoveData, Geometry{newStart, cur.size}};
        plan.steps << PlanStep{StepKind::GrowRight, Geometry{newStart, newSize}};
    }
    return plan;
}

QString describePlan(const QString& name, const ChangePlan& plan)
{
    if (!plan.blockReason.isEmpty())
        return QStringLiteral("⛔ ") + plan.blockReason;
    if (plan.steps.isEmpty())
        return {};
    const qint64 shift = plan.target.start - plan.before.start;
    if (!plan.needsMove())
    {
        const qint64 diff = plan.target.size - plan.before.size;
        return diff < 0 ? QStringLiteral("◀ THU NHỎ %1 xuống %2 (giảm %3) - chỉ đổi mép phải, không phải chép dữ liệu.")
                              .arg(name, formatSize(plan.target.size), formatSize(-diff))
                        : QStringLiteral("MỞ RỘNG %1 lên %2 (tăng %3) ▶ - chỉ đổi mép phải, không phải chép dữ liệu.")
                              .arg(name, formatSize(plan.target.size), formatSize(diff));
    }
    QString text;
    const QString dir = shift < 0 ? QStringLiteral("TRÁI") : QStringLiteral("PHẢI");
    const qint64 sizeDiff = plan.target.size - plan.before.size;
    if (sizeDiff == 0)
        text = QStringLiteral("↔ DỜI %1 sang %2 %3 (từ %4 tới %5), giữ nguyên kích thước %6.")
                   .arg(name, dir, formatSize(std::llabs(shift)), formatSize(plan.before.start), formatSize(plan.target.start), formatSize(plan.before.size));
    else if (sizeDiff > 0)
        text = QStringLiteral("MỞ RỘNG %1 về bên %2: mép trái dịch sang %3 %4 (từ %5 tới %6), kích thước %7 → %8.")
                   .arg(name, dir, shift < 0 ? QStringLiteral("trái") : QStringLiteral("phải"), formatSize(std::llabs(shift)),
                        formatSize(plan.before.start), formatSize(plan.target.start), formatSize(plan.before.size), formatSize(plan.target.size));
    else
        text = QStringLiteral("THU NHỎ %1 từ bên trái: mép trái dịch sang phải %2 (từ %3 tới %4), kích thước %5 → %6.")
                   .arg(name, formatSize(std::llabs(shift)), formatSize(plan.before.start), formatSize(plan.target.start), formatSize(plan.before.size),
                        formatSize(plan.target.size));
    text += QStringLiteral("\nCác bước (chạy lần lượt, dừng ngay nếu một bước lỗi):");
    int n = 1;
    for (const PlanStep& s : plan.steps)
    {
        switch (s.kind)
        {
        case StepKind::ShrinkRight:
            text += QStringLiteral("\n  %1. Thu nhỏ %2 xuống %3 (Windows, mép phải).").arg(n).arg(name, formatSize(s.after.size));
            break;
        case StepKind::MoveData:
            text += QStringLiteral("\n  %1. CHÉP %2 dữ liệu của %3 sang vị trí mới rồi đổi bảng phân vùng (chậm: phụ thuộc tốc độ đĩa).")
                        .arg(n)
                        .arg(formatSize(s.after.size), name);
            break;
        case StepKind::GrowRight:
            text += QStringLiteral("\n  %1. Mở rộng %2 lên %3 (Windows, mép phải).").arg(n).arg(name, formatSize(s.after.size));
            break;
        }
        ++n;
    }
    return text;
}

} // namespace DiskLayout
