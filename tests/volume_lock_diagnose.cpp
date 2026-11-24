// KIỂM TRA TAY (cần Administrator) - CHỈ ĐỌC: chẩn đoán vì sao một ổ KHÔNG KHÓA ĐƯỢC (FSCTL_LOCK_VOLUME "Access is denied") khi bảng "chương trình
// đang giữ ổ" của cửa sổ dời phân vùng không thấy ai. Không dời, không tháo ổ, không dừng gì.
//
//   volume_lock_diagnose <ổ> <tệp-nhật-ký.txt>        (chạy qua: Start-Process <exe> -ArgumentList 'D','<log>' -Verb RunAs)
//
// Các bước:
//  1. Thử khóa ổ rồi MỞ KHÓA NGAY (giống bước kiểm tra trước của ứng dụng; không tháo ổ) - xác nhận lỗi còn hay không.
//  2. VolumeHolders::scan (đúng hàm của ứng dụng) - in đủ.
//  3. HỎI "PID NÀO ĐANG MỞ" CHO MỌI tệp/thư mục trên ổ (FileProcessIdsUsingFileInformation, ~22 ms/lần, song song 12 luồng) - thấy cả nhân Windows
//     (PID 4) và tiến trình được bảo vệ; ghi cả tệp bị giữ ĐỘC QUYỀN (mở-đọc-thuộc-tính cũng bị từ chối "đang dùng").
//  4. fltmc instances (driver lọc gắn vào ổ), vssadmin list shadows/shadowstorage (bản sao bóng).
//  5. Thử khóa lại lần nữa.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QMap>
#include <QProcess>
#include <QSet>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "core/AppPaths.h"
#include "tools/diskcleanup/engine/VolumeHolders.h"

#include <windows.h>
#include <tlhelp32.h>

namespace
{
QFile g_log;

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

QString winError(DWORD code)
{
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString t = msg ? QString::fromWCharArray(msg).trimmed() : QString();
    if (msg)
        LocalFree(msg);
    return QString("%1 (mã %2)").arg(t).arg(code);
}

/// Thử khóa ổ rồi mở khóa ngay - KHÔNG tháo ổ.
QString tryLock(const QString& letter)
{
    const std::wstring dev = (QStringLiteral("\\\\.\\") + letter + QLatin1Char(':')).toStdWString();
    const HANDLE h = CreateFileW(dev.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return "không mở được ổ: " + winError(GetLastError());
    DWORD got = 0;
    QString result;
    bool locked = false;
    DWORD code = 0;
    for (int i = 0; i < 20 && !locked; ++i)
    {
        locked = DeviceIoControl(h, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr) != 0;
        if (!locked)
        {
            code = GetLastError();
            Sleep(250);
        }
    }
    if (locked)
    {
        DeviceIoControl(h, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &got, nullptr);
        result = "KHÓA ĐƯỢC (đã mở khóa ngay)";
    }
    else
        result = "KHÔNG khóa được: " + winError(code);
    CloseHandle(h);
    return result;
}

using NtQIF = LONG(NTAPI*)(HANDLE, PVOID, PVOID, ULONG, ULONG);

struct Probe
{
    std::wstring path;
    std::vector<quint32> pids;
    bool exclusive{false};
    bool denied{false};
};

void probeOne(Probe& p, NtQIF q)
{
    const HANDLE h = CreateFileW(p.path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        p.exclusive = e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION;
        p.denied = e == ERROR_ACCESS_DENIED;
        return;
    }
    std::vector<BYTE> buf(8192);
    for (int a = 0; a < 5; ++a)
    {
        struct
        {
            LONG s;
            ULONG_PTR i;
        } iosb{};
        const LONG st = q(h, &iosb, buf.data(), static_cast<ULONG>(buf.size()), 47);
        if (st == static_cast<LONG>(0xC0000004) || st == static_cast<LONG>(0x80000005))
        {
            buf.resize(buf.size() * 4);
            continue;
        }
        if (st == 0)
        {
            const ULONG n = *reinterpret_cast<const ULONG*>(buf.data());
            const auto* ids = reinterpret_cast<const ULONG_PTR*>(buf.data() + sizeof(ULONG_PTR));
            for (ULONG k = 0; k < n && (sizeof(ULONG_PTR) * (k + 2)) <= buf.size(); ++k)
                p.pids.push_back(static_cast<quint32>(ids[k]));
        }
        break;
    }
    CloseHandle(h);
}

QString runTool(const QString& exe, const QStringList& args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(AppPaths::systemExecutable(exe), args);
    if (!p.waitForFinished(120000))
        return "(timeout)";
    return QString::fromLocal8Bit(p.readAll()).trimmed();
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 3)
    {
        std::printf("Dung: volume_lock_diagnose <o> <tep-nhat-ky.txt>\n");
        return 2;
    }
    const QString letter = QString::fromLocal8Bit(argv[1]).left(1).toUpper();
    g_log.setFileName(QString::fromLocal8Bit(argv[2]));
    g_log.open(QIODevice::WriteOnly | QIODevice::Truncate);
    say("volume_lock_diagnose " + letter + ": - CHỈ ĐỌC (thử khóa rồi mở khóa ngay; không tháo, không dời, không dừng gì)");
    VolumeHolders::enableDebugPrivilege();

    say("\n[1] Thử khóa ổ: " + tryLock(letter));

    // Tên tiến trình
    QHash<quint32, QString> names;
    {
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe))
            do
                names.insert(pe.th32ProcessID, QString::fromWCharArray(pe.szExeFile));
            while (Process32NextW(snap, &pe));
        CloseHandle(snap);
    }
    names.insert(4, "System (nhân Windows)");

    {
        QElapsedTimer t;
        t.start();
        const VolumeHolders::Report r = VolumeHolders::scan(letter, nullptr, true);
        say(QString("\n[2] VolumeHolders::scan: %1 thứ giữ ổ, %2 ms, liệt kê %3, hỏi %4, không mở được %5, quyền Admin %6")
                .arg(r.holders.size()).arg(t.elapsed()).arg(r.entriesListed).arg(r.filesChecked).arg(r.filesDenied).arg(r.elevated));
        for (const VolumeHolders::Holder& h : r.holders)
            say(QString("    %1 PID %2 [%3] %4").arg(h.name).arg(h.pid).arg(h.stopLabel(), h.holdingSummary()));
        for (const QString& n : r.notes)
            say("    ghi chú: " + n);
        say("    driver lọc (theo scan): " + r.filters.join(", "));
        for (const QString& f : r.filters)
            if (const QString h = VolumeHolders::filterHint(f); !h.isEmpty())
                say("      - " + h);
        say("    không đọc được: " + r.unreadable.join(", "));
    }

    // [3] Hỏi MỌI tệp/thư mục.
    say("\n[3] Hỏi \"PID nào đang mở\" cho MỌI tệp/thư mục trên " + letter + ": (song song 12 luồng)...");
    std::vector<Probe> items;
    {
        const QString root = letter + ":\\";
        items.push_back({(QStringLiteral("\\\\?\\") + root).toStdWString(), {}, false, false});
        std::vector<QString> stack{root};
        while (!stack.empty())
        {
            const QString dir = stack.back();
            stack.pop_back();
            const QStringList entries = VolumeHolders::directoryEntriesByHandle(dir);
            for (const QString& name : entries)
            {
                const QString full = dir + name;
                const std::wstring native = (QStringLiteral("\\\\?\\") + full).toStdWString();
                items.push_back({native, {}, false, false});
                const DWORD attrs = GetFileAttributesW(native.c_str());
                if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) && !(attrs & FILE_ATTRIBUTE_REPARSE_POINT))
                    stack.push_back(full + QLatin1Char('\\'));
            }
        }
    }
    say(QString("    liệt kê %1 mục").arg(items.size()));
    const auto q = reinterpret_cast<NtQIF>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile")));
    std::atomic<size_t> next{0};
    std::atomic<size_t> done{0};
    QElapsedTimer t3;
    t3.start();
    std::vector<std::thread> pool;
    for (int i = 0; i < 12; ++i)
        pool.emplace_back([&]() {
            for (;;)
            {
                const size_t k = next.fetch_add(1);
                if (k >= items.size())
                    return;
                probeOne(items[k], q);
                const size_t d = done.fetch_add(1) + 1;
                if (d % 10000 == 0)
                    std::printf("    ... %zu / %zu (%lld s)\n", d, items.size(), static_cast<long long>(t3.elapsed() / 1000));
            }
        });
    for (std::thread& th : pool)
        th.join();
    const quint32 self = GetCurrentProcessId();
    QMap<quint32, QStringList> byPid;
    QStringList exclusive;
    int denied = 0;
    for (const Probe& p : items)
    {
        const QString path = QString::fromStdWString(p.path).mid(4);
        if (p.denied)
            ++denied;
        if (p.exclusive)
            exclusive << path;
        for (quint32 pid : p.pids)
            if (pid != self)
                byPid[pid] << path;
    }
    say(QString("    xong %1 mục trong %2 s; không mở được %3; GIỮ ĐỘC QUYỀN %4").arg(items.size()).arg(t3.elapsed() / 1000).arg(denied).arg(exclusive.size()));
    for (const QString& e : exclusive.mid(0, 30))
        say("    ĐỘC QUYỀN: " + e);
    for (auto it = byPid.constBegin(); it != byPid.constEnd(); ++it)
    {
        say(QString("    PID %1 %2: %3 mục").arg(it.key()).arg(names.value(it.key(), "?")).arg(it.value().size()));
        for (const QString& p : it.value().mid(0, 8))
            say("        " + p);
    }
    if (byPid.isEmpty() && exclusive.isEmpty())
        say("    => KHÔNG tiến trình nào (kể cả nhân Windows) đang mở tệp/thư mục nào trên ổ.");

    say("\n[4] Driver lọc gắn vào " + letter + ": (fltmc instances -v " + letter + ":)");
    say(runTool("fltmc.exe", {"instances", "-v", letter + ":"}));
    say("\n    Bản sao bóng (vssadmin list shadows /for=" + letter + ":)");
    say(runTool("vssadmin.exe", {"list", "shadows", "/for=" + letter + ":"}).left(1500));
    say("\n    Vùng lưu bản sao bóng (vssadmin list shadowstorage)");
    say(runTool("vssadmin.exe", {"list", "shadowstorage"}).left(1500));
    say("\n    Thông tin ổ (fsutil fsinfo volumeinfo " + letter + ":)");
    say(runTool("fsutil.exe", {"fsinfo", "volumeinfo", letter + ":"}).left(1500));
    say("\n    Bẩn? (fsutil dirty query " + letter + ":)");
    say(runTool("fsutil.exe", {"dirty", "query", letter + ":"}));

    say("\n[5] Thử khóa ổ lần nữa: " + tryLock(letter));
    say("\nXONG");
    return 0;
}
