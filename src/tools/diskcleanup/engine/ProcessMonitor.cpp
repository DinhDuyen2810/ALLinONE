#include "ProcessMonitor.h"

#include "core/PowerShellRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <chrono>
#include <functional>
#include <string>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#endif

// ---------------------------------------------------------------------------------------------------------------
// Thanh RAM (Win32_PhysicalMemory)
// ---------------------------------------------------------------------------------------------------------------
namespace ProcessMonitor
{
QString memoryTypeName(int smbiosType)
{
    switch (smbiosType)
    {
        case 20: return QStringLiteral("DDR");
        case 21: return QStringLiteral("DDR2");
        case 24: return QStringLiteral("DDR3");
        case 26: return QStringLiteral("DDR4");
        case 29: return QStringLiteral("LPDDR2");
        case 30: return QStringLiteral("LPDDR3");
        case 31: return QStringLiteral("LPDDR4");
        case 34: return QStringLiteral("DDR5");
        case 35: return QStringLiteral("LPDDR5");
        default: return {};
    }
}

namespace internal
{
QList<MemoryModule> parseMemoryModulesJson(const QByteArray& json, QString* error)
{
    QList<MemoryModule> out;
    const QByteArray trimmed = json.trimmed();
    if (trimmed.isEmpty())
        return out; // máy không báo thanh RAM nào (máy ảo...) - không phải lỗi
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(trimmed, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error)
            *error = "JSON thanh RAM hỏng: " + parseError.errorString();
        return out;
    }
    // ConvertTo-Json trả MỘT đối tượng (không bọc mảng) khi chỉ có đúng một thanh RAM.
    QJsonArray items;
    if (doc.isArray())
        items = doc.array();
    else if (doc.isObject())
        items.append(doc.object());
    for (const QJsonValue& v : items)
    {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        MemoryModule m;
        m.slot = o.value("Slot").toString().trimmed();
        m.capacityBytes = static_cast<qint64>(o.value("Capacity").toDouble());
        const int configured = o.value("Configured").toInt();
        m.speedMHz = configured > 0 ? configured : o.value("Speed").toInt();
        m.type = memoryTypeName(o.value("Type").toInt());
        m.manufacturer = o.value("Manufacturer").toString().trimmed();
        m.partNumber = o.value("Part").toString().trimmed();
        if (m.capacityBytes > 0)
            out.push_back(m);
    }
    return out;
}
} // namespace internal

QString describeMemoryModules(const QList<MemoryModule>& modules)
{
    if (modules.isEmpty())
        return {};
    const auto gb = [](qint64 bytes) {
        const double v = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        return qAbs(v - qRound(v)) < 0.05 ? QStringLiteral("%1 GB").arg(qRound(v)) : QStringLiteral("%1 GB").arg(v, 0, 'f', 1);
    };
    QList<qint64> caps;
    qint64 total = 0;
    QStringList types, makers;
    QList<int> speeds;
    for (const MemoryModule& m : modules)
    {
        caps.push_back(m.capacityBytes);
        total += m.capacityBytes;
        if (!m.type.isEmpty() && !types.contains(m.type))
            types.push_back(m.type);
        if (m.speedMHz > 0 && !speeds.contains(m.speedMHz))
            speeds.push_back(m.speedMHz);
        if (!m.manufacturer.isEmpty() && !makers.contains(m.manufacturer, Qt::CaseInsensitive))
            makers.push_back(m.manufacturer);
    }
    std::sort(caps.begin(), caps.end(), std::greater<qint64>());
    QStringList capTexts;
    for (qint64 c : caps)
        capTexts << gb(c);
    QString text = QStringLiteral("%1 thanh RAM: %2").arg(modules.size()).arg(capTexts.join(QStringLiteral(" + ")));
    if (types.size() == 1)
        text += QStringLiteral(" · ") + types.first();
    else if (types.size() > 1)
        text += QStringLiteral(" · ") + types.join(QLatin1Char('/'));
    if (speeds.size() == 1)
        text += (types.isEmpty() ? QStringLiteral(" · ") : QStringLiteral(" ")) + QString::number(speeds.first()) + QStringLiteral(" MHz");
    else if (speeds.size() > 1)
    {
        QStringList s;
        for (int v : speeds)
            s << QString::number(v);
        text += QStringLiteral(" · ") + s.join(QLatin1Char('/')) + QStringLiteral(" MHz");
    }
    if (makers.size() == 1)
        text += QStringLiteral(" · ") + makers.first();
    if (modules.size() > 1)
        text += QStringLiteral(" (tổng %1)").arg(gb(total));
    return text;
}

QList<MemoryModule> memoryModules(QString* error, const std::atomic_bool* cancel)
{
    const QString script =
        "$ErrorActionPreference='Stop'\n"
        "$m = @(Get-CimInstance Win32_PhysicalMemory | ForEach-Object { [pscustomobject]@{ "
        "Slot=[string]$_.DeviceLocator; Capacity=[double]$_.Capacity; Speed=[int]$_.Speed; "
        "Configured=[int]$_.ConfiguredClockSpeed; Type=[int]$_.SMBIOSMemoryType; "
        "Manufacturer=([string]$_.Manufacturer).Trim(); Part=([string]$_.PartNumber).Trim() } })\n"
        "ConvertTo-Json -InputObject $m -Compress";
    bool ok = false;
    QString runError;
    const QByteArray out = cancel ? PowerShellRunner::runCancelable(script, cancel, &ok, &runError, 20000)
                                  : PowerShellRunner::run(script, &ok, &runError, 20000);
    if (!ok)
    {
        if (error)
            *error = runError.isEmpty() ? QStringLiteral("Không đọc được thông tin thanh RAM.") : runError;
        return {};
    }
    return internal::parseMemoryModulesJson(out, error);
}
} // namespace ProcessMonitor

// ---------------------------------------------------------------------------------------------------------------
// ProcessSampler
// ---------------------------------------------------------------------------------------------------------------
#ifdef Q_OS_WIN
namespace
{
using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
constexpr ULONG kSystemProcessInformation = 5;
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);

struct RawUnicodeString
{
    USHORT length;
    USHORT maximumLength;
    PWSTR buffer;
};

/// SYSTEM_PROCESS_INFORMATION (bản x64) - dựng lại phần đầu của cấu trúc mà ntdll trả về; sau nó là mảng thông tin luồng
/// (không dùng). Cấu trúc này ổn định từ Windows Vista tới Windows 11; bố cục được khóa bằng static_assert bên dưới và
/// đối chiếu với GetProcessMemoryInfo/GetProcessHandleCount của CHÍNH tiến trình này trong diskcleanup_tests.
struct RawSystemProcess
{
    ULONG nextEntryOffset;
    ULONG numberOfThreads;
    LARGE_INTEGER workingSetPrivateSize;
    ULONG hardFaultCount;
    ULONG numberOfThreadsHighWatermark;
    ULONGLONG cycleTime;
    LARGE_INTEGER createTime;
    LARGE_INTEGER userTime;
    LARGE_INTEGER kernelTime;
    RawUnicodeString imageName;
    LONG basePriority;
    HANDLE uniqueProcessId;
    HANDLE inheritedFromUniqueProcessId;
    ULONG handleCount;
    ULONG sessionId;
    ULONG_PTR uniqueProcessKey;
    SIZE_T peakVirtualSize;
    SIZE_T virtualSize;
    ULONG pageFaultCount;
    SIZE_T peakWorkingSetSize;
    SIZE_T workingSetSize;
    SIZE_T quotaPeakPagedPoolUsage;
    SIZE_T quotaPagedPoolUsage;
    SIZE_T quotaPeakNonPagedPoolUsage;
    SIZE_T quotaNonPagedPoolUsage;
    SIZE_T pagefileUsage;
    SIZE_T peakPagefileUsage;
    SIZE_T privatePageCount;
    LARGE_INTEGER readOperationCount;
    LARGE_INTEGER writeOperationCount;
    LARGE_INTEGER otherOperationCount;
    LARGE_INTEGER readTransferCount;
    LARGE_INTEGER writeTransferCount;
    LARGE_INTEGER otherTransferCount;
};
#ifdef _WIN64
static_assert(offsetof(RawSystemProcess, imageName) == 56, "SYSTEM_PROCESS_INFORMATION: sai vị trí imageName");
static_assert(offsetof(RawSystemProcess, uniqueProcessId) == 80, "SYSTEM_PROCESS_INFORMATION: sai vị trí uniqueProcessId");
static_assert(offsetof(RawSystemProcess, handleCount) == 96, "SYSTEM_PROCESS_INFORMATION: sai vị trí handleCount");
static_assert(offsetof(RawSystemProcess, workingSetSize) == 144, "SYSTEM_PROCESS_INFORMATION: sai vị trí workingSetSize");
static_assert(offsetof(RawSystemProcess, privatePageCount) == 200, "SYSTEM_PROCESS_INFORMATION: sai vị trí privatePageCount");
static_assert(sizeof(RawSystemProcess) == 256, "SYSTEM_PROCESS_INFORMATION: sai kích thước");
#endif

struct Raw
{
    quint32 pid{0};
    quint32 parentPid{0};
    QString name;
    quint32 threads{0};
    quint32 handles{0};
    quint32 sessionId{0};
    qint64 workingSet{0};
    qint64 privateWorkingSet{0};
    qint64 privateBytes{0};
    qint64 peakWorkingSet{0};
    qint64 createTime{0}; // FILETIME (100 ns từ 1601)
    quint64 cpuTime{0};   // kernel + user, 100 ns
    qint64 ioRead{0};
    qint64 ioWrite{0};
};

quint64 fileTimeToU64(const FILETIME& ft)
{
    return (static_cast<quint64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

QDateTime fileTimeToDateTime(qint64 ft)
{
    if (ft <= 0)
        return {};
    return QDateTime::fromMSecsSinceEpoch(ft / 10000 - 11644473600000LL);
}
} // namespace

struct ProcessSampler::Impl
{
    struct Prev
    {
        quint64 cpu{0};
        qint64 io{0};
        qint64 createTime{0};
    };
    struct Static
    {
        qint64 createTime{0};
        QString path;
        QString user;
    };

    QHash<quint32, Prev> prev;
    QHash<quint32, Static> statics;
    QHash<QString, QString> sidNames;
    std::vector<char> buffer;
    std::vector<wchar_t> pathBuffer;
    std::chrono::steady_clock::time_point prevWall;
    quint64 prevSysTotal{0};
    quint64 prevSysIdle{0};
    bool havePrev{false};

    bool readNative(std::vector<Raw>& out);
    void readFallback(std::vector<Raw>& out);
    Static resolveStatic(quint32 pid);
    QString sidToName(PSID sid);
};

namespace
{
QString sidToNameImpl(PSID sid, QHash<QString, QString>& cache)
{
    wchar_t* sidText = nullptr;
    QString key;
    if (ConvertSidToStringSidW(sid, &sidText) && sidText)
    {
        key = QString::fromWCharArray(sidText);
        LocalFree(sidText);
    }
    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QString name;
    // Tài khoản dịch vụ có SID cố định - không cần hỏi LSA (nhanh, và tránh truy vấn miền máy chủ).
    if (key == QLatin1String("S-1-5-18"))
        name = QStringLiteral("NT AUTHORITY\\SYSTEM");
    else if (key == QLatin1String("S-1-5-19"))
        name = QStringLiteral("NT AUTHORITY\\LOCAL SERVICE");
    else if (key == QLatin1String("S-1-5-20"))
        name = QStringLiteral("NT AUTHORITY\\NETWORK SERVICE");
    else
    {
        wchar_t user[256] = {};
        wchar_t domain[256] = {};
        DWORD userLen = 255, domainLen = 255;
        SID_NAME_USE use;
        if (LookupAccountSidW(nullptr, sid, user, &userLen, domain, &domainLen, &use))
        {
            name = QString::fromWCharArray(domain);
            if (!name.isEmpty())
                name += QLatin1Char('\\');
            name += QString::fromWCharArray(user);
        }
        else
            name = key; // không dịch được -> hiện chuỗi SID
    }
    cache.insert(key, name);
    return name;
}
} // namespace

QString ProcessSampler::Impl::sidToName(PSID sid)
{
    return sidToNameImpl(sid, sidNames);
}

ProcessSampler::Impl::Static ProcessSampler::Impl::resolveStatic(quint32 pid)
{
    Static s;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return s; // không mở được (tiến trình hệ thống được bảo vệ, thiếu quyền) - để trống, không phải lỗi
    std::vector<wchar_t> buf(32768);
    DWORD len = static_cast<DWORD>(buf.size());
    if (QueryFullProcessImageNameW(h, 0, buf.data(), &len))
        s.path = QString::fromWCharArray(buf.data(), static_cast<int>(len));

    auto readUser = [&](HANDLE processHandle) -> QString {
        HANDLE token = nullptr;
        if (!OpenProcessToken(processHandle, TOKEN_QUERY, &token))
            return {};
        DWORD need = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &need);
        QString result;
        if (need > 0 && need < 4096)
        {
            std::vector<char> tu(need);
            if (GetTokenInformation(token, TokenUser, tu.data(), need, &need))
                result = sidToName(reinterpret_cast<TOKEN_USER*>(tu.data())->User.Sid);
        }
        CloseHandle(token);
        return result;
    };
    s.user = readUser(h);
    CloseHandle(h);
    if (s.user.isEmpty())
    {
        // Một số bản Windows chỉ cho mở token khi có PROCESS_QUERY_INFORMATION.
        if (HANDLE h2 = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid))
        {
            s.user = readUser(h2);
            CloseHandle(h2);
        }
    }
    return s;
}

bool ProcessSampler::Impl::readNative(std::vector<Raw>& out)
{
#ifdef _WIN64
    static const auto fn = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    if (!fn)
        return false;
    ULONG needed = 0;
    LONG status = kStatusInfoLengthMismatch;
    for (int attempt = 0; attempt < 8 && status == kStatusInfoLengthMismatch; ++attempt)
    {
        if (buffer.size() < (1u << 19))
            buffer.resize(1u << 19);
        status = fn(kSystemProcessInformation, buffer.data(), static_cast<ULONG>(buffer.size()), &needed);
        if (status == kStatusInfoLengthMismatch)
            buffer.resize(std::max<size_t>(buffer.size() * 2, static_cast<size_t>(needed) + (1u << 16)));
    }
    if (status < 0)
        return false;

    const char* base = buffer.data();
    const size_t limit = std::min<size_t>(needed ? needed : buffer.size(), buffer.size());
    size_t offset = 0;
    while (offset + sizeof(RawSystemProcess) <= limit)
    {
        const auto* e = reinterpret_cast<const RawSystemProcess*>(base + offset);
        Raw r;
        r.pid = static_cast<quint32>(reinterpret_cast<quintptr>(e->uniqueProcessId));
        r.parentPid = static_cast<quint32>(reinterpret_cast<quintptr>(e->inheritedFromUniqueProcessId));
        r.threads = e->numberOfThreads;
        r.handles = e->handleCount;
        r.sessionId = e->sessionId;
        r.workingSet = static_cast<qint64>(e->workingSetSize);
        r.privateWorkingSet = e->workingSetPrivateSize.QuadPart;
        r.privateBytes = static_cast<qint64>(e->privatePageCount);
        r.peakWorkingSet = static_cast<qint64>(e->peakWorkingSetSize);
        r.createTime = e->createTime.QuadPart;
        r.cpuTime = static_cast<quint64>(e->userTime.QuadPart) + static_cast<quint64>(e->kernelTime.QuadPart);
        r.ioRead = e->readTransferCount.QuadPart;
        r.ioWrite = e->writeTransferCount.QuadPart;
        // Tên nằm TRONG vùng đệm này - kiểm tra con trỏ rơi vào vùng hợp lệ trước khi đọc.
        const auto* nameBegin = reinterpret_cast<const char*>(e->imageName.buffer);
        const size_t nameBytes = e->imageName.length;
        if (e->imageName.buffer && nameBytes > 0 && nameBytes <= 1024 && (nameBytes % 2) == 0 && nameBegin >= base &&
            nameBegin + nameBytes <= base + limit)
            r.name = QString::fromWCharArray(e->imageName.buffer, static_cast<int>(nameBytes / 2));
        else if (r.pid == 0)
            r.name = QStringLiteral("System Idle Process");
        out.push_back(std::move(r));

        if (e->nextEntryOffset == 0)
            break;
        if (e->nextEntryOffset < sizeof(RawSystemProcess) || offset + e->nextEntryOffset > limit)
            break; // dữ liệu hỏng - dừng ở đây thay vì đọc ra ngoài vùng đệm
        offset += e->nextEntryOffset;
    }
    return !out.empty();
#else
    Q_UNUSED(out);
    return false; // bố cục cấu trúc ở trên chỉ đúng cho x64
#endif
}

void ProcessSampler::Impl::readFallback(std::vector<Raw>& out)
{
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            Raw r;
            r.pid = pe.th32ProcessID;
            r.parentPid = pe.th32ParentProcessID;
            r.threads = pe.cntThreads;
            r.name = QString::fromWCharArray(pe.szExeFile);
            if (r.pid == 0)
                r.name = QStringLiteral("System Idle Process");
            DWORD session = 0;
            if (ProcessIdToSessionId(r.pid, &session))
                r.sessionId = session;
            if (r.pid != 0)
            {
                if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, r.pid))
                {
                    FILETIME create, exitT, kernel, user;
                    if (GetProcessTimes(h, &create, &exitT, &kernel, &user))
                    {
                        r.createTime = static_cast<qint64>(fileTimeToU64(create));
                        r.cpuTime = fileTimeToU64(kernel) + fileTimeToU64(user);
                    }
                    PROCESS_MEMORY_COUNTERS_EX pmc;
                    pmc.cb = sizeof(pmc);
                    if (GetProcessMemoryInfo(h, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
                    {
                        r.workingSet = static_cast<qint64>(pmc.WorkingSetSize);
                        r.peakWorkingSet = static_cast<qint64>(pmc.PeakWorkingSetSize);
                        r.privateBytes = static_cast<qint64>(pmc.PrivateUsage);
                    }
                    DWORD handles = 0;
                    if (GetProcessHandleCount(h, &handles))
                        r.handles = handles;
                    IO_COUNTERS io;
                    if (GetProcessIoCounters(h, &io))
                    {
                        r.ioRead = static_cast<qint64>(io.ReadTransferCount);
                        r.ioWrite = static_cast<qint64>(io.WriteTransferCount);
                    }
                    CloseHandle(h);
                }
            }
            out.push_back(std::move(r));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

ProcessSampler::ProcessSampler()
    : m_impl(std::make_unique<Impl>())
{
}

ProcessSampler::~ProcessSampler() = default;

void ProcessSampler::reset()
{
    m_impl->prev.clear();
    m_impl->havePrev = false;
}

SystemSnapshot ProcessSampler::sample()
{
    Impl& d = *m_impl;
    SystemSnapshot snap;
    snap.takenAtMs = QDateTime::currentMSecsSinceEpoch();
    snap.logicalProcessors = qMax(1, static_cast<int>(GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)));
    snap.uptimeMs = static_cast<qint64>(GetTickCount64());

    // ---- RAM ----
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
    {
        snap.memory.totalPhysical = static_cast<qint64>(ms.ullTotalPhys);
        snap.memory.availablePhysical = static_cast<qint64>(ms.ullAvailPhys);
    }
    PERFORMANCE_INFORMATION pi;
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi)))
    {
        const qint64 page = static_cast<qint64>(pi.PageSize);
        snap.memory.commitUsed = static_cast<qint64>(pi.CommitTotal) * page;
        snap.memory.commitLimit = static_cast<qint64>(pi.CommitLimit) * page;
        snap.memory.commitPeak = static_cast<qint64>(pi.CommitPeak) * page;
        snap.memory.systemCache = static_cast<qint64>(pi.SystemCache) * page;
        snap.memory.kernelPaged = static_cast<qint64>(pi.KernelPaged) * page;
        snap.memory.kernelNonpaged = static_cast<qint64>(pi.KernelNonpaged) * page;
        snap.handleCount = pi.HandleCount;
        snap.threadCount = pi.ThreadCount;
        snap.processCount = pi.ProcessCount;
    }

    // ---- CPU toàn máy + khoảng thời gian giữa hai mẫu ----
    FILETIME idleFt, kernelFt, userFt;
    quint64 sysTotal = 0, sysIdle = 0;
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt))
    {
        sysIdle = fileTimeToU64(idleFt);
        sysTotal = fileTimeToU64(kernelFt) + fileTimeToU64(userFt); // kernel đã gồm cả thời gian rảnh
    }
    const auto now = std::chrono::steady_clock::now();
    const bool haveBase = d.havePrev && sysTotal > d.prevSysTotal;
    const double dTotal = haveBase ? static_cast<double>(sysTotal - d.prevSysTotal) : 0.0;
    const double dWallSec = haveBase ? std::chrono::duration<double>(now - d.prevWall).count() : 0.0;
    if (haveBase && dTotal > 0)
        snap.cpuPercent = std::clamp(100.0 * (dTotal - static_cast<double>(sysIdle - d.prevSysIdle)) / dTotal, 0.0, 100.0);

    // ---- Tiến trình ----
    std::vector<Raw> raws;
    const bool native = !m_forceFallback && d.readNative(raws);
    if (!native)
    {
        raws.clear();
        d.readFallback(raws);
    }
    snap.nativeApi = native;

    QHash<quint32, Impl::Prev> nextPrev;
    nextPrev.reserve(static_cast<qsizetype>(raws.size()));
    QHash<quint32, Impl::Static> nextStatics;
    nextStatics.reserve(static_cast<qsizetype>(raws.size()));
    snap.processes.reserve(static_cast<qsizetype>(raws.size()));
    qint64 threadSum = 0, handleSum = 0;
    for (const Raw& r : raws)
    {
        ProcessInfo p;
        p.pid = r.pid;
        p.parentPid = r.parentPid;
        p.name = r.name;
        p.sessionId = r.sessionId;
        p.threads = r.threads;
        p.handles = r.handles;
        p.workingSetBytes = r.workingSet;
        p.privateWorkingSetBytes = r.privateWorkingSet;
        p.privateBytes = r.privateBytes;
        p.peakWorkingSetBytes = r.peakWorkingSet;
        p.cpuTime100ns = r.cpuTime;
        p.ioReadBytes = r.ioRead;
        p.ioWriteBytes = r.ioWrite;
        p.startTime = fileTimeToDateTime(r.createTime);
        threadSum += r.threads;
        handleSum += r.handles;

        // Đường dẫn + người dùng: chỉ đổi khi tiến trình khác (cùng PID nhưng thời điểm tạo khác) - tra một lần, nhớ lại.
        auto st = d.statics.constFind(r.pid);
        Impl::Static stat;
        if (st != d.statics.constEnd() && st->createTime == r.createTime)
            stat = *st;
        else
        {
            stat.createTime = r.createTime;
            if (r.pid == 0 || r.pid == 4)
                stat.user = QStringLiteral("NT AUTHORITY\\SYSTEM");
            else
            {
                stat = d.resolveStatic(r.pid);
                stat.createTime = r.createTime;
            }
        }
        p.path = stat.path;
        p.user = stat.user;
        nextStatics.insert(r.pid, stat);

        // % CPU và tốc độ I/O: chênh lệch so với mẫu trước CỦA CÙNG tiến trình (cùng PID + cùng thời điểm tạo).
        const qint64 io = r.ioRead + r.ioWrite;
        const auto pv = d.prev.constFind(r.pid);
        if (haveBase && pv != d.prev.constEnd() && pv->createTime == r.createTime)
        {
            if (r.cpuTime >= pv->cpu && dTotal > 0)
                p.cpuPercent = std::clamp(100.0 * static_cast<double>(r.cpuTime - pv->cpu) / dTotal, 0.0, 100.0);
            if (io >= pv->io && dWallSec > 0)
                p.ioBytesPerSec = static_cast<qint64>(static_cast<double>(io - pv->io) / dWallSec);
        }
        nextPrev.insert(r.pid, {r.cpuTime, io, r.createTime});
        snap.processes.push_back(std::move(p));
    }
    d.prev = std::move(nextPrev);
    d.statics = std::move(nextStatics); // đồng thời bỏ mục của tiến trình đã thoát
    if (snap.processCount == 0)
        snap.processCount = static_cast<quint32>(snap.processes.size());
    if (snap.threadCount == 0)
        snap.threadCount = static_cast<quint32>(threadSum);
    if (snap.handleCount == 0)
        snap.handleCount = static_cast<quint32>(handleSum);

    d.prevSysTotal = sysTotal;
    d.prevSysIdle = sysIdle;
    d.prevWall = now;
    d.havePrev = true;
    return snap;
}
#else
struct ProcessSampler::Impl {};
ProcessSampler::ProcessSampler() : m_impl(std::make_unique<Impl>()) {}
ProcessSampler::~ProcessSampler() = default;
void ProcessSampler::reset() {}
SystemSnapshot ProcessSampler::sample() { return {}; }
#endif

// ---------------------------------------------------------------------------------------------------------------
// ProcessSamplerThread
// ---------------------------------------------------------------------------------------------------------------
ProcessSamplerThread::ProcessSamplerThread(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<std::shared_ptr<SystemSnapshot>>("std::shared_ptr<SystemSnapshot>");
}

ProcessSamplerThread::~ProcessSamplerThread()
{
    requestStop();
    wait();
}

void ProcessSamplerThread::setIntervalMs(int ms)
{
    QMutexLocker lock(&m_mutex);
    m_intervalMs = qBound(250, ms, 60000);
    m_cond.wakeAll();
}

void ProcessSamplerThread::setActive(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_active = on;
    m_cond.wakeAll();
}

bool ProcessSamplerThread::isActive() const
{
    QMutexLocker lock(&m_mutex);
    return m_active;
}

void ProcessSamplerThread::requestStop()
{
    QMutexLocker lock(&m_mutex);
    m_stop = true;
    m_cond.wakeAll();
}

void ProcessSamplerThread::run()
{
    ProcessSampler sampler;
    bool warm = false;
    for (;;)
    {
        {
            QMutexLocker lock(&m_mutex);
            while (!m_stop && !m_active)
            {
                warm = false;
                m_cond.wait(&m_mutex);
            }
            if (m_stop)
                return;
        }
        if (!warm)
        {
            // Vừa được bật: lấy một mẫu nền để mẫu báo đầu tiên đã có % CPU/I-O đúng.
            sampler.reset();
            sampler.sample();
            QMutexLocker lock(&m_mutex);
            if (!m_stop && m_active)
                m_cond.wait(&m_mutex, 300);
            if (m_stop)
                return;
            if (!m_active)
                continue;
            warm = true;
        }
        auto snap = std::make_shared<SystemSnapshot>(sampler.sample());
        ++m_sampleCount;
        emit sampled(snap);

        QMutexLocker lock(&m_mutex);
        if (m_stop)
            return;
        if (m_active)
            m_cond.wait(&m_mutex, static_cast<unsigned long>(m_intervalMs));
    }
}
