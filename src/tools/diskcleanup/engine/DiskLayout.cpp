#include "DiskLayout.h"

#include <QSet>

#include <algorithm>
#include <cmath>

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
                    ? QString(" (có %1 trống BÊN TRÁI nhưng Windows chỉ đổi kích thước bằng mép phải nên không dùng được).")
                          .arg(formatSize(freeBeforeBytes))
                    : QString(".");
    }
    else if (diff > freeAfterBytes + kNoChangeBytes)
    {
        text += QString(" ⚠ Vượt quá vùng trống liền kề bên phải (chỉ có %1).").arg(formatSize(freeAfterBytes));
    }
    return text;
}

} // namespace DiskLayout
