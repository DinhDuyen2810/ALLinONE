// Kiểm thử lõi dời phân vùng (PartitionMoveCore): chép chồng lấn đúng chiều, lũy đẳng khi chép lại, nhật ký phục hồi, vá boot sector
// NTFS - tất cả trên thiết bị khối GIẢ (bộ nhớ/tệp tạm do chính test tạo). KHÔNG đụng đĩa thật.
// Có "giả lập sự cố": ghi dở một khối (torn write) / mất nhật ký ở MỌI điểm rồi chạy tiếp từ nhật ký và so kết quả với đáp án.
// Build: cmake --build build --target partition_move_tests && build\partition_move_tests.exe
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include <windows.h>
#include <winioctl.h>
#include <cstddef>

#include "core/AppPaths.h"
#include "tools/diskcleanup/engine/PartitionMoveCore.h"
#include "tools/diskcleanup/engine/PartitionMover.h"

using namespace PartitionMove;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

namespace
{
/// Thiết bị trong bộ nhớ, có thể "hỏng" ở lần ghi thứ N (ghi dở nửa đầu khối rồi báo lỗi - mô phỏng mất điện giữa lúc ghi).
class MemDevice : public IBlockDevice
{
public:
    MemDevice(qint64 size, qint64 sector = 512) : m_data(static_cast<size_t>(size)), m_sector(sector) {}
    qint64 sectorSize() const override { return m_sector; }
    bool read(qint64 offset, char* buffer, qint64 length, QString* error) override
    {
        if (offset < 0 || offset + length > static_cast<qint64>(m_data.size()) || offset % m_sector || length % m_sector)
        {
            if (error) *error = "doc ngoai pham vi/lech sector";
            return false;
        }
        std::memcpy(buffer, m_data.data() + offset, static_cast<size_t>(length));
        ++reads;
        return true;
    }
    bool write(qint64 offset, const char* buffer, qint64 length, QString* error) override
    {
        if (offset < 0 || offset + length > static_cast<qint64>(m_data.size()) || offset % m_sector || length % m_sector)
        {
            if (error) *error = "ghi ngoai pham vi/lech sector";
            return false;
        }
        ++writes;
        if (failAtWrite > 0 && writes == failAtWrite)
        {
            const qint64 half = (length / m_sector / 2) * m_sector; // ghi dở: chỉ nửa đầu (có thể 0 sector)
            std::memcpy(m_data.data() + offset, buffer, static_cast<size_t>(half));
            if (error) *error = "mat dien giua luc ghi";
            return false;
        }
        std::memcpy(m_data.data() + offset, buffer, static_cast<size_t>(length));
        if (corruptReadbackAtWrite > 0 && writes == corruptReadbackAtWrite)
            m_data[static_cast<size_t>(offset)] ^= 0x5A; // bộ nhớ/đĩa "lật bit" sau khi ghi - kiểm tra đọc lại phải bắt được
        return true;
    }
    bool flush(QString* error) override
    {
        ++flushes;
        if (failFlushAt > 0 && flushes == failFlushAt)
        {
            if (error) *error = "flush loi";
            return false;
        }
        return true;
    }
    std::vector<char>& data() { return m_data; }
    int reads{0}, writes{0}, flushes{0};
    int failAtWrite{0}, failFlushAt{0}, corruptReadbackAtWrite{0};

private:
    std::vector<char> m_data;
    qint64 m_sector;
};

/// Kho nhật ký trong bộ nhớ: chỉ giữ bản lưu GẦN NHẤT (như tệp thật), có thể hỏng ở lần lưu thứ N.
class MemJournal : public IJournalStore
{
public:
    bool save(const JournalState& s, QString* error) override
    {
        ++saves;
        if (failAtSave > 0 && saves == failAtSave)
        {
            if (error) *error = "mat dien giua luc ghi nhat ky";
            return false; // bản lưu trước đó còn nguyên (ghi nguyên tử: temp + rename)
        }
        last = s;
        haveLast = true;
        return true;
    }
    JournalState last;
    bool haveLast{false};
    int saves{0};
    int failAtSave{0};
};

std::vector<char> randomBytes(size_t n, unsigned seed)
{
    std::mt19937 rng(seed);
    std::vector<char> v(n);
    for (auto& c : v)
        c = static_cast<char>(rng() & 0xFF);
    return v;
}

struct Scenario
{
    qint64 deviceSize;
    qint64 src, dst, length;
    qint64 chunk;
};

JournalState freshState(const Scenario& sc, qint64 sector = 512)
{
    JournalState st;
    st.srcOffset = sc.src;
    st.dstOffset = sc.dst;
    st.length = sc.length;
    st.sectorBytes = sector;
    return st;
}

/// Đáp án: vùng đích = dữ liệu nguồn ban đầu; mọi byte NGOÀI hợp của (nguồn, đích) giữ nguyên.
bool matchesExpected(const std::vector<char>& after, const std::vector<char>& before, const Scenario& sc)
{
    for (qint64 i = 0; i < sc.deviceSize; ++i)
    {
        const bool inDst = i >= sc.dst && i < sc.dst + sc.length;
        const bool inSrc = i >= sc.src && i < sc.src + sc.length;
        if (inDst)
        {
            if (after[static_cast<size_t>(i)] != before[static_cast<size_t>(i - sc.dst + sc.src)])
                return false;
        }
        else if (!inSrc)
        {
            if (after[static_cast<size_t>(i)] != before[static_cast<size_t>(i)])
                return false;
        }
    }
    return true;
}

/// Chạy chép tới khi xong; sau MỖI sự cố "khởi động lại" từ nhật ký đã lưu gần nhất (như tiến trình mới sau khi mất điện).
/// Trả về true nếu cuối cùng Done và kết quả đúng đáp án. crashes = số lần phải chạy lại.
bool runWithCrashes(const Scenario& sc, const std::vector<char>& before, int failWrite, int failSave, int failFlush, int* crashes, bool* gaveUp)
{
    MemDevice dev(sc.deviceSize);
    dev.data() = before;
    MemJournal journal;
    CopyOptions opt;
    opt.chunkBytes = sc.chunk;
    std::atomic_bool noCancel{false};

    JournalState st = freshState(sc);
    *crashes = 0;
    for (int attempt = 0; attempt < 12; ++attempt)
    {
        if (attempt == 0)
        {
            dev.failAtWrite = failWrite;
            journal.failAtSave = failSave;
            dev.failFlushAt = failFlush;
        }
        else
        {
            // "Khởi động lại": nhật ký gần nhất đã lưu (hoặc nhật ký rỗng nếu chưa kịp lưu lần nào) + thiết bị không còn lỗi.
            dev.failAtWrite = 0;
            journal.failAtSave = 0;
            dev.failFlushAt = 0;
            st = journal.haveLast ? journal.last : freshState(sc);
            // Qua JSON như thật: nhật ký nằm trong tệp.
            JournalState parsed;
            if (journal.haveLast)
            {
                QString why;
                if (!JournalState::fromJson(st.toJson(), &parsed, &why))
                {
                    std::printf("  nhat ky khong doc lai duoc: %s\n", qPrintable(why));
                    return false;
                }
                st = parsed;
            }
        }
        const CopyOutcome out = copyRange(dev, st, journal, opt, noCancel);
        if (out.result == CopyResult::Done)
            return matchesExpected(dev.data(), before, sc);
        ++*crashes;
    }
    *gaveUp = true;
    return false;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- validateMove / safeUnitBytes / saveIntervalBytes (thuần) ----
    {
        CHECK(validateMove(0, 4096, 8192, 512).isEmpty());
        CHECK(!validateMove(0, 0, 8192, 512).isEmpty());          // không đổi vị trí
        CHECK(!validateMove(0, 4096, 0, 512).isEmpty());          // độ dài 0
        CHECK(!validateMove(0, 4096, 8192 + 100, 512).isEmpty()); // lệch sector
        CHECK(!validateMove(100, 4096, 8192, 512).isEmpty());
        CHECK(!validateMove(-512, 4096, 8192, 512).isEmpty());
        CHECK(!validateMove(0, 4096, 8192, 100).isEmpty());       // sector không phải lũy thừa 2
        CHECK(!validateMove(0, 4096, 8192, 0).isEmpty());
        // Đơn vị: chồng lấn -> không dài hơn khoảng dời; không chồng -> theo chunk.
        CHECK(safeUnitBytes(0, 4096, 1 << 20, 16 * kMiB, 512) == 4096);
        CHECK(safeUnitBytes(4096, 0, 1 << 20, 16 * kMiB, 512) == 4096);
        CHECK(safeUnitBytes(0, 10 * kMiB, 100 * kMiB, 16 * kMiB, 512) == 10 * kMiB);   // chồng: bị kẹp bởi khoảng dời
        CHECK(safeUnitBytes(0, 50 * kMiB, 100 * kMiB, 16 * kMiB, 512) == 16 * kMiB);   // chồng nhưng khoảng dời lớn hơn chunk
        CHECK(safeUnitBytes(0, 200 * kMiB, 100 * kMiB, 16 * kMiB, 512) == 16 * kMiB);  // không chồng
        CHECK(safeUnitBytes(0, 512, 4096, 16 * kMiB, 512) == 512);
        CHECK(safeUnitBytes(0, 1024, 100000 * 512, 3000, 512) == 1024);
        // Nhóm lưu nhật ký: bội của unit; chồng lấn thì <= khoảng dời.
        CHECK(saveIntervalBytes(0, 4096, 1 << 20, 4096) == 4096);
        CHECK(saveIntervalBytes(0, 10 * kMiB, 100 * kMiB, 4 * kMiB) == 8 * kMiB);       // bội của 4 MiB, <= 10 MiB
        CHECK(saveIntervalBytes(0, 1000 * kMiB, 100 * kMiB, 16 * kMiB) == 128 * kMiB);  // không chồng: tối đa 128 MiB
        CHECK(saveIntervalBytes(0, 1000 * kMiB, 100 * kMiB, 16 * kMiB) % (16 * kMiB) == 0);
        CHECK(saveIntervalBytes(0, 600 * kMiB, 1000 * kMiB, 16 * kMiB) == 128 * kMiB);
        CHECK(saveIntervalBytes(0, 64 * kMiB, 1000 * kMiB, 16 * kMiB) == 64 * kMiB);    // chồng: kẹp bằng khoảng dời
    }

    // ---- JournalState JSON ----
    {
        JournalState s;
        s.diskNumber = 3;
        s.diskId = "{1234-ABCD}";
        s.partitionId = "{AAAA}";
        s.partitionNumber = 4;
        s.srcOffset = 123456789012LL * 4096;
        s.dstOffset = 123456789012LL * 4096 + 1048576;
        s.length = 5LL * 1024 * 1024 * 1024 * 1024; // 5 TiB: vượt xa 2^32
        s.unitBytes = 16 * kMiB;
        s.doneBytes = 3 * 16 * kMiB;
        s.sectorBytes = 4096;
        s.phase = JournalState::Copying;
        s.startedAt = "2026-11-09T10:00:00";
        s.note = "thử \"nháy\" và tiếng Việt";
        JournalState back;
        QString why;
        CHECK(JournalState::fromJson(s.toJson(), &back, &why));
        CHECK(back.diskNumber == 3 && back.diskId == s.diskId && back.partitionId == s.partitionId && back.partitionNumber == 4);
        CHECK(back.srcOffset == s.srcOffset && back.dstOffset == s.dstOffset && back.length == s.length);
        CHECK(back.unitBytes == s.unitBytes && back.doneBytes == s.doneBytes && back.sectorBytes == 4096 && back.phase == 0);
        CHECK(back.startedAt == s.startedAt && back.note == s.note);
        CHECK(back.overlapping() && back.backward()); // đích > nguồn và chồng nhau -> chép LÙI từ cuối
        // Hỏng/sửa bậy bị từ chối (không bao giờ dùng nhật ký sai).
        JournalState junk;
        CHECK(!JournalState::fromJson("khong phai json", &junk, &why));
        CHECK(!JournalState::fromJson("[]", &junk, &why));
        CHECK(!JournalState::fromJson("{}", &junk, &why));
        {
            JournalState bad = s;
            bad.doneBytes = bad.length + 512; // tiến độ vượt độ dài
            CHECK(!JournalState::fromJson(bad.toJson(), &junk, &why));
            bad = s;
            bad.unitBytes = 0;
            CHECK(!JournalState::fromJson(bad.toJson(), &junk, &why));
            bad = s;
            bad.phase = 9;
            CHECK(!JournalState::fromJson(bad.toJson(), &junk, &why));
            bad = s;
            bad.dstOffset = bad.srcOffset; // vị trí trùng
            CHECK(!JournalState::fromJson(bad.toJson(), &junk, &why));
            bad = s;
            bad.srcOffset += 7; // lệch sector
            CHECK(!JournalState::fromJson(bad.toJson(), &junk, &why));
            QByteArray j = s.toJson();
            j.replace("\"version\": 1", "\"version\": 2");
            CHECK(!JournalState::fromJson(j, &junk, &why));
        }
        // backward(): đích > nguồn và chồng nhau
        JournalState r;
        r.srcOffset = 0; r.dstOffset = 1024; r.length = 4096;
        CHECK(r.backward() && r.overlapping());
        r.dstOffset = 8192;
        CHECK(!r.backward() && !r.overlapping());
        r.srcOffset = 8192; r.dstOffset = 4096; r.length = 8192; // đích < nguồn, chồng nhau -> chép TĂNG
        CHECK(!r.backward() && r.overlapping());
    }

    // ---- Chép đúng ở mọi hình học (không sự cố) ----
    {
        const qint64 K = 1024;
        std::vector<Scenario> scenarios;
        for (qint64 chunk : {512LL, 4 * K, 16 * K, 64 * K, 1024 * K})
            for (qint64 delta : {512LL, 4 * K, 8 * K, 24 * K, 99 * K, 100 * K, 101 * K, 150 * K})
            {
                scenarios.push_back({1024 * K, 300 * K, 300 * K - delta, 100 * K, chunk}); // dời TRÁI (chép tăng)
                scenarios.push_back({1024 * K, 300 * K, 300 * K + delta, 100 * K, chunk}); // dời PHẢI (chép lùi khi chồng)
            }
        scenarios.push_back({256 * K, 0, 100 * K, 100 * K, 4 * K});     // sát đầu thiết bị
        scenarios.push_back({256 * K, 156 * K, 56 * K, 100 * K, 4 * K}); // sát cuối thiết bị
        scenarios.push_back({256 * K, 100 * K, 156 * K, 100 * K, 4 * K});
        scenarios.push_back({256 * K, 512, 1024, 512, 512});            // một sector
        int ok = 0;
        for (const Scenario& sc : scenarios)
        {
            const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 12345u + static_cast<unsigned>(sc.chunk + sc.dst));
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            MemJournal journal;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            JournalState st = freshState(sc);
            std::atomic_bool noCancel{false};
            qint64 lastDone = -1, lastTotal = -1;
            int progressCalls = 0;
            const CopyOutcome out = copyRange(dev, st, journal, opt, noCancel, [&](qint64 d, qint64 t) { lastDone = d; lastTotal = t; ++progressCalls; });
            const bool good = out.result == CopyResult::Done && matchesExpected(dev.data(), before, sc) && st.doneBytes == sc.length &&
                              st.phase == JournalState::Copied && journal.haveLast && journal.last.phase == JournalState::Copied &&
                              journal.last.doneBytes == sc.length && lastDone == sc.length && lastTotal == sc.length && progressCalls > 0;
            if (!good)
                std::printf("  sai o kich ban: src=%lld dst=%lld len=%lld chunk=%lld (%s)\n", static_cast<long long>(sc.src), static_cast<long long>(sc.dst),
                            static_cast<long long>(sc.length), static_cast<long long>(sc.chunk), qPrintable(out.error));
            CHECK(good);
            ok += good ? 1 : 0;
            // Đơn vị đã dùng không bao giờ dài hơn khoảng dời khi chồng lấn.
            if (st.overlapping())
                CHECK(st.unitBytes <= (sc.dst > sc.src ? sc.dst - sc.src : sc.src - sc.dst));
        }
        std::printf("Chep khong su co: %d/%d kich ban dung\n", ok, static_cast<int>(scenarios.size()));
    }

    // ---- SỰ CỐ ở mọi điểm: ghi dở một khối / mất nhật ký / flush lỗi -> chạy tiếp từ nhật ký -> kết quả vẫn ĐÚNG ----
    {
        const qint64 K = 1024;
        std::vector<Scenario> scenarios = {
            {400 * K, 150 * K, 150 * K - 512, 100 * K, 4 * K},   // dời trái 1 sector: đơn vị 512 byte (khắc nghiệt nhất)
            {400 * K, 150 * K, 150 * K + 512, 100 * K, 4 * K},   // dời phải 1 sector (chép lùi)
            {400 * K, 150 * K, 150 * K - 8 * K, 100 * K, 16 * K}, // đơn vị bị kẹp 8 KiB
            {400 * K, 150 * K, 150 * K + 24 * K, 100 * K, 16 * K},
            {400 * K, 150 * K, 150 * K + 60 * K, 100 * K, 16 * K}, // chồng một phần lớn
            {400 * K, 100 * K, 250 * K, 100 * K, 16 * K},          // KHÔNG chồng
            {400 * K, 250 * K, 100 * K, 100 * K, 16 * K},
        };
        long long runs = 0, bad = 0, gaveUp = 0, withCrash = 0;
        for (const Scenario& sc : scenarios)
        {
            const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 777u + static_cast<unsigned>(sc.dst));
            // Đếm số lần ghi/lưu/flush của một lượt sạch để biết cần thử bao nhiêu điểm sự cố.
            MemDevice probe(sc.deviceSize);
            probe.data() = before;
            MemJournal pj;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            JournalState ps = freshState(sc);
            std::atomic_bool nc{false};
            copyRange(probe, ps, pj, opt, nc);
            const int writes = probe.writes, saves = pj.saves, flushes = probe.flushes;
            CHECK(writes > 0 && saves > 0 && flushes >= saves);
            for (int w = 1; w <= writes; ++w) // hỏng ở lần ghi thứ w (ghi dở nửa khối)
            {
                int crashes = 0;
                bool gave = false;
                const bool good = runWithCrashes(sc, before, w, 0, 0, &crashes, &gave);
                ++runs;
                bad += good ? 0 : 1;
                gaveUp += gave ? 1 : 0;
                withCrash += crashes > 0 ? 1 : 0;
            }
            for (int s = 1; s <= saves; ++s) // mất nhật ký ở lần lưu thứ s (dữ liệu của nhóm đó đã ghi xong)
            {
                int crashes = 0;
                bool gave = false;
                const bool good = runWithCrashes(sc, before, 0, s, 0, &crashes, &gave);
                ++runs;
                bad += good ? 0 : 1;
                gaveUp += gave ? 1 : 0;
                withCrash += crashes > 0 ? 1 : 0;
            }
            for (int f = 1; f <= flushes; ++f) // flush lỗi
            {
                int crashes = 0;
                bool gave = false;
                const bool good = runWithCrashes(sc, before, 0, 0, f, &crashes, &gave);
                ++runs;
                bad += good ? 0 : 1;
                gaveUp += gave ? 1 : 0;
                withCrash += crashes > 0 ? 1 : 0;
            }
        }
        std::printf("Su co moi diem: %lld lan chay, %lld co su co that, %lld sai, %lld bo cuoc\n", runs, withCrash, bad, gaveUp);
        CHECK(bad == 0 && gaveUp == 0 && withCrash > 100);
    }

    // ---- Hai sự cố LIÊN TIẾP (mất điện rồi lại mất điện lúc chạy tiếp) ----
    {
        const qint64 K = 1024;
        const Scenario sc{400 * K, 150 * K, 150 * K - 4 * K, 100 * K, 16 * K};
        const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 4242u);
        MemDevice dev(sc.deviceSize);
        dev.data() = before;
        MemJournal journal;
        CopyOptions opt;
        opt.chunkBytes = sc.chunk;
        std::atomic_bool nc{false};
        JournalState st = freshState(sc);
        std::mt19937 rng(99);
        bool done = false;
        int rounds = 0;
        for (; rounds < 400 && !done; ++rounds)
        {
            dev.writes = 0;
            journal.saves = 0;
            dev.failAtWrite = 1 + static_cast<int>(rng() % 9); // thất bại rất sớm trong mỗi lượt
            journal.failAtSave = (rng() % 3 == 0) ? 1 + static_cast<int>(rng() % 3) : 0;
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            if (out.result == CopyResult::Done)
            {
                done = true;
                break;
            }
            st = journal.haveLast ? journal.last : freshState(sc); // khởi động lại từ nhật ký đã lưu
        }
        CHECK(done);
        CHECK(matchesExpected(dev.data(), before, sc));
        std::printf("Su co lien tiep: xong sau %d lan khoi dong lai\n", rounds);
    }

    // ---- Kiểm tra đọc lại bắt được dữ liệu bị lật bit; lỗi đọc/ghi/nhật ký báo đúng ----
    {
        const qint64 K = 1024;
        const Scenario sc{400 * K, 150 * K, 150 * K - 4 * K, 100 * K, 16 * K};
        const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 55u);
        std::atomic_bool nc{false};
        {
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            dev.corruptReadbackAtWrite = 3;
            MemJournal journal;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            JournalState st = freshState(sc);
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            // Sự cố bị bắt ngay ở lần ghi 3 (đọc lại KHÁC), không đi tiếp; nhật ký không ghi quá chỗ đã chắc chắn.
            CHECK(out.result == CopyResult::Failed && out.error.contains("KHÁC"));
            CHECK(!journal.haveLast || journal.last.doneBytes < 3 * st.unitBytes + 1);
        }
        {
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            dev.corruptReadbackAtWrite = 3;
            MemJournal journal;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            opt.verifyReadback = false; // đối chứng: tắt kiểm tra thì lỗi lọt qua - chứng tỏ kiểm tra là thứ bắt được nó
            JournalState st = freshState(sc);
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            CHECK(out.result == CopyResult::Done && !matchesExpected(dev.data(), before, sc));
        }
        {
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            MemJournal journal;
            CopyOptions opt;
            JournalState st = freshState(sc);
            st.length = sc.deviceSize; // vượt phạm vi thiết bị -> lỗi ghi/đọc rõ ràng, không crash
            st.dstOffset = 1024;
            st.srcOffset = 0;
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            CHECK(out.result == CopyResult::Failed && !out.error.isEmpty());
        }
        {
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            MemJournal journal;
            CopyOptions opt;
            JournalState st = freshState(sc);
            st.dstOffset = st.srcOffset; // tham số sai bị từ chối trước khi đụng thiết bị
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            CHECK(out.result == CopyResult::Failed && dev.writes == 0 && dev.reads == 0);
        }
        {
            // Nhật ký bị sửa (doneBytes không khớp đơn vị) -> từ chối chạy tiếp, KHÔNG ghi gì.
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            MemJournal journal;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            JournalState st = freshState(sc);
            st.unitBytes = 4 * K;
            st.doneBytes = 1000; // không phải bội của đơn vị
            const CopyOutcome out = copyRange(dev, st, journal, opt, nc);
            CHECK(out.result == CopyResult::Failed && dev.writes == 0);
        }
    }

    // ---- Dừng theo yêu cầu rồi chạy tiếp ----
    {
        const qint64 K = 1024;
        for (qint64 delta : {-4 * K, 4 * K, -150 * K, 150 * K})
        {
            const Scenario sc{600 * K, 250 * K, 250 * K + delta, 100 * K, 8 * K};
            const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 31337u);
            MemDevice dev(sc.deviceSize);
            dev.data() = before;
            MemJournal journal;
            CopyOptions opt;
            opt.chunkBytes = sc.chunk;
            JournalState st = freshState(sc);
            std::atomic_bool cancel{false};
            int calls = 0;
            const CopyOutcome first = copyRange(dev, st, journal, opt, cancel, [&](qint64, qint64) {
                if (++calls == 5)
                    cancel = true;
            });
            CHECK(first.result == CopyResult::Cancelled);
            CHECK(journal.haveLast && journal.last.doneBytes == st.doneBytes && st.doneBytes > 0 && st.doneBytes < sc.length);
            CHECK(journal.last.phase == JournalState::Copying);
            // Chạy tiếp từ bản nhật ký (qua JSON).
            JournalState resumed;
            CHECK(JournalState::fromJson(journal.last.toJson(), &resumed, nullptr));
            cancel = false;
            const CopyOutcome second = copyRange(dev, resumed, journal, opt, cancel);
            CHECK(second.result == CopyResult::Done && matchesExpected(dev.data(), before, sc));
            // Dừng ngay từ đầu: không đụng thiết bị.
            MemDevice dev2(sc.deviceSize);
            dev2.data() = before;
            MemJournal j2;
            JournalState st2 = freshState(sc);
            std::atomic_bool stopped{true};
            const CopyOutcome none = copyRange(dev2, st2, j2, opt, stopped);
            CHECK(none.result == CopyResult::Cancelled && dev2.writes == 0 && dev2.reads == 0 && st2.doneBytes == 0);
        }
    }

    // ---- Chép lại một phần ĐÃ XONG (chạy tiếp khi nhật ký đã ở cuối) ----
    {
        const qint64 K = 1024;
        const Scenario sc{300 * K, 100 * K, 100 * K - 4 * K, 100 * K, 16 * K};
        const auto before = randomBytes(static_cast<size_t>(sc.deviceSize), 8u);
        MemDevice dev(sc.deviceSize);
        dev.data() = before;
        MemJournal journal;
        CopyOptions opt;
        opt.chunkBytes = sc.chunk;
        JournalState st = freshState(sc);
        std::atomic_bool nc{false};
        CHECK(copyRange(dev, st, journal, opt, nc).result == CopyResult::Done);
        const int writesBefore = dev.writes;
        CHECK(copyRange(dev, st, journal, opt, nc).result == CopyResult::Done && dev.writes == writesBefore); // không ghi gì thêm
    }

    // ---- Boot sector NTFS: vá "Hidden sectors" ở sector đầu và bản sao lưu ở sector cuối ----
    {
        auto makeVbr = [](quint32 hidden, bool ntfs) {
            QByteArray v(512, '\0');
            std::memcpy(v.data() + 3, ntfs ? "NTFS    " : "EXFAT   ", 8);
            v[0x0B] = 0x00; v[0x0C] = 0x02; // 512 byte/sector
            v[0x1C] = static_cast<char>(hidden & 0xFF);
            v[0x1D] = static_cast<char>((hidden >> 8) & 0xFF);
            v[0x1E] = static_cast<char>((hidden >> 16) & 0xFF);
            v[0x1F] = static_cast<char>((hidden >> 24) & 0xFF);
            v[510] = char(0x55); v[511] = char(0xAA);
            return v;
        };
        CHECK(readNtfsHiddenSectors(makeVbr(2048, true)) == 2048);
        CHECK(readNtfsHiddenSectors(makeVbr(0xDEADBEEF, true)) == 0xDEADBEEFLL);
        CHECK(readNtfsHiddenSectors(makeVbr(2048, false)) == -1);
        CHECK(readNtfsHiddenSectors(QByteArray(100, 'x')) == -1);
        QByteArray v = makeVbr(2048, true);
        CHECK(patchNtfsHiddenSectors(&v, 1234567));
        CHECK(readNtfsHiddenSectors(v) == 1234567 && v[510] == char(0x55) && v[511] == char(0xAA) && v.mid(0, 0x1C) == makeVbr(0, true).mid(0, 0x1C));
        CHECK(patchNtfsHiddenSectors(&v, 0x1FFFFFFFFULL) && readNtfsHiddenSectors(v) == 1234567); // quá 32 bit: giữ nguyên
        QByteArray notNtfs = makeVbr(5, false);
        CHECK(!patchNtfsHiddenSectors(&notNtfs, 99) && readNtfsHiddenSectors(makeVbr(5, false)) == -1);
        QByteArray tiny(100, 'x');
        CHECK(!patchNtfsHiddenSectors(&tiny, 1) && !patchNtfsHiddenSectors(nullptr, 1));

        MemDevice dev(256 * 1024);
        const qint64 dst = 64 * 1024, len = 100 * 1024;
        auto put = [&](qint64 off, const QByteArray& b) { std::memcpy(dev.data().data() + off, b.constData(), 512); };
        auto get = [&](qint64 off) { return QByteArray(dev.data().data() + off, 512); };
        put(dst, makeVbr(100, true));
        put(dst + len - 512, makeVbr(100, true));
        QString err;
        CHECK(patchNtfsBootSectorsAt(dev, dst, len, &err));
        CHECK(readNtfsHiddenSectors(get(dst)) == dst / 512 && readNtfsHiddenSectors(get(dst + len - 512)) == dst / 512);
        // Bản sao lưu không phải NTFS: bỏ qua, không lỗi, không đụng tới.
        put(dst, makeVbr(100, true));
        QByteArray garbage(512, 'g');
        put(dst + len - 512, garbage);
        CHECK(patchNtfsBootSectorsAt(dev, dst, len, &err));
        CHECK(readNtfsHiddenSectors(get(dst)) == dst / 512 && get(dst + len - 512) == garbage);
        // Sector đầu không phải NTFS: lỗi, không ghi gì.
        put(dst, makeVbr(100, false));
        const int w = dev.writes;
        CHECK(!patchNtfsBootSectorsAt(dev, dst, len, &err) && err.contains("NTFS") && dev.writes == w);
    }

    // ---- Thiết bị tệp thật (QTemporaryDir): cùng thuật toán chạy trên tệp, sector 4096 ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        class FileDevice : public IBlockDevice
        {
        public:
            explicit FileDevice(const QString& path) : m_file(path) { m_ok = m_file.open(QIODevice::ReadWrite); }
            bool ok() const { return m_ok; }
            qint64 sectorSize() const override { return 4096; }
            bool read(qint64 offset, char* buffer, qint64 length, QString* error) override
            {
                if (!m_file.seek(offset) || m_file.read(buffer, length) != length)
                {
                    if (error) *error = m_file.errorString();
                    return false;
                }
                return true;
            }
            bool write(qint64 offset, const char* buffer, qint64 length, QString* error) override
            {
                if (!m_file.seek(offset) || m_file.write(buffer, length) != length)
                {
                    if (error) *error = m_file.errorString();
                    return false;
                }
                return true;
            }
            bool flush(QString*) override { return m_file.flush(); }
            QFile m_file;
            bool m_ok{false};
        };
        const qint64 size = 8 * kMiB;
        const QString path = tmp.path() + "/disk.img";
        const auto before = randomBytes(static_cast<size_t>(size), 2024u);
        {
            QFile f(path);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write(before.data(), static_cast<qint64>(before.size()));
        }
        FileDevice dev(path);
        CHECK(dev.ok());
        MemJournal journal;
        CopyOptions opt;
        opt.chunkBytes = 512 * 1024;
        JournalState st;
        st.srcOffset = 3 * kMiB;
        st.dstOffset = 2 * kMiB + 4096; // dời trái 1 MiB - 4096 byte, vùng 4 MiB (chồng lấn)
        st.length = 4 * kMiB;
        st.sectorBytes = 4096;
        std::atomic_bool nc{false};
        CHECK(copyRange(dev, st, journal, opt, nc).result == CopyResult::Done);
        dev.m_file.flush();
        QFile r(path);
        CHECK(r.open(QIODevice::ReadOnly));
        const QByteArray after = r.readAll();
        bool same = true;
        for (qint64 i = 0; i < st.length && same; i += 4096)
            same = std::memcmp(after.constData() + st.dstOffset + i, before.data() + st.srcOffset + i, 4096) == 0;
        CHECK(same && after.size() == size);
    }

    // ---- PartitionMover: lõi thuần của bảng phân vùng (bộ đệm DRIVE_LAYOUT_INFORMATION_EX dựng tay, KHÔNG đụng đĩa) ----
    {
        struct P
        {
            int number;
            qint64 start, length;
        };
        const qint64 diskSize = 100 * 1024 * kMiB;
        auto makeLayout = [&](bool gpt, const std::vector<P>& parts, size_t extraTail = 0) {
            const size_t size = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + parts.size() * sizeof(PARTITION_INFORMATION_EX);
            std::vector<char> raw(size + extraTail, 0x7E); // phần đuôi thừa: lấp đầy "rác" để chắc chắn không bị đọc/ghi nhầm
            std::memset(raw.data(), 0, size);
            auto* l = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(raw.data());
            l->PartitionStyle = gpt ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
            l->PartitionCount = static_cast<DWORD>(parts.size());
            if (gpt)
            {
                l->Gpt.StartingUsableOffset.QuadPart = kMiB;
                l->Gpt.UsableLength.QuadPart = diskSize - 2 * kMiB;
                l->Gpt.MaxPartitionCount = 128;
                l->Gpt.DiskId = GUID{0x11111111, 0x2222, 0x3333, {1, 2, 3, 4, 5, 6, 7, 8}};
            }
            else
                l->Mbr.Signature = 0xABCD1234;
            for (size_t i = 0; i < parts.size(); ++i)
            {
                PARTITION_INFORMATION_EX& e = l->PartitionEntry[i];
                e.PartitionStyle = gpt ? PARTITION_STYLE_GPT : PARTITION_STYLE_MBR;
                e.StartingOffset.QuadPart = parts[i].start;
                e.PartitionLength.QuadPart = parts[i].length;
                e.PartitionNumber = static_cast<DWORD>(parts[i].number);
                e.RewritePartition = FALSE;
                if (gpt)
                    e.Gpt.PartitionId = GUID{static_cast<unsigned long>(0xAAAA0000 + parts[i].number), 1, 2, {9, 9, 9, 9, 9, 9, 9, static_cast<unsigned char>(parts[i].number)}};
                else
                    e.Mbr.HiddenSectors = static_cast<DWORD>(parts[i].start / 512);
            }
            return raw;
        };
        const std::vector<P> layout = {{1, 1 * kMiB, 100 * kMiB}, {2, 101 * kMiB, 20 * 1024 * kMiB}, {3, 30000 * kMiB, 10 * 1024 * kMiB}};
        const auto gptRaw = makeLayout(true, layout, 300);
        const auto mbrRaw = makeLayout(false, layout);

        auto v = PartitionMover::internal::inspectLayout(gptRaw);
        CHECK(v.ok && v.gpt && v.count == 3 && v.usableStart == kMiB && v.usableLength == diskSize - 2 * kMiB && v.diskId.contains("11111111"));
        auto vm = PartitionMover::internal::inspectLayout(mbrRaw);
        CHECK(vm.ok && !vm.gpt && vm.count == 3 && vm.diskId == "MBR:ABCD1234");
        // Hỏng/không hiểu được: không ok, không crash.
        CHECK(!PartitionMover::internal::inspectLayout({}).ok);
        CHECK(!PartitionMover::internal::inspectLayout(std::vector<char>(10, 0)).ok);
        {
            auto bad = gptRaw;
            reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(bad.data())->PartitionCount = 100000; // vượt bộ đệm
            CHECK(!PartitionMover::internal::inspectLayout(bad).ok);
            auto raw2 = gptRaw;
            reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(raw2.data())->PartitionStyle = PARTITION_STYLE_RAW;
            CHECK(!PartitionMover::internal::inspectLayout(raw2).ok);
        }
        CHECK(PartitionMover::internal::layoutByteSize(gptRaw) == gptRaw.size() - 300); // bỏ phần đuôi thừa của bộ đệm GET
        CHECK(PartitionMover::internal::layoutByteSize({}) == 0);

        qint64 start = 0, length = 0;
        QString guid;
        CHECK(PartitionMover::internal::findEntry(gptRaw, 2, &start, &length, &guid) && start == 101 * kMiB && length == 20 * 1024 * kMiB && guid.contains("AAAA0002"));
        CHECK(!PartitionMover::internal::findEntry(gptRaw, 9, &start, &length, &guid));
        CHECK(PartitionMover::internal::findEntry(mbrRaw, 3, &start, &length, &guid) && guid.isEmpty() && start == 30000 * kMiB);

        // checkDestination: hợp lệ / chồng lên phân vùng khác / ngoài vùng dùng được / sát đầu đĩa / thiếu phân vùng.
        using PartitionMover::internal::checkDestination;
        const qint64 len2 = 20 * 1024 * kMiB;
        CHECK(checkDestination(gptRaw, 2, 101 * kMiB + 1024 * kMiB, len2, diskSize).isEmpty());           // dời phải 1 GiB (vẫn trống tới phân vùng 3)
        CHECK(checkDestination(gptRaw, 2, 101 * kMiB + 10000 * kMiB, len2, diskSize).contains("chồng"));  // lấn vào phân vùng 3
        CHECK(checkDestination(gptRaw, 2, 50 * kMiB, len2, diskSize).contains("chồng"));                 // lấn vào phân vùng 1
        CHECK(!checkDestination(gptRaw, 3, diskSize - 5 * 1024 * kMiB, 10 * 1024 * kMiB, diskSize).isEmpty());
        CHECK(!checkDestination(gptRaw, 2, 0, len2, diskSize).isEmpty());                                // sát đầu đĩa
        CHECK(!checkDestination(gptRaw, 9, 50000 * kMiB, len2, diskSize).isEmpty());                     // không có phân vùng 9
        CHECK(checkDestination(gptRaw, 3, 30000 * kMiB + 100 * kMiB, 10 * 1024 * kMiB, diskSize).isEmpty());
        CHECK(!checkDestination({}, 1, kMiB, kMiB, diskSize).isEmpty());
        // MBR: vượt cuối đĩa / vượt 2 TiB bị chặn.
        CHECK(checkDestination(mbrRaw, 3, 31000 * kMiB, 10 * 1024 * kMiB, diskSize).isEmpty());
        CHECK(!checkDestination(mbrRaw, 3, 95 * 1024 * kMiB, 10 * 1024 * kMiB, diskSize).isEmpty());
        CHECK(!checkDestination(mbrRaw, 3, 3LL * 1024 * 1024 * kMiB, 10 * 1024 * kMiB, 0).isEmpty());
        // Mục trống (độ dài 0) không bị coi là chồng.
        {
            auto withEmpty = makeLayout(false, {{1, 1 * kMiB, 100 * kMiB}, {0, 0, 0}, {3, 30000 * kMiB, 10 * 1024 * kMiB}, {0, 0, 0}});
            CHECK(checkDestination(withEmpty, 3, 31000 * kMiB, 10 * 1024 * kMiB, diskSize).isEmpty());
        }

        // patchStart: CHỈ đổi đúng mục được chọn (vị trí + RewritePartition; MBR thêm HiddenSectors); mọi byte còn lại giữ nguyên.
        for (bool gpt : {true, false})
        {
            auto raw = gpt ? gptRaw : mbrRaw;
            const auto before = raw;
            QString err;
            const qint64 newStart = 5000 * kMiB;
            CHECK(PartitionMover::internal::patchStart(&raw, 2, newStart, 512, &err));
            const auto* l = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(raw.data());
            const auto* lb = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(before.data());
            CHECK(l->PartitionEntry[1].StartingOffset.QuadPart == newStart && l->PartitionEntry[1].RewritePartition == TRUE);
            CHECK(l->PartitionEntry[1].PartitionLength.QuadPart == lb->PartitionEntry[1].PartitionLength.QuadPart);
            if (!gpt)
                CHECK(l->PartitionEntry[1].Mbr.HiddenSectors == static_cast<DWORD>(newStart / 512));
            // Các mục khác y nguyên từng byte.
            const size_t base = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
            for (int i : {0, 2})
                CHECK(std::memcmp(raw.data() + base + i * sizeof(PARTITION_INFORMATION_EX), before.data() + base + i * sizeof(PARTITION_INFORMATION_EX),
                                  sizeof(PARTITION_INFORMATION_EX)) == 0);
            CHECK(std::memcmp(raw.data(), before.data(), base) == 0); // phần đầu (kiểu bảng, số mục, GUID đĩa) y nguyên
            CHECK(!PartitionMover::internal::patchStart(&raw, 9, newStart, 512, &err) && !err.isEmpty());
        }
        {
            std::vector<char> tiny(3, 0);
            QString err;
            CHECK(!PartitionMover::internal::patchStart(&tiny, 1, 1, 512, &err) && !PartitionMover::internal::patchStart(nullptr, 1, 1, 512, &err));
        }

        // ---- notMovableReason ----
        auto info = [](const char* type, const char* fs, bool boot, bool sys, bool active, const char* letter) {
            PartitionManager::PartitionInfo p;
            p.diskNumber = 1;
            p.partitionNumber = 2;
            p.type = type;
            p.fileSystem = fs;
            p.isBoot = boot;
            p.isSystem = sys;
            p.isActive = active;
            p.driveLetter = letter;
            p.offsetBytes = 100 * kMiB;
            p.sizeBytes = 10 * 1024 * kMiB;
            return p;
        };
        CHECK(PartitionMover::notMovableReason(info("Basic", "NTFS", false, false, false, "")).isEmpty());
        CHECK(PartitionMover::notMovableReason(info("IFS", "NTFS", false, false, false, "Z")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Basic", "FAT32", false, false, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Basic", "", false, false, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Recovery", "NTFS", false, false, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("System", "FAT32", false, true, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Reserved", "", false, false, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Logical", "NTFS", false, false, false, "")).isEmpty());
        CHECK(!PartitionMover::notMovableReason(info("Basic", "NTFS", true, false, false, "")).isEmpty());   // khởi động
        CHECK(!PartitionMover::notMovableReason(info("Basic", "NTFS", false, true, false, "")).isEmpty());   // hệ thống
        CHECK(!PartitionMover::notMovableReason(info("IFS", "NTFS", false, false, true, "")).isEmpty());     // MBR đang hoạt động
        {
            const QString sysLetter = qEnvironmentVariable("SystemDrive").left(1);
            if (!sysLetter.isEmpty())
                CHECK(!PartitionMover::notMovableReason(info("Basic", "NTFS", false, false, false, qPrintable(sysLetter))).isEmpty()); // ổ Windows
        }
        {
            auto p = info("Basic", "NTFS", false, false, false, "");
            p.offsetBytes = -1;
            CHECK(!PartitionMover::notMovableReason(p).isEmpty());
        }

        // ---- Nhật ký trên đĩa: đọc/xóa, hỏng thì từ chối, mô tả, canAbandon ----
        {
            QTemporaryDir dataDir;
            CHECK(dataDir.isValid());
            AppPaths::setDataDirOverride(dataDir.path());
            JournalState out;
            QString err;
            CHECK(!PartitionMover::loadJournal(&out, &err) && err.isEmpty()); // chưa có nhật ký
            JournalState s;
            s.diskNumber = 2;
            s.partitionNumber = 3;
            s.srcOffset = 10 * 1024 * kMiB;
            s.dstOffset = 10 * 1024 * kMiB - 100 * kMiB;
            s.length = 20 * 1024 * kMiB;
            s.unitBytes = 16 * kMiB;
            s.doneBytes = 5 * 16 * kMiB;
            s.sectorBytes = 512;
            {
                QFile f(PartitionMover::journalPath());
                CHECK(f.open(QIODevice::WriteOnly));
                f.write(s.toJson());
            }
            CHECK(PartitionMover::loadJournal(&out, &err) && out.doneBytes == s.doneBytes && out.partitionNumber == 3);
            CHECK(PartitionMover::describeJournal(out).contains("Đĩa 2") && PartitionMover::describeJournal(out).contains("%"));
            CHECK(!PartitionMover::canAbandon(out)); // chồng lấn + đã ghi -> chỉ "Tiếp tục"
            JournalState fresh = s;
            fresh.doneBytes = 0;
            CHECK(PartitionMover::canAbandon(fresh)); // chưa ghi gì
            JournalState apart = s;
            apart.dstOffset = s.srcOffset + 30 * 1024 * kMiB; // không chồng lấn -> nguồn còn nguyên
            CHECK(PartitionMover::canAbandon(apart));
            {
                QFile f(PartitionMover::journalPath());
                CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
                f.write("{\"version\": 1, \"phase\": 0}"); // thiếu trường -> từ chối, báo lỗi
            }
            CHECK(!PartitionMover::loadJournal(&out, &err) && !err.isEmpty());
            PartitionMover::discardJournal();
            CHECK(!QFile::exists(PartitionMover::journalPath()));
            AppPaths::setDataDirOverride(QString());
        }
    }

    // ---- Ai đang giữ ổ: tiến trình chạy từ ổ + ứng dụng chạy từ ổ (lỗi thật người dùng gặp: "Không khóa được ổ ... Access is denied") ----
    {
        QTemporaryDir dataDir;
        CHECK(dataDir.isValid());
        AppPaths::setDataDirOverride(dataDir.path()); // runningFromDriveReason() hỏi thư mục dữ liệu - không đụng dữ liệu thật
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        const QString letter = exe.left(1);
        const QStringList procs = PartitionMover::processesRunningFromDrive(letter);
        bool self = false;
        for (const QString& p : procs)
            self = self || p.contains("CHÍNH ỨNG DỤNG NÀY");
        CHECK(self); // chính tiến trình test chạy từ ổ này và phải được nêu đích danh
        CHECK(PartitionMover::processesRunningFromDrive(letter.toLower()) == procs); // không phân biệt hoa thường
        CHECK(PartitionMover::processesRunningFromDrive(QString()).isEmpty() && PartitionMover::processesRunningFromDrive("#").isEmpty());
        const QString why = PartitionMover::runningFromDriveReason(letter);
        CHECK(why.contains("chạy từ chính ổ") && why.contains("C:\\OneForAll"));
        CHECK(PartitionMover::runningFromDriveReason(QString()).isEmpty());
        // Ổ khác với ổ chứa exe (và khác ổ chứa thư mục dữ liệu tạm): không có lý do.
        const QString tempLetter = QDir::toNativeSeparators(dataDir.path()).left(1).toUpper();
        for (char c = 'A'; c <= 'Z'; ++c)
        {
            const QString l = QString(QChar(c));
            if (l.compare(letter, Qt::CaseInsensitive) != 0 && l.compare(tempLetter, Qt::CaseInsensitive) != 0)
            {
                CHECK(PartitionMover::runningFromDriveReason(l).isEmpty());
                break;
            }
        }
        // Không có quyền Administrator: preflight từ chối ngay, không đụng đĩa.
        if (!PartitionManager::isElevated())
            CHECK(PartitionMover::preflightMove(0, 1, "Z").contains("Administrator"));
        AppPaths::setDataDirOverride(QString());
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
