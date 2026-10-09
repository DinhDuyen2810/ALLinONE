// Stress test cho lõi Disk Cleanup + VPN & Location.
//  - Cây thư mục tạm hàng chục nghìn tệp (lồng rất sâu, đường dẫn > 260 ký tự, tên Unicode/dài/có khoảng
//    trắng/có dấu chấm cuối, tệp 0 byte/chỉ đọc/ẩn, .lnk, junction - kể cả junction tự trỏ vòng -, hardlink,
//    symlink) -> CleanupScanner/LargeFileScanner/DuplicateFinder, so với số liệu test TỰ GHI lúc tạo.
//  - Bật/dừng/hủy đối tượng quét ở thời điểm ngẫu nhiên hàng trăm lần: không treo, không crash, đúng một
//    tín hiệu kết thúc mỗi lượt.
//  - CleanupExecutor/RecycleBinOps xóa hàng nghìn tệp trong lúc một luồng khác tạo/xóa/KHÓA tệp cùng thư
//    mục; tệp "canh gác" (cạnh đó, sau junction, tên gần giống) phải còn nguyên từng byte.
//  - Fuzz hàng chục nghìn ca cho các hàm kiểm an toàn/đổi kích thước phân vùng/JSON/dựng script PowerShell.
//  - VpnConnector gọi THẬT RAS API tới tên hồ sơ KHÔNG tồn tại, kèm hủy ngẫu nhiên, kiểm rò handle.
//
// KHÔNG đụng hệ thống thật: mọi tệp nằm trong QTemporaryDir của test; không Resize-Partition/diskpart;
// không RecycleBinOps::empty(); không tạo/xóa/kết nối hồ sơ VPN nào. LƯU Ý: phần xóa "vào Thùng rác"
// để lại vài trăm tệp tạm nhỏ (tên del_*.tmp) trong Thùng rác thật của máy chạy test.
//
// Cách dùng: disk_vpn_stress_tests.exe [hạt giống] [tỉ lệ %] [số tệp vào Thùng rác]
//   - hạt giống: mặc định 20261009 - in ra đầu lượt chạy để tái hiện được lượt lỗi.
//   - tỉ lệ %: thu nhỏ/phóng to số tệp + số vòng lặp (mặc định 100).
//   - số tệp vào Thùng rác: mặc định 300 (phần còn lại xóa vĩnh viễn để không làm đầy Thùng rác).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"
#include "tools/diskcleanup/engine/CategoryRegistry.h"
#include "tools/diskcleanup/engine/CleanupExecutor.h"
#include "tools/diskcleanup/engine/CleanupScanner.h"
#include "tools/diskcleanup/engine/DuplicateFinder.h"
#include "tools/diskcleanup/engine/FsSafety.h"
#include "tools/diskcleanup/engine/LargeFileScanner.h"
#include "tools/diskcleanup/engine/PartitionManager.h"
#include "tools/diskcleanup/engine/RecycleBinOps.h"
#include "tools/vpn/engine/VpnConnector.h"
#include "tools/vpn/engine/VpnController.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

// ---------------------------------------------------------------------------------------------------
// Tiện ích chung
// ---------------------------------------------------------------------------------------------------
static std::mt19937_64 g_rng;
static quint64 rnd(quint64 n) { return n == 0 ? 0 : g_rng() % n; }
static int g_scale = 100;
static int scaled(int n) { return qMax(1, n * g_scale / 100); }

static void say(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::fflush(stdout);
}

/// "\\?\C:\..." - mọi thao tác tệp THÔ của test đi qua dạng này để tạo được tên có dấu chấm cuối và đường
/// dẫn dài hơn 260 ký tự (độc lập với FsSafety đang được kiểm).
static std::wstring ext(const QString& path)
{
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}

static QByteArray contentFor(quint64 seed, qint64 size)
{
    QByteArray data(static_cast<int>(size), '\0');
    quint64 x = seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL;
    for (int i = 0; i < data.size(); ++i)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        data[i] = static_cast<char>(x & 0xFF);
    }
    return data;
}

static bool rawWrite(const QString& path, const QByteArray& data, DWORD attributes = FILE_ATTRIBUTE_NORMAL)
{
    const HANDLE h = CreateFileW(ext(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, attributes, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    const bool ok = data.isEmpty() || (WriteFile(h, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr) &&
                                       written == static_cast<DWORD>(data.size()));
    CloseHandle(h);
    return ok;
}

static bool rawRead(const QString& path, QByteArray* out)
{
    const HANDLE h = CreateFileW(ext(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    out->clear();
    char buffer[8192];
    DWORD got = 0;
    while (ReadFile(h, buffer, sizeof(buffer), &got, nullptr) && got > 0)
        out->append(buffer, static_cast<int>(got));
    CloseHandle(h);
    return true;
}

static bool rawExists(const QString& path)
{
    return GetFileAttributesW(ext(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool rawMkdir(const QString& path)
{
    return CreateDirectoryW(ext(path).c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}

static bool makeJunction(const QString& link, const QString& target)
{
    QProcess proc;
    proc.start(AppPaths::systemExecutable("cmd.exe"),
               {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
    return proc.waitForFinished(15000) && proc.exitCode() == 0;
}

/// Đếm số mục trực tiếp trong một thư mục (không đệ quy) bằng API thô.
static int rawCountEntries(const QString& dir)
{
    WIN32_FIND_DATAW data;
    const HANDLE h = FindFirstFileW(ext(dir + "/*").c_str(), &data);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    int count = 0;
    do
    {
        if (wcscmp(data.cFileName, L".") != 0 && wcscmp(data.cFileName, L"..") != 0)
            ++count;
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return count;
}

/// Dọn thư mục tạm của test bằng API thô (QTemporaryDir không tự xóa được tên có dấu chấm cuối/đường dẫn
/// dài). CHỈ xóa bên dưới 'mustBeUnder'; liên kết thư mục (junction/symlink) chỉ bị GỠ, không đi vào.
static void rawRemoveTree(const QString& dir, const QString& mustBeUnder)
{
    if (mustBeUnder.size() < 10 || !(dir == mustBeUnder || dir.startsWith(mustBeUnder + "/")))
        return;
    WIN32_FIND_DATAW data;
    const HANDLE h = FindFirstFileW(ext(dir + "/*").c_str(), &data);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
                continue;
            const QString full = dir + "/" + QString::fromWCharArray(data.cFileName);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                SetFileAttributesW(ext(full).c_str(), FILE_ATTRIBUTE_NORMAL);
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    RemoveDirectoryW(ext(full).c_str());
                else
                    rawRemoveTree(full, mustBeUnder);
            }
            else
            {
                DeleteFileW(ext(full).c_str());
            }
        } while (FindNextFileW(h, &data));
        FindClose(h);
    }
    RemoveDirectoryW(ext(dir).c_str());
}

static DWORD handleCount()
{
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

/// Bộ đếm thất bại cho một tính chất fuzz: in vài ca đầu, cuối cùng CHECK(bad == 0) một lần.
struct FuzzProperty
{
    const char* name;
    int cases{0};
    int bad{0};
    void expect(bool ok, const QString& input)
    {
        ++cases;
        if (ok)
            return;
        if (++bad <= 5)
            say("  [fuzz %s] ca hong: \"%s\"\n", name, qPrintable(QString(input).replace('\n', "\\n")));
    }
};
#define CHECK_FUZZ(prop)                                                                  \
    do {                                                                                  \
        say("  fuzz %-34s %6d ca, %d hong\n", (prop).name, (prop).cases, (prop).bad);     \
        CHECK((prop).bad == 0);                                                           \
    } while (0)

// ---------------------------------------------------------------------------------------------------
// Sổ ghi những gì test đã tạo - "số liệu tự đếm độc lập" để so với kết quả của các lớp quét.
// ---------------------------------------------------------------------------------------------------
struct Manifest
{
    QHash<QString, qint64> visible;      // tệp thường mà walkFiles PHẢI thấy -> kích thước
    QSet<QString> mustNotSee;            // đường dẫn KHÔNG được xuất hiện (sau junction, .lnk, symlink)
    QHash<QString, QString> hardlinkOf;  // đường dẫn hardlink -> đường dẫn "chính" (cùng một tệp vật lý)
    QList<QStringList> duplicateGroups;  // nhóm trùng nội dung mong đợi (đường dẫn "chính", đã sắp xếp)
    QHash<QString, qint64> groupSize;    // khóa nhóm (join '|') -> kích thước mỗi tệp
    qint64 totalBytes() const
    {
        qint64 sum = 0;
        for (auto it = visible.constBegin(); it != visible.constEnd(); ++it)
            sum += it.value();
        return sum;
    }
};

static quint64 g_contentSeed = 1;

static void addFile(Manifest& m, const QString& path, qint64 size, DWORD attributes = FILE_ATTRIBUTE_NORMAL, bool visible = true)
{
    if (!rawWrite(path, contentFor(g_contentSeed++, size), attributes))
    {
        say("KHONG TAO DUOC TEP: %s (loi %lu)\n", qPrintable(path), GetLastError());
        ++g_fail;
        return;
    }
    if (visible)
        m.visible.insert(path, size);
    else
        m.mustNotSee.insert(path);
}

static QString groupKey(QStringList paths)
{
    std::sort(paths.begin(), paths.end());
    return paths.join('|');
}

struct TerminalSignals
{
    std::atomic<int> finished{0};
    std::atomic<int> stopped{0};
    int total() const { return finished.load() + stopped.load(); }
};

/// Bật/dừng một lớp quét ở thời điểm ngẫu nhiên. 'fullOk' được gọi sau các lượt KHÔNG bị dừng (và trước đó
/// còn cố ý để lại một requestStop() "cũ") - kết quả phải đầy đủ và đúng như lượt quét bình thường.
template <typename Scanner>
static void stormScanner(const char* label, Scanner& scanner, TerminalSignals& terminal, int iterations, int fullEvery,
                         const std::function<bool()>& fullOk)
{
    int hangs = 0, badSignalCount = 0, wrongFull = 0, fullRuns = 0, stoppedRuns = 0;
    qint64 maxStopLatencyMs = 0;
    QElapsedTimer total;
    total.start();
    for (int i = 0; i < iterations && hangs == 0; ++i)
    {
        const int before = terminal.total();
        const int stoppedBefore = terminal.stopped.load();
        const bool full = fullEvery > 0 && (i % fullEvery) == fullEvery - 1;
        const int mode = static_cast<int>(rnd(5));
        QElapsedTimer latency;
        if (full)
        {
            scanner.requestStop(); // cờ dừng còn sót từ trước KHÔNG được làm hỏng lượt quét mới
            scanner.startScan();
        }
        else
        {
            scanner.startScan();
            if (mode == 1)
                QThread::usleep(static_cast<unsigned long>(rnd(2000)));
            else if (mode == 2)
                QThread::msleep(static_cast<unsigned long>(rnd(40)));
            else if (mode == 3)
            {
                scanner.startScan(); // gọi chồng lúc đang chạy: phải bị bỏ qua
                QThread::usleep(static_cast<unsigned long>(rnd(500)));
            }
            scanner.requestStop();
            latency.start();
            if (mode == 4)
            {
                scanner.startScan(); // bấm Quét lại NGAY sau khi bấm Dừng (được nhận nếu lượt trước vừa thoát)
                scanner.requestStop();
            }
        }
        if (!scanner.wait(120000))
        {
            ++hangs;
            break;
        }
        if (latency.isValid())
            maxStopLatencyMs = qMax(maxStopLatencyMs, latency.elapsed());

        const int delta = terminal.total() - before;
        const bool deltaOk = (!full && mode == 4) ? (delta == 1 || delta == 2) : (delta == 1);
        if (!deltaOk)
            ++badSignalCount;
        if (terminal.stopped.load() != stoppedBefore)
            ++stoppedRuns;
        if (full)
        {
            ++fullRuns;
            if (terminal.stopped.load() != stoppedBefore || !fullOk())
                ++wrongFull;
        }
    }
    say("  %-16s %d luot (%d luot chay het, %d luot bi dung), dung cham nhat %lld ms, tong %lld ms\n", label, iterations, fullRuns,
        stoppedRuns, static_cast<long long>(maxStopLatencyMs), static_cast<long long>(total.elapsed()));
    CHECK(hangs == 0);
    CHECK(badSignalCount == 0);
    CHECK(wrongFull == 0);
    CHECK(maxStopLatencyMs < 10000);
}

// ---------------------------------------------------------------------------------------------------
// "Trọng tài" độc lập cho fuzz unsafeCleanupRootReason: đưa một chuỗi về dạng mà Win32 thật sự hiểu.
// ---------------------------------------------------------------------------------------------------
static QString win32Canonical(const QString& input)
{
    QString p = QDir::fromNativeSeparators(input.trimmed());
    if (p.startsWith(QLatin1String("//?/UNC/"), Qt::CaseInsensitive))
        p = "//" + p.mid(8);
    else if (p.startsWith(QLatin1String("//?/")) || p.startsWith(QLatin1String("//./")))
        p = p.mid(4);
    static const QRegularExpression adminShare(QStringLiteral("^//[^/]+/([A-Za-z])\\$(/|$)"));
    const QRegularExpressionMatch match = adminShare.match(p);
    if (match.hasMatch())
        p = match.captured(1) + ":/" + p.mid(match.capturedEnd(0));

    const bool unc = p.startsWith(QLatin1String("//"));
    QString prefix;
    QString rest = p;
    if (unc)
    {
        prefix = "//";
        rest = p.mid(2);
    }
    else if (p.size() >= 2 && p[1] == QLatin1Char(':'))
    {
        prefix = p.left(2).toLower() + "/";
        rest = p.mid(2);
    }
    else
    {
        return QString(); // tương đối - không có dạng chuẩn tuyệt đối
    }
    // Win32 xử lý "." và ".." TRƯỚC (theo đúng chuỗi đã viết: "x/ /.." là "x"), sau đó mới bỏ dấu chấm/khoảng
    // trắng ở cuối tên.
    QStringList resolved;
    for (const QString& component : rest.split(QLatin1Char('/'), Qt::SkipEmptyParts))
    {
        if (component == ".")
            continue;
        if (component == "..")
        {
            if (!resolved.isEmpty())
                resolved.removeLast();
            continue;
        }
        resolved << component;
    }
    QStringList out;
    for (QString component : resolved)
    {
        while (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            component.chop(1);
        if (!component.isEmpty())
            out << component.toLower();
    }
    return prefix + out.join('/');
}

/// Chuỗi 'candidate' (theo cách Win32 hiểu) có phải gốc ổ đĩa/gốc chia sẻ, hoặc trùng/chứa một thư mục
/// được bảo vệ không.
static bool oracleDangerous(const QString& candidate, const QStringList& protectedDirs)
{
    const QString canon = win32Canonical(candidate);
    if (canon.isEmpty())
        return true;
    if (canon.startsWith("//"))
    {
        if (canon.mid(2).split('/', Qt::SkipEmptyParts).size() < 3)
            return true;
    }
    else if (canon.size() <= 3)
    {
        return true; // "c:/"
    }
    for (const QString& prot : protectedDirs)
    {
        const QString canonProt = win32Canonical(prot);
        if (canonProt == canon || canonProt.startsWith(canon + "/"))
            return true;
    }
    return false;
}

// Dấu nháy đơn theo tokenizer của PowerShell + đọc một chuỗi '...' (xem PowerShellRunner::quoteLiteral).
static bool isPsQuote(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x0027 || u == 0x2018 || u == 0x2019 || u == 0x201A || u == 0x201B;
}

/// script[pos] là dấu nháy MỞ. Trả về vị trí ngay sau dấu nháy ĐÓNG (-1 nếu chuỗi không đóng); *decoded là
/// nội dung PowerShell sẽ nhận được.
static int readPsLiteral(const QString& script, int pos, QString* decoded)
{
    decoded->clear();
    int i = pos + 1;
    while (i < script.size())
    {
        if (isPsQuote(script[i]))
        {
            if (i + 1 < script.size() && isPsQuote(script[i + 1]))
            {
                decoded->append(script[i + 1]);
                i += 2;
                continue;
            }
            return i + 1;
        }
        decoded->append(script[i]);
        ++i;
    }
    return -1;
}

static QString randomPsString(int maxLen, bool harmlessOnly)
{
    // harmlessOnly: bảng ký tự cho lượt chạy powershell.exe THẬT - nếu thoát chuỗi hỏng, phần "lệnh" lọt ra
    // chỉ có thể ghép từ q/x/số/dấu nên không thành lệnh có thật nào.
    static const QList<QChar> quotes = {QChar(0x27), QChar(0x2018), QChar(0x2019), QChar(0x201A), QChar(0x201B)};
    static const QString harmless = QStringLiteral("qx09 ;$(){}`\"#|&@-,.=+[]<>!~^%");
    static const QString extra = QStringLiteral("\n\r\t\\/abcXYZ");
    const int len = static_cast<int>(rnd(static_cast<quint64>(maxLen) + 1));
    QString s;
    for (int i = 0; i < len; ++i)
    {
        const int kind = static_cast<int>(rnd(10));
        if (kind < 4)
            s.append(quotes[static_cast<int>(rnd(quotes.size()))]);
        else if (kind < 8 || harmlessOnly)
            s.append(harmless[static_cast<int>(rnd(harmless.size()))]);
        else if (kind == 8)
            s.append(extra[static_cast<int>(rnd(extra.size()))]);
        else
            s.append(QChar(static_cast<ushort>(0x00C0 + rnd(0x1F00)))); // chữ có dấu, Hy Lạp, Kirin, tiếng Việt...
    }
    return s;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Mọi tệp dữ liệu của ứng dụng (vpn_profiles.json, log) ghi vào thư mục tạm.
    QTemporaryDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    const quint64 seed = argc > 1 ? QString(argv[1]).toULongLong() : 20261009ULL;
    g_scale = argc > 2 ? qBound(5, QString(argv[2]).toInt(), 1000) : 100;
    const int recycleFileCount = argc > 3 ? qBound(0, QString(argv[3]).toInt(), 5000) : 300;
    g_rng.seed(seed);
    say("hat giong=%llu ti le=%d%% tep vao Thung rac=%d\n", static_cast<unsigned long long>(seed), g_scale, recycleFileCount);

    // Chó canh: bất kỳ chỗ nào treo (wait() không trả về, hộp thoại Shell bật lên...) -> thoát với mã 3.
    std::atomic_bool allDone{false};
    std::thread watchdog([&allDone]() {
        for (int i = 0; i < 25 * 60 * 10 && !allDone; ++i)
            Sleep(100);
        if (!allDone)
        {
            std::printf("WATCHDOG: qua 25 phut chua xong - coi nhu TREO\npassed=%d failed=%d\n", g_pass, g_fail + 1);
            std::fflush(stdout);
            std::_Exit(3);
        }
    });

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString t = QDir::cleanPath(tmp.path());
    const QString scan = t + "/scan";
    const QString outside = t + "/outside";
    QElapsedTimer sectionTimer;

    // =================================================================================================
    // A. Dựng cây thư mục lớn
    // =================================================================================================
    sectionTimer.start();
    Manifest m;
    bool haveJunction = false;
    bool haveFileSymlink = false;
    bool haveDirSymlink = false;
    {
        QDir().mkpath(scan);
        QDir().mkpath(outside + "/deep");
        // Dữ liệu NGOÀI cây quét - chỉ tới được qua junction/symlink. Kích thước lớn hơn mọi tệp trong cây
        // để nếu bị "lạc" sang thì chắc chắn lộ ra ở đầu danh sách tệp lớn.
        rawWrite(outside + "/sentinel_big.bin", contentFor(900001, 300000));
        rawWrite(outside + "/deep/sentinel_dup.bin", contentFor(900002, 7001));
        m.mustNotSee << scan + "/junction_out/sentinel_big.bin" << scan + "/junction_out/deep/sentinel_dup.bin";

        // 1) 40 thư mục x 5 thư mục con x 100 tệp, tên đủ kiểu.
        const int dirCount = scaled(40);
        for (int d = 0; d < dirCount; ++d)
        {
            QString dirName;
            switch (d % 4)
            {
                case 0: dirName = QString::fromUtf8("thư mục %1 có dấu & khoảng trắng").arg(d); break;
                case 1: dirName = QString("dir_%1").arg(d); break;
                case 2: dirName = QString::fromUtf8("目录%1 😀 дир").arg(d); break;
                default: dirName = QString("long_%1_").arg(d) + QString(100, QLatin1Char('L')); break;
            }
            for (int s = 0; s < 5; ++s)
            {
                const QString dir = scan + "/" + dirName + QString("/s%1").arg(s);
                QDir().mkpath(dir);
                for (int f = 0; f < 100; ++f)
                {
                    QString name;
                    switch (f % 5)
                    {
                        case 0: name = QString("f%1.bin").arg(f, 5, 10, QLatin1Char('0')); break;
                        case 1: name = QString::fromUtf8("tệp số %1 (bản sao).txt").arg(f); break;
                        case 2: name = QString::fromUtf8("ファイル%1.dat").arg(f); break;
                        case 3: name = QString(" leading space %1.tmp").arg(f); break;
                        default: name = QString("no_extension_%1").arg(f); break;
                    }
                    const qint64 size = (f % 10 == 9) ? 0 : static_cast<qint64>(1 + rnd(3000));
                    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
                    if (f % 25 == 7)
                        attributes = FILE_ATTRIBUTE_READONLY;
                    else if (f % 33 == 5)
                        attributes = FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM;
                    if (f % 50 == 11)
                        addFile(m, dir + QString("/shortcut %1.LNK").arg(f), size, attributes, false); // lối tắt: bỏ qua
                    else
                        addFile(m, dir + "/" + name, size, attributes);
                }
            }
        }

        // 2) Chuỗi lồng 60 cấp: đường dẫn dài hơn 1000 ký tự (vượt xa MAX_PATH).
        QString deep = scan + "/deep";
        rawMkdir(deep);
        const int depth = scaled(60);
        for (int level = 0; level < depth; ++level)
        {
            deep += QString("/level_%1_xxxxxxxx").arg(level, 2, 10, QLatin1Char('0'));
            CHECK(rawMkdir(deep));
            for (int f = 0; f < 3; ++f)
                addFile(m, deep + QString("/deep_%1.bin").arg(f), static_cast<qint64>(10 + rnd(2000)));
        }
        say("  duong dan sau nhat dai %d ky tu\n", static_cast<int>(deep.size()) + 11);

        // 3) Tệp trùng lặp: mỗi nhóm một kích thước riêng (>= 5000, lớn hơn mọi tệp thường), kèm tệp "mồi"
        //    cùng kích thước khác nội dung, hardlink (cùng MỘT tệp vật lý - không phải bản sao).
        const QString dups = scan + "/dups";
        for (const char* sub : {"a", "b", "c", "d"})
            QDir().mkpath(dups + "/" + sub);
        const QStringList subs = {"a", "b", "c", "d"};
        const int groupCount = scaled(200);
        for (int g = 0; g < groupCount; ++g)
        {
            const qint64 size = 5000 + g;
            const QByteArray content = contentFor(500000 + static_cast<quint64>(g), size);
            const int copies = 2 + static_cast<int>(rnd(3));
            QStringList group;
            for (int c = 0; c < copies; ++c)
            {
                const QString path = dups + "/" + subs[c] + QString("/g%1_copy%2.bin").arg(g, 4, 10, QLatin1Char('0')).arg(c);
                CHECK(rawWrite(path, content));
                m.visible.insert(path, size);
                group << path;
            }
            if (g % 2 == 0) // mồi: cùng kích thước, khác nội dung
                addFile(m, dups + QString("/a/g%1_decoy.bin").arg(g, 4, 10, QLatin1Char('0')), size);
            if (g % 5 == 0)
            {
                const QString link = dups + QString("/d/g%1_hardlink.bin").arg(g, 4, 10, QLatin1Char('0'));
                if (CreateHardLinkW(ext(link).c_str(), ext(group[0]).c_str(), nullptr))
                {
                    m.visible.insert(link, size);
                    m.hardlinkOf.insert(link, group[0]);
                }
            }
            std::sort(group.begin(), group.end());
            m.duplicateGroups << group;
            m.groupSize.insert(group.join('|'), size);
        }
        // Nhiều nhóm KHÁC nội dung nhưng CÙNG kích thước (một "xô" kích thước chứa nhiều nhóm băm).
        const int sameSizeGroups = scaled(30);
        for (int g = 0; g < sameSizeGroups; ++g)
        {
            const QByteArray content = contentFor(700000 + static_cast<quint64>(g), 9000);
            QStringList group;
            for (int c = 0; c < 2; ++c)
            {
                const QString path = dups + "/" + subs[c + 2] + QString("/same_%1_%2.bin").arg(g, 3, 10, QLatin1Char('0')).arg(c);
                CHECK(rawWrite(path, content));
                m.visible.insert(path, 9000);
                group << path;
            }
            std::sort(group.begin(), group.end());
            m.duplicateGroups << group;
            m.groupSize.insert(group.join('|'), 9000);
        }

        // 4) Tên có dấu chấm cuối (chỉ tạo được qua \\?\): Win32 thường hiểu "twin." thành "twin".
        //    - twin (A) / twin. (B) / other (B): "twin." trùng NỘI DUNG với "other" nhưng không thể băm/xóa
        //      đúng tệp qua Win32 -> không được vào nhóm nào.
        //    - twin2 (C) / twin2. (D) / other2 (C): nhóm đúng là {twin2, other2}; nếu "twin2." bị băm nhầm
        //      thành nội dung của "twin2" thì nó sẽ lọt vào nhóm.
        const QString weird = scan + "/weird";
        QDir().mkpath(weird);
        const QByteArray cA = contentFor(800001, 12000), cB = contentFor(800002, 12000);
        const QByteArray cC = contentFor(800003, 13000), cD = contentFor(800004, 13000);
        CHECK(rawWrite(weird + "/twin", cA));
        CHECK(rawWrite(weird + "/twin.", cB));
        CHECK(rawWrite(weird + "/other", cB));
        CHECK(rawWrite(weird + "/twin2", cC));
        CHECK(rawWrite(weird + "/twin2.", cD));
        CHECK(rawWrite(weird + "/other2", cC));
        m.visible.insert(weird + "/twin", 12000);
        m.visible.insert(weird + "/twin.", 12000);
        m.visible.insert(weird + "/other", 12000);
        m.visible.insert(weird + "/twin2", 13000);
        m.visible.insert(weird + "/twin2.", 13000);
        m.visible.insert(weird + "/other2", 13000);
        QStringList twinGroup = {weird + "/other2", weird + "/twin2"};
        std::sort(twinGroup.begin(), twinGroup.end());
        m.duplicateGroups << twinGroup;
        m.groupSize.insert(twinGroup.join('|'), 13000);
        CHECK(rawMkdir(weird + "/dotdir."));
        addFile(m, weird + "/dotdir./inside.bin", 777);
        addFile(m, weird + "/trailing space ", 55);

        // 5) Vài mục cấp 1 + junction/symlink.
        addFile(m, scan + "/top file.bin", 2500);
        addFile(m, scan + "/top.log", 100);
        addFile(m, scan + "/top.lnk", 64, FILE_ATTRIBUTE_NORMAL, false);
        if (CreateHardLinkW(ext(scan + "/hard_top.bin").c_str(), ext(outside + "/sentinel_big.bin").c_str(), nullptr))
            m.visible.insert(scan + "/hard_top.bin", 300000); // hardlink LÀ một tệp thường trong cây

        haveJunction = makeJunction(scan + "/junction_out", outside);
        if (haveJunction)
        {
            CHECK(makeJunction(scan + "/dir_1/s0/jloop", scan));   // junction trỏ ngược lên gốc: đi theo = lặp vô hạn
            CHECK(makeJunction(dups + "/jalias", dups + "/a"));    // bí danh của dups/a: đi theo = "bản trùng" giả
        }
        else
        {
            say("LUU Y: khong tao duoc junction (mklink /J) - bo qua cac kiem tra junction\n");
        }
        // Symlink cần Developer Mode/quyền riêng - không tạo được thì bỏ qua.
        haveFileSymlink = CreateSymbolicLinkW(ext(scan + "/sym_file.bin").c_str(), ext(outside + "/sentinel_big.bin").c_str(),
                                              0x2 /*ALLOW_UNPRIVILEGED_CREATE*/) != 0;
        haveDirSymlink = CreateSymbolicLinkW(ext(scan + "/sym_dir").c_str(), ext(outside).c_str(),
                                             SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2) != 0;
        if (haveFileSymlink)
            m.mustNotSee << scan + "/sym_file.bin";
        if (haveDirSymlink)
            m.mustNotSee << scan + "/sym_dir/sentinel_big.bin";
        say("A. dung cay: %lld tep nhin thay duoc, %lld byte, junction=%d symlink tep=%d symlink thu muc=%d (%lld ms)\n",
            static_cast<long long>(m.visible.size()), static_cast<long long>(m.totalBytes()), haveJunction ? 1 : 0,
            haveFileSymlink ? 1 : 0, haveDirSymlink ? 1 : 0, static_cast<long long>(sectionTimer.elapsed()));
    }

    // Số liệu mong đợi dùng lại ở nhiều phần.
    const qint64 expectedFiles = m.visible.size();
    const qint64 expectedBytes = m.totalBytes();
    qint64 expectedNonEmptyFiles = 0;
    for (auto it = m.visible.constBegin(); it != m.visible.constEnd(); ++it)
        if (it.value() >= 1)
            ++expectedNonEmptyFiles;
    QSet<QString> expectedGroupKeys;
    qint64 expectedWasted = 0;
    for (const QStringList& group : m.duplicateGroups)
    {
        expectedGroupKeys.insert(group.join('|'));
        expectedWasted += m.groupSize.value(group.join('|')) * (group.size() - 1);
    }
    auto canonicalGroupKey = [&](const QStringList& paths) {
        QStringList canonical;
        for (const QString& p : paths)
            canonical << m.hardlinkOf.value(p, p);
        return groupKey(canonical);
    };

    // =================================================================================================
    // B. Kết quả quét so với sổ ghi
    // =================================================================================================
    sectionTimer.restart();
    {
        // B1. FsSafety::walkFiles
        std::atomic_bool stop{false};
        qint64 seenCount = 0, seenBytes = 0;
        int unknown = 0, wrongSize = 0, forbidden = 0;
        QSet<QString> seenPaths;
        CHECK(FsSafety::walkFiles(scan, stop, [&](const FsSafety::WalkEntry& e) {
            ++seenCount;
            seenBytes += e.sizeBytes;
            seenPaths.insert(e.path);
            if (m.mustNotSee.contains(e.path))
                ++forbidden;
            const auto it = m.visible.constFind(e.path);
            if (it == m.visible.constEnd())
                ++unknown;
            else if (it.value() != e.sizeBytes)
                ++wrongSize;
        }));
        say("B1. walkFiles: %lld tep / %lld byte (mong doi %lld / %lld)\n", static_cast<long long>(seenCount),
            static_cast<long long>(seenBytes), static_cast<long long>(expectedFiles), static_cast<long long>(expectedBytes));
        CHECK(seenCount == expectedFiles);
        CHECK(seenBytes == expectedBytes);
        CHECK(seenPaths.size() == expectedFiles); // không tệp nào bị báo hai lần
        CHECK(unknown == 0);
        CHECK(wrongSize == 0);
        CHECK(forbidden == 0);

        // B2. LargeFileScanner: mọi tệp >= 1 byte, sắp giảm dần; rồi cắt còn 50 tệp lớn nhất.
        LargeFileScanner large;
        QList<LargeFileEntry> results;
        QObject::connect(&large, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) { results = r; });
        large.setRootPath(scan);
        large.setMinSizeBytes(1);
        large.setMaxResults(std::numeric_limits<int>::max());
        large.startScan();
        CHECK(large.wait(120000));
        CHECK(results.size() == expectedNonEmptyFiles);
        bool sorted = true, sizesMatch = true, datesValid = true;
        for (int i = 0; i < results.size(); ++i)
        {
            if (i > 0 && results[i - 1].sizeBytes < results[i].sizeBytes)
                sorted = false;
            if (m.visible.value(results[i].path, -1) != results[i].sizeBytes)
                sizesMatch = false;
            if (!results[i].lastModified.isValid())
                datesValid = false;
        }
        CHECK(sorted);
        CHECK(sizesMatch);
        CHECK(datesValid);

        QList<qint64> allSizes = m.visible.values();
        std::sort(allSizes.begin(), allSizes.end(), std::greater<qint64>());
        large.setMaxResults(50);
        results.clear();
        large.startScan();
        CHECK(large.wait(120000));
        CHECK(results.size() == 50);
        bool topMatches = results.size() == 50;
        for (int i = 0; i < results.size() && i < allSizes.size(); ++i)
            if (results[i].sizeBytes != allSizes[i])
                topMatches = false;
        CHECK(topMatches);
        say("B2. LargeFileScanner: %lld tep >= 1 byte; 50 tep lon nhat khop\n", static_cast<long long>(expectedNonEmptyFiles));

        // B3. DuplicateFinder: băm MỌI tệp >= 1 byte (hàng chục nghìn tệp trùng kích thước ngẫu nhiên).
        DuplicateFinder finder;
        QList<DuplicateGroup> groups;
        qint64 wasted = -1;
        int totalGroups = -1;
        QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64 w, int total) {
            groups = g;
            wasted = w;
            totalGroups = total;
        });
        finder.setRootPath(scan);
        finder.setMinSizeBytes(1);
        finder.setMaxGroups(std::numeric_limits<int>::max());
        finder.startScan();
        CHECK(finder.wait(600000));
        int unexpectedGroups = 0, wrongGroupSize = 0, groupWithUnsafePath = 0, misaligned = 0;
        QSet<QString> foundKeys;
        for (const DuplicateGroup& g : groups)
        {
            const QString key = canonicalGroupKey(g.paths);
            foundKeys.insert(key);
            if (!expectedGroupKeys.contains(key))
            {
                if (++unexpectedGroups <= 3)
                    say("  nhom KHONG mong doi: %s\n", qPrintable(g.paths.join(" | ")));
            }
            else if (m.groupSize.value(key) != g.sizeEachBytes)
            {
                ++wrongGroupSize;
            }
            if (g.lastWriteTimes.size() != g.paths.size())
                ++misaligned;
            for (const QString& p : g.paths)
                if (p.endsWith('.') || p.endsWith(' ') || m.mustNotSee.contains(p))
                    ++groupWithUnsafePath;
        }
        say("B3. DuplicateFinder: %lld nhom (mong doi %lld), lang phi %lld byte (mong doi %lld)\n",
            static_cast<long long>(groups.size()), static_cast<long long>(expectedGroupKeys.size()),
            static_cast<long long>(wasted), static_cast<long long>(expectedWasted));
        CHECK(unexpectedGroups == 0);
        CHECK(wrongGroupSize == 0);
        CHECK(misaligned == 0);
        CHECK(groupWithUnsafePath == 0);
        CHECK(foundKeys == expectedGroupKeys);
        CHECK(groups.size() == expectedGroupKeys.size());
        CHECK(totalGroups == expectedGroupKeys.size());
        CHECK(wasted == expectedWasted);
        bool wasteSorted = true;
        for (int i = 1; i < groups.size(); ++i)
            if (groups[i - 1].wastedBytes() < groups[i].wastedBytes())
                wasteSorted = false;
        CHECK(wasteSorted);
        // fileUnchanged đúng với MỌI tệp trong kết quả (dùng ngay trước khi xóa ở giao diện)
        int changed = 0;
        for (const DuplicateGroup& g : groups)
            for (int i = 0; i < g.paths.size(); ++i)
                if (!DuplicateFinder::fileUnchanged(g.paths[i], g.sizeEachBytes, g.lastWriteTimes.value(i)))
                    ++changed;
        CHECK(changed == 0);

        // Cắt còn 10 nhóm: vẫn báo đúng tổng số nhóm thật + tổng lãng phí thật.
        finder.setMaxGroups(10);
        groups.clear();
        finder.startScan();
        CHECK(finder.wait(600000));
        CHECK(groups.size() == qMin<qsizetype>(10, expectedGroupKeys.size()));
        CHECK(totalGroups == expectedGroupKeys.size());
        CHECK(wasted == expectedWasted);

        // B4. CleanupScanner: mỗi mục cấp 1 là một mục; junction/symlink thư mục không được liệt kê.
        QHash<QString, qint64> expectedTop; // tên mục cấp 1 -> tổng byte
        for (auto it = m.visible.constBegin(); it != m.visible.constEnd(); ++it)
        {
            const QString relative = it.key().mid(scan.size() + 1);
            expectedTop[relative.section('/', 0, 0)] += it.value();
        }
        expectedTop.insert("top.lnk", 64); // lối tắt cấp 1 là MỘT mục, tính theo kích thước của chính nó
        if (haveFileSymlink)
            expectedTop.insert("sym_file.bin", 0);
        // (.lnk nằm SÂU bên trong thư mục không được cộng vào kích thước thư mục - walkFiles bỏ qua lối tắt)
        qint64 expectedTopBytes = 0;
        for (auto it = expectedTop.constBegin(); it != expectedTop.constEnd(); ++it)
            expectedTopBytes += it.value();

        CleanupCategory category;
        category.id = CleanupCategoryId::UserTemp;
        category.name = "Stress";
        category.rootPaths = {scan};
        QList<CleanupItem> items;
        qint64 finishedBytes = -1;
        int finishedCount = -1;
        CleanupScanner scanner;
        QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items.push_back(i); });
        QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 bytes, int count) {
            finishedBytes = bytes;
            finishedCount = count;
        });
        scanner.setCategories({category});
        scanner.startScan();
        CHECK(scanner.wait(120000));
        int wrongItems = 0;
        QSet<QString> itemNames;
        for (const CleanupItem& item : items)
        {
            const QString name = item.path.mid(scan.size() + 1);
            itemNames.insert(name);
            if (!expectedTop.contains(name) || expectedTop.value(name) != item.sizeBytes)
            {
                if (++wrongItems <= 5)
                    say("  muc sai: %s = %lld (mong doi %lld)\n", qPrintable(name), static_cast<long long>(item.sizeBytes),
                        static_cast<long long>(expectedTop.value(name, -1)));
            }
        }
        say("B4. CleanupScanner: %lld muc / %lld byte (mong doi %lld / %lld)\n", static_cast<long long>(items.size()),
            static_cast<long long>(finishedBytes), static_cast<long long>(expectedTop.size()), static_cast<long long>(expectedTopBytes));
        CHECK(wrongItems == 0);
        CHECK(items.size() == expectedTop.size());
        CHECK(finishedCount == expectedTop.size());
        CHECK(finishedBytes == expectedTopBytes);
        CHECK(!itemNames.contains("junction_out"));
        CHECK(!itemNames.contains("sym_dir"));

        // Toàn bộ gốc là MỘT mục (kiểu Windows.old) + lọc theo mẫu tên.
        category.keepRootFolder = false;
        items.clear();
        scanner.setCategories({category});
        scanner.startScan();
        CHECK(scanner.wait(120000));
        CHECK(items.size() == 1);
        CHECK(items.size() == 1 && items[0].isDirectory && items[0].sizeBytes == expectedBytes);
        category.keepRootFolder = true;
        category.namePatterns = {"*.bin"};
        items.clear();
        scanner.setCategories({category});
        scanner.startScan();
        CHECK(scanner.wait(120000));
        CHECK(items.size() == (m.visible.contains(scan + "/hard_top.bin") ? 2 : 1) + (haveFileSymlink ? 1 : 0));
        say("B. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // =================================================================================================
    // C. Bật/dừng ở thời điểm ngẫu nhiên + hủy đối tượng lúc đang chạy
    // =================================================================================================
    sectionTimer.restart();
    {
        say("C. bat/dung ngau nhien:\n");
        {
            LargeFileScanner large;
            TerminalSignals terminal;
            QList<LargeFileEntry> last;
            QObject::connect(&large, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) {
                last = r;
                ++terminal.finished;
            });
            QObject::connect(&large, &LargeFileScanner::scanStopped, [&]() { ++terminal.stopped; });
            large.setRootPath(scan);
            large.setMinSizeBytes(1);
            large.setMaxResults(std::numeric_limits<int>::max());
            stormScanner("LargeFileScanner", large, terminal, scaled(300), 15, [&]() { return last.size() == expectedNonEmptyFiles; });
        }
        {
            CleanupCategory category;
            category.id = CleanupCategoryId::UserTemp;
            category.name = "Stress";
            category.rootPaths = {scan};
            category.keepRootFolder = false;
            CleanupCategory second = category;
            second.id = CleanupCategoryId::WindowsTemp;
            second.keepRootFolder = true;
            CleanupScanner scanner;
            TerminalSignals terminal;
            std::atomic<qint64> lastBytes{-1};
            std::atomic<int> categoriesFinished{0};
            QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 bytes, int) {
                lastBytes = bytes;
                ++terminal.finished;
            });
            QObject::connect(&scanner, &CleanupScanner::scanStopped, [&]() { ++terminal.stopped; });
            QObject::connect(&scanner, &CleanupScanner::categoryFinished, [&](CleanupCategoryId, qint64, int) { ++categoriesFinished; });
            scanner.setCategories({category, second});
            stormScanner("CleanupScanner", scanner, terminal, scaled(300), 15, [&]() { return lastBytes.load() == 2 * expectedBytes + 64; });
        }
        {
            DuplicateFinder finder;
            TerminalSignals terminal;
            std::atomic<int> lastTotal{-1};
            QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup>, qint64, int total) {
                lastTotal = total;
                ++terminal.finished;
            });
            QObject::connect(&finder, &DuplicateFinder::scanStopped, [&]() { ++terminal.stopped; });
            finder.setRootPath(scan);
            finder.setMinSizeBytes(1);
            stormScanner("DuplicateFinder", finder, terminal, scaled(300), 60,
                         [&]() { return lastTotal.load() == expectedGroupKeys.size(); });
        }

        // Hủy đối tượng lúc luồng còn đang chạy: destructor phải tự dừng + chờ, không crash, không treo.
        const int destroyRounds = scaled(150);
        QElapsedTimer destroyTimer;
        destroyTimer.start();
        for (int i = 0; i < destroyRounds; ++i)
        {
            const unsigned long delayUs = static_cast<unsigned long>(rnd(3000));
            switch (i % 3)
            {
                case 0:
                {
                    auto* s = new LargeFileScanner;
                    s->setRootPath(scan);
                    s->startScan();
                    QThread::usleep(delayUs);
                    delete s;
                    break;
                }
                case 1:
                {
                    auto* s = new DuplicateFinder;
                    s->setRootPath(scan);
                    s->setMinSizeBytes(1);
                    s->startScan();
                    QThread::usleep(delayUs * 10);
                    delete s;
                    break;
                }
                default:
                {
                    CleanupCategory category;
                    category.id = CleanupCategoryId::UserTemp;
                    category.name = "Stress";
                    category.rootPaths = {scan};
                    auto* s = new CleanupScanner;
                    s->setCategories({category});
                    s->startScan();
                    QThread::usleep(delayUs);
                    delete s;
                    break;
                }
            }
        }
        say("  huy doi tuong luc dang chay: %d luot, %lld ms\n", destroyRounds, static_cast<long long>(destroyTimer.elapsed()));
        CHECK(destroyTimer.elapsed() < 120000);

        // Sau tất cả: cây thư mục KHÔNG bị ai sửa (các lớp quét chỉ đọc).
        std::atomic_bool stop{false};
        qint64 count = 0, bytes = 0;
        FsSafety::walkFiles(scan, stop, [&](const FsSafety::WalkEntry& e) {
            ++count;
            bytes += e.sizeBytes;
        });
        CHECK(count == expectedFiles && bytes == expectedBytes);

        // walkFiles với cờ dừng đã bật sẵn: trả về false, không gọi lại lần nào.
        std::atomic_bool stopNow{true};
        int calls = 0;
        CHECK(!FsSafety::walkFiles(scan, stopNow, [&](const FsSafety::WalkEntry&) { ++calls; }));
        CHECK(calls == 0);
        say("C. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // =================================================================================================
    // D. Xóa hàng nghìn tệp trong lúc luồng khác tạo/xóa/khóa tệp cùng thư mục
    // =================================================================================================
    sectionTimer.restart();
    {
        const QString arena = t + "/arena";
        const QString outside2 = t + "/outside2";
        QDir().mkpath(arena + "/keepdir/nested");
        QDir().mkpath(outside2 + "/deep");
        QDir::setCurrent(t); // đường dẫn TƯƠNG ĐỐI lọt qua sẽ rơi vào thư mục tạm này, không phải nơi khác

        // Tệp canh gác: KHÔNG nằm trong danh sách xóa nào - phải còn nguyên từng byte.
        QHash<QString, QByteArray> guards;
        auto addGuard = [&](const QString& path, qint64 size) {
            const QByteArray content = contentFor(g_contentSeed++, size);
            CHECK(rawWrite(path, content));
            guards.insert(path, content);
        };
        const int keepCount = scaled(1500);
        for (int i = 0; i < keepCount; ++i)
            addGuard(arena + QString("/keep_%1.dat").arg(i, 5, 10, QLatin1Char('0')), 1 + static_cast<qint64>(rnd(300)));
        for (int i = 0; i < 200; ++i)
            addGuard(arena + QString("/keepdir/%1k%2.dat").arg(i % 2 ? "nested/" : "").arg(i), 50);
        addGuard(outside2 + "/sentinel.txt", 400);
        addGuard(outside2 + "/deep/sentinel2.txt", 500);
        addGuard(arena + "/twin", 600);           // "twin." được yêu cầu xóa -> "twin" KHÔNG được mất
        addGuard(arena + "/twin.", 700);          // bản thân "twin." bị từ chối nên cũng còn
        addGuard(arena + "/spaced", 610);
        addGuard(arena + "/spaced ", 710);
        addGuard(t + "/rel_keep.txt", 123);       // sẽ bị yêu cầu xóa bằng đường dẫn TƯƠNG ĐỐI
        addGuard(arena + "/del_guard_twin.tmp", 99); // tên gần giống tệp bị xóa
        const bool arenaJunction = makeJunction(arena + "/j_keep", outside2);

        // Tệp/thư mục SẼ bị xóa.
        struct Victim
        {
            QString path;
            qint64 size;
            bool isDir;
        };
        QList<Victim> victims;
        const int delCount = scaled(4000);
        for (int i = 0; i < delCount; ++i)
        {
            QString name;
            switch (i % 4)
            {
                case 0: name = QString("del_%1.tmp").arg(i, 5, 10, QLatin1Char('0')); break;
                case 1: name = QString::fromUtf8("del_%1 tệp tạm có dấu.tmp").arg(i, 5, 10, QLatin1Char('0')); break;
                case 2: name = QString::fromUtf8("del_%1_日本語😀.tmp").arg(i, 5, 10, QLatin1Char('0')); break;
                default: name = QString("del_%1").arg(i, 5, 10, QLatin1Char('0')) + QString(60, QLatin1Char('z')); break;
            }
            const qint64 size = static_cast<qint64>(rnd(200));
            CHECK(rawWrite(arena + "/" + name, contentFor(g_contentSeed++, size),
                           i % 17 == 3 ? FILE_ATTRIBUTE_READONLY : (i % 29 == 4 ? FILE_ATTRIBUTE_HIDDEN : FILE_ATTRIBUTE_NORMAL)));
            victims.push_back({arena + "/" + name, size, false});
        }
        const int delDirCount = 30;
        for (int d = 0; d < delDirCount; ++d)
        {
            const QString dir = arena + QString("/deldir_%1").arg(d, 2, 10, QLatin1Char('0'));
            QDir().mkpath(dir + "/sub/subsub");
            qint64 total = 0;
            for (int f = 0; f < 20; ++f)
            {
                const qint64 size = 10 + static_cast<qint64>(rnd(100));
                total += size;
                CHECK(rawWrite(dir + (f % 2 ? "/sub/" : "/sub/subsub/") + QString("x%1.bin").arg(f), contentFor(g_contentSeed++, size)));
            }
            // Thư mục bị xóa CHỨA junction trỏ ra ngoài: Shell chỉ được gỡ liên kết, không đụng tới đích.
            if (arenaJunction && d % 3 == 0)
                CHECK(makeJunction(dir + "/sub/j_out", outside2));
            victims.push_back({dir, total, true});
        }

        // Các "bẫy": không mục nào trong số này được phép dẫn tới việc xóa bất kỳ tệp canh gác nào.
        const QStringList traps = {
            QString(),                                   // rỗng
            "   ",
            "rel_keep.txt",                              // tương đối (theo thư mục làm việc = thư mục tạm)
            "./rel_keep.txt",
            arena + "/keep_*.dat",                       // ký tự đại diện: Shell sẽ xóa MỌI tệp khớp
            arena + "/keep_0000?.dat",
            arena + "/twin.",                            // Win32 hiểu thành "twin"
            arena + "/spaced ",                          // Win32 hiểu thành "spaced"
            "\\\\?\\" + QDir::toNativeSeparators(arena + "/twin."), // Qt tự bỏ tiền tố \\?\ - vẫn là "twin."
            arena + "/j_keep",                           // junction thư mục
            QDir::toNativeSeparators(arena + "/keep_00002.dat") + QChar(0) + "x", // NUL chen giữa
        };

        auto verifyGuards = [&](const char* when) {
            int missing = 0, changed = 0;
            for (auto it = guards.constBegin(); it != guards.constEnd(); ++it)
            {
                QByteArray now;
                if (!rawRead(it.key(), &now))
                {
                    if (++missing <= 5)
                        say("  [%s] TEP CANH GAC BI MAT: %s\n", when, qPrintable(it.key()));
                }
                else if (now != it.value())
                {
                    ++changed;
                }
            }
            CHECK(missing == 0);
            CHECK(changed == 0);
            CHECK(rawCountEntries(arena + "/keepdir") == 101);        // 100 tệp + thư mục nested
            CHECK(rawCountEntries(arena + "/keepdir/nested") == 100);
            CHECK(rawCountEntries(outside2) == 2);
            if (arenaJunction)
            {
                const FsSafety::RawInfo link = FsSafety::rawInfo(arena + "/j_keep");
                CHECK(link.exists && link.isDir && link.isLink);
            }
        };

        // Lớp chặn thuần ở RecycleBinOps: chỉ toàn bẫy -> không xóa gì, báo lỗi.
        {
            QString error;
            // (trừ junction: gỡ một liên kết thư mục là thao tác hợp lệ của Shell - chỉ CleanupExecutor mới
            // từ chối nó theo chính sách riêng, kiểm ở phần dưới)
            QStringList directTraps = traps;
            directTraps.removeAll(arena + "/j_keep");
            CHECK(!RecycleBinOps::permanentlyDelete(directTraps, &error));
            CHECK(!error.isEmpty());
            error.clear();
            CHECK(!RecycleBinOps::moveToRecycleBin(directTraps, &error));
            CHECK(!error.isEmpty());
            verifyGuards("bay truc tiep");
        }

        // Chạy executor với luồng gây nhiễu. Trả về số mục còn lại.
        auto runExecutor = [&](const char* label, const QList<Victim>& batch, bool permanent, bool withNoise) -> int {
            QStringList paths;
            QHash<QString, qint64> sizeOf;
            for (const Victim& v : batch)
            {
                paths << v.path;
                sizeOf.insert(v.path.toLower(), v.size);
            }
            // Xen vào giữa: mục lặp (khác hoa/thường, dấu gạch ngược), mục đã tự biến mất, và các bẫy.
            for (int i = 0; i < batch.size(); i += 37)
                paths.insert(static_cast<int>(rnd(paths.size())), i % 2 ? batch[i].path.toUpper() : QDir::toNativeSeparators(batch[i].path));
            const QString ghost1 = arena + "/da_bien_mat_1.tmp";
            const QString ghost2 = arena + "/da_bien_mat_2.tmp";
            paths << ghost1 << ghost2;
            for (const QString& trap : traps)
                paths.insert(static_cast<int>(rnd(paths.size())), trap);
            QList<qint64> sizes; // thẳng hàng với 'paths'
            for (const QString& p : paths)
            {
                const QString key = QDir::cleanPath(QDir::fromNativeSeparators(p)).toLower();
                sizes << (p == ghost1 || p == ghost2 ? 7 : sizeOf.value(key, 0));
            }

            std::vector<char> lockedEver(static_cast<size_t>(batch.size()), 0);
            std::atomic_bool noiseStop{false};
            std::atomic<int> lockCount{0};
            std::thread noise;
            if (withNoise)
            {
                noise = std::thread([&, noiseSeed = g_rng()]() {
                    std::mt19937 r(static_cast<unsigned>(noiseSeed));
                    int n = 0;
                    while (!noiseStop)
                    {
                        // tạo/xóa tệp nhiễu ngay trong thư mục đang bị dọn
                        const QString noisePath = arena + QString("/noise_%1.tmp").arg(n % 40);
                        rawWrite(noisePath, QByteArray("nhieu"));
                        if (n % 3 == 0)
                            DeleteFileW(ext(noisePath).c_str());
                        ++n;
                        // khóa độc quyền một tệp/thư mục con trong danh sách xóa
                        const size_t idx = r() % static_cast<size_t>(batch.size());
                        // Thư mục: chỉ khóa bên trong deldir_00/06/12/18/24 - Shell mất ~1,2 giây cho mỗi thư mục
                        // có tệp đang bị khóa bên trong, và số lần lỗi như vậy có trần (xem RecycleBinOps).
                        if (batch[static_cast<int>(idx)].isDir && batch[static_cast<int>(idx)].path.right(2).toInt() % 6 != 0)
                            continue;
                        const QString target = batch[static_cast<int>(idx)].isDir
                                                   ? batch[static_cast<int>(idx)].path + "/sub/x1.bin"
                                                   : batch[static_cast<int>(idx)].path;
                        const HANDLE h = CreateFileW(ext(target).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                        if (h != INVALID_HANDLE_VALUE)
                        {
                            lockedEver[idx] = 1;
                            ++lockCount;
                            Sleep(r() % 3);
                            CloseHandle(h);
                        }
                        // khóa cả một tệp canh gác (không liên quan tới danh sách xóa)
                        const HANDLE k = CreateFileW(ext(arena + QString("/keep_%1.dat").arg(r() % static_cast<unsigned>(keepCount), 5, 10, QLatin1Char('0'))).c_str(),
                                                     GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                        if (k != INVALID_HANDLE_VALUE)
                            CloseHandle(k);
                    }
                });
            }

            CleanupExecutor executor;
            executor.setItems(paths, sizes);
            executor.setPermanentDelete(permanent);
            std::atomic<int> finishedSignals{0};
            bool success = false;
            QString note;
            qint64 freedBytes = -1;
            int deletedCount = -1;
            QObject::connect(&executor, &CleanupExecutor::executionFinished, [&](bool ok, QString n, qint64 bytes, int count) {
                success = ok;
                note = n;
                freedBytes = bytes;
                deletedCount = count;
                ++finishedSignals;
            });
            QElapsedTimer timer;
            timer.start();
            executor.start();
            const bool exited = executor.wait(15 * 60 * 1000);
            const qint64 elapsed = timer.elapsed();
            noiseStop = true;
            if (noise.joinable())
                noise.join();
            CHECK(exited);
            CHECK(finishedSignals.load() == 1);

            int remaining = 0, remainingNeverLocked = 0, gone = 0;
            qint64 goneBytes = 0;
            for (int i = 0; i < batch.size(); ++i)
            {
                if (rawExists(batch[i].path))
                {
                    ++remaining;
                    if (!lockedEver[static_cast<size_t>(i)])
                    {
                        if (++remainingNeverLocked <= 5)
                            say("  [%s] con lai du KHONG he bi khoa: %s\n", label, qPrintable(batch[i].path));
                    }
                }
                else
                {
                    ++gone;
                    goneBytes += batch[i].size;
                }
            }
            say("  %-22s %lld muc, %d da xoa, %d con lai (luot khoa: %d), bao cao %d muc/%lld byte, %lld ms\n", label,
                static_cast<long long>(batch.size()), gone, remaining, lockCount.load(), deletedCount,
                static_cast<long long>(freedBytes), static_cast<long long>(elapsed));
            CHECK(remainingNeverLocked == 0);                 // chỉ tệp TỪNG bị khóa mới được phép còn lại
            CHECK(deletedCount == gone + 2);                  // + 2 mục "đã tự biến mất" coi như đã đạt
            CHECK(freedBytes == goneBytes + 14);
            CHECK(success == (gone + 2 > 0));
            CHECK(note.contains(QString::fromUtf8("bị bỏ qua vì lý do an toàn"))); // các bẫy được báo là bị từ chối
            if (!withNoise)
                CHECK(remaining == 0);
            verifyGuards(label);
            return remaining;
        };

        say("D. xoa trong luc bi gay nhieu:\n");
        const int recycleFiles = qMin(recycleFileCount, static_cast<int>(victims.size()) - delDirCount);
        QList<Victim> recycleBatch = victims.mid(0, recycleFiles);
        recycleBatch += victims.mid(victims.size() - delDirCount, 6); // 6 thư mục vào Thùng rác
        QList<Victim> permanentBatch = victims.mid(recycleFiles, victims.size() - delDirCount - recycleFiles);
        permanentBatch += victims.mid(victims.size() - delDirCount + 6);

        const RecycleBinOps::Status binBefore = RecycleBinOps::queryStatus();
        if (!recycleBatch.isEmpty())
            runExecutor("vao Thung rac + nhieu", recycleBatch, false, true);
        const RecycleBinOps::Status binAfter = RecycleBinOps::queryStatus();
        say("  Thung rac: %lld -> %lld muc\n", static_cast<long long>(binBefore.itemCount), static_cast<long long>(binAfter.itemCount));
        runExecutor("xoa vinh vien + nhieu", permanentBatch, true, true);

        // Hết nhiễu: phần còn lại (từng bị khóa) phải xóa được sạch.
        QList<Victim> leftovers;
        for (const Victim& v : victims)
            if (rawExists(v.path))
                leftovers << v;
        if (!leftovers.isEmpty())
            runExecutor("phan con lai", leftovers, true, false);
        int stillThere = 0;
        for (const Victim& v : victims)
            if (rawExists(v.path))
                ++stillThere;
        CHECK(stillThere == 0);

        // Hai luồng cùng gọi RecycleBinOps trên hai danh sách rời nhau.
        {
            QStringList listA, listB;
            for (int i = 0; i < scaled(300); ++i)
            {
                listA << arena + QString("/par_a_%1.tmp").arg(i);
                listB << arena + QString("/par_b_%1.tmp").arg(i);
                rawWrite(listA.last(), contentFor(g_contentSeed++, 20));
                rawWrite(listB.last(), contentFor(g_contentSeed++, 20));
            }
            bool okA = false, okB = false;
            std::thread ta([&]() { okA = RecycleBinOps::permanentlyDelete(listA, nullptr); });
            std::thread tb([&]() { okB = RecycleBinOps::permanentlyDelete(listB, nullptr); });
            ta.join();
            tb.join();
            int left = 0;
            for (const QString& p : listA + listB)
                if (rawExists(p))
                    ++left;
            CHECK(okA && okB);
            CHECK(left == 0);
            verifyGuards("hai luong");
        }

        // Một tệp bị khóa GIỮA danh sách không được chặn các tệp đứng sau nó (hồi quy: Shell dừng cả lô).
        {
            QStringList list;
            for (int i = 0; i < 200; ++i)
            {
                list << arena + QString("/lock_%1.tmp").arg(i, 3, 10, QLatin1Char('0'));
                rawWrite(list.last(), QByteArray("x"));
            }
            std::vector<HANDLE> locks;
            const QList<int> lockedIndexes = {0, 1, 63, 64, 65, 130, 199};
            for (int idx : lockedIndexes)
                locks.push_back(CreateFileW(ext(list[idx]).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr));
            QString error;
            CHECK(!RecycleBinOps::permanentlyDelete(list, &error));
            CHECK(!error.isEmpty());
            int left = 0, wrongLeft = 0;
            for (int i = 0; i < list.size(); ++i)
            {
                if (rawExists(list[i]))
                {
                    ++left;
                    if (!lockedIndexes.contains(i))
                        ++wrongLeft;
                }
            }
            say("  khoa 7/200 tep giua danh sach: con lai %d (mong doi 7)\n", left);
            CHECK(left == lockedIndexes.size());
            CHECK(wrongLeft == 0);
            for (HANDLE h : locks)
                if (h != INVALID_HANDLE_VALUE)
                    CloseHandle(h);
            CHECK(RecycleBinOps::permanentlyDelete(list, &error)); // hết khóa: xóa nốt, mục đã mất được bỏ qua
            for (const QString& p : list)
                if (rawExists(p))
                    ++wrongLeft;
            CHECK(wrongLeft == 0);
            verifyGuards("tep bi khoa");
        }
        QDir::setCurrent(QCoreApplication::applicationDirPath());
        say("D. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // =================================================================================================
    // E. Fuzz các hàm kiểm an toàn đường dẫn
    // =================================================================================================
    sectionTimer.restart();
    {
        say("E. fuzz duong dan:\n");
        const QStringList prot = {"C:/Users/Ai Do", "C:/Program Files", "C:/Windows", "D:/Du Lieu/Ho So", "E:/a/b/c"};

        // Biến một đường dẫn thành một BÍ DANH mà Win32 hiểu là cùng một nơi.
        auto alias = [&](const QString& base, bool allowExotic) {
            QStringList parts = base.split('/', Qt::SkipEmptyParts); // "C:", "Users", "Ai Do"
            QString out;
            for (int i = 0; i < parts.size(); ++i)
            {
                QString part = parts[i];
                if (rnd(3) == 0)
                    part = rnd(2) ? part.toUpper() : part.toLower();
                if (i > 0)
                {
                    const int k = static_cast<int>(rnd(8));
                    if (k == 0)
                        out += "/.";
                    else if (k == 1)
                        out += "/zz/..";
                    else if (k == 2)
                        out += "/";
                    if (allowExotic && rnd(6) == 0)
                        part += rnd(2) ? "." : (i == parts.size() - 1 ? " " : ".");
                }
                out += (i == 0 ? QString() : QString(rnd(2) ? "/" : "\\")) + part;
            }
            const int tail = static_cast<int>(rnd(6));
            if (tail == 0)
                out += "/";
            else if (tail == 1)
                out += "\\\\";
            else if (tail == 2)
                out += "/.";
            else if (tail == 3 && allowExotic)
                out += rnd(2) ? "." : "  ";
            if (allowExotic)
            {
                const int prefix = static_cast<int>(rnd(8));
                if (prefix == 0)
                    out = "\\\\?\\" + QDir::toNativeSeparators(out);
                else if (prefix == 1)
                    out = "\\\\.\\" + QDir::toNativeSeparators(out);
                else if (prefix == 2 && out.size() >= 2 && out[1] == ':')
                    out = QString(rnd(2) ? "\\\\localhost\\" : "//127.0.0.1/") + out[0] + "$" + out.mid(2);
            }
            if (rnd(5) == 0)
                out = " " + out + "  ";
            return out;
        };

        // E1. Mọi bí danh của gốc ổ đĩa / thư mục được bảo vệ / thư mục CHA của chúng đều phải bị từ chối.
        FuzzProperty dangerousRejected{"bi danh nguy hiem bi tu choi"};
        QStringList dangerousBases = {"C:", "D:", "E:", "c:"};
        for (const QString& p : prot)
        {
            QString cur = p;
            while (cur.count('/') >= 1)
            {
                dangerousBases << cur;
                cur = cur.left(cur.lastIndexOf('/'));
            }
        }
        for (int i = 0; i < scaled(30000); ++i)
        {
            const QString candidate = alias(dangerousBases[static_cast<int>(rnd(dangerousBases.size()))], true);
            dangerousRejected.expect(!FsSafety::unsafeCleanupRootReason(candidate, prot).isEmpty(), candidate);
        }
        CHECK_FUZZ(dangerousRejected);

        // E2. Thư mục CON hợp lệ (cách viết bình thường) không bị chặn nhầm.
        FuzzProperty safeAccepted{"thu muc con hop le duoc nhan"};
        const QStringList safeBases = {"C:/Windows/Temp", "C:/Users/Ai Do/AppData/Local/Temp", "C:/Program Files/App/Cache",
                                       "D:/Du Lieu/Ho So/x", "E:/a/b/c/d", "C:/UsersBackup", "C:/Windows.old", "F:/Temp",
                                       "//may/chiase/Temp", "C:/Users/Ai Do2"};
        for (int i = 0; i < scaled(15000); ++i)
        {
            const QString base = safeBases[static_cast<int>(rnd(safeBases.size()))];
            const QString candidate = base.startsWith("//") ? base : alias(base, false);
            safeAccepted.expect(FsSafety::unsafeCleanupRootReason(candidate, prot).isEmpty(), candidate);
        }
        CHECK_FUZZ(safeAccepted);

        // E3. Chuỗi ghép ngẫu nhiên từ các mảnh "gần đúng": hễ được CHẤP NHẬN thì trọng tài độc lập cũng phải
        //     thấy nó không nguy hiểm; và mọi thứ unsafeShellPathReason chấp nhận thì bất biến qua Win32.
        FuzzProperty acceptedIsSafe{"duoc nhan => trong tai dong y"};
        FuzzProperty shellStable{"shell-safe => Win32 khong doi"};
        const QStringList pieces = {"C:", "c:", "D:", "E:", "/", "\\", "/", "Users", "users", "Ai Do", "AI DO", "Windows", "Program Files",
                                    "Temp", "a", "b", "c", "Du Lieu", "Ho So", ".", "..", " ", "?", "*", "$", "~1", "PROGRA~1", "//",
                                    "\\\\?\\", "\\\\.\\", "localhost", "C$", "admin$", "x", ":", "\"", "<", "|", "NUL", "con", "UNC",
                                    "%TEMP%", "\t", "..."};
        int acceptedCount = 0;
        for (int i = 0; i < scaled(60000); ++i)
        {
            // Một nửa số ca bắt đầu bằng "X:/" và có dấu gạch giữa các mảnh - để có đủ ca "gần hợp lệ".
            QString candidate;
            const int n = 1 + static_cast<int>(rnd(9));
            const bool wellFormed = rnd(2) == 0;
            if (wellFormed)
                candidate = pieces[static_cast<int>(rnd(4))] + (rnd(2) ? "/" : "\\");
            for (int k = 0; k < n; ++k)
            {
                candidate += pieces[static_cast<int>(rnd(pieces.size()))];
                if (wellFormed && rnd(10) < 7)
                    candidate += rnd(2) ? "/" : "\\";
            }
            const bool accepted = FsSafety::unsafeCleanupRootReason(candidate, prot).isEmpty();
            if (accepted)
            {
                ++acceptedCount;
                acceptedIsSafe.expect(!oracleDangerous(candidate, prot), candidate);
            }
            if (FsSafety::unsafeShellPathReason(candidate).isEmpty())
            {
                const std::wstring native = QString(candidate).replace(QLatin1Char('/'), QLatin1Char('\\')).toStdWString();
                wchar_t full[4096] = {};
                const DWORD len = GetFullPathNameW(native.c_str(), 4096, full, nullptr);
                const bool stable = len > 0 && len < 4096 && _wcsicmp(full, native.c_str()) == 0 &&
                                    candidate.indexOf('*') < 0 && candidate.indexOf('?') < 0;
                shellStable.expect(stable, candidate);
            }
        }
        say("  (chuoi ngau nhien duoc chap nhan: %d)\n", acceptedCount);
        CHECK_FUZZ(acceptedIsSafe);
        CHECK_FUZZ(shellStable);
        CHECK(acceptedCount > 100); // bộ sinh phải tạo ra đủ ca "hợp lệ" thì tính chất trên mới có nghĩa

        // E4. Bảng cố định các dạng nguy hiểm đã biết.
        for (const char* bad : {"", " ", ".", "..", "/", "\\", "C:", "C:/", "c:\\", "C:\\.", "C:/..", "C:/x/..", "C:\\\\", "C:/./",
                                "\\\\?\\C:\\", "\\\\?\\C:", "\\\\.\\C:\\", "//?/C:/", "\\\\?\\C:\\Windows", "\\\\?\\C:\\Users",
                                "\\\\.\\C:\\Users\\Ai Do", "\\\\server\\share", "\\\\server\\share\\", "\\\\?\\UNC\\server\\share",
                                "\\\\localhost\\C$", "\\\\localhost\\C$\\Users", "\\\\127.0.0.1\\c$\\WINDOWS", "\\\\may\\ADMIN$\\Temp",
                                "C:/Windows.", "C:/Windows ", "C:/Windows./", "C:/Users./Ai Do", "C:/Users/Ai Do.", "C:Windows",
                                "C:../x", "\\Temp", "Temp", "~", "%TEMP%", "C:/Windows::$DATA", "C:/Users:stream", "C:/Temp/*",
                                "C:/Temp/a?c", "C:/Te\"mp", "C:/Temp/<x>", "C:/Temp/a|b", "\\\\.\\PhysicalDrive0",
                                "\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\", "\\\\?\\GLOBALROOT\\Device\\HarddiskVolume1\\x",
                                "\\??\\C:\\Windows", "C:/Program Files/../Windows", "C:/Users/Ai Do/..", "C:/Users/Ai Do/../../Windows"})
            CHECK(!FsSafety::unsafeCleanupRootReason(bad, prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason(QString("C:/Temp") + QChar(0) + "x", prot).isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason("C:/").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason("C:/Temp/x.").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason("C:/Temp/x ").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason(" C:/Temp/x").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason("C:/Temp/*.tmp").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason("x.tmp").isEmpty());
        CHECK(FsSafety::unsafeShellPathReason("C:/Temp/x.tmp").isEmpty());
        CHECK(FsSafety::unsafeShellPathReason(t + QString::fromUtf8("/thư mục có dấu/tệp 😀.bin")).isEmpty());
        CHECK(FsSafety::unsafeShellPathReason(t + "/" + QString(300, 'd') + "/x.bin").isEmpty()); // đường dẫn dài vẫn hợp lệ

        // Tên ngắn 8.3 của một thư mục được bảo vệ THẬT (nếu ổ đĩa có bật tên ngắn).
        const QString windowsDir = FsSafety::windowsDirectory();
        const QString programFiles = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
        if (!programFiles.isEmpty())
        {
            wchar_t shortPath[MAX_PATH] = {};
            const DWORD len = GetShortPathNameW(QDir::toNativeSeparators(programFiles).toStdWString().c_str(), shortPath, MAX_PATH);
            const QString shortName = QDir::fromNativeSeparators(QString::fromWCharArray(shortPath, static_cast<int>(len)));
            if (len > 0 && len < MAX_PATH && shortName.compare(programFiles, Qt::CaseInsensitive) != 0)
            {
                say("  ten ngan 8.3: %s = %s\n", qPrintable(shortName), qPrintable(programFiles));
                CHECK(!FsSafety::unsafeCleanupRootReason(shortName, {programFiles, windowsDir}).isEmpty());
                CHECK(!FsSafety::unsafeCleanupRootReason(programFiles, {shortName}).isEmpty());
            }
            else
            {
                say("  (o dia khong co ten ngan 8.3 cho %s - bo qua)\n", qPrintable(programFiles));
            }
        }

        // E5. rawInfo/existsNoFollow/fileIdentity với đầu vào kỳ quặc (không có đường dẫn mạng nào) + rò handle.
        const QString realFile = scan + "/top file.bin";
        const DWORD handlesBefore = handleCount();
        const QStringList odd = {QString(), " ", ".", "..", "C:", "/", "\\", "nul", "NUL", "C:/nul", "con", "C:/con.txt",
                                 t + "/khong_ton_tai", t + "/*", scan + "/*", scan + "/top*", scan + "/t?p file.bin", t + "/scan/",
                                 QString(40000, QLatin1Char('a')), "C:/" + QString(40000, QLatin1Char('b')),
                                 scan + QChar(0) + "x", realFile + "::$DATA", scan + "/weird/twin.", scan + "/weird/dotdir."};
        for (const QString& p : {QString("//"), QString("\\\\?\\C:\\"), QString("\\\\?\\") + QDir::toNativeSeparators(realFile)})
        {
            FsSafety::rawInfo(p); // một lần là đủ: chỉ cần không crash
            FsSafety::existsNoFollow(p);
        }
        for (int round = 0; round < scaled(300); ++round)
        {
            for (const QString& p : odd)
            {
                FsSafety::rawInfo(p);
                FsSafety::existsNoFollow(p);
                FsSafety::FileIdentity id;
                FsSafety::fileIdentity(p, &id);
            }
            FsSafety::FileIdentity id;
            FsSafety::fileIdentity(realFile, &id);
        }
        // Ký tự đại diện không bao giờ được coi là "tồn tại" (FindFirstFile sẽ khớp một tệp KHÁC).
        CHECK(!FsSafety::existsNoFollow(scan + "/*"));
        CHECK(!FsSafety::existsNoFollow(scan + "/top*"));
        CHECK(!FsSafety::existsNoFollow(scan + "/t?p file.bin"));
        CHECK(FsSafety::existsNoFollow(realFile));
        CHECK(FsSafety::existsNoFollow(scan + "/weird/twin."));      // chính nó, không phải "twin"
        CHECK(FsSafety::rawInfo(scan + "/weird/twin.").sizeBytes == 12000);
        CHECK(FsSafety::rawInfo(scan + "/weird/dotdir.").isDir);
        CHECK(!FsSafety::existsNoFollow(scan + "/weird/twin.."));
        for (int round = 0; round < 3; ++round)
        {
            std::atomic_bool stop{false};
            FsSafety::walkFiles(scan, stop, [](const FsSafety::WalkEntry&) {});
        }
        const DWORD handlesAfter = handleCount();
        say("  handle truoc/sau: %lu -> %lu\n", handlesBefore, handlesAfter);
        CHECK(handlesAfter <= handlesBefore + 8);

        // E6. RecycleBinOps::internal::recycleVerdict
        FuzzProperty verdict{"recycleVerdict"};
        using RecycleBinOps::internal::DriveKind;
        for (int i = 0; i < scaled(20000); ++i)
        {
            const DriveKind kind = static_cast<DriveKind>(rnd(4));
            const bool nuke = rnd(4) == 0;
            const qint64 capMb = rnd(5) == 0 ? -1 : static_cast<qint64>(rnd(0x100000000ULL));
            const qint64 size = rnd(3) == 0 ? static_cast<qint64>(g_rng() >> 1) : static_cast<qint64>(rnd(1ULL << 42));
            const bool refused = !RecycleBinOps::internal::recycleVerdict(kind, nuke, capMb, size).isEmpty();
            const bool mustRefuse = kind != DriveKind::Fixed || nuke || (capMb >= 0 && size > capMb * 1024 * 1024);
            verdict.expect(refused == mustRefuse, QString("kind=%1 nuke=%2 cap=%3 size=%4").arg(int(kind)).arg(nuke).arg(capMb).arg(size));
        }
        CHECK_FUZZ(verdict);
        say("E. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // =================================================================================================
    // F. Fuzz đổi kích thước phân vùng (THUẦN - không lệnh nào được chạy)
    // =================================================================================================
    sectionTimer.restart();
    {
        say("F. fuzz phan vung:\n");
        using namespace PartitionManager;
        const double GB = 1024.0 * 1024.0 * 1024.0;
        auto randomSize = [&]() -> qint64 {
            switch (rnd(8))
            {
                case 0: return 0;
                case 1: return -static_cast<qint64>(rnd(1ULL << 40));
                case 2: return std::numeric_limits<qint64>::max();
                case 3: return std::numeric_limits<qint64>::min();
                case 4: return static_cast<qint64>(rnd(1ULL << 62));
                default: return static_cast<qint64>(rnd(4000ULL)) * 1024 * 1024 * 512; // bội số của 512 MB tới ~2 TB
            }
        };
        const QList<double> specialGb = {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                         -std::numeric_limits<double>::infinity(), 0.0, -0.0, -1.0, 1e-300, 1e300, -1e300,
                                         std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min(),
                                         8589934591.99, 8589934592.0, 8589934592.01, 1e10, 9.3e18};

        // F1. clampResizeBytes: không bao giờ ra ngoài [min, max]; NaN/khoảng hỏng -> 0; đơn điệu.
        FuzzProperty clampInRange{"clamp trong khoang"};
        FuzzProperty clampMonotonic{"clamp don dieu"};
        FuzzProperty clampExact{"clamp dung gia tri"};
        for (int i = 0; i < scaled(60000); ++i)
        {
            SupportedSizeRange range;
            range.ok = rnd(10) != 0;
            range.minBytes = randomSize();
            range.maxBytes = randomSize();
            if (rnd(3) != 0 && range.minBytes > range.maxBytes)
                std::swap(range.minBytes, range.maxBytes);
            const double gb = rnd(6) == 0 ? specialGb[static_cast<int>(rnd(specialGb.size()))]
                                          : (static_cast<double>(rnd(400000)) / 100.0 - (rnd(20) == 0 ? 500.0 : 0.0));
            const qint64 result = internal::clampResizeBytes(gb, range);
            const bool validRange = range.ok && range.minBytes >= 0 && range.maxBytes >= range.minBytes;
            const QString input = QString("gb=%1 min=%2 max=%3 ok=%4 -> %5").arg(gb).arg(range.minBytes).arg(range.maxBytes).arg(range.ok).arg(result);
            if (!validRange || std::isnan(gb))
            {
                clampInRange.expect(result == 0, input);
                continue;
            }
            clampInRange.expect(result >= range.minBytes && result <= range.maxBytes, input);
            const double bytes = gb * GB;
            if (std::isfinite(bytes) && bytes >= static_cast<double>(range.minBytes) + 1 && bytes <= static_cast<double>(range.maxBytes) - 1 &&
                std::fabs(bytes) < 9e18)
                clampExact.expect(result == static_cast<qint64>(bytes), input);
            const double other = gb + static_cast<double>(rnd(1000)) / 100.0;
            if (std::isfinite(gb) && std::isfinite(other))
                clampMonotonic.expect(internal::clampResizeBytes(other, range) >= result, input);
            // Kết quả của clamp luôn qua được/không qua được resizeBlockReason một cách NHẤT QUÁN
            if (result > 0)
            {
                PartitionInfo p;
                p.diskNumber = 0;
                p.partitionNumber = 1;
                p.sizeBytes = 123456789;
                const bool allowed = internal::resizeBlockReason(0, 1, 123456789, {p}, range, result).isEmpty();
                clampInRange.expect(allowed == (result != p.sizeBytes), input);
            }
        }
        CHECK_FUZZ(clampInRange);
        CHECK_FUZZ(clampMonotonic);
        CHECK_FUZZ(clampExact);
        SupportedSizeRange plain;
        plain.ok = true;
        plain.minBytes = 10 * 1024LL * 1024 * 1024;
        plain.maxBytes = 100 * 1024LL * 1024 * 1024;
        CHECK(internal::clampResizeBytes(std::numeric_limits<double>::infinity(), plain) == plain.maxBytes);
        CHECK(internal::clampResizeBytes(1e300, plain) == plain.maxBytes);
        CHECK(internal::clampResizeBytes(-std::numeric_limits<double>::infinity(), plain) == plain.minBytes);
        CHECK(internal::clampResizeBytes(std::numeric_limits<double>::quiet_NaN(), plain) == 0);

        // F2. resizeBlockReason: hễ ĐƯỢC PHÉP thì mọi điều kiện an toàn phải đúng (kiểm độc lập).
        FuzzProperty allowedIsSafe{"resize duoc phep => an toan"};
        FuzzProperty validIsAllowed{"resize hop le duoc phep"};
        int allowedCount = 0;
        for (int i = 0; i < scaled(80000); ++i)
        {
            QList<PartitionInfo> list;
            const int n = static_cast<int>(rnd(6));
            for (int k = 0; k < n; ++k)
            {
                PartitionInfo p;
                p.diskNumber = static_cast<int>(rnd(4)) - (rnd(15) == 0 ? 2 : 0);
                p.partitionNumber = static_cast<int>(rnd(5)) - (rnd(15) == 0 ? 2 : 0);
                p.sizeBytes = randomSize();
                list << p;
            }
            SupportedSizeRange range;
            range.ok = rnd(8) != 0;
            range.minBytes = randomSize();
            range.maxBytes = randomSize();
            if (rnd(4) != 0 && range.minBytes > range.maxBytes)
                std::swap(range.minBytes, range.maxBytes);
            const int disk = static_cast<int>(rnd(5)) - 1;
            const int part = static_cast<int>(rnd(6)) - 1;
            qint64 sizeAtQuery = randomSize();
            qint64 newSize = randomSize();
            if (!list.isEmpty() && rnd(2) == 0) // nghiêng về ca "gần hợp lệ"
            {
                const PartitionInfo& pick = list[static_cast<int>(rnd(list.size()))];
                sizeAtQuery = pick.sizeBytes;
                if (rnd(2) == 0 && range.maxBytes > range.minBytes && range.minBytes >= 0)
                    newSize = range.minBytes + static_cast<qint64>(rnd(static_cast<quint64>(range.maxBytes - range.minBytes) + 1));
            }
            const int useDisk = (!list.isEmpty() && rnd(2) == 0) ? list[0].diskNumber : disk;
            const int usePart = (!list.isEmpty() && rnd(2) == 0) ? list[0].partitionNumber : part;
            if (!list.isEmpty() && rnd(3) == 0)
                sizeAtQuery = list[0].sizeBytes;

            const bool allowed = internal::resizeBlockReason(useDisk, usePart, sizeAtQuery, list, range, newSize).isEmpty();
            // Phân vùng ĐẦU TIÊN khớp (đĩa, số) trong danh sách vừa đọc lại.
            const PartitionInfo* current = nullptr;
            for (const PartitionInfo& p : list)
                if (p.diskNumber == useDisk && p.partitionNumber == usePart)
                {
                    current = &p;
                    break;
                }
            const bool safe = useDisk >= 0 && usePart >= 0 && current && sizeAtQuery > 0 && current->sizeBytes == sizeAtQuery &&
                              range.ok && range.minBytes >= 0 && range.minBytes <= range.maxBytes && newSize > 0 &&
                              newSize >= range.minBytes && newSize <= range.maxBytes && newSize != current->sizeBytes;
            const QString input = QString("disk=%1 part=%2 atQuery=%3 new=%4 min=%5 max=%6 ok=%7 n=%8")
                                      .arg(useDisk).arg(usePart).arg(sizeAtQuery).arg(newSize).arg(range.minBytes).arg(range.maxBytes).arg(range.ok).arg(n);
            if (allowed)
            {
                ++allowedCount;
                allowedIsSafe.expect(safe, input);
            }
            else
            {
                validIsAllowed.expect(!safe, input);
            }
        }
        say("  (so ca resize duoc phep: %d)\n", allowedCount);
        CHECK_FUZZ(allowedIsSafe);
        CHECK_FUZZ(validIsAllowed);
        CHECK(allowedCount > 50);

        // F3. isSameSizeAtInputPrecision: ô nhập 2 chữ số thập phân CHƯA sửa = không đổi gì.
        FuzzProperty samePrecision{"o nhap chua sua = khong doi"};
        for (int i = 0; i < scaled(30000); ++i)
        {
            const qint64 current = 1 + static_cast<qint64>(rnd(4ULL * 1024 * 1024 * 1024 * 1024)); // tới 4 TB, lẻ tới từng byte
            const double shown = std::round(static_cast<double>(current) / GB * 100.0) / 100.0;    // QDoubleSpinBox(decimals=2)
            const QString input = QString::number(current);
            samePrecision.expect(internal::isSameSizeAtInputPrecision(shown, current), input);
            samePrecision.expect(!internal::isSameSizeAtInputPrecision(shown + 0.01, current), input);
            samePrecision.expect(!internal::isSameSizeAtInputPrecision(shown - 0.01, current), input);
        }
        CHECK_FUZZ(samePrecision);
        CHECK(!internal::isSameSizeAtInputPrecision(std::numeric_limits<double>::quiet_NaN(), 1000));
        // Ca thật: 500.107.862.016 byte hiện là 465,76 GB; đổi ngược ra byte KHÁC kích thước thật.
        CHECK(static_cast<qint64>(465.76 * GB) != 500107862016LL);
        CHECK(internal::isSameSizeAtInputPrecision(465.76, 500107862016LL));

        // F4. Phân tích JSON: đột biến ngẫu nhiên + giá trị cực đoan - không crash, kết quả luôn nhất quán.
        const QByteArray goodPartitions = R"([{"DiskNumber":0,"PartitionNumber":1,"DriveLetter":"","Type":"System","Size":104857600,"IsBoot":false,"IsSystem":true,"IsActive":false,"FileSystem":"FAT32","Label":"","SizeRemaining":70000000},{"DiskNumber":0,"PartitionNumber":3,"DriveLetter":"C","Type":"Basic","Size":255000000000,"IsBoot":true,"IsSystem":false,"IsActive":false,"FileSystem":"NTFS","Label":"Windows","SizeRemaining":100000000000}])";
        const QByteArray goodRange = R"({"SizeMin":120000000000,"SizeMax":255000000000})";
        const QByteArray goodVpn = QString::fromUtf8(R"([{"Name":"Hà Lan ’1’","ServerAddress":"nl.example.com","TunnelType":"Ikev2","ConnectionStatus":"Connected"},{"Name":"B","ServerAddress":"b","TunnelType":"Sstp","ConnectionStatus":"Disconnected"}])").toUtf8();
        const QList<QByteArray> tokens = {"1e308", "-1e308", "1e999", "-0", "9223372036854775807", "9223372036854775808",
                                          "-9223372036854775809", "18446744073709551616", "null", "true", "[]", "{}", "\"\"",
                                          "\"x\"", "NaN", "Infinity", "0.0000001", "1.5", "[[[[[[[[", "\xEF\xBB\xBF", "\\u0000", "\x00"};
        auto mutate = [&](QByteArray json) {
            const int edits = 1 + static_cast<int>(rnd(4));
            for (int e = 0; e < edits && !json.isEmpty(); ++e)
            {
                const int pos = static_cast<int>(rnd(json.size()));
                switch (rnd(6))
                {
                    case 0: json[pos] = static_cast<char>(rnd(256)); break;
                    case 1: json.remove(pos, 1 + static_cast<int>(rnd(8))); break;
                    case 2: json.insert(pos, tokens[static_cast<int>(rnd(tokens.size()))]); break;
                    case 3: json.truncate(pos); break;
                    case 4:
                    {
                        // thay MỘT số bằng một giá trị cực đoan
                        const int digit = json.indexOf(':', pos);
                        if (digit >= 0)
                            json.insert(digit + 1, tokens[static_cast<int>(rnd(8))] + ",\"x\":");
                        break;
                    }
                    default: json.insert(pos, json.mid(static_cast<int>(rnd(json.size())), static_cast<int>(rnd(40)))); break;
                }
            }
            return json;
        };
        FuzzProperty rangeJson{"parseSupportedSizeJson"};
        FuzzProperty partitionsJson{"parsePartitionsJson"};
        FuzzProperty vpnJson{"parseConnectionsJson"};
        for (int i = 0; i < scaled(30000); ++i)
        {
            QString error;
            const QByteArray a = mutate(goodRange);
            const SupportedSizeRange r = internal::parseSupportedSizeJson(a, &error);
            rangeJson.expect(!r.ok || (r.minBytes >= 0 && r.maxBytes > 0 && r.maxBytes >= r.minBytes), QString::fromLatin1(a.toPercentEncoding()));
            rangeJson.expect(r.ok || !error.isEmpty(), QString::fromLatin1(a.toPercentEncoding()));

            error.clear();
            const QByteArray b = mutate(goodPartitions);
            const QList<PartitionInfo> list = internal::parsePartitionsJson(b, &error);
            // Không phân vùng nào từ JSON hỏng được phép qua lớp kiểm cuối với kích thước "bịa" (<= 0)
            for (const PartitionInfo& p : list)
                if (p.sizeBytes <= 0)
                    partitionsJson.expect(!internal::resizeBlockReason(p.diskNumber, p.partitionNumber, p.sizeBytes, list, plain, plain.minBytes).isEmpty(),
                                          QString::fromLatin1(b.toPercentEncoding()));
            partitionsJson.expect(list.size() <= 64, QString::fromLatin1(b.toPercentEncoding()));

            error.clear();
            const QByteArray c = mutate(goodVpn);
            const QList<VpnConnectionStatus> vpn = VpnController::internal::parseConnectionsJson(c, &error);
            vpnJson.expect(vpn.size() <= 64, QString::fromLatin1(c.toPercentEncoding()));
        }
        CHECK_FUZZ(rangeJson);
        CHECK_FUZZ(partitionsJson);
        CHECK_FUZZ(vpnJson);

        // Giá trị cực đoan: bão hòa chứ không tràn thành số âm khổng lồ.
        QString error;
        QList<PartitionInfo> huge = internal::parsePartitionsJson(R"({"DiskNumber":0,"PartitionNumber":1,"Size":1e300,"SizeRemaining":-1e300})", &error);
        CHECK(huge.size() == 1 && huge[0].sizeBytes == std::numeric_limits<qint64>::max());
        CHECK(huge.size() == 1 && huge[0].freeBytes == std::numeric_limits<qint64>::min());
        CHECK(!internal::parseSupportedSizeJson(R"({"SizeMin":1e300,"SizeMax":1000})", &error).ok);
        CHECK(!internal::parseSupportedSizeJson(R"({"SizeMin":-5,"SizeMax":1000})", &error).ok);
        CHECK(!internal::parseSupportedSizeJson(R"({"SizeMin":"100","SizeMax":"1000"})", &error).ok);
        CHECK(!internal::parseSupportedSizeJson(R"([{"SizeMin":1,"SizeMax":1000}])", &error).ok);
        CHECK(internal::parseSupportedSizeJson(R"({"SizeMin":0,"SizeMax":1e300})", &error).maxBytes == std::numeric_limits<qint64>::max());
        CHECK(internal::parsePartitionsJson(QByteArray(200000, '['), &error).isEmpty());
        CHECK(internal::parsePartitionsJson(QByteArray(5 * 1024 * 1024, ' ') + "[]", &error).isEmpty());
        // Thiếu trường Size -> kích thước 0 -> không bao giờ qua được lớp kiểm cuối dù "khớp" 0 == 0.
        QList<PartitionInfo> noSize = internal::parsePartitionsJson(R"({"DiskNumber":2,"PartitionNumber":1})", &error);
        CHECK(noSize.size() == 1 && noSize[0].sizeBytes == 0);
        CHECK(!internal::resizeBlockReason(2, 1, 0, noSize, plain, plain.minBytes).isEmpty());

        // F5. Script dựng từ số: chỉ đúng các con số đó xuất hiện sau tham số.
        FuzzProperty scripts{"script phan vung"};
        static const QRegularExpression resizeLine(QStringLiteral("Resize-Partition -DiskNumber (-?\\d+) -PartitionNumber (-?\\d+) -Size (-?\\d+)\\n"));
        static const QRegularExpression sizeLine(QStringLiteral("Get-PartitionSupportedSize -DiskNumber (-?\\d+) -PartitionNumber (-?\\d+)\\n"));
        for (int i = 0; i < scaled(20000); ++i)
        {
            const int disk = static_cast<int>(g_rng());
            const int part = static_cast<int>(g_rng());
            const qint64 size = static_cast<qint64>(g_rng());
            const QString resize = internal::buildResizeScript(disk, part, size);
            const auto m1 = resizeLine.match(resize);
            scripts.expect(m1.hasMatch() && m1.captured(1) == QString::number(disk) && m1.captured(2) == QString::number(part) &&
                               m1.captured(3) == QString::number(size) && resize.count("Resize-Partition") == 1,
                           resize);
            const auto m2 = sizeLine.match(internal::buildSupportedSizeScript(disk, part));
            scripts.expect(m2.hasMatch() && m2.captured(1) == QString::number(disk) && m2.captured(2) == QString::number(part), resize);
        }
        CHECK_FUZZ(scripts);
        say("F. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // =================================================================================================
    // G. VPN: thoát chuỗi PowerShell, dựng script, tệp nhãn, VpnConnector
    // =================================================================================================
    sectionTimer.restart();
    {
        say("G. VPN:\n");
        // G1. quoteLiteral: đọc lại bằng đúng luật tokenizer của PowerShell phải ra NGUYÊN chuỗi gốc và
        //     chuỗi phải đóng ĐÚNG ở dấu nháy cuối cùng.
        FuzzProperty quote{"quoteLiteral (luat tokenizer)"};
        for (int i = 0; i < scaled(60000); ++i)
        {
            const QString value = randomPsString(40, false);
            const QString script = "X '" + PowerShellRunner::quoteLiteral(value) + "' Y";
            QString decoded;
            const int end = readPsLiteral(script, 2, &decoded);
            quote.expect(end == script.size() - 2 && decoded == value, value);
            quote.expect(VpnController::internal::escapePsString(value) == PowerShellRunner::quoteLiteral(value), value);
        }
        CHECK_FUZZ(quote);

        // G2. Script thêm/xóa hồ sơ: tên/địa chỉ chỉ xuất hiện BÊN TRONG chuỗi '...'; "%1".."%4" trong tên
        //     không bị QString::arg thay thế chéo.
        FuzzProperty addScript{"buildAddConnectionScript"};
        FuzzProperty removeScript{"buildRemoveConnectionScript"};
        const QStringList placeholders = {"%1", "%2", "%3", "%4", "%L1", "%%", "$(", "`", "\n", "';", "-Force", "| iex"};
        for (int i = 0; i < scaled(20000); ++i)
        {
            VpnProfile profile;
            profile.name = randomPsString(20, false) + placeholders[static_cast<int>(rnd(placeholders.size()))] + randomPsString(10, false);
            profile.serverAddress = placeholders[static_cast<int>(rnd(placeholders.size()))] + randomPsString(20, false);
            profile.tunnelType = static_cast<VpnTunnelType>(rnd(5));
            const QString script = VpnController::internal::buildAddConnectionScript(profile);
            const QString nameMarker = "Add-VpnConnection -Name ";
            const int namePos = script.indexOf(nameMarker) + nameMarker.size();
            QString name, server;
            const int afterName = readPsLiteral(script, namePos, &name);
            const QString serverMarker = " -ServerAddress ";
            const bool serverFollows = afterName > 0 && script.mid(afterName, serverMarker.size()) == serverMarker;
            const int afterServer = serverFollows ? readPsLiteral(script, afterName + serverMarker.size(), &server) : -1;
            static const QRegularExpression tail(QStringLiteral(
                "^ -TunnelType (Automatic|Ikev2|L2tp|Sstp|Pptp) -AuthenticationMethod (Eap|MSChapv2) -EncryptionLevel Required "
                "-Force -ErrorAction Stop \\| Out-Null\\n  Write-Output 'OK'\\n\\} catch \\{\\n"));
            addScript.expect(afterServer > 0 && name == profile.name && server == profile.serverAddress &&
                                 tail.match(script.mid(afterServer)).hasMatch(),
                             profile.name + " @ " + profile.serverAddress);

            const QString remove = VpnController::internal::buildRemoveConnectionScript(profile.name);
            const QString removeMarker = "Remove-VpnConnection -Name ";
            QString removed;
            const int afterRemoved = readPsLiteral(remove, remove.indexOf(removeMarker) + removeMarker.size(), &removed);
            removeScript.expect(afterRemoved > 0 && removed == profile.name &&
                                    remove.mid(afterRemoved).startsWith(" -Force -ErrorAction Stop\n  Write-Output 'OK'\n"),
                                profile.name);
        }
        CHECK_FUZZ(addScript);
        CHECK_FUZZ(removeScript);

        // G3. Kiểm bằng CHÍNH powershell.exe: chuỗi đã thoát phải được PowerShell đọc lại nguyên vẹn. Bảng
        //     ký tự chỉ gồm dấu nháy + ký tự vô hại (xem randomPsString) nên dù có lọt cũng không thành lệnh.
        {
            int mismatches = 0, total = 0;
            bool ranAll = true;
            for (int batch = 0; batch < 3; ++batch)
            {
                QStringList values;
                // Mỗi dòng trả về bắt đầu bằng ':' để chuỗi rỗng (Base64 rỗng) không thành dòng trống bị cắt mất.
                QString script = "function o($s){[Console]::Out.WriteLine(':'+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($s)))}\n";
                for (int i = 0; i < 100; ++i)
                {
                    values << randomPsString(24, true);
                    script += "o '" + PowerShellRunner::quoteLiteral(values.last()) + "'\n";
                }
                bool ok = false;
                QString error;
                const QByteArray out = PowerShellRunner::run(script, &ok, &error, 60000);
                if (!ok)
                {
                    say("  powershell.exe khong chay duoc: %s\n", qPrintable(error));
                    ranAll = false;
                    break;
                }
                const QList<QByteArray> lines = out.trimmed().split('\n');
                if (lines.size() != values.size())
                {
                    ++mismatches;
                    say("  so dong tra ve %lld != %lld\n", static_cast<long long>(lines.size()), static_cast<long long>(values.size()));
                    continue;
                }
                for (int i = 0; i < values.size(); ++i)
                {
                    ++total;
                    const QByteArray raw = QByteArray::fromBase64(lines[i].trimmed().mid(1));
                    const QString back = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), raw.size() / 2);
                    if (back != values[i])
                        ++mismatches;
                }
            }
            say("  powershell.exe that doc lai %d chuoi, %d sai\n", total, mismatches);
            CHECK(ranAll);
            CHECK(mismatches == 0);
            CHECK(!ranAll || total == 300);
        }

        // G4. Tệp nhãn (vpn_profiles.json): ghi/đọc hàng trăm tên ngẫu nhiên trong lúc luồng khác đọc liên
        //     tục; tệp luôn là JSON hợp lệ, không có trường mật khẩu.
        {
            const QString file = AppPaths::profileFile("vpn_profiles.json");
            CHECK(file.startsWith(dataDir.path()));
            QHash<QString, VpnController::VpnProfileMeta> expected;
            // Trước hết KHÔNG có luồng đọc nào: ghi liên tiếp phải luôn thành công.
            int quietFailures = 0;
            for (int i = 0; i < scaled(100); ++i)
            {
                const QString name = QString("OneForAllTest-yen-tinh-%1").arg(i % 20);
                const VpnController::VpnProfileMeta meta{QString("Q%1").arg(i), QString("user%1").arg(i)};
                QString error;
                if (!VpnController::setProfileMeta(name, meta, &error))
                {
                    if (++quietFailures <= 3)
                        say("  ghi tep nhan that bai (khong co luong doc): %s\n", qPrintable(error));
                }
                else
                {
                    expected[name] = meta;
                }
            }
            CHECK(quietFailures == 0);
            std::atomic_bool readerStop{false};
            std::atomic<int> corruptReads{0}, reads{0};
            std::thread reader([&]() {
                while (!readerStop)
                {
                    {
                        QFile f(file);
                        if (f.open(QIODevice::ReadOnly))
                        {
                            const QByteArray bytes = f.readAll();
                            ++reads;
                            if (!bytes.isEmpty() && !QJsonDocument::fromJson(bytes).isObject())
                                ++corruptReads;
                        }
                    } // đóng tệp NGAY: giữ mở sẽ chặn QSaveFile thay tệp ở mọi lần ghi
                    VpnController::profileMeta("OneForAllTest-bat-ky");
                    Sleep(1);
                }
            });
            int writeFailures = 0, wrongReadBack = 0;
            const int writes = scaled(300);
            for (int i = 0; i < writes; ++i)
            {
                const QString name = "OneForAllTest-" + QString::number(rnd(60)) + randomPsString(6, false).remove(QChar('\n'));
                const VpnController::VpnProfileMeta meta{randomPsString(12, false), "user" + QString::number(i)};
                QString error;
                if (VpnController::setProfileMeta(name, meta, &error))
                {
                    expected[name] = meta;
                    const VpnController::VpnProfileMeta back = VpnController::profileMeta(name);
                    // (luồng đọc đang giữ tệp mở có thể làm lần ĐỌC này trượt - khi đó trả về rỗng, không sai)
                    if (!(back.countryLabel == meta.countryLabel && back.username == meta.username) &&
                        !(back.countryLabel.isEmpty() && back.username.isEmpty()))
                        ++wrongReadBack;
                }
                else
                {
                    // QSaveFile không thay được tệp đang bị luồng đọc mở: phải BÁO lỗi, không được im lặng
                    if (++writeFailures == 1)
                        say("  (vi du loi ghi khi tep dang bi doc: %s)\n", qPrintable(error));
                    if (error.isEmpty())
                        ++wrongReadBack;
                }
            }
            readerStop = true;
            reader.join();
            say("  tep nhan: %d lan ghi (%d lan bi tu choi vi tep dang mo), %d lan doc song song\n", writes, writeFailures, reads.load());
            CHECK(corruptReads.load() == 0);
            CHECK(wrongReadBack == 0);
            CHECK(writeFailures < writes);
            int lost = 0;
            for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
            {
                const VpnController::VpnProfileMeta back = VpnController::profileMeta(it.key());
                if (back.countryLabel != it.value().countryLabel || back.username != it.value().username)
                    ++lost;
            }
            CHECK(lost == 0);
            QFile f(file);
            CHECK(f.open(QIODevice::ReadOnly));
            const QByteArray content = f.readAll();
            CHECK(QJsonDocument::fromJson(content).isObject());
            CHECK(!content.toLower().contains("password") && !content.toLower().contains("matkhau"));
        }

        // G5. VpnConnector: RAS API THẬT nhưng tới tên hồ sơ KHÔNG tồn tại (không có gì được kết nối/tạo/xóa),
        //     hủy ở thời điểm ngẫu nhiên; không treo, đúng một tín hiệu, không rò handle, không lộ mật khẩu.
        {
            const QString missing = QString("OneForAllTest-KhongTonTai-%1").arg(QCoreApplication::applicationPid());
            const QString secret = "MatKhauGia-KHONG-co-that-9f3a";
            VpnConnector connector;
            std::atomic<int> finishedSignals{0};
            bool lastOk = true;
            QString lastMessage;
            QObject::connect(&connector, &VpnConnector::operationFinished, [&](bool ok, QString message) {
                lastOk = ok;
                lastMessage = message;
                ++finishedSignals;
            });
            auto runOnce = [&](int mode, bool cancel) {
                const int before = finishedSignals.load();
                if (mode == 0)
                    connector.setConnectTarget(missing, "nguoi dung\\gia", secret);
                else if (mode == 1)
                    connector.setConnectTarget(missing, QString(), QString());
                else if (mode == 2)
                    connector.setDisconnectTarget(missing);
                else
                    connector.setConnectTarget(QString(300 + static_cast<int>(rnd(300)), 'n'), QString(300, 'u'), QString(300, 'p'));
                connector.start();
                if (cancel)
                {
                    QThread::usleep(static_cast<unsigned long>(rnd(3000)));
                    connector.requestCancel();
                }
                const bool exited = connector.wait(90000);
                return exited && finishedSignals.load() == before + 1;
            };
            for (int i = 0; i < 5; ++i) // làm nóng: nạp rasapi32/rasman, tạo các handle dùng chung một lần
                runOnce(i % 3, false);
            const DWORD handlesBefore = handleCount();
            int bad = 0, leaked = 0, wrongResult = 0;
            QElapsedTimer timer;
            timer.start();
            const int rounds = scaled(60);
            for (int i = 0; i < rounds; ++i)
            {
                const int mode = static_cast<int>(rnd(4));
                if (!runOnce(mode, rnd(2) == 0))
                {
                    ++bad;
                    break;
                }
                if (lastMessage.isEmpty() || lastMessage.contains(secret))
                    ++leaked;
                if (mode <= 1 && (lastOk || !lastMessage.contains("623")))
                    ++wrongResult; // hồ sơ không tồn tại: luôn là lỗi 623, kể cả khi có yêu cầu hủy
                if (mode == 2 && !lastOk)
                    ++wrongResult; // không có gì để ngắt - không phải lỗi
                if (mode == 3 && lastOk)
                    ++wrongResult;
            }
            const DWORD handlesAfter = handleCount();
            say("  VpnConnector: %d luot, %lld ms, handle %lu -> %lu, thong bao cuoi: %s\n", rounds,
                static_cast<long long>(timer.elapsed()), handlesBefore, handlesAfter, qPrintable(lastMessage));
            CHECK(bad == 0);
            CHECK(leaked == 0);
            CHECK(wrongResult == 0);
            CHECK(handlesAfter <= handlesBefore + 10);

            // Hủy đối tượng ngay sau start(): destructor phải tự chờ luồng thoát.
            for (int i = 0; i < scaled(30); ++i)
            {
                auto* c = new VpnConnector;
                c->setConnectTarget(missing, "u", secret);
                c->start();
                if (i % 2)
                    QThread::usleep(static_cast<unsigned long>(rnd(2000)));
                delete c;
            }
            CHECK(true); // tới được đây = không crash
        }
        say("G. xong (%lld ms)\n", static_cast<long long>(sectionTimer.elapsed()));
    }

    // Dọn thư mục tạm bằng API thô (tên có dấu chấm cuối/đường dẫn dài QTemporaryDir không tự xóa được).
    rawRemoveTree(t, t);
    CHECK(!rawExists(scan));

    allDone = true;
    watchdog.join();
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
