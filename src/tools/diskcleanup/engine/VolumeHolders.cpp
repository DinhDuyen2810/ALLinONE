#include "VolumeHolders.h"

#include "FsSafety.h"
#include "core/WinElevation.h"

#include <QDir>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <string>
#endif

namespace
{
QString normalizedLetter(const QString& driveLetter)
{
    const QString t = driveLetter.trimmed();
    if (t.isEmpty() || !t.at(0).isLetter() || t.at(0).unicode() > 127)
        return {};
    if (t.size() > 1 && t.at(1) != QLatin1Char(':'))
        return {};
    return t.left(1).toUpper();
}

#ifdef Q_OS_WIN
using NtQuerySystemInformationFn = LONG(NTAPI*)(ULONG, PVOID, ULONG, PULONG);
constexpr ULONG kSystemExtendedHandleInformation = 64;
constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);

struct HandleEntry
{
    PVOID Object;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR HandleValue;
    ULONG GrantedAccess;
    USHORT CreatorBackTraceIndex;
    USHORT ObjectTypeIndex;
    ULONG HandleAttributes;
    ULONG Reserved;
};

struct HandleTable
{
    ULONG_PTR NumberOfHandles;
    ULONG_PTR Reserved;
    HandleEntry Handles[1];
};

bool snapshotHandles(std::vector<char>* buffer)
{
    const auto query = reinterpret_cast<NtQuerySystemInformationFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    if (!query)
        return false;
    ULONG size = 8u << 20;
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        buffer->assign(size, 0);
        ULONG needed = 0;
        const LONG status = query(kSystemExtendedHandleInformation, buffer->data(), size, &needed);
        if (status == 0)
            return true;
        if (status != kStatusInfoLengthMismatch)
            return false;
        size = std::max<ULONG>(size * 2, needed + (1u << 20));
    }
    return false;
}

QString stripWin32Prefix(QString s)
{
    if (s.startsWith(QLatin1String("\\\\?\\UNC\\")))
        return QStringLiteral("\\\\") + s.mid(8);
    if (s.startsWith(QLatin1String("\\\\?\\")))
        return s.mid(4);
    return s;
}

/// Một truy vấn handle trên luồng phụ có hạn giờ: GetFileType / GetVolumeInformationByHandleW / GetFinalPathNameByHandleW trên một handle ống dẫn
/// đồng bộ đang chờ đọc có thể chặn VÔ HẠN (khóa đối tượng tệp). Luồng treo bị BỎ LẠI cùng đối tượng của nó (rò rỉ có chủ ý, có trần).
class QueryThread
{
public:
    struct Job
    {
        HANDLE handle{nullptr};
        DWORD targetSerial{0};
        bool onTarget{false};
        std::wstring path;
    };

    QueryThread()
    {
        m_request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_thread = CreateThread(nullptr, 64 * 1024, &QueryThread::entry, this, 0, nullptr);
    }
    bool valid() const { return m_request && m_done && m_thread; }
    /// false = hết giờ: luồng đang kẹt, đối tượng này phải bị bỏ lại (KHÔNG xóa).
    bool run(HANDLE h, DWORD targetSerial, DWORD timeoutMs, Job* result)
    {
        m_job = Job{};
        m_job.handle = h;
        m_job.targetSerial = targetSerial;
        SetEvent(m_request);
        if (WaitForSingleObject(m_done, timeoutMs) != WAIT_OBJECT_0)
            return false;
        *result = m_job;
        return true;
    }
    void shutdown()
    {
        m_quit = true;
        SetEvent(m_request);
        WaitForSingleObject(m_thread, 2000);
        CloseHandle(m_thread);
        CloseHandle(m_request);
        CloseHandle(m_done);
    }

private:
    static DWORD WINAPI entry(LPVOID self)
    {
        auto* t = static_cast<QueryThread*>(self);
        for (;;)
        {
            WaitForSingleObject(t->m_request, INFINITE);
            if (t->m_quit)
                return 0;
            Job& j = t->m_job;
            if (GetFileType(j.handle) == FILE_TYPE_DISK)
            {
                DWORD serial = 0;
                if (GetVolumeInformationByHandleW(j.handle, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0) &&
                    serial == j.targetSerial)
                {
                    j.onTarget = true;
                    wchar_t buf[1024];
                    const DWORD n = GetFinalPathNameByHandleW(j.handle, buf, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                    if (n > 0 && n < 1024)
                        j.path.assign(buf, n);
                }
            }
            SetEvent(t->m_done);
        }
    }

    HANDLE m_request{nullptr};
    HANDLE m_done{nullptr};
    HANDLE m_thread{nullptr};
    Job m_job;
    volatile bool m_quit{false};
};

QString processImagePath(quint32 pid)
{
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t buf[2048];
    DWORD len = 2048;
    QString out;
    if (QueryFullProcessImageNameW(h, 0, buf, &len))
        out = QString::fromWCharArray(buf, static_cast<int>(len));
    CloseHandle(h);
    return out;
}

bool processIsCritical(quint32 pid)
{
    using IsProcessCriticalFn = BOOL(WINAPI*)(HANDLE, PBOOL);
    static const auto fn = reinterpret_cast<IsProcessCriticalFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsProcessCritical")));
    if (!fn)
        return false;
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return false;
    BOOL critical = FALSE;
    const bool ok = fn(h, &critical) != 0;
    CloseHandle(h);
    return ok && critical;
}

/// Có DLL/exe nạp từ ổ 'prefix' ("D:\") không.
bool hasModuleOnDrive(quint32 pid, const QString& prefix)
{
    const HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!h)
        return false;
    std::vector<HMODULE> mods(1024);
    DWORD needed = 0;
    bool found = false;
    if (EnumProcessModulesEx(h, mods.data(), static_cast<DWORD>(mods.size() * sizeof(HMODULE)), &needed, LIST_MODULES_ALL))
    {
        const size_t count = std::min<size_t>(mods.size(), needed / sizeof(HMODULE));
        wchar_t buf[2048];
        for (size_t i = 0; i < count && !found; ++i)
        {
            const DWORD n = GetModuleFileNameExW(h, mods[i], buf, 2048);
            if (n > 0 && QString::fromWCharArray(buf, static_cast<int>(n)).startsWith(prefix, Qt::CaseInsensitive))
                found = true;
        }
    }
    CloseHandle(h);
    return found;
}

struct ServiceInfo
{
    QString name;
    QString display;
};

QHash<quint32, QList<ServiceInfo>> servicesByPid()
{
    QHash<quint32, QList<ServiceInfo>> out;
    const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (!scm)
        return out;
    DWORD needed = 0, count = 0, resume = 0;
    std::vector<BYTE> buf(256 * 1024);
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        const BOOL ok = EnumServicesStatusExW(scm, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_ACTIVE, buf.data(),
                                              static_cast<DWORD>(buf.size()), &needed, &count, &resume, nullptr);
        const auto* entries = reinterpret_cast<const ENUM_SERVICE_STATUS_PROCESSW*>(buf.data());
        for (DWORD i = 0; i < count; ++i)
        {
            const quint32 pid = entries[i].ServiceStatusProcess.dwProcessId;
            if (pid == 0)
                continue;
            out[pid].push_back({QString::fromWCharArray(entries[i].lpServiceName), QString::fromWCharArray(entries[i].lpDisplayName)});
        }
        if (ok)
            break;
        if (GetLastError() != ERROR_MORE_DATA)
            break;
        buf.resize(std::max<size_t>(buf.size() * 2, needed + 4096));
    }
    CloseServiceHandle(scm);
    return out;
}

bool waitServiceStopped(SC_HANDLE svc, DWORD timeoutMs)
{
    const DWORD start = GetTickCount();
    SERVICE_STATUS_PROCESS st{};
    DWORD bytes = 0;
    while (QueryServiceStatusEx(svc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&st), sizeof(st), &bytes))
    {
        if (st.dwCurrentState == SERVICE_STOPPED)
            return true;
        if (GetTickCount() - start > timeoutMs)
            return false;
        Sleep(250);
    }
    return false;
}

QString winErrorText(DWORD code)
{
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                   reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString text = msg ? QString::fromWCharArray(msg).trimmed() : QString();
    if (msg)
        LocalFree(msg);
    return QStringLiteral("%1 (mã %2)").arg(text).arg(code);
}

/// Dừng một dịch vụ (dừng trước các dịch vụ phụ thuộc đang chạy). Đã dừng sẵn = thành công.
bool stopService(SC_HANDLE scm, const QString& name, QString* error)
{
    const std::wstring wname = name.toStdWString();
    const SC_HANDLE svc = OpenServiceW(scm, wname.c_str(), SERVICE_STOP | SERVICE_QUERY_STATUS | SERVICE_ENUMERATE_DEPENDENTS);
    if (!svc)
    {
        *error = QStringLiteral("Không mở được dịch vụ %1: %2").arg(name, winErrorText(GetLastError()));
        return false;
    }
    // Dịch vụ phụ thuộc đang chạy phải dừng trước (SCM từ chối dừng dịch vụ còn dịch vụ khác đang dựa vào).
    DWORD needed = 0, count = 0;
    EnumDependentServicesW(svc, SERVICE_ACTIVE, nullptr, 0, &needed, &count);
    if (needed > 0)
    {
        std::vector<BYTE> buf(needed);
        if (EnumDependentServicesW(svc, SERVICE_ACTIVE, reinterpret_cast<LPENUM_SERVICE_STATUSW>(buf.data()), needed, &needed, &count))
        {
            const auto* deps = reinterpret_cast<const ENUM_SERVICE_STATUSW*>(buf.data());
            for (DWORD i = 0; i < count; ++i)
            {
                const SC_HANDLE dep = OpenServiceW(scm, deps[i].lpServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS);
                if (!dep)
                    continue;
                SERVICE_STATUS st{};
                ControlService(dep, SERVICE_CONTROL_STOP, &st);
                waitServiceStopped(dep, 15000);
                CloseServiceHandle(dep);
            }
        }
    }
    SERVICE_STATUS st{};
    if (!ControlService(svc, SERVICE_CONTROL_STOP, &st))
    {
        const DWORD code = GetLastError();
        if (code != ERROR_SERVICE_NOT_ACTIVE)
        {
            *error = code == ERROR_INVALID_SERVICE_CONTROL || code == ERROR_SERVICE_CANNOT_ACCEPT_CTRL
                         ? QStringLiteral("Dịch vụ %1 không cho phép dừng (%2).").arg(name, winErrorText(code))
                         : QStringLiteral("Không dừng được dịch vụ %1: %2").arg(name, winErrorText(code));
            CloseServiceHandle(svc);
            return false;
        }
    }
    const bool stopped = waitServiceStopped(svc, 20000);
    CloseServiceHandle(svc);
    if (!stopped)
        *error = QStringLiteral("Dịch vụ %1 chưa dừng hẳn sau 20 giây.").arg(name);
    return stopped;
}
#endif
} // namespace

namespace VolumeHolders
{

QString Holder::holdingSummary() const
{
    QStringList parts;
    if (imageOnDrive)
        parts << QStringLiteral("chạy từ ổ");
    else if (moduleOnDrive)
        parts << QStringLiteral("nạp DLL từ ổ");
    if (openHandles > 0)
    {
        QString s = QStringLiteral("%1 tệp/thư mục đang mở").arg(openHandles);
        if (!samplePaths.isEmpty())
            s += QStringLiteral(": ") + samplePaths.first();
        parts << s;
    }
    return parts.join(QStringLiteral(" · "));
}

bool isCriticalProcessName(const QString& exeName)
{
    static const QSet<QString> critical{"system", "registry", "secure system", "memory compression", "smss.exe", "csrss.exe",
                                        "wininit.exe", "winlogon.exe", "services.exe", "lsass.exe", "lsaiso.exe", "fontdrvhost.exe",
                                        "dwm.exe", "system idle process", "[system process]"};
    return critical.contains(exeName.trimmed().toLower());
}

bool enableDebugPrivilege()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege", &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
#else
    return false;
#endif
}

Report scan(const QString& driveLetter, const std::atomic_bool* stopFlag)
{
    Report report;
    const QString letter = normalizedLetter(driveLetter);
    if (letter.isEmpty())
    {
        report.error = QStringLiteral("Chữ ổ không hợp lệ.");
        return report;
    }
#ifdef Q_OS_WIN
    auto stopped = [&]() { return stopFlag && stopFlag->load(); };
    const QString prefix = letter + QStringLiteral(":\\");
    const std::wstring root = prefix.toStdWString();
    DWORD targetSerial = 0;
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, &targetSerial, nullptr, nullptr, nullptr, 0))
    {
        report.error = QStringLiteral("Không đọc được ổ %1: - %2").arg(letter, winErrorText(GetLastError()));
        return report;
    }
    report.elevated = WinElevation::isElevated();
    if (report.elevated)
        enableDebugPrivilege();
    for (const char* name : {"pagefile.sys", "swapfile.sys"})
    {
        const FsSafety::RawInfo info = FsSafety::rawInfo(prefix + QString::fromLatin1(name));
        if (info.exists && !info.isDir)
            report.pagefileOnDrive = true;
    }

    // Tên tiến trình theo PID.
    QHash<quint32, QString> names;
    {
        const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE)
        {
            PROCESSENTRY32W pe{};
            pe.dwSize = sizeof(pe);
            if (Process32FirstW(snap, &pe))
                do
                    names.insert(pe.th32ProcessID, QString::fromWCharArray(pe.szExeFile));
                while (Process32NextW(snap, &pe));
            CloseHandle(snap);
        }
    }
    const quint32 self = GetCurrentProcessId();
    QHash<quint32, Holder> holders;
    auto holderFor = [&](quint32 pid) -> Holder& {
        auto it = holders.find(pid);
        if (it == holders.end())
        {
            Holder h;
            h.pid = pid;
            h.name = names.value(pid, QStringLiteral("PID %1").arg(pid));
            h.self = pid == self;
            it = holders.insert(pid, h);
        }
        return it.value();
    };

    // 1) exe / DLL nạp từ ổ.
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
    {
        if (stopped())
            return report;
        const quint32 pid = it.key();
        if (pid <= 4)
            continue;
        const QString image = processImagePath(pid);
        if (!image.isEmpty() && image.startsWith(prefix, Qt::CaseInsensitive))
            holderFor(pid).imageOnDrive = true;
        else if (hasModuleOnDrive(pid, prefix))
            holderFor(pid).moduleOnDrive = true;
    }

    // 2) handle tệp/thư mục trên ổ.
    HANDLE probe = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    std::vector<char> table;
    if (probe != INVALID_HANDLE_VALUE && snapshotHandles(&table))
    {
        const auto* info = reinterpret_cast<const HandleTable*>(table.data());
        const ULONG_PTR total = info->NumberOfHandles;
        int fileType = -1;
        for (ULONG_PTR i = 0; i < total; ++i)
        {
            const HandleEntry& e = info->Handles[i];
            if (e.UniqueProcessId == self && reinterpret_cast<HANDLE>(e.HandleValue) == probe)
            {
                fileType = e.ObjectTypeIndex;
                break;
            }
        }
        if (fileType >= 0)
        {
            QHash<quint32, HANDLE> processHandles; // nullptr = không mở được
            QSet<quint32> unreadable;
            QHash<quintptr, QueryThread::Job> byObject; // cùng một đối tượng tệp -> cùng kết quả (khi biết địa chỉ đối tượng)
            auto* worker = new QueryThread();
            int abandoned = 0;
            for (ULONG_PTR i = 0; i < total && worker && worker->valid(); ++i)
            {
                if ((i & 0x3FF) == 0 && stopped())
                    break;
                const HandleEntry& e = info->Handles[i];
                if (e.ObjectTypeIndex != fileType)
                    continue;
                const quint32 pid = static_cast<quint32>(e.UniqueProcessId);
                if (pid <= 4 || (pid == self && reinterpret_cast<HANDLE>(e.HandleValue) == probe))
                    continue;
                QueryThread::Job job;
                const quintptr objectKey = reinterpret_cast<quintptr>(e.Object);
                if (objectKey != 0 && byObject.contains(objectKey))
                {
                    job = byObject.value(objectKey);
                }
                else
                {
                    HANDLE proc = nullptr;
                    if (!processHandles.contains(pid))
                    {
                        proc = pid == self ? GetCurrentProcess() : OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
                        processHandles.insert(pid, proc);
                        if (!proc)
                            unreadable.insert(pid);
                    }
                    proc = processHandles.value(pid);
                    if (!proc)
                        continue;
                    HANDLE dup = nullptr;
                    if (!DuplicateHandle(proc, reinterpret_cast<HANDLE>(e.HandleValue), GetCurrentProcess(), &dup, 0, FALSE,
                                         DUPLICATE_SAME_ACCESS))
                        continue;
                    if (!worker->run(dup, targetSerial, 300, &job))
                    {
                        // Treo (ống dẫn đồng bộ đang chờ): bỏ lại luồng + handle nhân bản, dùng luồng mới. Có trần để không rò rỉ vô hạn.
                        worker = ++abandoned <= 12 ? new QueryThread() : nullptr;
                        if (!worker)
                            report.handleScanComplete = false;
                        continue;
                    }
                    CloseHandle(dup);
                    if (objectKey != 0)
                        byObject.insert(objectKey, job);
                }
                if (!job.onTarget)
                    continue;
                Holder& h = holderFor(pid);
                ++h.openHandles;
                const QString path = job.path.empty() ? QStringLiteral("%1: (cả ổ)").arg(letter)
                                                      : stripWin32Prefix(QString::fromStdWString(job.path));
                if (h.samplePaths.size() < 5 && !h.samplePaths.contains(path, Qt::CaseInsensitive))
                    h.samplePaths << path;
            }
            if (worker)
            {
                worker->shutdown();
                delete worker;
            }
            for (auto it = processHandles.constBegin(); it != processHandles.constEnd(); ++it)
                if (it.value() && it.key() != self)
                    CloseHandle(it.value());
            for (quint32 pid : unreadable)
                if (!holders.contains(pid))
                    report.unreadable << QStringLiteral("%1 (PID %2)").arg(names.value(pid, QStringLiteral("?")), QString::number(pid));
            std::sort(report.unreadable.begin(), report.unreadable.end(),
                      [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
        }
        else
        {
            report.handleScanComplete = false;
        }
    }
    else
    {
        report.handleScanComplete = false;
    }
    if (probe != INVALID_HANDLE_VALUE)
        CloseHandle(probe);

    // 3) dịch vụ + mức "hệ thống".
    const QHash<quint32, QList<ServiceInfo>> services = servicesByPid();
    for (auto it = holders.begin(); it != holders.end(); ++it)
    {
        Holder& h = it.value();
        h.imagePath = processImagePath(h.pid);
        for (const ServiceInfo& s : services.value(h.pid))
        {
            h.serviceNames << s.name;
            h.serviceLabels << (s.display.isEmpty() || s.display == s.name ? s.name : QStringLiteral("%1 (%2)").arg(s.display, s.name));
        }
        h.critical = isCriticalProcessName(h.name) || processIsCritical(h.pid) ||
                     // svchost không có dịch vụ dừng được: KHÔNG bao giờ diệt cả tiến trình chủ dịch vụ
                     (h.name.compare(QLatin1String("svchost.exe"), Qt::CaseInsensitive) == 0 && h.serviceNames.isEmpty());
        report.holders << h;
    }
    std::sort(report.holders.begin(), report.holders.end(), [](const Holder& a, const Holder& b) {
        if (a.stoppable() != b.stoppable())
            return a.stoppable();
        if (a.openHandles != b.openHandles)
            return a.openHandles > b.openHandles;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
#else
    Q_UNUSED(stopFlag);
    report.error = QStringLiteral("Chỉ hỗ trợ trên Windows.");
#endif
    return report;
}

bool stop(const Holder& holder, QString* error)
{
    auto fail = [&](const QString& e) {
        if (error)
            *error = e;
        return false;
    };
    if (!holder.stoppable())
        return fail(holder.self ? QStringLiteral("Đây là chính ứng dụng này.")
                                : QStringLiteral("%1 là tiến trình hệ thống của Windows - không được dừng.").arg(holder.name));
#ifdef Q_OS_WIN
    if (!holder.serviceNames.isEmpty())
    {
        const SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!scm)
            return fail(QStringLiteral("Không mở được Service Control Manager: %1").arg(winErrorText(GetLastError())));
        QStringList errors;
        for (const QString& name : holder.serviceNames)
        {
            QString e;
            if (!stopService(scm, name, &e))
                errors << e;
        }
        CloseServiceHandle(scm);
        if (!errors.isEmpty())
            return fail(errors.join('\n'));
        return true;
    }
    // Tiến trình thường: kiểm lại đúng tiến trình đó (PID có thể đã được Windows cấp cho chương trình khác từ lúc quét).
    const HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, holder.pid);
    if (!h)
    {
        const DWORD code = GetLastError();
        if (code == ERROR_INVALID_PARAMETER)
            return true; // đã thoát
        return fail(QStringLiteral("Không mở được tiến trình %1 (PID %2): %3").arg(holder.name).arg(holder.pid).arg(winErrorText(code)));
    }
    wchar_t buf[2048];
    DWORD len = 2048;
    const QString image = QueryFullProcessImageNameW(h, 0, buf, &len) ? QString::fromWCharArray(buf, static_cast<int>(len)) : QString();
    if (!holder.imagePath.isEmpty() && image.compare(holder.imagePath, Qt::CaseInsensitive) != 0)
    {
        CloseHandle(h);
        return fail(QStringLiteral("PID %1 không còn là %2 (đã thoát) - hãy quét lại.").arg(holder.pid).arg(holder.name));
    }
    if (!TerminateProcess(h, 1))
    {
        const DWORD code = GetLastError();
        CloseHandle(h);
        return fail(QStringLiteral("Không dừng được %1 (PID %2): %3").arg(holder.name).arg(holder.pid).arg(winErrorText(code)));
    }
    WaitForSingleObject(h, 5000);
    CloseHandle(h);
    return true;
#else
    return fail(QStringLiteral("Chỉ hỗ trợ trên Windows."));
#endif
}
} // namespace VolumeHolders
