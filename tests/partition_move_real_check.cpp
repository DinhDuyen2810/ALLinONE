// KIỂM TRA TAY (cần Administrator - bấm Yes ở UAC): dời phân vùng THẬT trên một ổ ĐĨA ẢO (VHDX) do chính công cụ này tạo trong thư mục tạm.
// KHÔNG chạy như test tự động (quy tắc 5 của CLAUDE.md). Không bao giờ đụng tới đĩa thật: chỉ thao tác trên số đĩa của VHDX vừa tạo, và từ chối
// nếu đĩa đó không phải loại "File Backed Virtual".
//
// Mỗi kịch bản dùng ĐÚNG mã của ứng dụng (PartitionMover::MoveWorker + PartitionManager::resizePartition + DiskLayout::planChange):
//   1. dời PHẢI (chồng lấn, chép lùi)     2. dời TRÁI (chồng lấn, chép tiến)    3. nhảy sang vùng trống KHÁC (không chồng)
//   4. kéo mép TRÁI sang trái (dời + mở rộng mép phải)                          5. kéo mép TRÁI sang phải (thu nhỏ mép phải + dời)
//   6. tạm dừng giữa chừng rồi chạy tiếp từ nhật ký
//   7. từ chối dời chồng lên phân vùng khác
//   8. (v1.33.2) 4 tiến trình con GIỮ ổ - đứng trong thư mục, mở tệp, mở thẳng cả ổ (đọc thô), ánh xạ tệp vào bộ nhớ: khóa ổ phải THẤT BẠI,
//      VolumeHolders::scan phải dò ra ĐỦ 4 PID, dừng được cả 4, rồi khóa + dời được
//   9. (v1.33.2) thứ giữ ổ KHÔNG dừng: chưa cho buộc tháo thì khóa thất bại; cho "buộc tháo ổ" (như chkdsk /x) thì khóa + dời được
// Chạy HAI lượt: đĩa GPT và đĩa MBR (MBR: Windows nhớ chữ ổ theo vị trí - kiểm chữ ổ cũ được trả lại sau mỗi lần dời).
// Sau mỗi kịch bản: dữ liệu (SHA-256 từng tệp) phải y nguyên, vị trí/kích thước đúng, ổ gắn lại có chữ ổ cũ, "Hidden sectors" của boot sector
// = vị trí mới, và chkdsk (chỉ đọc) báo sạch.
//
// Dùng: partition_move_real_check <tệp-nhật-ký.txt> [gpt|mbr]   (nên chạy qua:  Start-Process <exe> -ArgumentList <log> -Verb RunAs -Wait)
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "core/AppPaths.h"
#include "tools/diskcleanup/engine/DiskLayout.h"
#include "tools/diskcleanup/engine/PartitionManager.h"
#include "tools/diskcleanup/engine/PartitionMoveCore.h"
#include "tools/diskcleanup/engine/PartitionMover.h"
#include "tools/diskcleanup/engine/VolumeHolders.h"

#include <windows.h>

namespace
{
constexpr qint64 MiB = 1024LL * 1024;
QFile g_log;
int g_failed = 0, g_passed = 0;

void say(const QString& line)
{
    const QByteArray b = (line + "\n").toUtf8();
    if (g_log.isOpen())
    {
        g_log.write(b);
        g_log.flush();
    }
    std::fwrite(b.constData(), 1, static_cast<size_t>(b.size()), stdout);
    std::fflush(stdout);
}

void check(bool ok, const QString& what)
{
    if (ok)
        ++g_passed;
    else
        ++g_failed;
    say(QString("  [%1] %2").arg(ok ? "PASS" : "FAIL", what));
}

QString runProcess(const QString& program, const QStringList& args, int timeoutMs = 180000, int* exitCode = nullptr)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(10000) || !p.waitForFinished(timeoutMs))
    {
        p.kill();
        p.waitForFinished(3000);
        if (exitCode)
            *exitCode = -1;
        return "(lỗi/timeout)";
    }
    if (exitCode)
        *exitCode = p.exitCode();
    return QString::fromLocal8Bit(p.readAll());
}

QString diskpart(const QString& script)
{
    QTemporaryDir dir;
    const QString path = dir.path() + "/dp.txt";
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(script.toLocal8Bit());
    f.close();
    return runProcess(AppPaths::systemExecutable("diskpart.exe"), {"/s", QDir::toNativeSeparators(path)});
}

QString powershell(const QString& script)
{
    return runProcess(AppPaths::powershellExecutable(), {"-NoProfile", "-NonInteractive", "-Command", script}, 120000).trimmed();
}

QMap<QString, QByteArray> hashTree(const QString& root)
{
    QMap<QString, QByteArray> out;
    QDir base(root);
    std::vector<QString> stack{root};
    while (!stack.empty())
    {
        const QString dir = stack.back();
        stack.pop_back();
        for (const QFileInfo& fi : QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
        {
            if (fi.isDir())
            {
                stack.push_back(fi.absoluteFilePath());
                continue;
            }
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
            {
                out[base.relativeFilePath(fi.absoluteFilePath())] = "UNREADABLE";
                continue;
            }
            QCryptographicHash h(QCryptographicHash::Sha256);
            h.addData(&f);
            out[base.relativeFilePath(fi.absoluteFilePath())] = h.result().toHex();
        }
    }
    return out;
}

void fillData(const QString& root)
{
    std::mt19937 rng(20261109);
    QDir().mkpath(root + "/data/sub/deeper");
    auto writeRandom = [&](const QString& path, qint64 size) {
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        qint64 left = size;
        QByteArray chunk(1 << 16, 0);
        while (left > 0)
        {
            for (int i = 0; i < chunk.size(); i += 4)
            {
                const quint32 v = static_cast<quint32>(rng());
                std::memcpy(chunk.data() + i, &v, 4);
            }
            const qint64 n = std::min<qint64>(left, chunk.size());
            f.write(chunk.constData(), n);
            left -= n;
        }
    };
    for (int i = 0; i < 60; ++i)
        writeRandom(root + "/data/f" + QString::number(i) + ".bin", 1 + static_cast<qint64>(rng() % 200000));
    for (int i = 0; i < 12; ++i)
        writeRandom(root + "/data/sub/big" + QString::number(i) + ".bin", 1 * MiB + static_cast<qint64>(rng() % (6 * MiB)));
    for (int i = 0; i < 300; ++i)
        writeRandom(root + "/data/sub/deeper/tiny" + QString::number(i) + ".txt", static_cast<qint64>(rng() % 700));
    writeRandom(root + "/data/zero.txt", 0);
}

struct Env
{
    QString vhd;
    QString letter;
    int disk{-1};
    int partition{-1};
};

PartitionManager::PartitionInfo findPartition(const Env& env)
{
    QString err;
    for (const auto& p : PartitionManager::listPartitions(&err))
        if (p.diskNumber == env.disk && p.partitionNumber == env.partition)
            return p;
    return {};
}

QList<PartitionManager::PartitionInfo> diskPartitions(int disk)
{
    QList<PartitionManager::PartitionInfo> out;
    QString err;
    for (const auto& p : PartitionManager::listPartitions(&err))
        if (p.diskNumber == disk)
            out << p;
    return out;
}

/// Một lần dời qua MoveWorker (đồng bộ). pauseAfterBytes > 0: yêu cầu tạm dừng khi đã chép tới mức đó.
struct MoveResult
{
    bool ok{false};
    bool resumable{false};
    QString message;
    qint64 maxProgress{0};
};

MoveResult runMove(PartitionMover::MoveWorker& worker, qint64 pauseAfterBytes = 0)
{
    MoveResult r;
    bool done = false;
    QObject::connect(&worker, &PartitionMover::MoveWorker::moveFinished, &worker,
                     [&](bool ok, bool resumable, QString message) {
                         r.ok = ok;
                         r.resumable = resumable;
                         r.message = message;
                         done = true;
                     },
                     Qt::DirectConnection);
    QObject::connect(&worker, &PartitionMover::MoveWorker::progress, &worker,
                     [&](qint64 doneBytes, qint64 total, QString phase) {
                         Q_UNUSED(total);
                         r.maxProgress = std::max(r.maxProgress, doneBytes);
                         if (pauseAfterBytes > 0 && doneBytes >= pauseAfterBytes)
                             worker.requestPause();
                         if (doneBytes == 0)
                             say("    · " + phase);
                     },
                     Qt::DirectConnection);
    worker.start();
    worker.wait();
    QObject::disconnect(&worker, nullptr, nullptr, nullptr);
    if (!done)
        r.message = "luồng kết thúc mà không báo kết quả";
    return r;
}

/// Kiểm sau một kịch bản. wantStart/wantSize < 0 = không kiểm.
void verify(const Env& env, const QMap<QString, QByteArray>& manifest, qint64 wantStart, qint64 wantSize, const QString& title)
{
    say("  Kiểm tra sau: " + title);
    // Windows có thể cần vài giây để gắn lại chữ ổ.
    QElapsedTimer t;
    t.start();
    PartitionManager::PartitionInfo p;
    while (t.elapsed() < 30000)
    {
        p = findPartition(env);
        if (!p.driveLetter.isEmpty())
            break;
        QThread::msleep(500);
    }
    check(p.partitionNumber == env.partition, "phân vùng còn đó");
    if (wantStart >= 0)
        check(p.offsetBytes == wantStart, QString("vị trí = %1 MiB (thực tế %2 MiB)").arg(wantStart / MiB).arg(p.offsetBytes / MiB));
    if (wantSize >= 0)
        check(std::llabs(p.sizeBytes - wantSize) <= 8 * MiB, QString("kích thước ≈ %1 MiB (thực tế %2 MiB)").arg(wantSize / MiB).arg(p.sizeBytes / MiB));
    check(p.driveLetter.compare(env.letter, Qt::CaseInsensitive) == 0, QString("giữ chữ ổ %1: (thực tế \"%2\")").arg(env.letter, p.driveLetter));
    check(p.fileSystem == "NTFS", "hệ thống tệp vẫn là NTFS");
    const QString root = env.letter + ":/";
    auto windowsManaged = [](const QString& rel) {
        return rel.startsWith(QLatin1String("System Volume Information"), Qt::CaseInsensitive) ||
               rel.startsWith(QLatin1String("$RECYCLE.BIN"), Qt::CaseInsensitive);
    };
    const auto after = hashTree(root);
    int bad = 0, missing = 0, extra = 0;
    QStringList names;
    for (auto it = manifest.begin(); it != manifest.end(); ++it)
    {
        if (windowsManaged(it.key()))
            continue;
        if (!after.contains(it.key()))
        {
            ++missing;
            names << "thiếu " + it.key();
        }
        else if (after[it.key()] != it.value())
        {
            ++bad;
            names << "sai " + it.key();
        }
    }
    QStringList windowsExtras;
    for (auto it = after.begin(); it != after.end(); ++it)
        if (!manifest.contains(it.key()))
        {
            if (windowsManaged(it.key()))
                windowsExtras << it.key();
            else
            {
                ++extra;
                names << "thừa " + it.key();
            }
        }
    if (!names.isEmpty())
        say("    tệp khác biệt: " + names.mid(0, 8).join(", "));
    if (!windowsExtras.isEmpty())
        say("    (Windows tự tạo, không tính: " + windowsExtras.mid(0, 4).join(", ") + ")");
    check(missing == 0 && bad == 0 && extra == 0, QString("dữ liệu y nguyên: %1 tệp, thiếu %2, sai hash %3, thừa %4")
                                                     .arg(manifest.size()).arg(missing).arg(bad).arg(extra));
    // Hidden sectors trong boot sector = vị trí / 512.
    {
        const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QString("\\\\.\\%1:").arg(env.letter).utf16()), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                     OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
        bool ok = false;
        qint64 hidden = -2;
        if (h != INVALID_HANDLE_VALUE)
        {
            std::vector<char> raw(8192);
            char* aligned = raw.data() + ((4096 - (reinterpret_cast<quintptr>(raw.data()) % 4096)) % 4096);
            DWORD got = 0;
            if (ReadFile(h, aligned, 4096, &got, nullptr) && got == 4096)
            {
                hidden = PartitionMove::readNtfsHiddenSectors(QByteArray(aligned, 512));
                ok = hidden == p.offsetBytes / 512;
            }
            CloseHandle(h);
        }
        check(ok, QString("Hidden sectors = %1 (mong đợi %2)").arg(hidden).arg(p.offsetBytes / 512));
    }
    int code = -1;
    const QString out = runProcess(AppPaths::systemExecutable("chkdsk.exe"), {env.letter + ":"}, 240000, &code);
    check(code == 0, QString("chkdsk (chỉ đọc) báo sạch, mã thoát %1").arg(code));
    if (code != 0)
        say("    chkdsk: " + out.left(600));
}

bool executePlan(Env& env, const DiskLayout::ChangePlan& plan, qint64 pauseAfterBytes = 0, bool* pausedOut = nullptr)
{
    for (const auto& step : plan.steps)
    {
        const auto cur = findPartition(env);
        if (step.kind == DiskLayout::StepKind::MoveData)
        {
            PartitionMover::MoveRequest req;
            req.diskNumber = env.disk;
            req.partitionNumber = env.partition;
            req.expectedStart = cur.offsetBytes;
            req.expectedLength = cur.sizeBytes;
            req.newStart = step.after.start;
            req.driveLetter = env.letter;
            PartitionMover::MoveWorker worker;
            worker.setRequest(req);
            MoveResult r = runMove(worker, pauseAfterBytes);
            say(QString("    kết quả dời: ok=%1 resumable=%2 chép tới %3 MiB - %4").arg(r.ok).arg(r.resumable).arg(r.maxProgress / MiB).arg(r.message.left(300)));
            if (pauseAfterBytes > 0 && !r.ok && r.resumable)
            {
                if (pausedOut)
                    *pausedOut = true;
                return true; // dừng ở đây - kịch bản gọi tự chạy tiếp
            }
            if (!r.ok)
                return false;
        }
        else
        {
            QString err;
            say(QString("    Resize-Partition -> %1 MiB").arg(step.after.size / MiB));
            if (!PartitionManager::resizePartition(env.disk, env.partition, step.after.size, &err))
            {
                say("    Resize-Partition lỗi: " + err);
                return false;
            }
        }
    }
    return true;
}

/// Làm tròn tới bội số 1 MiB gần nhất - giao diện thật luôn "bắt dính" vị trí mới vào lưới 1 MiB (DiskLayout::snapMoveStart).
qint64 alignMiB(qint64 bytes)
{
    return ((bytes + MiB / 2) / MiB) * MiB;
}

DiskLayout::ChangePlan makePlan(const Env& env, qint64 newStart, qint64 newSize)
{
    const auto parts = diskPartitions(env.disk);
    PartitionManager::DiskInfo di;
    di.number = env.disk;
    QString err;
    for (const auto& d : PartitionManager::listDisks(&err))
        if (d.number == env.disk)
            di = d;
    const auto rows = DiskLayout::build({di}, parts);
    int idx = -1;
    for (int i = 0; i < parts.size(); ++i)
        if (parts[i].partitionNumber == env.partition)
            idx = i;
    // build() đánh chỉ số theo danh sách ĐƯA VÀO (đã lọc theo đĩa), nên idx khớp.
    return DiskLayout::planChange(rows.isEmpty() ? QList<DiskLayout::Segment>() : rows[0].segments, idx, newStart, newSize, 0, di.sizeBytes);
}

void detachAndDelete(const QString& vhd)
{
    diskpart(QString("select vdisk file=\"%1\"\ndetach vdisk\n").arg(QDir::toNativeSeparators(vhd)));
    QFile::remove(vhd);
}
/// Tiến trình con GIỮ ổ thử (chính exe này với "--hold"): đứng trong thư mục / mở tệp / mở thẳng cả ổ / ánh xạ tệp vào bộ nhớ rồi ngủ.
struct Holder
{
    QProcess* proc{nullptr};
    QString mode;
    quint32 pid{0};
};

Holder startHolder(const QString& mode, const QString& target, int seconds = 600)
{
    Holder h;
    h.mode = mode;
    h.proc = new QProcess();
    h.proc->setProcessChannelMode(QProcess::MergedChannels);
    h.proc->start(QCoreApplication::applicationFilePath(), {"--hold", mode, target, QString::number(seconds)});
    if (!h.proc->waitForStarted(10000))
        return h;
    QElapsedTimer t;
    t.start();
    QByteArray out;
    while (t.elapsed() < 15000 && !out.contains("READY"))
    {
        h.proc->waitForReadyRead(500);
        out += h.proc->readAll();
    }
    if (out.contains("READY"))
        h.pid = static_cast<quint32>(h.proc->processId());
    say(QString("    giữ ổ kiểu %1: PID %2 - %3").arg(mode).arg(h.pid).arg(QString::fromLocal8Bit(out).simplified().left(160)));
    return h;
}

void killHolder(Holder& h)
{
    if (!h.proc)
        return;
    if (h.proc->state() != QProcess::NotRunning)
    {
        h.proc->kill();
        h.proc->waitForFinished(5000);
    }
    delete h.proc;
    h.proc = nullptr;
}

int holdMain(const QString& mode, const QString& target, int seconds)
{
    // Không cần quyền Administrator. In "READY" khi đã giữ xong.
    if (mode == "cwd")
    {
        if (!SetCurrentDirectoryW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16())))
            return 3;
    }
    else if (mode == "file")
    {
        const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return 3;
    }
    else if (mode == "raw")
    {
        const QString dev = QStringLiteral("\\\\.\\") + target.left(1) + QLatin1Char(':');
        const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(dev.utf16()), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                     OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return 3;
    }
    else if (mode == "map")
    {
        const HANDLE f = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(target).utf16()), GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return 3;
        const HANDLE m = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void* v = m ? MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (m)
            CloseHandle(m);
        CloseHandle(f); // chỉ còn view - không còn handle tệp nào
        if (!v)
            return 3;
    }
    else
        return 2;
    std::printf("READY %s\n", qPrintable(mode));
    std::fflush(stdout);
    Sleep(static_cast<DWORD>(seconds) * 1000);
    return 0;
}

/// Một lượt dời trên VHDX (style "gpt" hoặc "mbr"): 7 kịch bản cũ + [8] chương trình giữ ổ (dò + dừng) + [9] buộc tháo ổ.
void runSuite(const QString& style)
{
    Env env;
    env.vhd = QDir::toNativeSeparators(QDir::tempPath() + "/oneforall_move_test_" + style + ".vhdx");
    QFile::remove(env.vhd);
    // Chọn chữ ổ trống từ Y xuống.
    QStringList used;
    for (const QFileInfo& d : QDir::drives())
        used << d.absolutePath().left(1).toUpper();
    for (char c = 'Y'; c >= 'E'; --c)
        if (!used.contains(QString(QChar(c))))
        {
            env.letter = QString(QChar(c));
            break;
        }
    if (env.letter.isEmpty())
    {
        say("Không còn chữ ổ trống.");
        ++g_failed;
        return;
    }

    say("\n==================== LƯỢT " + style.toUpper() + " ====================");
    say("Tạo VHDX 1500 MB rồi gắn vào máy (chưa phân vùng)");
    const QString dpOut = diskpart(QString("create vdisk file=\"%1\" maximum=1500 type=expandable\nselect vdisk file=\"%1\"\nattach vdisk\n").arg(env.vhd));
    say("  diskpart: " + dpOut.simplified().left(300));
    if (!QFile::exists(env.vhd))
    {
        say("Không tạo được VHDX.");
        ++g_failed;
        return;
    }

    // Số đĩa của VHDX + KHÓA AN TOÀN: phải là đĩa ảo "File Backed Virtual" VÀ đúng là đĩa của tệp VHDX này, rồi mới phân vùng (select disk N tường minh).
    const QString diskNum = powershell(QString("(Get-DiskImage -ImagePath '%1' | Get-Disk).Number").arg(env.vhd));
    bool okNum = false;
    env.disk = diskNum.toInt(&okNum);
    const QString bus = powershell(QString("(Get-Disk -Number %1).BusType").arg(env.disk));
    const QString size = powershell(QString("(Get-Disk -Number %1).Size").arg(env.disk));
    say(QString("  đĩa số %1, loại bus: %2, dung lượng: %3").arg(env.disk).arg(bus, size));
    if (!okNum || env.disk < 0 || !bus.contains("Virtual", Qt::CaseInsensitive) || size.toLongLong() != 1500LL * MiB)
    {
        say("KHÔNG xác nhận được đây là đĩa ảo của VHDX - DỪNG, không đụng gì.");
        detachAndDelete(env.vhd);
        ++g_failed;
        return;
    }
    say("Phân vùng đĩa ảo (" + style.toUpper() + "): FILLER 200 MB + MOVETEST 300 MB (NTFS), chữ ổ " + env.letter + ":");
    const QString dp2 = diskpart(QString("select disk %1\nconvert %3\ncreate partition primary size=200\nformat fs=ntfs quick label=FILLER\n"
                                         "create partition primary size=300\nformat fs=ntfs quick label=MOVETEST\nassign letter=%2\n")
                                     .arg(env.disk)
                                     .arg(env.letter)
                                     .arg(style));
    say("  diskpart: " + dp2.simplified().left(400));
    for (const auto& p : diskPartitions(env.disk))
        if (p.driveLetter.compare(env.letter, Qt::CaseInsensitive) == 0)
            env.partition = p.partitionNumber;
    check(env.partition > 0, QString("tìm thấy phân vùng MOVETEST: số %1").arg(env.partition));
    if (env.partition <= 0)
    {
        detachAndDelete(env.vhd);
        return;
    }
    {
        const auto p = findPartition(env);
        say(QString("  bố cục ban đầu: MOVETEST tại %1 MiB, dài %2 MiB, %3").arg(p.offsetBytes / MiB).arg(p.sizeBytes / MiB).arg(p.fileSystem));
        check(PartitionMover::notMovableReason(p).isEmpty(), "notMovableReason cho phép dời phân vùng này");
    }

    say("Ghi dữ liệu thử vào " + env.letter + ": ...");
    fillData(env.letter + ":/");
    const auto manifest = hashTree(env.letter + ":/");
    say(QString("  %1 tệp, đã tính SHA-256").arg(manifest.size()));

    const auto startPart = findPartition(env);
    const qint64 s0 = startPart.offsetBytes, size0 = startPart.sizeBytes;

    // ---- 1. Dời PHẢI 100 MiB (chồng lấn, chép lùi) ----
    {
        say("\n[1] Dời PHẢI 100 MiB (chồng lấn)");
        const qint64 dst1 = alignMiB(s0 + 100 * MiB);
        const auto plan = makePlan(env, dst1, size0);
        say("  " + DiskLayout::describePlan(env.letter + ":", plan).replace("\n", "\n  "));
        check(plan.blockReason.isEmpty() && plan.needsMove(), "kế hoạch hợp lệ");
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "thực hiện xong");
            verify(env, manifest, dst1, size0, "sau khi dời phải");
        }
    }
    // ---- 2. Dời TRÁI 40 MiB (chồng lấn, chép tiến) ----
    {
        say("\n[2] Dời TRÁI 40 MiB (chồng lấn)");
        const auto cur = findPartition(env);
        const qint64 dst2 = alignMiB(cur.offsetBytes - 40 * MiB);
        const auto plan = makePlan(env, dst2, cur.sizeBytes);
        check(plan.blockReason.isEmpty() && plan.needsMove(), "kế hoạch hợp lệ " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "thực hiện xong");
            verify(env, manifest, dst2, size0, "sau khi dời trái");
        }
    }
    // ---- 3. Nhảy sang vùng trống KHÁC (không chồng lấn) ----
    {
        say("\n[3] Nhảy sang vùng trống xa (không chồng lấn)");
        const auto cur = findPartition(env);
        const qint64 target = 1000 * MiB;
        const auto plan = makePlan(env, target, cur.sizeBytes);
        check(plan.blockReason.isEmpty() && plan.needsMove(), "kế hoạch hợp lệ " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "thực hiện xong");
            verify(env, manifest, target, size0, "sau khi nhảy");
        }
    }
    // ---- 4. Kéo mép TRÁI sang trái: dời trái + mở rộng mép phải về chỗ cũ ----
    {
        say("\n[4] Kéo mép TRÁI sang trái (mở rộng về bên trái)");
        const auto cur = findPartition(env);
        const qint64 curEnd = cur.offsetBytes + cur.sizeBytes;
        const qint64 ns = 700 * MiB;
        const auto plan = makePlan(env, ns, curEnd - ns);
        say("  " + DiskLayout::describePlan(env.letter + ":", plan).replace("\n", "\n  "));
        check(plan.blockReason.isEmpty() && plan.steps.size() == 2, "kế hoạch 2 bước " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "thực hiện xong");
            verify(env, manifest, ns, curEnd - ns, "sau khi mở rộng trái");
        }
    }
    // ---- 5. Kéo mép TRÁI sang phải: thu nhỏ + dời phải ----
    {
        say("\n[5] Kéo mép TRÁI sang phải (thu nhỏ từ bên trái)");
        const auto cur = findPartition(env);
        const qint64 curEnd = cur.offsetBytes + cur.sizeBytes;
        const qint64 ns = cur.offsetBytes + 100 * MiB;
        const auto plan = makePlan(env, ns, curEnd - ns);
        say("  " + DiskLayout::describePlan(env.letter + ":", plan).replace("\n", "\n  "));
        check(plan.blockReason.isEmpty() && plan.steps.size() == 2, "kế hoạch 2 bước " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "thực hiện xong");
            verify(env, manifest, ns, curEnd - ns, "sau khi thu nhỏ từ trái");
        }
    }
    // ---- 6. Tạm dừng giữa chừng rồi chạy tiếp từ nhật ký (chồng lấn: ổ cũ đã bị ghi đè một phần) ----
    {
        say("\n[6] Tạm dừng giữa chừng rồi chạy tiếp từ nhật ký");
        const auto cur = findPartition(env);
        const qint64 dst6 = alignMiB(cur.offsetBytes - 30 * MiB);
        const auto plan = makePlan(env, dst6, cur.sizeBytes);
        check(plan.blockReason.isEmpty() && plan.needsMove(), "kế hoạch hợp lệ " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            bool paused = false;
            check(executePlan(env, plan, cur.sizeBytes / 3, &paused) && paused, "đã tạm dừng ở khoảng 1/3");
            PartitionMove::JournalState j;
            QString jerr;
            const bool haveJournal = PartitionMover::loadJournal(&j, &jerr);
            check(haveJournal && j.doneBytes > 0 && j.doneBytes < j.length, QString("nhật ký còn đó, tiến độ %1/%2 MiB").arg(j.doneBytes / MiB).arg(j.length / MiB));
            if (haveJournal)
            {
                say("  " + PartitionMover::describeJournal(j));
                PartitionMover::MoveWorker worker;
                worker.setResume(j, env.letter);
                const MoveResult r = runMove(worker);
                say(QString("    kết quả chạy tiếp: ok=%1 resumable=%2 - %3").arg(r.ok).arg(r.resumable).arg(r.message.left(300)));
                check(r.ok, "chạy tiếp xong");
                check(!QFile::exists(PartitionMover::journalPath()), "nhật ký đã được xóa sau khi xong");
                verify(env, manifest, dst6, cur.sizeBytes, "sau khi chạy tiếp");
            }
        }
    }
    // ---- 7. Từ chối đúng: đích chồng lên phân vùng khác; dữ liệu không bị đụng ----
    {
        say("\n[7] Từ chối dời chồng lên phân vùng FILLER");
        const auto cur = findPartition(env);
        PartitionMover::MoveRequest req;
        req.diskNumber = env.disk;
        req.partitionNumber = env.partition;
        req.expectedStart = cur.offsetBytes;
        req.expectedLength = cur.sizeBytes;
        req.newStart = 100 * MiB; // chồng lên FILLER (1..201 MiB)
        req.driveLetter = env.letter;
        PartitionMover::MoveWorker worker;
        worker.setRequest(req);
        const MoveResult r = runMove(worker);
        check(!r.ok && !r.resumable && r.message.contains("chồng"), "bị từ chối: " + r.message.left(120));
        check(!QFile::exists(PartitionMover::journalPath()), "không để lại nhật ký");
        verify(env, manifest, cur.offsetBytes, cur.sizeBytes, "dữ liệu vẫn nguyên sau khi bị từ chối");
    }
    // ---- 8. Chương trình GIỮ ổ: dò ra ĐỦ (cả 4 kiểu khó thấy), dừng hết, rồi khóa + dời được ----
    {
        say("\n[8] Chương trình giữ ổ: thư mục làm việc, tệp đang mở, mở thẳng cả ổ, ánh xạ bộ nhớ -> dò + dừng hết -> dời");
        const QString root = env.letter + ":/";
        QList<Holder> holders;
        holders << startHolder("cwd", root + "data/sub") << startHolder("file", root + "data/f1.bin") << startHolder("raw", env.letter)
                << startHolder("map", root + "data/sub/big0.bin");
        bool allStarted = true;
        for (const Holder& h : holders)
            allStarted = allStarted && h.pid != 0;
        check(allStarted, "4 tiến trình giữ ổ đã chạy");
        const QString blocked = PartitionMover::preflightMove(env.disk, env.partition, env.letter);
        check(blocked.contains("Không khóa được"), "khóa ổ THẤT BẠI khi còn chương trình giữ ổ: " + blocked.left(160));
        QElapsedTimer t;
        t.start();
        const VolumeHolders::Report rep = VolumeHolders::scan(env.letter, nullptr, true);
        say(QString("    VolumeHolders::scan: %1 thứ giữ ổ, %2 ms, kiểm kỹ %3 tệp").arg(rep.holders.size()).arg(t.elapsed()).arg(rep.filesChecked));
        for (const VolumeHolders::Holder& vh : rep.holders)
            say(QString("      %1 PID %2 - %3").arg(vh.name).arg(vh.pid).arg(vh.holdingSummary().left(160)));
        for (const Holder& h : holders)
        {
            const VolumeHolders::Holder* found = nullptr;
            for (const VolumeHolders::Holder& vh : rep.holders)
                if (vh.pid == h.pid)
                    found = &vh;
            check(found != nullptr && found->stoppable(), QString("dò ra tiến trình giữ ổ kiểu %1 (PID %2)").arg(h.mode).arg(h.pid));
            if (found)
            {
                QString err;
                check(VolumeHolders::stop(*found, &err), QString("dừng được kiểu %1 %2").arg(h.mode, err));
            }
        }
        for (Holder& h : holders)
            if (h.proc)
                h.proc->waitForFinished(5000);
        const QString after = PartitionMover::preflightMove(env.disk, env.partition, env.letter);
        check(after.isEmpty(), "khóa ổ ĐƯỢC sau khi dừng hết: " + after.left(160));
        for (Holder& h : holders)
            killHolder(h);
        const auto cur = findPartition(env);
        const qint64 dst8 = alignMiB(cur.offsetBytes + 20 * MiB);
        const auto plan = makePlan(env, dst8, cur.sizeBytes);
        check(plan.blockReason.isEmpty(), "kế hoạch hợp lệ " + plan.blockReason);
        if (plan.blockReason.isEmpty())
        {
            check(executePlan(env, plan), "dời PHẢI 20 MiB sau khi dừng hết");
            verify(env, manifest, dst8, cur.sizeBytes, "sau khi dừng chương trình giữ ổ rồi dời");
        }
    }
    // ---- 9. Thứ giữ ổ KHÔNG dừng (giả lập phần mềm tự bảo vệ): BUỘC THÁO ổ (như chkdsk /x) rồi dời ----
    {
        say("\n[9] Buộc tháo ổ khi còn thứ giữ ổ không dừng được (mở thẳng cả ổ + tệp đang mở + thư mục làm việc)");
        const QString root = env.letter + ":/";
        QList<Holder> holders;
        holders << startHolder("raw", env.letter) << startHolder("file", root + "data/f2.bin") << startHolder("cwd", root + "data");
        PartitionMover::setForceDismountAllowed(false);
        const QString blocked = PartitionMover::preflightMove(env.disk, env.partition, env.letter);
        check(blocked.contains("Không khóa được"), "chưa cho buộc tháo: khóa thất bại như mong đợi");
        PartitionMover::setForceDismountAllowed(true);
        const QString forced = PartitionMover::preflightMove(env.disk, env.partition, env.letter);
        check(forced.isEmpty(), "cho buộc tháo: kiểm tra trước (khóa) ĐẠT: " + forced.left(200));
        const auto cur = findPartition(env);
        const qint64 dst9 = alignMiB(cur.offsetBytes - 20 * MiB);
        const auto plan = makePlan(env, dst9, cur.sizeBytes);
        check(plan.blockReason.isEmpty(), "kế hoạch hợp lệ " + plan.blockReason);
        if (plan.blockReason.isEmpty())
            check(executePlan(env, plan), "dời TRÁI 20 MiB với buộc tháo ổ (tiến trình giữ ổ vẫn chạy)");
        PartitionMover::setForceDismountAllowed(false);
        for (Holder& h : holders)
            killHolder(h);
        if (plan.blockReason.isEmpty())
            verify(env, manifest, dst9, cur.sizeBytes, "sau khi buộc tháo ổ rồi dời");
    }

    say("\nDọn dẹp: tháo và xóa VHDX tạm...");
    detachAndDelete(env.vhd);
    check(!QFile::exists(env.vhd), "đã xóa VHDX tạm");
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc >= 5 && QString::fromLocal8Bit(argv[1]) == "--hold")
        return holdMain(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]), QString::fromLocal8Bit(argv[4]).toInt());
    if (argc < 2)
    {
        std::printf("Dùng: partition_move_real_check <tệp-nhật-ký.txt> [gpt|mbr]\n");
        return 2;
    }
    g_log.setFileName(QString::fromLocal8Bit(argv[1]));
    g_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    say("partition_move_real_check - dời phân vùng THẬT trên ổ đĩa ảo (VHDX) tạm");
    if (!PartitionManager::isElevated())
    {
        say("CẦN quyền Administrator (chạy bằng Start-Process -Verb RunAs).");
        say("RESULT: SKIPPED");
        return 2;
    }
    QTemporaryDir dataDir; // nhật ký phục hồi của lần chạy này nằm trong thư mục tạm riêng
    AppPaths::setDataDirOverride(dataDir.path());
    QStringList styles{"gpt", "mbr"};
    if (argc >= 3)
        styles = QStringList{QString::fromLocal8Bit(argv[2]).toLower()};
    for (const QString& style : styles)
        runSuite(style);
    say(QString("\nTỔNG KẾT: %1 đạt, %2 hỏng").arg(g_passed).arg(g_failed));
    say(g_failed == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    return g_failed == 0 ? 0 : 1;
}
