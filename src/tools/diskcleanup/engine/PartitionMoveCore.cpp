#include "PartitionMoveCore.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace PartitionMove
{
namespace
{
constexpr qint64 kSaveGroupBytes = 128 * kMiB; // nhóm đơn vị tối đa giữa hai lần ghi nhật ký

/// Bộ đệm căn lề 4096 (cần cho I/O không đệm của đĩa vật lý).
struct AlignedBuffer
{
    explicit AlignedBuffer(size_t size)
    {
#ifdef _WIN32
        ptr = static_cast<char*>(_aligned_malloc(size, 4096));
#else
        ptr = static_cast<char*>(std::aligned_alloc(4096, (size + 4095) / 4096 * 4096));
#endif
    }
    ~AlignedBuffer()
    {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;
    char* ptr{nullptr};
};

qint64 roundDown(qint64 value, qint64 unit)
{
    return unit > 0 ? (value / unit) * unit : value;
}

qint64 absDiff(qint64 a, qint64 b)
{
    return a > b ? a - b : b - a;
}
} // namespace

QByteArray JournalState::toJson() const
{
    QJsonObject o;
    o["version"] = version;
    o["diskNumber"] = diskNumber;
    o["diskId"] = diskId;
    o["partitionId"] = partitionId;
    o["partitionNumber"] = partitionNumber;
    // Số lớn lưu dạng chuỗi: JSON/double chỉ chính xác tới 2^53 byte (8 PB) - vẫn dư, nhưng chuỗi tránh mọi nghi ngờ.
    o["srcOffset"] = QString::number(srcOffset);
    o["dstOffset"] = QString::number(dstOffset);
    o["length"] = QString::number(length);
    o["unitBytes"] = QString::number(unitBytes);
    o["doneBytes"] = QString::number(doneBytes);
    o["sectorBytes"] = QString::number(sectorBytes);
    o["phase"] = phase;
    o["startedAt"] = startedAt;
    o["note"] = note;
    return QJsonDocument(o).toJson(QJsonDocument::Indented);
}

bool JournalState::fromJson(const QByteArray& json, JournalState* out, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail("Nhật ký không phải JSON hợp lệ.");
    const QJsonObject o = doc.object();
    auto num = [&](const char* key, qint64* v) {
        const QJsonValue val = o.value(key);
        if (!val.isString())
            return false;
        bool ok = false;
        const qint64 n = val.toString().toLongLong(&ok);
        if (!ok)
            return false;
        *v = n;
        return true;
    };
    JournalState s;
    if (!o.value("version").isDouble() || o.value("version").toInt() != 1)
        return fail("Phiên bản nhật ký không được hỗ trợ.");
    if (!num("srcOffset", &s.srcOffset) || !num("dstOffset", &s.dstOffset) || !num("length", &s.length) ||
        !num("unitBytes", &s.unitBytes) || !num("doneBytes", &s.doneBytes) || !num("sectorBytes", &s.sectorBytes))
        return fail("Nhật ký thiếu hoặc sai trường số.");
    s.diskNumber = o.value("diskNumber").toInt(-1);
    s.diskId = o.value("diskId").toString();
    s.partitionId = o.value("partitionId").toString();
    s.partitionNumber = o.value("partitionNumber").toInt(-1);
    s.phase = o.value("phase").toInt(-1);
    s.startedAt = o.value("startedAt").toString();
    s.note = o.value("note").toString();
    if (s.phase < Copying || s.phase > TableDone)
        return fail("Giai đoạn trong nhật ký không hợp lệ.");
    if (const QString why = validateMove(s.srcOffset, s.dstOffset, s.length, s.sectorBytes); !why.isEmpty())
        return fail("Nhật ký có số liệu không hợp lệ: " + why);
    if (s.doneBytes < 0 || s.doneBytes > s.length || s.unitBytes <= 0 || s.unitBytes % s.sectorBytes != 0)
        return fail("Nhật ký có tiến độ/đơn vị không hợp lệ.");
    *out = s;
    return true;
}

QString validateMove(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 sectorBytes)
{
    if (sectorBytes < 512 || (sectorBytes & (sectorBytes - 1)) != 0 || sectorBytes > 65536)
        return QStringLiteral("kích thước sector không hợp lệ (%1)").arg(sectorBytes);
    if (length <= 0)
        return QStringLiteral("độ dài dữ liệu phải > 0");
    if (srcOffset < 0 || dstOffset < 0)
        return QStringLiteral("vị trí không được âm");
    if (srcOffset == dstOffset)
        return QStringLiteral("vị trí mới trùng vị trí cũ - không có gì để dời");
    if (srcOffset % sectorBytes != 0 || dstOffset % sectorBytes != 0 || length % sectorBytes != 0)
        return QStringLiteral("vị trí/độ dài phải là bội số của sector (%1 byte)").arg(sectorBytes);
    return {};
}

qint64 safeUnitBytes(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 chunkBytes, qint64 sectorBytes)
{
    qint64 unit = std::max(sectorBytes, roundDown(chunkBytes, sectorBytes));
    const qint64 delta = absDiff(srcOffset, dstOffset);
    if (delta < length) // chồng lấn: một đơn vị không được dài hơn khoảng dời
        unit = std::min(unit, std::max(sectorBytes, roundDown(delta, sectorBytes)));
    return unit;
}

qint64 saveIntervalBytes(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 unitBytes)
{
    qint64 group = std::max(unitBytes, roundDown(kSaveGroupBytes, unitBytes));
    const qint64 delta = absDiff(srcOffset, dstOffset);
    if (delta < length) // chồng lấn: cả nhóm cũng không được dài hơn khoảng dời (nếu không thì chép lại nhóm sẽ đọc nguồn đã bị đè)
        group = std::min(group, std::max(unitBytes, roundDown(delta, unitBytes)));
    return group;
}

CopyOutcome copyRange(IBlockDevice& device, JournalState& state, IJournalStore& journal, const CopyOptions& options,
                      const std::atomic_bool& cancel, const std::function<void(qint64, qint64)>& progress)
{
    CopyOutcome out;
    const qint64 sector = device.sectorSize();
    if (const QString why = validateMove(state.srcOffset, state.dstOffset, state.length, sector); !why.isEmpty())
    {
        out.error = "Tham số dời không hợp lệ: " + why;
        return out;
    }
    state.sectorBytes = sector;
    if (state.unitBytes <= 0)
        state.unitBytes = safeUnitBytes(state.srcOffset, state.dstOffset, state.length, options.chunkBytes, sector);
    // Nhật ký do lần chạy trước (có thể dùng chunk khác) tạo ra: không bao giờ tin đơn vị lớn hơn mức lũy đẳng cho phép.
    state.unitBytes = std::min(state.unitBytes, safeUnitBytes(state.srcOffset, state.dstOffset, state.length, state.unitBytes, sector));
    if (state.doneBytes < 0 || state.doneBytes > state.length || state.doneBytes % state.unitBytes != 0)
    {
        // doneBytes luôn là bội của nhóm (bội của unit) do chính lớp này ghi; khác đi là nhật ký bị sửa/hỏng - KHÔNG đoán.
        if (state.doneBytes != state.length)
        {
            out.error = "Tiến độ trong nhật ký không khớp đơn vị chép - từ chối chạy tiếp để khỏi làm hỏng dữ liệu.";
            return out;
        }
    }

    const qint64 unit = state.unitBytes;
    const qint64 group = saveIntervalBytes(state.srcOffset, state.dstOffset, state.length, unit);
    const bool backward = state.backward();
    AlignedBuffer src(static_cast<size_t>(unit));
    AlignedBuffer check(static_cast<size_t>(unit));
    if (!src.ptr || !check.ptr)
    {
        out.error = "Không đủ bộ nhớ cho bộ đệm chép.";
        return out;
    }

    qint64 sinceSave = 0;
    qint64 lastSaved = state.doneBytes;
    QString err;
    while (state.doneBytes < state.length)
    {
        if (cancel.load())
        {
            // Dừng đúng ranh giới nhóm: lưu những gì đã xong (đã flush) để chạy tiếp được.
            if (state.doneBytes != lastSaved)
            {
                if (!device.flush(&err) || !journal.save(state, &err))
                {
                    out.error = "Không ghi được nhật ký khi dừng: " + err;
                    out.result = CopyResult::Failed;
                    out.doneBytes = lastSaved;
                    return out;
                }
            }
            out.result = CopyResult::Cancelled;
            out.doneBytes = state.doneBytes;
            return out;
        }

        const qint64 n = std::min(unit, state.length - state.doneBytes);
        const qint64 rel = backward ? state.length - state.doneBytes - n : state.doneBytes;
        if (!device.read(state.srcOffset + rel, src.ptr, n, &err))
        {
            out.error = QStringLiteral("Lỗi đọc tại vị trí %1: %2").arg(state.srcOffset + rel).arg(err);
            out.doneBytes = lastSaved;
            return out;
        }
        if (!device.write(state.dstOffset + rel, src.ptr, n, &err))
        {
            out.error = QStringLiteral("Lỗi ghi tại vị trí %1: %2").arg(state.dstOffset + rel).arg(err);
            out.doneBytes = lastSaved;
            return out;
        }
        if (options.verifyReadback)
        {
            if (!device.read(state.dstOffset + rel, check.ptr, n, &err))
            {
                out.error = QStringLiteral("Lỗi đọc lại tại vị trí %1: %2").arg(state.dstOffset + rel).arg(err);
                out.doneBytes = lastSaved;
                return out;
            }
            if (std::memcmp(src.ptr, check.ptr, static_cast<size_t>(n)) != 0)
            {
                out.error = QStringLiteral("Dữ liệu đọc lại tại vị trí %1 KHÁC dữ liệu đã ghi - dừng ngay (đĩa/bộ nhớ có thể đang lỗi).")
                                .arg(state.dstOffset + rel);
                out.doneBytes = lastSaved;
                return out;
            }
        }
        state.doneBytes += n;
        sinceSave += n;
        if (sinceSave >= group || state.doneBytes >= state.length)
        {
            if (!device.flush(&err))
            {
                state.doneBytes = lastSaved; // chưa chắc đã bền - coi như chưa xong nhóm này
                out.error = "Không flush được xuống đĩa: " + err;
                out.doneBytes = lastSaved;
                return out;
            }
            if (state.doneBytes >= state.length)
                state.phase = JournalState::Copied;
            if (!journal.save(state, &err))
            {
                out.error = "Không ghi được nhật ký: " + err;
                out.doneBytes = lastSaved;
                return out;
            }
            lastSaved = state.doneBytes;
            sinceSave = 0;
        }
        if (progress)
            progress(state.doneBytes, state.length);
    }
    out.result = CopyResult::Done;
    out.doneBytes = state.doneBytes;
    return out;
}

bool patchNtfsHiddenSectors(QByteArray* sector, quint64 startLba)
{
    if (!sector || sector->size() < 512)
        return false;
    if (std::memcmp(sector->constData() + 3, "NTFS    ", 8) != 0)
        return false;
    if (startLba > 0xFFFFFFFFULL)
        return true; // trường chỉ 32 bit: để nguyên (Windows cũng chỉ ghi được phần thấp)
    char* p = sector->data() + 0x1C;
    const quint32 v = static_cast<quint32>(startLba);
    p[0] = static_cast<char>(v & 0xFF);
    p[1] = static_cast<char>((v >> 8) & 0xFF);
    p[2] = static_cast<char>((v >> 16) & 0xFF);
    p[3] = static_cast<char>((v >> 24) & 0xFF);
    return true;
}

qint64 readNtfsHiddenSectors(const QByteArray& sector)
{
    if (sector.size() < 512 || std::memcmp(sector.constData() + 3, "NTFS    ", 8) != 0)
        return -1;
    const auto* p = reinterpret_cast<const unsigned char*>(sector.constData() + 0x1C);
    return static_cast<qint64>(p[0]) | (static_cast<qint64>(p[1]) << 8) | (static_cast<qint64>(p[2]) << 16) | (static_cast<qint64>(p[3]) << 24);
}

bool patchNtfsBootSectorsAt(IBlockDevice& device, qint64 dstOffset, qint64 length, QString* error)
{
    const qint64 sector = device.sectorSize();
    AlignedBuffer buf(static_cast<size_t>(sector));
    if (!buf.ptr)
    {
        if (error)
            *error = "Không đủ bộ nhớ.";
        return false;
    }
    QString err;
    auto patchAt = [&](qint64 offset, bool mustBeNtfs) {
        if (!device.read(offset, buf.ptr, sector, &err))
            return false;
        QByteArray s(buf.ptr, static_cast<int>(sector));
        if (!patchNtfsHiddenSectors(&s, static_cast<quint64>(dstOffset / sector)))
        {
            if (mustBeNtfs)
                err = "Boot sector tại vị trí mới không phải NTFS.";
            return !mustBeNtfs;
        }
        std::memcpy(buf.ptr, s.constData(), static_cast<size_t>(sector));
        return device.write(offset, buf.ptr, sector, &err);
    };
    if (!patchAt(dstOffset, true) || !patchAt(dstOffset + length - sector, false) || !device.flush(&err))
    {
        if (error)
            *error = err;
        return false;
    }
    return true;
}
} // namespace PartitionMove
