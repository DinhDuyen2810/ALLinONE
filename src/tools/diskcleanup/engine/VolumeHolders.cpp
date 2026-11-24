#include "VolumeHolders.h"

#include "FsSafety.h"
#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"
#include "core/WinElevation.h"

#include <QDir>
#include <QHash>
#include <QProcess>
#include <QSet>
#include <QSettings>

#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <thread>
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

/// Tên đối tượng nhân của một handle (NtQueryObject ObjectNameInformation), vd "\Device\HarddiskVolume4\thu muc\tep". Có thể TREO với ống dẫn
/// đồng bộ đang chờ - chỉ gọi trên luồng phụ có hạn giờ.
std::wstring objectName(HANDLE h)
{
    using NtQueryObjectFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    static const auto query =
        reinterpret_cast<NtQueryObjectFn>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryObject")));
    if (!query)
        return {};
    struct UnicodeString
    {
        USHORT Length;
        USHORT MaximumLength;
        PWSTR Buffer;
    };
    alignas(8) BYTE buf[4096];
    ULONG ret = 0;
    if (query(h, 1 /*ObjectNameInformation*/, buf, sizeof(buf), &ret) != 0)
        return {};
    const auto* u = reinterpret_cast<const UnicodeString*>(buf);
    if (!u->Buffer || u->Length == 0)
        return {};
    return std::wstring(u->Buffer, u->Length / sizeof(wchar_t));
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
        const std::wstring* targetDevice{nullptr}; // "\Device\HarddiskVolumeN" của ổ cần tìm
        bool onTarget{false};
        bool wholeVolume{false}; // handle mở thẳng CẢ ổ (đọc thô) - không có đường dẫn tệp
        std::wstring path;       // "\\?\X:\..." (GetFinalPathNameByHandleW) hoặc phần sau tên thiết bị (theo tên đối tượng)
        bool pathRelativeToDevice{false};
    };

    QueryThread()
    {
        m_request = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_thread = CreateThread(nullptr, 64 * 1024, &QueryThread::entry, this, 0, nullptr);
    }
    bool valid() const { return m_request && m_done && m_thread; }
    /// false = hết giờ: luồng đang kẹt, đối tượng này phải bị bỏ lại (KHÔNG xóa).
    bool run(HANDLE h, DWORD targetSerial, const std::wstring* targetDevice, DWORD timeoutMs, Job* result)
    {
        m_job = Job{};
        m_job.handle = h;
        m_job.targetSerial = targetSerial;
        m_job.targetDevice = targetDevice;
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
                if (GetVolumeInformationByHandleW(j.handle, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
                {
                    if (serial == j.targetSerial)
                    {
                        j.onTarget = true;
                        wchar_t buf[1024];
                        const DWORD n = GetFinalPathNameByHandleW(j.handle, buf, 1024, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
                        if (n > 0 && n < 1024)
                            j.path.assign(buf, n);
                    }
                }
                else if (j.targetDevice && !j.targetDevice->empty())
                {
                    // Handle mở THẲNG thiết bị ổ ("\\.\X:" - phần mềm bảo mật/EDR, sao lưu, công cụ tìm tệp đọc thô ổ): đo thật, cả
                    // GetVolumeInformationByHandleW lẫn GetFinalPathNameByHandleW đều thất bại (mã 1) - chỉ TÊN đối tượng
                    // ("\Device\HarddiskVolumeN") cho biết nó thuộc ổ nào. Loại handle này chặn khóa ổ mà bản cũ không thấy.
                    const std::wstring name = objectName(j.handle);
                    const std::wstring& dev = *j.targetDevice;
                    if (!name.empty() && name.size() >= dev.size() && _wcsnicmp(name.c_str(), dev.c_str(), dev.size()) == 0 &&
                        (name.size() == dev.size() || name[dev.size()] == L'\\'))
                    {
                        j.onTarget = true;
                        j.wholeVolume = name.size() == dev.size();
                        j.path = name.substr(dev.size());
                        j.pathRelativeToDevice = true;
                    }
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

namespace
{
#ifdef Q_OS_WIN
using NtQueryInformationFileFn = LONG(NTAPI*)(HANDLE, PVOID, PVOID, ULONG, ULONG);
constexpr ULONG kFileProcessIdsUsingFileInformation = 47;
constexpr LONG kStatusBufferOverflow = static_cast<LONG>(0x80000005);

struct IoStatus
{
    LONG status;
    ULONG_PTR information;
};

/// "D:\..." -> "\\?\D:\..." (vượt MAX_PATH).
std::wstring longPath(const QString& path)
{
    return (QStringLiteral("\\\\?\\") + path).toStdWString();
}

/// PID các tiến trình đang mở 'path' (tệp/thư mục/thiết bị). Mở CHỈ đọc thuộc tính, chia sẻ toàn phần (không cản ai, không đọc nội dung, không đi
/// theo liên kết). *exclusive = ngay cả cách mở đó cũng bị từ chối "đang dùng" (nhân Windows giữ độc quyền, vd pagefile); *denied = thiếu quyền.
QList<quint32> pidsUsing(const std::wstring& nativePath, bool* exclusive, bool* denied)
{
    static const auto query = reinterpret_cast<NtQueryInformationFileFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationFile")));
    *exclusive = false;
    *denied = false;
    QList<quint32> out;
    const HANDLE h = CreateFileW(nativePath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
    {
        const DWORD e = GetLastError();
        *exclusive = (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION);
        *denied = (e == ERROR_ACCESS_DENIED);
        return out;
    }
    if (query)
    {
        std::vector<BYTE> buf(4096);
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            IoStatus iosb{};
            const LONG st = query(h, &iosb, buf.data(), static_cast<ULONG>(buf.size()), kFileProcessIdsUsingFileInformation);
            if (st == kStatusInfoLengthMismatch || st == kStatusBufferOverflow)
            {
                buf.resize(buf.size() * 4);
                continue;
            }
            if (st == 0)
            {
                const ULONG n = *reinterpret_cast<const ULONG*>(buf.data());
                const auto* ids = reinterpret_cast<const ULONG_PTR*>(buf.data() + sizeof(ULONG_PTR));
                const size_t maxIds = (buf.size() - sizeof(ULONG_PTR)) / sizeof(ULONG_PTR);
                for (ULONG k = 0; k < n && k < maxIds; ++k)
                    out << static_cast<quint32>(ids[k]);
            }
            break;
        }
    }
    CloseHandle(h);
    return out;
}

struct DirEntry
{
    QString name;
    DWORD attributes{0};
    qint64 lastWrite{0};
};

/// Liệt kê một thư mục qua HANDLE mở với FILE_FLAG_BACKUP_SEMANTICS (GetFileInformationByHandleEx FileIdBothDirectoryInfo) - với SeBackupPrivilege
/// (Administrator) đọc được cả thư mục mà FindFirstFile bị từ chối, như "System Volume Information" (bản sao bóng, chỉ mục, tracking.log...).
bool listDirectoryByHandle(const QString& dirWithSlash, std::vector<DirEntry>* out)
{
    const HANDLE h = CreateFileW(longPath(dirWithSlash).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    alignas(8) static thread_local BYTE buf[64 * 1024];
    bool any = false;
    while (GetFileInformationByHandleEx(h, FileIdBothDirectoryInfo, buf, sizeof(buf)))
    {
        any = true;
        const BYTE* p = buf;
        for (;;)
        {
            const auto* info = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(p);
            const QString name = QString::fromWCharArray(info->FileName, static_cast<int>(info->FileNameLength / sizeof(wchar_t)));
            if (name != QLatin1String(".") && name != QLatin1String(".."))
                out->push_back({name, info->FileAttributes, info->LastWriteTime.QuadPart});
            if (info->NextEntryOffset == 0)
                break;
            p += info->NextEntryOffset;
        }
    }
    CloseHandle(h);
    return any;
}

bool enablePrivilege(const wchar_t* name)
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    const bool ok = LookupPrivilegeValueW(nullptr, name, &tp.Privileges[0].Luid) &&
                    AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

struct WslDistro
{
    QString name;
    QString basePath;
};

/// Các bản Linux WSL của tài khoản này (HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss\{GUID}: DistributionName, BasePath).
QList<WslDistro> wslDistros()
{
    QList<WslDistro> out;
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"), QSettings::NativeFormat);
    for (const QString& key : reg.childGroups())
    {
        reg.beginGroup(key);
        WslDistro d;
        d.name = reg.value("DistributionName").toString();
        d.basePath = reg.value("BasePath").toString();
        reg.endGroup();
        if (d.basePath.startsWith(QLatin1String("\\\\?\\")))
            d.basePath = d.basePath.mid(4);
        if (!d.basePath.isEmpty())
            out << d;
    }
    return out;
}

/// Tệp registry (hive) đang nạp từ ổ có thiết bị 'deviceName' ("\Device\HarddiskVolume4") - trả về dạng "X:\...".
QStringList hivesOnDevice(const QString& deviceName, const QString& letter)
{
    QStringList out;
    if (deviceName.isEmpty())
        return out;
    QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\hivelist"), QSettings::NativeFormat);
    const QString prefix = deviceName + QLatin1Char('\\');
    for (const QString& key : reg.allKeys())
    {
        const QString v = reg.value(key).toString();
        if (v.startsWith(prefix, Qt::CaseInsensitive))
            out << letter + QStringLiteral(":\\") + v.mid(prefix.size());
    }
    return out;
}
#endif
} // namespace

namespace VolumeHolders
{

QList<quint32> processesUsingPath(const QString& path)
{
#ifdef Q_OS_WIN
    bool exclusive = false, denied = false;
    QList<quint32> pids = pidsUsing(longPath(QDir::toNativeSeparators(path)), &exclusive, &denied);
    pids.removeAll(GetCurrentProcessId()); // chính lần mở để hỏi
    return pids;
#else
    Q_UNUSED(path);
    return {};
#endif
}

QStringList directoryEntriesByHandle(const QString& dir)
{
    QStringList out;
#ifdef Q_OS_WIN
    QString d = QDir::toNativeSeparators(dir);
    if (!d.endsWith(QLatin1Char('\\')))
        d += QLatin1Char('\\');
    std::vector<DirEntry> entries;
    listDirectoryByHandle(d, &entries);
    for (const DirEntry& e : entries)
        out << e.name;
#else
    Q_UNUSED(dir);
#endif
    return out;
}

bool Holder::stoppable() const
{
    if (kind == HolderKind::Wsl || kind == HolderKind::VirtualDisk)
        return true;
    return kind == HolderKind::Process && !critical && !self && pid > 4;
}

QString Holder::stopLabel() const
{
    switch (kind)
    {
        case HolderKind::Wsl: return QStringLiteral("⛔ Tắt WSL");
        case HolderKind::VirtualDisk: return QStringLiteral("⏏ Tháo ổ ảo");
        case HolderKind::System: return QStringLiteral("Windows");
        case HolderKind::Process: break;
    }
    if (self)
        return QStringLiteral("Ứng dụng này");
    return stoppable() ? QStringLiteral("⛔ Dừng") : QStringLiteral("Hệ thống");
}

QString Holder::holdingSummary() const
{
    QStringList parts;
    if (imageOnDrive)
        parts << QStringLiteral("chạy từ ổ");
    else if (moduleOnDrive)
        parts << QStringLiteral("nạp DLL từ ổ");
    if (mappedOnDrive)
        parts << QStringLiteral("ánh xạ tệp vào bộ nhớ");
    if (openHandles > 0)
    {
        QString s = QStringLiteral("%1 tệp/thư mục đang mở").arg(openHandles);
        if (!samplePaths.isEmpty())
            s += QStringLiteral(": ") + samplePaths.first();
        parts << s;
    }
    if (!hint.isEmpty())
    {
        // Thứ không phải chương trình thường (nhân Windows/WSL/ổ ảo): cách gỡ quan trọng hơn danh sách tệp - đặt lên trước (cột hẹp cắt phần sau).
        if (kind == HolderKind::Process)
            parts << hint;
        else
            parts.prepend(hint);
    }
    return parts.join(QStringLiteral(" · "));
}

bool isCriticalProcessName(const QString& exeName)
{
    static const QSet<QString> critical{"system", "registry", "secure system", "memory compression", "smss.exe", "csrss.exe",
                                        "wininit.exe", "winlogon.exe", "services.exe", "lsass.exe", "lsaiso.exe", "fontdrvhost.exe",
                                        "dwm.exe", "system idle process", "[system process]",
                                        // máy ảo Hyper-V/WSL: diệt tiến trình = tắt ngang máy ảo (hỏng dữ liệu trong máy ảo). WSL có nút riêng.
                                        "vmmem", "vmmemwsl", "vmwp.exe", "vmcompute.exe", "vmms.exe"};
    return critical.contains(exeName.trimmed().toLower());
}

bool isDiskImagePath(const QString& path)
{
    static const QStringList ext{".vhd", ".vhdx", ".avhd", ".avhdx", ".iso", ".vhds"};
    for (const QString& e : ext)
        if (path.endsWith(e, Qt::CaseInsensitive))
            return true;
    return false;
}

QString systemHoldHint(const QString& path)
{
    const QString p = QDir::toNativeSeparators(path);
    const QString name = p.section(QLatin1Char('\\'), -1);
    if (name.compare(QLatin1String("pagefile.sys"), Qt::CaseInsensitive) == 0 ||
        name.compare(QLatin1String("swapfile.sys"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("tệp hoán trang (bộ nhớ ảo) - tắt bộ nhớ ảo trên ổ này rồi khởi động lại máy");
    if (name.endsWith(QLatin1String(".sys"), Qt::CaseInsensitive))
        return QStringLiteral("trình điều khiển (driver) nạp từ ổ này - gỡ phần mềm đó (hoặc cài lại sang ổ khác) rồi khởi động lại máy");
    if (p.contains(QStringLiteral("(cả ổ")))
        return QStringLiteral("thành phần của Windows mở thẳng cả ổ");
    if (name.compare(QLatin1String("ext4.vhdx"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("ổ đĩa của một bản Linux WSL - tắt WSL (wsl --shutdown)");
    if (isDiskImagePath(p))
        return QStringLiteral("ổ đĩa ảo đang gắn từ tệp này (máy ảo, WSL, Docker hoặc ổ ảo/ISO đã Mount) - tháo ổ ảo / tắt máy ảo");
    if (p.contains(QLatin1String("\\System Volume Information\\"), Qt::CaseInsensitive) ||
        p.endsWith(QLatin1String("\\System Volume Information"), Qt::CaseInsensitive))
        return QStringLiteral("bản sao bóng (Volume Shadow Copy) / điểm khôi phục / chỉ mục của Windows trên ổ - tắt \"Bảo vệ hệ thống\" cho ổ này "
                              "(hoặc xóa bản sao bóng) rồi thử lại");
    if (name.compare(QLatin1String("NTUSER.DAT"), Qt::CaseInsensitive) == 0 ||
        name.compare(QLatin1String("UsrClass.dat"), Qt::CaseInsensitive) == 0 || p.contains(QLatin1String("\\config\\"), Qt::CaseInsensitive))
        return QStringLiteral("tệp registry (hive) đang nạp - thường là hồ sơ người dùng nằm trên ổ này; đăng xuất tài khoản đó");
    if (p.contains(QLatin1String("\\$Extend\\"), Qt::CaseInsensitive) || name.startsWith(QLatin1Char('$')))
        return QStringLiteral("siêu dữ liệu NTFS");
    return {};
}

bool enableDebugPrivilege()
{
#ifdef Q_OS_WIN
    const bool debug = enablePrivilege(L"SeDebugPrivilege");
    enablePrivilege(L"SeBackupPrivilege"); // mở được tệp/thư mục bị hạn chế quyền để HỎI ai đang dùng (không đọc nội dung)
    return debug;
#else
    return false;
#endif
}

Report scan(const QString& driveLetter, const std::atomic_bool* stopFlag, bool deep, const std::function<void(int)>& progress)
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
    // "\Device\HarddiskVolumeN" của ổ - để nhận ra handle mở thẳng cả ổ, tệp ánh xạ bộ nhớ, trình điều khiển, registry hive.
    std::wstring targetDevice;
    {
        wchar_t dev[512] = {};
        const std::wstring drive = (letter + QLatin1Char(':')).toStdWString();
        if (QueryDosDeviceW(drive.c_str(), dev, 512))
            targetDevice = dev;
    }
    const QString deviceName = QString::fromStdWString(targetDevice);
    // Đường dẫn dạng thiết bị -> "X:\..." (rỗng nếu không thuộc ổ này).
    auto fromDevicePath = [&](const QString& devPath) -> QString {
        if (deviceName.isEmpty() || !devPath.startsWith(deviceName, Qt::CaseInsensitive))
            return {};
        const QString rest = devPath.mid(deviceName.size());
        if (rest.isEmpty())
            return QStringLiteral("%1: (cả ổ - đọc thô)").arg(letter);
        if (!rest.startsWith(QLatin1Char('\\')))
            return {};
        return letter + QLatin1Char(':') + rest;
    };
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
    names.insert(4, QStringLiteral("System"));
    const quint32 self = GetCurrentProcessId();
    QHash<quint32, Holder> holders;
    QHash<quint32, QHash<QString, QString>> pathsByPid; // tệp KHÁC NHAU mỗi tiến trình đang mở (khóa chữ thường -> đường dẫn gốc), gộp mọi bước
    auto holderFor = [&](quint32 pid) -> Holder& {
        auto it = holders.find(pid);
        if (it == holders.end())
        {
            Holder h;
            h.pid = pid;
            h.name = names.value(pid, QStringLiteral("PID %1").arg(pid));
            h.self = pid == self;
            if (pid == 4)
            {
                h.kind = HolderKind::System;
                h.name = QStringLiteral("System (nhân Windows)");
                h.critical = true;
            }
            it = holders.insert(pid, h);
        }
        return it.value();
    };
    auto addPath = [&](quint32 pid, const QString& path) {
        Holder& h = holderFor(pid);
        QHash<QString, QString>& seen = pathsByPid[pid];
        const QString key = path.toLower();
        if (seen.contains(key))
            return;
        seen.insert(key, path);
        if (h.samplePaths.size() < 5)
            h.samplePaths << path;
    };

    // 1) exe nằm trên ổ + mọi thứ ÁNH XẠ vào bộ nhớ từ ổ: DLL/exe (MEM_IMAGE) và tệp dữ liệu ánh xạ (MEM_MAPPED - giữ ổ kể cả khi chương
    //    trình đã đóng handle tệp; NTFS không xả được nên từ chối khóa ổ). VirtualQueryEx + GetMappedFileNameW (như Process Explorer).
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
        if (targetDevice.empty())
            continue;
        const HANDLE proc = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!proc)
            continue;
        MEMORY_BASIC_INFORMATION mbi;
        const char* addr = nullptr;
        const void* lastBase = nullptr;
        int regions = 0;
        while (VirtualQueryEx(proc, addr, &mbi, sizeof(mbi)) == sizeof(mbi) && ++regions < 200000)
        {
            if ((mbi.Type == MEM_MAPPED || mbi.Type == MEM_IMAGE) && mbi.AllocationBase != lastBase)
            {
                lastBase = mbi.AllocationBase;
                wchar_t mapped[1024];
                const DWORD n = GetMappedFileNameW(proc, mbi.BaseAddress, mapped, 1024);
                if (n > 0 && n < 1024)
                {
                    const QString path = fromDevicePath(QString::fromWCharArray(mapped, static_cast<int>(n)));
                    if (!path.isEmpty())
                    {
                        Holder& h = holderFor(pid);
                        if (mbi.Type == MEM_IMAGE)
                        {
                            if (path.compare(image, Qt::CaseInsensitive) != 0)
                                h.moduleOnDrive = true;
                        }
                        else
                        {
                            h.mappedOnDrive = true;
                            addPath(pid, path);
                        }
                    }
                }
            }
            const char* next = static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize;
            if (next <= addr)
                break;
            addr = next;
        }
        CloseHandle(proc);
    }

    // 1b) trình điều khiển (driver .sys) nạp từ ổ - nhân Windows giữ ảnh của nó.
    {
        std::vector<LPVOID> drivers(2048);
        DWORD needed = 0;
        if (EnumDeviceDrivers(drivers.data(), static_cast<DWORD>(drivers.size() * sizeof(LPVOID)), &needed))
        {
            const size_t count = std::min<size_t>(drivers.size(), needed / sizeof(LPVOID));
            for (size_t i = 0; i < count; ++i)
            {
                wchar_t file[1024];
                const DWORD n = GetDeviceDriverFileNameW(drivers[i], file, 1024);
                if (n == 0 || n >= 1024)
                    continue;
                QString p = QString::fromWCharArray(file, static_cast<int>(n));
                if (p.startsWith(QLatin1String("\\??\\")))
                    p = p.mid(4);
                QString onDrive = p.startsWith(prefix, Qt::CaseInsensitive) ? p : fromDevicePath(p);
                if (!onDrive.isEmpty())
                    addPath(4, onDrive);
            }
        }
    }

    // 2) handle tệp/thư mục trên ổ (nhân bản handle - thấy được tiến trình thường).
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
                    if (!processHandles.contains(pid))
                    {
                        HANDLE proc = pid == self ? GetCurrentProcess() : OpenProcess(PROCESS_DUP_HANDLE, FALSE, pid);
                        processHandles.insert(pid, proc);
                        if (!proc)
                            unreadable.insert(pid);
                    }
                    const HANDLE proc = processHandles.value(pid);
                    if (!proc)
                        continue;
                    HANDLE dup = nullptr;
                    if (!DuplicateHandle(proc, reinterpret_cast<HANDLE>(e.HandleValue), GetCurrentProcess(), &dup, 0, FALSE,
                                         DUPLICATE_SAME_ACCESS))
                        continue;
                    if (!worker->run(dup, targetSerial, &targetDevice, 300, &job))
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
                if (job.wholeVolume)
                    addPath(pid, QStringLiteral("%1: (cả ổ - đọc thô)").arg(letter));
                else if (job.pathRelativeToDevice)
                    addPath(pid, letter + QLatin1Char(':') + QString::fromStdWString(job.path));
                else
                    addPath(pid, job.path.empty() ? QStringLiteral("%1: (cả ổ)").arg(letter) : stripWin32Prefix(QString::fromStdWString(job.path)));
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

    // 3) KIỂM KỸ: hỏi Windows "PID nào đang mở tệp này" (FileProcessIdsUsingFileInformation) - thấy CẢ nhân Windows (PID 4), tiến trình được bảo
    //    vệ, máy ảo. Mỗi lần hỏi Windows phải duyệt cả bảng handle của hệ thống (đo thật: ~22 ms/tệp) nên KHÔNG hỏi mọi tệp (140.000 tệp ~ 50 phút):
    //    liệt kê cả ổ (rẻ) rồi chỉ hỏi các tệp CÓ KHẢ NĂNG đang bị giữ, chạy song song, có trần số tệp + thời gian.
    if (deep && !stopped())
    {
        struct Candidate
        {
            QString display;
            std::wstring native;
            int priority; // 0 luôn kiểm (thiết bị ổ, gốc, ổ ảo, System Volume Information, WSL); 1 ghi trong 24 giờ; 2 CSDL/nhật ký; 3 thư mục 3 cấp đầu
        };
        static const QStringList imageExt{".vhd", ".vhdx", ".avhd", ".avhdx", ".vhds", ".iso", ".vmdk", ".vdi", ".hc", ".tc", ".img", ".qcow2"};
        static const QStringList dbExt{".db", ".db-wal", ".db-shm", ".db-journal", ".sqlite", ".sqlite3", ".sqlite-wal", ".sqlite-shm", ".ldb",
                                       ".edb", ".jrs", ".chk", ".mdf", ".ldf", ".ndf", ".pst", ".ost", ".lock", ".lck", ".log", ".etl",
                                       ".blf", ".regtrans-ms"};
        auto endsWithAny = [](const QString& name, const QStringList& exts) {
            for (const QString& e : exts)
                if (name.endsWith(e, Qt::CaseInsensitive))
                    return true;
            return false;
        };
        QStringList wslBases;
        for (const WslDistro& d : wslDistros())
            if (d.basePath.startsWith(prefix, Qt::CaseInsensitive))
                wslBases << QDir::toNativeSeparators(d.basePath).toLower() + QLatin1Char('\\');
        auto underWsl = [&](const QString& path) {
            const QString l = path.toLower() + QLatin1Char('\\');
            for (const QString& b : wslBases)
                if (l.startsWith(b))
                    return true;
            return false;
        };

        // KHÔNG hỏi trên chính thiết bị ổ ("\\.\X:"): đo thật, Windows trả về MỌI tiến trình của máy cho câu hỏi đó - vô nghĩa.
        std::vector<Candidate> cands;
        cands.push_back({prefix, longPath(prefix), 0});
        const qint64 recentLimit = FsSafety::nowFileTime() - 24LL * 3600LL * 10000000LL;
        struct Dir
        {
            QString path; // kết thúc bằng '\'
            int depth;
            bool special; // trong System Volume Information hoặc thư mục WSL: kiểm hết
        };
        std::vector<Dir> stack{{prefix, 0, false}};
        while (!stack.empty() && !stopped())
        {
            const Dir d = stack.back();
            stack.pop_back();
            std::vector<DirEntry> entries;
            {
                WIN32_FIND_DATAW data;
                const HANDLE find = FindFirstFileExW(longPath(d.path + QLatin1Char('*')).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                                     nullptr, FIND_FIRST_EX_LARGE_FETCH);
                if (find != INVALID_HANDLE_VALUE)
                {
                    do
                    {
                        const wchar_t* n = data.cFileName;
                        if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0)))
                            continue;
                        entries.push_back({QString::fromWCharArray(n), data.dwFileAttributes,
                                           (static_cast<qint64>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime});
                    } while (FindNextFileW(find, &data));
                    FindClose(find);
                }
                else if (GetLastError() == ERROR_ACCESS_DENIED)
                {
                    listDirectoryByHandle(d.path, &entries); // vd System Volume Information - cần SeBackupPrivilege (Administrator)
                }
            }
            for (const DirEntry& entry : entries)
            {
                if (stopped())
                    break;
                ++report.entriesListed;
                const QString& name = entry.name;
                const QString full = d.path + name;
                const bool special = d.special || (d.depth == 0 && name.compare(QLatin1String("System Volume Information"), Qt::CaseInsensitive) == 0) ||
                                     underWsl(full);
                if (entry.attributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (special || d.depth < 3)
                        cands.push_back({full, longPath(full), special ? 0 : 3});
                    if (!(entry.attributes & FILE_ATTRIBUTE_REPARSE_POINT)) // junction/điểm gắn: KHÔNG đi vào
                        stack.push_back({full + QLatin1Char('\\'), d.depth + 1, special});
                    continue;
                }
                const qint64 written = entry.lastWrite;
                int priority = -1;
                if (special || d.depth == 0 || endsWithAny(name, imageExt))
                    priority = 0;
                else if (written >= recentLimit)
                    priority = 1;
                else if (endsWithAny(name, dbExt))
                    priority = 2;
                if (priority >= 0)
                    cands.push_back({full, longPath(full), priority});
            }
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
        constexpr size_t kMaxCandidates = 6000;
        if (cands.size() > kMaxCandidates)
        {
            cands.resize(kMaxCandidates);
            report.deepLimited = true;
        }

        // Hỏi song song: mỗi luồng lấy tệp kế tiếp; tổng thời gian tối đa 90 giây.
        std::vector<QList<quint32>> results(cands.size());
        std::vector<char> processed(cands.size(), 0), exclusiveHold(cands.size(), 0), deniedOpen(cands.size(), 0);
        std::atomic<size_t> next{0};
        std::atomic<int> done{0};
        QElapsedTimer budget;
        budget.start();
        auto work = [&]() {
            for (;;)
            {
                if (stopped() || budget.elapsed() > 90000)
                    return;
                const size_t i = next.fetch_add(1);
                if (i >= cands.size())
                    return;
                bool exclusive = false, denied = false;
                results[i] = pidsUsing(cands[i].native, &exclusive, &denied);
                exclusiveHold[i] = exclusive;
                deniedOpen[i] = denied;
                processed[i] = 1;
                const int n = done.fetch_add(1) + 1;
                if (progress && (n % 200) == 0)
                    progress(n);
            }
        };
        const unsigned hw = std::thread::hardware_concurrency();
        const int threadCount = static_cast<int>(std::clamp<unsigned>(hw == 0 ? 4 : hw, 4, 12));
        std::vector<std::thread> pool;
        for (int t = 0; t < threadCount; ++t)
            pool.emplace_back(work);
        for (std::thread& t : pool)
            t.join();
        report.filesChecked = done.load();
        if (static_cast<size_t>(report.filesChecked) < cands.size())
            report.deepLimited = true;
        for (size_t i = 0; i < cands.size(); ++i)
        {
            if (!processed[i])
                continue;
            if (deniedOpen[i])
                ++report.filesDenied;
            if (exclusiveHold[i])
                addPath(4, cands[i].display); // ngay cả mở-đọc-thuộc-tính cũng bị từ chối: nhân Windows giữ độc quyền
            for (quint32 pid : results[i])
                if (pid != self) // chính lần mở để hỏi của tiến trình này cũng nằm trong danh sách
                    addPath(pid, cands[i].display);
        }
        report.deepScanDone = !stopped();
    }

    // 4) registry hive nạp từ ổ (nhân Windows giữ).
    for (const QString& hive : hivesOnDevice(deviceName, letter))
        addPath(4, hive);

    // 5) WSL: bản Linux nằm trên ổ + máy ảo WSL đang chạy.
    {
        QStringList onDrive;
        QStringList vhdxPaths;
        for (const WslDistro& d : wslDistros())
            if (d.basePath.startsWith(prefix, Qt::CaseInsensitive))
            {
                onDrive << d.name;
                vhdxPaths << QDir::toNativeSeparators(d.basePath + QStringLiteral("\\ext4.vhdx"));
            }
        if (!onDrive.isEmpty())
        {
            quint32 vmPid = 0;
            for (auto it = names.constBegin(); it != names.constEnd(); ++it)
                if (it.value().compare(QLatin1String("vmmemWSL"), Qt::CaseInsensitive) == 0 ||
                    it.value().compare(QLatin1String("vmmem"), Qt::CaseInsensitive) == 0)
                    vmPid = it.key();
            bool vhdxHeld = false;
            for (auto it = pathsByPid.constBegin(); it != pathsByPid.constEnd(); ++it)
                for (const QString& v : vhdxPaths)
                    if (it.value().contains(v.toLower()))
                        vhdxHeld = true;
            if (vmPid != 0 || vhdxHeld)
            {
                Holder w;
                w.kind = HolderKind::Wsl;
                w.pid = vmPid;
                w.name = QStringLiteral("WSL - máy ảo Linux (%1)").arg(onDrive.join(", "));
                w.samplePaths = vhdxPaths.mid(0, 5);
                w.openHandles = static_cast<int>(vhdxPaths.size());
                w.hint = QStringLiteral("ổ đĩa ext4.vhdx của các bản Linux WSL nằm trên ổ này - máy ảo WSL đang chạy giữ chúng; \"Tắt WSL\" = wsl --shutdown "
                                        "(dừng MỌI bản Linux đang chạy)");
                report.holders << w;
                // Tiến trình vmmem tương ứng không hiện riêng (đã gộp vào dòng WSL).
                holders.remove(vmPid);
            }
            else
            {
                report.notes << QStringLiteral("WSL có %1 bản Linux trên ổ %2: (%3) - đang tắt; đừng mở WSL/Docker trong lúc dời.")
                                    .arg(onDrive.size())
                                    .arg(letter)
                                    .arg(onDrive.join(", "));
            }
        }
    }

    // 6) dịch vụ + mức "hệ thống" + gợi ý cho thứ nhân Windows giữ; tách ổ ảo thành dòng "Tháo" riêng.
    const QHash<quint32, QList<ServiceInfo>> services = servicesByPid();
    for (auto it = holders.begin(); it != holders.end(); ++it)
    {
        Holder& h = it.value();
        h.openHandles = static_cast<int>(pathsByPid.value(h.pid).size());
        if (h.kind == HolderKind::System)
        {
            // Mỗi tệp ổ ảo (không phải WSL) mà nhân đang giữ -> một dòng "Tháo ổ ảo".
            QStringList rest;
            QStringList hints;
            for (const QString& display : pathsByPid.value(4))
            {
                if (isDiskImagePath(display) && !display.endsWith(QLatin1String("ext4.vhdx"), Qt::CaseInsensitive))
                {
                    Holder v;
                    v.kind = HolderKind::VirtualDisk;
                    v.pid = 4;
                    v.name = QStringLiteral("Ổ đĩa ảo: %1").arg(display.section(QLatin1Char('\\'), -1));
                    v.diskImagePath = display;
                    v.samplePaths = {display};
                    v.openHandles = 1;
                    v.hint = systemHoldHint(display);
                    report.holders << v;
                    continue;
                }
                const QString hint = systemHoldHint(display);
                if (!hint.isEmpty() && !hints.contains(hint))
                    hints << hint;
                rest << display;
            }
            if (rest.isEmpty())
                continue; // chỉ có ổ ảo - đã thành dòng riêng
            h.openHandles = static_cast<int>(rest.size());
            h.hint = hints.join(QStringLiteral("; "));
            report.holders << h;
            continue;
        }
        h.imagePath = processImagePath(h.pid);
        for (const QString& p : pathsByPid.value(h.pid))
            if (p.contains(QStringLiteral("(cả ổ - đọc thô)")))
                h.hint = QStringLiteral("mở THẲNG cả ổ (đọc thô) - thường là phần mềm bảo mật/EDR, sao lưu, đồng bộ hoặc tìm tệp (Everything)");
        for (const ServiceInfo& s : services.value(h.pid))
        {
            h.serviceNames << s.name;
            h.serviceLabels << (s.display.isEmpty() || s.display == s.name ? s.name : QStringLiteral("%1 (%2)").arg(s.display, s.name));
        }
        h.critical = isCriticalProcessName(h.name) || processIsCritical(h.pid) ||
                     // svchost không có dịch vụ dừng được: KHÔNG bao giờ diệt cả tiến trình chủ dịch vụ
                     (h.name.compare(QLatin1String("svchost.exe"), Qt::CaseInsensitive) == 0 && h.serviceNames.isEmpty());
        if (h.critical && isCriticalProcessName(h.name) &&
            (h.name.startsWith(QLatin1String("vm"), Qt::CaseInsensitive)))
            h.hint = QStringLiteral("máy ảo Hyper-V đang chạy - tắt máy ảo đó (Hyper-V Manager) rồi thử lại");
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
    Q_UNUSED(deep);
    Q_UNUSED(progress);
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
                                : QStringLiteral("%1 là thành phần hệ thống của Windows - không được dừng từ đây.").arg(holder.name));
#ifdef Q_OS_WIN
    if (holder.kind == HolderKind::Wsl)
    {
        QProcess p;
        p.start(AppPaths::systemExecutable("wsl.exe"), {"--shutdown"});
        if (!p.waitForStarted(10000))
            return fail(QStringLiteral("Không chạy được wsl.exe: %1").arg(p.errorString()));
        if (!p.waitForFinished(90000))
        {
            p.kill();
            return fail(QStringLiteral("wsl --shutdown chưa xong sau 90 giây."));
        }
        if (p.exitCode() != 0)
            return fail(QStringLiteral("wsl --shutdown báo lỗi (mã %1): %2")
                            .arg(p.exitCode())
                            .arg(QString::fromLocal8Bit(p.readAllStandardError() + p.readAllStandardOutput()).trimmed()));
        return true;
    }
    if (holder.kind == HolderKind::VirtualDisk)
    {
        if (holder.diskImagePath.isEmpty())
            return fail(QStringLiteral("Không có đường dẫn tệp ổ ảo."));
        bool ok = false;
        QString err;
        PowerShellRunner::run(QStringLiteral("Dismount-DiskImage -ImagePath %1 -ErrorAction Stop | Out-Null")
                                  .arg(PowerShellRunner::quoteLiteral(holder.diskImagePath)),
                              &ok, &err, 60000);
        if (!ok)
            return fail(QStringLiteral("Không tháo được ổ ảo %1 (nếu là ổ của máy ảo Hyper-V/Docker, hãy tắt máy ảo đó): %2")
                            .arg(holder.diskImagePath, err));
        return true;
    }
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
