#include "PartitionMover.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "ProcessMonitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <algorithm>
#include <cstddef>
#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#ifndef FSCTL_IS_VOLUME_DIRTY
#define FSCTL_IS_VOLUME_DIRTY CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 78, METHOD_BUFFERED, FILE_ANY_ACCESS)
#endif
#ifndef VOLUME_IS_DIRTY
#define VOLUME_IS_DIRTY 0x00000001
#endif
#ifndef IOCTL_VOLUME_OFFLINE
#define IOCTL_VOLUME_OFFLINE CTL_CODE(IOCTL_VOLUME_BASE, 3, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif
#ifndef IOCTL_VOLUME_ONLINE
#define IOCTL_VOLUME_ONLINE CTL_CODE(IOCTL_VOLUME_BASE, 2, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#endif

namespace PartitionMover
{
using PartitionMove::JournalState;
using PartitionMove::kMiB;

namespace
{
QString winError(DWORD code)
{
    wchar_t* msg = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                   reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    QString text = n ? QString::fromWCharArray(msg, static_cast<int>(n)).trimmed() : QString();
    if (msg)
        LocalFree(msg);
    return QStringLiteral("%1 (mã %2)").arg(text.isEmpty() ? QStringLiteral("lỗi Windows") : text).arg(code);
}

QString guidToString(const GUID& g)
{
    return QUuid(g).toString(QUuid::WithBraces).toUpper();
}

class AutoHandle
{
public:
    AutoHandle() = default;
    explicit AutoHandle(HANDLE h) : m_h(h) {}
    ~AutoHandle() { reset(); }
    AutoHandle(const AutoHandle&) = delete;
    AutoHandle& operator=(const AutoHandle&) = delete;
    void reset(HANDLE h = INVALID_HANDLE_VALUE)
    {
        if (m_h != INVALID_HANDLE_VALUE && m_h != nullptr)
            CloseHandle(m_h);
        m_h = h;
    }
    HANDLE get() const { return m_h; }
    bool valid() const { return m_h != INVALID_HANDLE_VALUE && m_h != nullptr; }

private:
    HANDLE m_h{INVALID_HANDLE_VALUE};
};

bool ioctl(HANDLE h, DWORD code, void* in, DWORD inSize, void* out, DWORD outSize, DWORD* returned = nullptr)
{
    DWORD got = 0;
    const BOOL ok = DeviceIoControl(h, code, in, inSize, out, outSize, &got, nullptr);
    if (returned)
        *returned = got;
    return ok != 0;
}

/// Đĩa vật lý mở để đọc/ghi thô (không đệm). offset/length là bội sector, bộ đệm căn lề 4096 (xem PartitionMoveCore).
class PhysicalDisk : public PartitionMove::IBlockDevice
{
public:
    bool open(int diskNumber, QString* error)
    {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(diskNumber);
        m_h.reset(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, nullptr));
        if (!m_h.valid())
        {
            if (error)
                *error = QStringLiteral("Không mở được đĩa %1 để ghi: %2").arg(diskNumber).arg(winError(GetLastError()));
            return false;
        }
        DISK_GEOMETRY_EX geo{};
        if (!ioctl(m_h.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, &geo, sizeof(geo)))
        {
            if (error)
                *error = QStringLiteral("Không đọc được thông số đĩa: %1").arg(winError(GetLastError()));
            return false;
        }
        m_sector = geo.Geometry.BytesPerSector;
        m_size = geo.DiskSize.QuadPart;
        if (m_sector < 512 || m_size <= 0)
        {
            if (error)
                *error = QStringLiteral("Thông số đĩa không hợp lệ.");
            return false;
        }
        return true;
    }
    HANDLE handle() const { return m_h.get(); }
    qint64 sizeBytes() const { return m_size; }

    qint64 sectorSize() const override { return m_sector; }
    bool read(qint64 offset, char* buffer, qint64 length, QString* error) override { return transfer(false, offset, const_cast<char*>(buffer), length, error); }
    bool write(qint64 offset, const char* buffer, qint64 length, QString* error) override { return transfer(true, offset, const_cast<char*>(buffer), length, error); }
    bool flush(QString* error) override
    {
        if (!FlushFileBuffers(m_h.get()))
        {
            if (error)
                *error = winError(GetLastError());
            return false;
        }
        return true;
    }

private:
    bool transfer(bool writing, qint64 offset, char* buffer, qint64 length, QString* error)
    {
        constexpr qint64 kMaxRequest = 4 * kMiB; // chia nhỏ: một số trình điều khiển từ chối yêu cầu quá lớn
        while (length > 0)
        {
            const DWORD n = static_cast<DWORD>(std::min(length, kMaxRequest));
            LARGE_INTEGER li;
            li.QuadPart = offset;
            DWORD done = 0;
            const BOOL ok = SetFilePointerEx(m_h.get(), li, nullptr, FILE_BEGIN) &&
                            (writing ? WriteFile(m_h.get(), buffer, n, &done, nullptr) : ReadFile(m_h.get(), buffer, n, &done, nullptr));
            if (!ok || done != n)
            {
                if (error)
                    *error = QStringLiteral("%1 %2 byte tại %3: %4")
                                 .arg(writing ? "ghi" : "đọc")
                                 .arg(n)
                                 .arg(offset)
                                 .arg(ok ? QStringLiteral("chỉ xong %1 byte").arg(done) : winError(GetLastError()));
                return false;
            }
            offset += n;
            buffer += n;
            length -= n;
        }
        return true;
    }

    AutoHandle m_h;
    qint64 m_sector{512};
    qint64 m_size{0};
};

bool readLayoutRaw(HANDLE disk, std::vector<char>* raw, QString* error)
{
    for (DWORD size = 8192; size <= (1u << 20); size *= 2)
    {
        raw->assign(size, 0);
        DWORD got = 0;
        if (ioctl(disk, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, raw->data(), size, &got))
        {
            raw->resize(got);
            return true;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            break;
    }
    if (error)
        *error = QStringLiteral("Không đọc được bảng phân vùng của đĩa: %1").arg(winError(GetLastError()));
    return false;
}

/// Tìm ổ (volume) nằm đúng tại [start, start+length) của đĩa. Rỗng nếu không có. Trả về dạng "\\?\Volume{GUID}" (KHÔNG có "\" cuối).
QString findVolume(int diskNumber, qint64 start, qint64 length)
{
    wchar_t name[MAX_PATH + 1] = {};
    const HANDLE find = FindFirstVolumeW(name, MAX_PATH);
    if (find == INVALID_HANDLE_VALUE)
        return {};
    QString found;
    do
    {
        std::wstring path(name);
        while (!path.empty() && path.back() == L'\\')
            path.pop_back();
        AutoHandle h(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h.valid())
            continue;
        std::vector<char> buf(sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT), 0);
        DWORD got = 0;
        if (!ioctl(h.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf.data(), static_cast<DWORD>(buf.size()), &got))
            continue;
        const auto* ex = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf.data());
        if (ex->NumberOfDiskExtents == 1 && static_cast<int>(ex->Extents[0].DiskNumber) == diskNumber && ex->Extents[0].StartingOffset.QuadPart == start &&
            ex->Extents[0].ExtentLength.QuadPart == length)
        {
            found = QString::fromStdWString(path);
            break;
        }
    } while (FindNextVolumeW(find, name, MAX_PATH));
    FindVolumeClose(find);
    return found;
}

/// Ổ đã khóa + tháo: giữ tay cầm mở cho tới khi hủy đối tượng (khi đó mở khóa) - nên không chương trình nào gắn lại ổ trong lúc đang ghi.
class LockedVolume
{
public:
    bool lockAndDismount(const QString& volumePath, QString* error, const QString& driveLetter = QString(), bool dismount = true)
    {
        const std::wstring path = volumePath.toStdWString();
        m_h.reset(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr));
        if (!m_h.valid())
        {
            if (error)
                *error = QStringLiteral("Không mở được ổ để khóa: %1").arg(winError(GetLastError()));
            return false;
        }
        bool locked = false;
        for (int attempt = 0; attempt < 20 && !locked; ++attempt)
        {
            locked = ioctl(m_h.get(), FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0);
            if (!locked)
                Sleep(500);
        }
        if (!locked)
        {
            const DWORD code = GetLastError();
            if (error)
            {
                QString msg = QStringLiteral("Không khóa được ổ%1 - đang có chương trình giữ ổ này (cửa sổ Explorer/Terminal/VS Code đang mở ở ổ, chương trình đang chạy từ ổ, "
                                             "tệp đang mở, pagefile, phần mềm quét/đồng bộ...). Hãy đóng chúng rồi thử lại. (%2)")
                                  .arg(driveLetter.isEmpty() ? QString() : (" " + driveLetter.left(1).toUpper() + ":"), winError(code));
                const QStringList procs = processesRunningFromDrive(driveLetter);
                if (!procs.isEmpty())
                    msg += QStringLiteral("\nChương trình đang chạy từ ổ %1: %2").arg(driveLetter.left(1).toUpper(), procs.mid(0, 12).join(", ")) +
                           (procs.size() > 12 ? QStringLiteral(" (và %1 chương trình khác)").arg(procs.size() - 12) : QString());
                *error = msg;
            }
            m_h.reset();
            return false;
        }
        m_locked = true;
        if (!dismount)
            return true; // chỉ thử khóa (preflight): giữ khóa tới khi release()
        FlushFileBuffers(m_h.get());
        if (!ioctl(m_h.get(), FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0))
        {
            if (error)
                *error = QStringLiteral("Không tháo (dismount) được ổ: %1").arg(winError(GetLastError()));
            release();
            return false;
        }
        return true;
    }
    void release()
    {
        if (m_h.valid() && m_locked)
            ioctl(m_h.get(), FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0);
        m_locked = false;
        m_h.reset();
    }
    ~LockedVolume() { release(); }

private:
    AutoHandle m_h;
    bool m_locked{false};
};

/// true nếu cờ "bẩn" của ổ đang bật (cần chkdsk) - dữ liệu có thể không nhất quán, không nên dời.
bool volumeIsDirty(const QString& volumePath)
{
    AutoHandle h(CreateFileW(volumePath.toStdWString().c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!h.valid())
        return false;
    DWORD flags = 0;
    return ioctl(h.get(), FSCTL_IS_VOLUME_DIRTY, nullptr, 0, &flags, sizeof(flags)) && (flags & VOLUME_IS_DIRTY) != 0;
}

/// Ghi nhật ký NGUYÊN TỬ + BỀN: tệp tạm -> flush -> đổi tên đè. Mất điện giữa chừng thì còn nguyên bản cũ hoặc bản mới, không bao giờ nửa vời.
class FileJournal : public PartitionMove::IJournalStore
{
public:
    bool save(const JournalState& state, QString* error) override
    {
        const QString path = journalPath();
        const QString tmp = path + ".tmp";
        const QByteArray data = state.toJson();
        const HANDLE h = CreateFileW(QDir::toNativeSeparators(tmp).toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
        if (h == INVALID_HANDLE_VALUE)
        {
            if (error)
                *error = QStringLiteral("không tạo được tệp nhật ký: %1").arg(winError(GetLastError()));
            return false;
        }
        DWORD written = 0;
        const bool ok = WriteFile(h, data.constData(), static_cast<DWORD>(data.size()), &written, nullptr) && written == static_cast<DWORD>(data.size()) && FlushFileBuffers(h);
        const DWORD err = GetLastError();
        CloseHandle(h);
        if (!ok)
        {
            if (error)
                *error = QStringLiteral("không ghi được nhật ký: %1").arg(winError(err));
            return false;
        }
        if (!MoveFileExW(QDir::toNativeSeparators(tmp).toStdWString().c_str(), QDir::toNativeSeparators(path).toStdWString().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            if (error)
                *error = QStringLiteral("không thay được tệp nhật ký: %1").arg(winError(GetLastError()));
            return false;
        }
        return true;
    }
};

/// Chờ Windows gắn ổ mới ở vị trí mới (partmgr tạo lại thiết bị phân vùng sau khi đổi bảng).
QString waitForVolume(int diskNumber, qint64 start, qint64 length, int timeoutMs)
{
    for (int waited = 0; waited <= timeoutMs; waited += 500)
    {
        const QString v = findVolume(diskNumber, start, length);
        if (!v.isEmpty())
            return v;
        Sleep(500);
    }
    return {};
}

/// Đọc được hệ thống tệp NTFS + liệt kê thư mục gốc của ổ -> Windows đã gắn lại và đọc được dữ liệu.
bool volumeMountsAsNtfs(const QString& volumePath, QString* detail)
{
    const std::wstring root = volumePath.toStdWString() + L"\\";
    wchar_t fs[MAX_PATH + 1] = {};
    if (!GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, nullptr, fs, MAX_PATH))
    {
        if (detail)
            *detail = QStringLiteral("GetVolumeInformation: %1").arg(winError(GetLastError()));
        return false;
    }
    if (std::wstring(fs) != L"NTFS")
    {
        if (detail)
            *detail = QStringLiteral("hệ thống tệp đọc được là \"%1\", không phải NTFS").arg(QString::fromWCharArray(fs));
        return false;
    }
    WIN32_FIND_DATAW fd;
    const HANDLE f = FindFirstFileExW((root + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
    if (f == INVALID_HANDLE_VALUE)
    {
        if (detail)
            *detail = QStringLiteral("không liệt kê được thư mục gốc: %1").arg(winError(GetLastError()));
        return false;
    }
    FindClose(f);
    return true;
}

/// Gán lại chữ ổ nếu Windows làm mất (đĩa MBR định danh ổ theo vị trí nên chữ ổ cũ không đi theo).
void ensureDriveLetter(const QString& volumePath, const QString& letter)
{
    if (letter.isEmpty())
        return;
    const std::wstring vol = volumePath.toStdWString() + L"\\";
    wchar_t names[1024] = {};
    DWORD len = 0;
    if (GetVolumePathNamesForVolumeNameW(vol.c_str(), names, 1024, &len) && names[0] != L'\0')
        return; // đã có điểm gắn (thường là chữ ổ cũ)
    const std::wstring mount = letter.left(1).toUpper().toStdWString() + L":\\";
    SetVolumeMountPointW(mount.c_str(), vol.c_str());
}

QString letterOfPath(const QString& path)
{
    const QString n = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    return n.size() >= 2 && n[1] == QLatin1Char(':') ? n.left(1).toUpper() : QString();
}
} // namespace

namespace
{
using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);

/// Thư mục làm việc hiện tại của một tiến trình x64 (đọc từ PEB: ProcessParameters @0x20, CurrentDirectory.DosPath @0x38). Rỗng nếu không đọc được
/// (thiếu quyền, tiến trình 32 bit, tiến trình hệ thống...). Chỉ ĐỌC bộ nhớ tiến trình khác.
QString processCurrentDirectory(HANDLE process, NtQueryInformationProcessFn query)
{
#ifdef _WIN64
    struct BasicInfo
    {
        PVOID exitStatus;
        PVOID pebBase;
        PVOID affinity;
        PVOID basePriority;
        PVOID pid;
        PVOID parentPid;
    } info{};
    if (query(process, 0 /*ProcessBasicInformation*/, &info, sizeof(info), nullptr) != 0 || !info.pebBase)
        return {};
    PVOID params = nullptr;
    if (!ReadProcessMemory(process, static_cast<char*>(info.pebBase) + 0x20, &params, sizeof(params), nullptr) || !params)
        return {};
    struct UnicodeString
    {
        USHORT length;
        USHORT maximum;
        ULONG pad;
        PWSTR buffer;
    } cur{};
    if (!ReadProcessMemory(process, static_cast<char*>(params) + 0x38, &cur, sizeof(cur), nullptr) || !cur.buffer || cur.length == 0 || cur.length > 2 * 1024)
        return {};
    std::wstring path(cur.length / sizeof(wchar_t), L'\0');
    if (!ReadProcessMemory(process, cur.buffer, path.data(), cur.length, nullptr))
        return {};
    return QString::fromStdWString(path);
#else
    Q_UNUSED(process);
    Q_UNUSED(query);
    return {};
#endif
}
} // namespace

QStringList processesRunningFromDrive(const QString& driveLetter)
{
    QStringList out;
    if (driveLetter.isEmpty())
        return out;
    const QString prefix = driveLetter.left(1).toUpper() + QStringLiteral(":\\");
    ProcessSampler sampler;
    const SystemSnapshot snap = sampler.sample();
    const quint32 self = GetCurrentProcessId();
    const auto query = reinterpret_cast<NtQueryInformationProcessFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    for (const ProcessInfo& p : snap.processes)
    {
        // 1) chạy TỪ ổ (tệp exe nằm trên ổ)
        if (!p.path.isEmpty() && QDir::toNativeSeparators(p.path).startsWith(prefix, Qt::CaseInsensitive))
        {
            const QString label = p.pid == self ? QStringLiteral("%1 (CHÍNH ỨNG DỤNG NÀY)").arg(p.name) : p.name;
            if (!out.contains(label))
                out << label;
            continue;
        }
        // 2) đang ĐỨNG ở một thư mục trên ổ (terminal, VS Code, trình soạn thảo...) - giữ tay cầm thư mục nên cũng chặn khóa ổ
        if (query && p.pid != 0 && p.pid != 4 && p.pid != self)
        {
            const HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, p.pid);
            if (h)
            {
                const QString cwd = processCurrentDirectory(h, query);
                CloseHandle(h);
                if (QDir::toNativeSeparators(cwd).startsWith(prefix, Qt::CaseInsensitive))
                {
                    const QString label = QStringLiteral("%1 (thư mục làm việc: %2)").arg(p.name, QDir::toNativeSeparators(cwd));
                    if (!out.contains(label))
                        out << label;
                }
            }
        }
    }
    return out;
}

QString runningFromDriveReason(const QString& driveLetter)
{
    if (driveLetter.isEmpty())
        return {};
    const QString letter = driveLetter.left(1).toUpper();
    const QString appDir = QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
    if (letterOfPath(appDir) == letter)
        return QStringLiteral("ứng dụng One for ALL đang chạy từ chính ổ %1: (%2) - tháo ổ này sẽ làm ứng dụng sập và ổ cũng không khóa được. "
                              "Hãy chép thư mục ứng dụng sang ổ khác (ví dụ C:\\OneForAll) rồi chạy từ đó")
            .arg(letter, appDir);
    if (letterOfPath(AppPaths::dataDir()) == letter)
        return QStringLiteral("thư mục dữ liệu của ứng dụng (chứa nhật ký phục hồi) nằm trên chính ổ %1:").arg(letter);
    return {};
}

QString preflightMove(int diskNumber, int partitionNumber, const QString& driveLetter)
{
    if (!PartitionManager::isElevated())
        return QStringLiteral("Cần quyền Administrator để dời phân vùng.");
    if (const QString why = runningFromDriveReason(driveLetter); !why.isEmpty())
        return QStringLiteral("Không dời được: ") + why + QStringLiteral(".");
    // Thư mục làm việc nằm trên ổ cũng giữ ổ (tay cầm thư mục) - ứng dụng tự chuyển sang thư mục tạm (an toàn, chỉ ảnh hưởng tiến trình này).
    if (!driveLetter.isEmpty() && letterOfPath(QDir::currentPath()) == driveLetter.left(1).toUpper())
        QDir::setCurrent(QDir::tempPath());

    PhysicalDisk disk;
    QString err;
    if (!disk.open(diskNumber, &err))
        return err;
    std::vector<char> raw;
    if (!readLayoutRaw(disk.handle(), &raw, &err))
        return err;
    qint64 start = 0, length = 0;
    if (!internal::findEntry(raw, partitionNumber, &start, &length, nullptr))
        return QStringLiteral("Không thấy phân vùng số %1 trên đĩa %2.").arg(partitionNumber).arg(diskNumber);
    const QString volumePath = findVolume(diskNumber, start, length);
    if (volumePath.isEmpty())
        return QStringLiteral("Không tìm thấy ổ (volume) tương ứng với phân vùng này - phân vùng chưa gắn hay không có hệ thống tệp.");
    if (volumeIsDirty(volumePath))
        return QStringLiteral("Ổ đang ở trạng thái \"bẩn\" (chưa đóng đúng cách). Hãy chạy chkdsk %1: /f rồi thử lại.")
            .arg(driveLetter.isEmpty() ? QStringLiteral("<ổ>") : driveLetter.left(1));
    // Thử khóa rồi mở khóa ngay (KHÔNG tháo ổ, không ghi gì): nếu không khóa được thì dừng TRƯỚC khi đổi bất cứ thứ gì.
    LockedVolume probe;
    if (!probe.lockAndDismount(volumePath, &err, driveLetter, /*dismount=*/false))
        return err;
    probe.release();
    return {};
}

// ------------------------------------------------------------------------------------------------ nhật ký

QString journalPath()
{
    return QDir(AppPaths::dataDir()).filePath("partition_move_journal.json");
}

bool loadJournal(JournalState* out, QString* error)
{
    if (error)
        error->clear();
    QFile f(journalPath());
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = QStringLiteral("Không mở được tệp nhật ký: ") + f.errorString();
        return false;
    }
    QString why;
    if (!JournalState::fromJson(f.readAll(), out, &why))
    {
        if (error)
            *error = why;
        return false;
    }
    return true;
}

void discardJournal()
{
    QFile::remove(journalPath());
    QFile::remove(journalPath() + ".tmp");
}

QString describeJournal(const JournalState& s)
{
    const double gb = 1024.0 * 1024.0 * 1024.0;
    const int pct = s.length > 0 ? static_cast<int>(100.0 * static_cast<double>(s.doneBytes) / static_cast<double>(s.length)) : 0;
    QString phase;
    switch (s.phase)
    {
        case JournalState::Copying: phase = QStringLiteral("đang chép dữ liệu (%1%)").arg(pct); break;
        case JournalState::Copied: phase = QStringLiteral("đã chép xong, chưa đổi bảng phân vùng"); break;
        case JournalState::Patched: phase = QStringLiteral("đã vá boot sector, chưa đổi bảng phân vùng"); break;
        default: phase = QStringLiteral("đã đổi bảng phân vùng, chưa kiểm tra lại"); break;
    }
    return QStringLiteral("Đĩa %1, phân vùng %2: dời từ %3 GB tới %4 GB (%5 GB dữ liệu) - %6.")
        .arg(s.diskNumber)
        .arg(s.partitionNumber)
        .arg(QString::number(static_cast<double>(s.srcOffset) / gb, 'f', 2), QString::number(static_cast<double>(s.dstOffset) / gb, 'f', 2),
             QString::number(static_cast<double>(s.length) / gb, 'f', 2), phase);
}

bool canAbandon(const JournalState& s)
{
    return !s.overlapping() || (s.phase == JournalState::Copying && s.doneBytes == 0);
}

QString notMovableReason(const PartitionManager::PartitionInfo& p)
{
    if (p.offsetBytes < 0 || p.sizeBytes <= 0)
        return QStringLiteral("không biết vị trí của phân vùng trên đĩa");
    if (p.type.compare("Basic", Qt::CaseInsensitive) != 0 && p.type.compare("IFS", Qt::CaseInsensitive) != 0)
        return QStringLiteral("loại phân vùng \"%1\" không được hỗ trợ (chỉ dời được phân vùng dữ liệu thường)").arg(p.type);
    if (p.fileSystem.compare("NTFS", Qt::CaseInsensitive) != 0)
        return QStringLiteral("chỉ hỗ trợ hệ thống tệp NTFS (phân vùng này: %1)").arg(p.hasFileSystem() ? p.fileSystem : QStringLiteral("không có/không nhận ra"));
    if (p.isBoot || p.isSystem || p.isActive)
        return QStringLiteral("đây là phân vùng khởi động/hệ thống - dời nó có thể làm máy không khởi động được");
    const QString sysDrive = qEnvironmentVariable("SystemDrive").left(1).toUpper();
    if (!p.driveLetter.isEmpty() && !sysDrive.isEmpty() && p.driveLetter.left(1).toUpper() == sysDrive)
        return QStringLiteral("đây là ổ chứa Windows (%1:)").arg(sysDrive);
    return {};
}

// ------------------------------------------------------------------------------------------------ lõi thuần của bảng phân vùng

namespace internal
{
namespace
{
const DRIVE_LAYOUT_INFORMATION_EX* asLayout(const std::vector<char>& raw)
{
    if (raw.size() < offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry))
        return nullptr;
    const auto* l = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX*>(raw.data());
    const size_t need = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + static_cast<size_t>(l->PartitionCount) * sizeof(PARTITION_INFORMATION_EX);
    if (l->PartitionCount > 256 || raw.size() < need)
        return nullptr;
    return l;
}
} // namespace

size_t layoutByteSize(const std::vector<char>& raw)
{
    const auto* l = asLayout(raw);
    return l ? offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry) + static_cast<size_t>(l->PartitionCount) * sizeof(PARTITION_INFORMATION_EX) : 0;
}

LayoutView inspectLayout(const std::vector<char>& raw)
{
    LayoutView v;
    const auto* l = asLayout(raw);
    if (!l)
        return v;
    v.count = static_cast<int>(l->PartitionCount);
    if (l->PartitionStyle == PARTITION_STYLE_GPT)
    {
        v.gpt = true;
        v.usableStart = l->Gpt.StartingUsableOffset.QuadPart;
        v.usableLength = l->Gpt.UsableLength.QuadPart;
        v.diskId = guidToString(l->Gpt.DiskId);
        v.ok = true;
    }
    else if (l->PartitionStyle == PARTITION_STYLE_MBR)
    {
        v.gpt = false;
        v.diskId = QStringLiteral("MBR:%1").arg(l->Mbr.Signature, 8, 16, QLatin1Char('0')).toUpper();
        v.ok = true;
    }
    return v;
}

bool findEntry(const std::vector<char>& raw, int partitionNumber, qint64* start, qint64* length, QString* partitionGuid)
{
    const auto* l = asLayout(raw);
    if (!l)
        return false;
    for (DWORD i = 0; i < l->PartitionCount; ++i)
    {
        const PARTITION_INFORMATION_EX& e = l->PartitionEntry[i];
        if (static_cast<int>(e.PartitionNumber) != partitionNumber || e.PartitionLength.QuadPart <= 0)
            continue;
        if (start)
            *start = e.StartingOffset.QuadPart;
        if (length)
            *length = e.PartitionLength.QuadPart;
        if (partitionGuid)
            *partitionGuid = e.PartitionStyle == PARTITION_STYLE_GPT ? guidToString(e.Gpt.PartitionId) : QString();
        return true;
    }
    return false;
}

QString checkDestination(const std::vector<char>& raw, int partitionNumber, qint64 newStart, qint64 length, qint64 diskSizeBytes)
{
    const auto* l = asLayout(raw);
    if (!l)
        return QStringLiteral("không đọc được bảng phân vùng");
    const LayoutView v = inspectLayout(raw);
    if (!v.ok)
        return QStringLiteral("đĩa không phải GPT/MBR (có thể là đĩa động hoặc chưa khởi tạo)");
    const qint64 newEnd = newStart + length;
    if (newStart < kMiB)
        return QStringLiteral("vị trí đích quá sát đầu đĩa");
    if (v.gpt)
    {
        if (newStart < v.usableStart || newEnd > v.usableStart + v.usableLength)
            return QStringLiteral("vị trí đích nằm ngoài vùng dùng được của bảng GPT");
    }
    else
    {
        if (diskSizeBytes > 0 && newEnd > diskSizeBytes)
            return QStringLiteral("vị trí đích vượt cuối đĩa");
        if (newEnd / 512 > 0xFFFFFFFFLL)
            return QStringLiteral("đĩa MBR không địa chỉ hóa được vị trí quá 2 TiB");
    }
    bool found = false;
    for (DWORD i = 0; i < l->PartitionCount; ++i)
    {
        const PARTITION_INFORMATION_EX& e = l->PartitionEntry[i];
        if (e.PartitionLength.QuadPart <= 0)
            continue; // mục trống (MBR luôn trả đủ 4 mục)
        if (static_cast<int>(e.PartitionNumber) == partitionNumber)
        {
            found = true;
            continue;
        }
        const qint64 s = e.StartingOffset.QuadPart, t = s + e.PartitionLength.QuadPart;
        if (newStart < t && newEnd > s)
            return QStringLiteral("vị trí đích chồng lên phân vùng số %1").arg(e.PartitionNumber);
    }
    return found ? QString() : QStringLiteral("không thấy phân vùng số %1 trong bảng").arg(partitionNumber);
}

bool patchStart(std::vector<char>* raw, int partitionNumber, qint64 newStart, qint64 sectorBytes, QString* error)
{
    if (!raw || asLayout(*raw) == nullptr)
    {
        if (error)
            *error = QStringLiteral("bộ đệm bảng phân vùng không hợp lệ");
        return false;
    }
    auto* l = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX*>(raw->data());
    for (DWORD i = 0; i < l->PartitionCount; ++i)
    {
        PARTITION_INFORMATION_EX& e = l->PartitionEntry[i];
        if (static_cast<int>(e.PartitionNumber) != partitionNumber || e.PartitionLength.QuadPart <= 0)
            continue;
        e.StartingOffset.QuadPart = newStart;
        e.RewritePartition = TRUE;
        if (e.PartitionStyle == PARTITION_STYLE_MBR && sectorBytes > 0)
            e.Mbr.HiddenSectors = static_cast<DWORD>(newStart / sectorBytes);
        return true;
    }
    if (error)
        *error = QStringLiteral("không tìm thấy phân vùng số %1 để sửa").arg(partitionNumber);
    return false;
}
} // namespace internal

// ------------------------------------------------------------------------------------------------ luồng dời

MoveWorker::MoveWorker(QObject* parent)
    : QThread(parent)
{
}

void MoveWorker::setRequest(const MoveRequest& request)
{
    m_request = request;
    m_resume = false;
}

void MoveWorker::setResume(const JournalState& journal, const QString& driveLetter)
{
    m_journal = journal;
    m_resumeLetter = driveLetter;
    m_resume = true;
}

void MoveWorker::run()
{
    m_pause = false;
    const auto finish = [this](bool ok, bool resumable, const QString& message) {
        Logger::instance().info("PartitionMover", QStringLiteral("Kết thúc: ok=%1 resumable=%2 - %3").arg(ok).arg(resumable).arg(message));
        emit moveFinished(ok, resumable, message);
    };
    const auto say = [this](const QString& phase, qint64 done = 0, qint64 total = 0) { emit progress(done, total, phase); };

    if (!PartitionManager::isElevated())
    {
        finish(false, false, QStringLiteral("Cần quyền Administrator để dời phân vùng."));
        return;
    }

    JournalState st;
    MoveRequest req = m_request;
    QString letter = req.driveLetter;
    const bool resuming = m_resume;
    if (resuming)
    {
        st = m_journal;
        req.diskNumber = st.diskNumber;
        req.partitionNumber = st.partitionNumber;
        letter = m_resumeLetter;
    }
    QString err;

    say(QStringLiteral("Kiểm tra điều kiện trước khi ghi..."));
    PhysicalDisk disk;
    if (!disk.open(req.diskNumber, &err))
    {
        finish(false, resuming, err);
        return;
    }
    std::vector<char> raw;
    if (!readLayoutRaw(disk.handle(), &raw, &err))
    {
        finish(false, resuming, err);
        return;
    }
    const internal::LayoutView view = internal::inspectLayout(raw);
    if (!view.ok)
    {
        finish(false, resuming, QStringLiteral("Đĩa %1 không phải GPT/MBR (đĩa động/chưa khởi tạo) - không hỗ trợ.").arg(req.diskNumber));
        return;
    }
    qint64 curStart = 0, curLen = 0;
    QString curGuid;
    if (!internal::findEntry(raw, req.partitionNumber, &curStart, &curLen, &curGuid))
    {
        finish(false, resuming, QStringLiteral("Không thấy phân vùng số %1 trên đĩa %2.").arg(req.partitionNumber).arg(req.diskNumber));
        return;
    }
    if (!ioctl(disk.handle(), IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0) && GetLastError() == ERROR_WRITE_PROTECT)
    {
        finish(false, resuming, QStringLiteral("Đĩa đang ở chế độ chỉ đọc."));
        return;
    }

    bool tableAlreadyMoved = false;
    if (resuming)
    {
        if (!st.diskId.isEmpty() && st.diskId != view.diskId)
        {
            finish(false, true, QStringLiteral("Đĩa %1 hiện có định danh khác với đĩa trong nhật ký (%2 ≠ %3) - có thể đã đổi đĩa. KHÔNG tiếp tục.")
                                    .arg(req.diskNumber).arg(view.diskId, st.diskId));
            return;
        }
        if (curStart == st.dstOffset && curLen == st.length)
            tableAlreadyMoved = true; // bảng đã trỏ tới chỗ mới: chỉ còn việc kiểm tra lại
        else if (!(curStart == st.srcOffset && curLen == st.length))
        {
            finish(false, true, QStringLiteral("Phân vùng số %1 hiện ở vị trí %2 (dài %3), không khớp nhật ký (cũ %4 / mới %5, dài %6). "
                                               "Nhật ký được GIỮ NGUYÊN - đừng định dạng ổ này; cần kiểm tra thủ công.")
                                    .arg(req.partitionNumber).arg(curStart).arg(curLen).arg(st.srcOffset).arg(st.dstOffset).arg(st.length));
            return;
        }
    }
    else
    {
        if (curStart != req.expectedStart || curLen != req.expectedLength)
        {
            finish(false, false, QStringLiteral("Phân vùng đã đổi so với lúc bạn xác nhận (vị trí %1 → %2, độ dài %3 → %4). Hãy làm mới và thử lại.")
                                     .arg(req.expectedStart).arg(curStart).arg(req.expectedLength).arg(curLen));
            return;
        }
        if (const QString why = internal::checkDestination(raw, req.partitionNumber, req.newStart, curLen, disk.sizeBytes()); !why.isEmpty())
        {
            finish(false, false, QStringLiteral("Không dời được: ") + why + ".");
            return;
        }
        if (const QString why = PartitionMove::validateMove(curStart, req.newStart, curLen, disk.sectorSize()); !why.isEmpty())
        {
            finish(false, false, QStringLiteral("Không dời được: ") + why + ".");
            return;
        }
        // Boot sector tại vị trí cũ phải là NTFS (loại BitLocker "-FVE-FS-", ReFS, exFAT... bị từ chối - dời bản mã BitLocker làm mất dữ liệu).
        std::vector<char> sectorBuf(static_cast<size_t>(disk.sectorSize()) + 4096);
        char* aligned = sectorBuf.data() + ((4096 - (reinterpret_cast<quintptr>(sectorBuf.data()) % 4096)) % 4096);
        if (!disk.read(curStart, aligned, disk.sectorSize(), &err))
        {
            finish(false, false, QStringLiteral("Không đọc được boot sector của phân vùng: ") + err);
            return;
        }
        if (PartitionMove::readNtfsHiddenSectors(QByteArray(aligned, static_cast<int>(disk.sectorSize()))) < 0)
        {
            finish(false, false, QStringLiteral("Boot sector của phân vùng không phải NTFS (có thể là BitLocker/ReFS/exFAT) - không dời được."));
            return;
        }
        // Thư mục dữ liệu của ứng dụng (nơi đặt nhật ký) KHÔNG được nằm trên ổ sắp tháo.
        if (!req.driveLetter.isEmpty() && letterOfPath(journalPath()).compare(req.driveLetter.left(1), Qt::CaseInsensitive) == 0)
        {
            finish(false, false, QStringLiteral("Thư mục dữ liệu của ứng dụng (chứa nhật ký phục hồi) nằm trên chính ổ %1: sắp tháo - không thể dời.").arg(req.driveLetter.left(1)));
            return;
        }
        st = JournalState{};
        st.diskNumber = req.diskNumber;
        st.diskId = view.diskId;
        st.partitionId = curGuid;
        st.partitionNumber = req.partitionNumber;
        st.srcOffset = curStart;
        st.dstOffset = req.newStart;
        st.length = curLen;
        st.sectorBytes = disk.sectorSize();
        st.phase = JournalState::Copying;
        st.startedAt = QDateTime::currentDateTime().toString(Qt::ISODate);
        st.note = QStringLiteral("Dời %1: ").arg(req.driveLetter.isEmpty() ? QStringLiteral("phân vùng") : req.driveLetter);
    }

    // ---- Khóa + tháo ổ ----
    LockedVolume volume;
    if (!tableAlreadyMoved)
    {
        const QString volumePath = findVolume(req.diskNumber, st.srcOffset, st.length);
        if (volumePath.isEmpty())
        {
            if (!resuming)
            {
                finish(false, false, QStringLiteral("Không tìm thấy ổ (volume) tương ứng với phân vùng này - phân vùng chưa gắn hay không có hệ thống tệp."));
                return;
            }
            // Chạy tiếp: ổ cũ có thể đã hỏng một phần nên không còn gắn được - vẫn tiếp tục chép (không ai đang dùng nó).
        }
        else
        {
            if (!resuming && volumeIsDirty(volumePath))
            {
                finish(false, false, QStringLiteral("Ổ đang ở trạng thái \"bẩn\" (chưa đóng đúng cách). Hãy chạy chkdsk %1: /f rồi thử lại.")
                                         .arg(req.driveLetter.isEmpty() ? QStringLiteral("<ổ>") : req.driveLetter.left(1)));
                return;
            }
            say(QStringLiteral("Khóa và tháo ổ %1...").arg(req.driveLetter.isEmpty() ? QStringLiteral("(không có chữ ổ)") : req.driveLetter.left(1) + ":"));
            if (!volume.lockAndDismount(volumePath, &err, letter.isEmpty() ? req.driveLetter : letter))
            {
                finish(false, resuming, err);
                return;
            }
        }

        // ---- Chép dữ liệu ----
        FileJournal journal;
        if (!resuming)
        {
            if (!journal.save(st, &err)) // TRƯỚC khi ghi bất cứ thứ gì lên đĩa
            {
                finish(false, false, QStringLiteral("Không ghi được nhật ký phục hồi - không bắt đầu: ") + err);
                return;
            }
        }
        if (st.phase == JournalState::Copying)
        {
            say(QStringLiteral("Đang chép dữ liệu..."), st.doneBytes, st.length);
            PartitionMove::CopyOptions options;
            const PartitionMove::CopyOutcome out = PartitionMove::copyRange(disk, st, journal, options, m_pause, [&](qint64 d, qint64 t) {
                emit progress(d, t, QStringLiteral("Đang chép dữ liệu..."));
            });
            if (out.result == PartitionMove::CopyResult::Cancelled)
            {
                finish(false, true, QStringLiteral("Đã tạm dừng. Nhật ký giữ đúng chỗ dừng - bấm \"Tiếp tục\" để chạy tiếp. %1")
                                        .arg(PartitionMover::canAbandon(st) ? QString() : QStringLiteral("KHÔNG định dạng/dùng ổ này trong lúc dang dở.")));
                return;
            }
            if (out.result != PartitionMove::CopyResult::Done)
            {
                finish(false, true, QStringLiteral("Dừng vì lỗi khi chép: %1\nNhật ký còn nguyên - sửa nguyên nhân (cáp/nguồn/đĩa) rồi bấm \"Tiếp tục\". "
                                                   "KHÔNG định dạng ổ này.").arg(out.error));
                return;
            }
        }

        // ---- Vá boot sector NTFS tại vị trí mới ----
        if (st.phase <= JournalState::Copied)
        {
            say(QStringLiteral("Vá boot sector tại vị trí mới..."));
            if (!PartitionMove::patchNtfsBootSectorsAt(disk, st.dstOffset, st.length, &err))
            {
                finish(false, true, QStringLiteral("Dữ liệu đã chép xong nhưng không vá được boot sector: %1\nBấm \"Tiếp tục\" để thử lại.").arg(err));
                return;
            }
            st.phase = JournalState::Patched;
            if (!journal.save(st, &err))
            {
                finish(false, true, QStringLiteral("Không ghi được nhật ký: ") + err);
                return;
            }
        }

        // ---- Đổi bảng phân vùng ----
        say(QStringLiteral("Đổi bảng phân vùng..."));
        std::vector<char> fresh;
        if (!readLayoutRaw(disk.handle(), &fresh, &err))
        {
            finish(false, true, err);
            return;
        }
        qint64 nowStart = 0, nowLen = 0;
        if (!internal::findEntry(fresh, st.partitionNumber, &nowStart, &nowLen, nullptr) || nowStart != st.srcOffset || nowLen != st.length)
        {
            finish(false, true, QStringLiteral("Bảng phân vùng đã thay đổi trong lúc dời - dừng. Nhật ký được giữ; cần kiểm tra thủ công."));
            return;
        }
        if (const QString why = internal::checkDestination(fresh, st.partitionNumber, st.dstOffset, st.length, disk.sizeBytes()); !why.isEmpty())
        {
            finish(false, true, QStringLiteral("Vị trí đích không còn hợp lệ: ") + why);
            return;
        }
        if (!internal::patchStart(&fresh, st.partitionNumber, st.dstOffset, disk.sectorSize(), &err))
        {
            finish(false, true, err);
            return;
        }
        // Tháo trạng thái "ổ ngoại tuyến" nếu có (chưa dùng) - giữ khóa cho tới sau khi đổi bảng để không ai gắn ổ ở vị trí cũ nửa vời.
        const size_t bytes = internal::layoutByteSize(fresh);
        if (!ioctl(disk.handle(), IOCTL_DISK_SET_DRIVE_LAYOUT_EX, fresh.data(), static_cast<DWORD>(bytes), nullptr, 0))
        {
            finish(false, true, QStringLiteral("Dữ liệu đã chép xong nhưng Windows từ chối đổi bảng phân vùng: %1\nBấm \"Tiếp tục\" để thử lại "
                                               "(KHÔNG định dạng ổ này).").arg(winError(GetLastError())));
            return;
        }
        st.phase = JournalState::TableDone;
        journal.save(st, &err); // nếu lỡ không ghi được: lần chạy tiếp thấy bảng đã ở chỗ mới nên vẫn đúng
        ioctl(disk.handle(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0);
        volume.release();
    }

    // ---- Chờ Windows gắn ổ mới + kiểm tra ----
    say(QStringLiteral("Chờ Windows gắn lại ổ ở vị trí mới..."));
    ioctl(disk.handle(), IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0);
    const QString newVolume = waitForVolume(req.diskNumber, st.dstOffset, st.length, 60000);
    if (newVolume.isEmpty())
    {
        finish(false, true, QStringLiteral("Đã đổi bảng phân vùng nhưng Windows chưa gắn ổ mới sau 60 giây. Thử bấm \"Làm mới\" hoặc rút/cắm lại đĩa; "
                                           "nếu vẫn không thấy, bấm \"Tiếp tục\" để kiểm tra lại. Nhật ký được giữ."));
        return;
    }
    QString detail;
    if (!volumeMountsAsNtfs(newVolume, &detail))
    {
        finish(false, true, QStringLiteral("Ổ ở vị trí mới chưa đọc được (%1). Nhật ký được giữ - KHÔNG định dạng. Thử \"Tiếp tục\" sau vài giây; "
                                           "nếu vẫn lỗi, chạy chkdsk trên ổ này.").arg(detail));
        return;
    }
    ensureDriveLetter(newVolume, letter);
    discardJournal();
    finish(true, false, QStringLiteral("Đã dời xong và kiểm tra: ổ ở vị trí mới gắn được và đọc được thư mục gốc."));
}

// ------------------------------------------------------------------------------------------------ cả kế hoạch

PlanWorker::PlanWorker(QObject* parent)
    : QThread(parent)
{
}

void PlanWorker::setPlan(int diskNumber, int partitionNumber, const QList<DiskLayout::PlanStep>& steps, const QString& driveLetter)
{
    m_disk = diskNumber;
    m_partition = partitionNumber;
    m_steps = steps;
    m_letter = driveLetter;
}

void PlanWorker::requestPause()
{
    m_pause = true;
    QMutexLocker lock(&m_mutex);
    if (m_current)
        m_current->requestPause();
}

void PlanWorker::run()
{
    m_pause = false;
    const int count = static_cast<int>(m_steps.size());
    const auto fail = [&](int step, bool resumable, const QString& message) {
        Logger::instance().info("PartitionMover", QStringLiteral("Kế hoạch dừng ở bước %1: %2").arg(step + 1).arg(message));
        emit planFinished(false, resumable, message, step);
    };
    if (!PartitionManager::isElevated())
    {
        fail(0, false, QStringLiteral("Cần quyền Administrator để đổi vị trí/kích thước phân vùng."));
        return;
    }
    // Có bước chép dữ liệu: kiểm tra TRƯỚC (khóa thử ổ, chương trình đang giữ ổ...) để không đổi gì (kể cả bước thu nhỏ của Windows) rồi mới thấy không chép được.
    bool hasMove = false;
    for (const auto& st : m_steps)
        hasMove = hasMove || st.kind == DiskLayout::StepKind::MoveData;
    if (hasMove)
    {
        emit progress(0, count, 0, 0, QStringLiteral("Kiểm tra trước: ổ có khóa/tháo được không..."));
        const QString why = preflightMove(m_disk, m_partition, m_letter);
        if (!why.isEmpty())
        {
            Logger::instance().info("PartitionMover", QStringLiteral("Kiểm tra trước không đạt: %1").arg(why));
            emit planFinished(false, false, why + QStringLiteral("\nChưa có gì bị thay đổi."), -2);
            return;
        }
    }
    qint64 originalSize = -1;
    for (int i = 0; i < count; ++i)
    {
        const DiskLayout::PlanStep& step = m_steps[i];
        emit progress(i, count, 0, 0, QStringLiteral("Bước %1/%2: đọc lại thông tin phân vùng từ Windows...").arg(i + 1).arg(count));
        QString err;
        const auto parts = PartitionManager::listPartitions(&err);
        const PartitionManager::PartitionInfo* cur = nullptr;
        for (const auto& p : parts)
            if (p.diskNumber == m_disk && p.partitionNumber == m_partition)
                cur = &p;
        if (!cur)
        {
            fail(i, false, QStringLiteral("Không đọc lại được phân vùng từ Windows%1.").arg(err.isEmpty() ? QString() : (": " + err)));
            return;
        }
        if (i == 0)
            originalSize = cur->sizeBytes;
        if (step.kind == DiskLayout::StepKind::MoveData)
        {
            MoveRequest req;
            req.diskNumber = m_disk;
            req.partitionNumber = m_partition;
            req.expectedStart = cur->offsetBytes;
            req.expectedLength = cur->sizeBytes;
            req.newStart = step.after.start;
            req.driveLetter = m_letter.isEmpty() ? cur->driveLetter : m_letter;
            MoveWorker worker;
            worker.setRequest(req);
            bool ok = false, resumable = false;
            QString message;
            connect(&worker, &MoveWorker::progress, this,
                    [this, i, count](qint64 d, qint64 t, QString phase) { emit progress(i, count, d, t, phase); }, Qt::DirectConnection);
            connect(&worker, &MoveWorker::moveFinished, this,
                    [&](bool o, bool r, QString m) {
                        ok = o;
                        resumable = r;
                        message = m;
                    },
                    Qt::DirectConnection);
            {
                QMutexLocker lock(&m_mutex);
                m_current = &worker;
                if (m_pause)
                    worker.requestPause();
            }
            worker.start();
            worker.wait();
            {
                QMutexLocker lock(&m_mutex);
                m_current = nullptr;
            }
            if (!ok)
            {
                QString extra;
                // Chép lỗi mà CHƯA ghi gì (không resumable, không có nhật ký) sau một bước thu nhỏ: hoàn tác thu nhỏ để không để đĩa nửa vời (vùng trống ở sai phía).
                PartitionMove::JournalState j;
                if (!resumable && i > 0 && m_steps[i - 1].kind == DiskLayout::StepKind::ShrinkRight && originalSize > 0 && !loadJournal(&j))
                {
                    emit progress(i, count, 0, 0, QStringLiteral("Hoàn tác bước thu nhỏ (khôi phục kích thước ban đầu)..."));
                    QString undoErr;
                    const auto partsNow = PartitionManager::listPartitions(&undoErr);
                    const PartitionManager::PartitionInfo* now = nullptr;
                    for (const auto& pp : partsNow)
                        if (pp.diskNumber == m_disk && pp.partitionNumber == m_partition)
                            now = &pp;
                    bool restored = false;
                    if (now)
                    {
                        QString rangeErr2;
                        const auto range2 = PartitionManager::querySupportedSize(m_disk, m_partition, &rangeErr2);
                        const QString block = PartitionManager::internal::resizeBlockReason(m_disk, m_partition, now->sizeBytes, partsNow, range2, originalSize);
                        restored = block.isEmpty() && PartitionManager::resizePartition(m_disk, m_partition, originalSize, &undoErr);
                        if (!block.isEmpty())
                            undoErr = block;
                    }
                    extra = restored ? QStringLiteral("\nĐã tự khôi phục kích thước ban đầu của phân vùng (không còn vùng trống thừa bên phải).")
                                     : QStringLiteral("\nBước thu nhỏ trước đó đã xong nhưng CHƯA khôi phục được tự động (%1): dữ liệu vẫn nguyên vẹn - "
                                                      "hãy kéo mép phải để mở rộng lại nếu muốn.").arg(undoErr);
                }
                fail(i, resumable, message + extra);
                return;
            }
        }
        else
        {
            emit progress(i, count, 0, 0,
                          QStringLiteral("Bước %1/%2: %3 phân vùng (Windows)...").arg(i + 1).arg(count).arg(step.kind == DiskLayout::StepKind::ShrinkRight ? "thu nhỏ" : "mở rộng"));
            QString rangeErr;
            const auto range = PartitionManager::querySupportedSize(m_disk, m_partition, &rangeErr);
            const QString reason = PartitionManager::internal::resizeBlockReason(m_disk, m_partition, cur->sizeBytes, parts, range, step.after.size);
            if (!reason.isEmpty())
            {
                fail(i, false, reason + (rangeErr.isEmpty() ? QString() : (" (" + rangeErr + ")")));
                return;
            }
            QString resizeErr;
            if (!PartitionManager::resizePartition(m_disk, m_partition, step.after.size, &resizeErr))
            {
                fail(i, false, QStringLiteral("Windows không đổi được kích thước: ") + resizeErr);
                return;
            }
        }
    }
    Logger::instance().info("PartitionMover", QStringLiteral("Hoàn tất kế hoạch %1 bước").arg(count));
    emit planFinished(true, false, QStringLiteral("Đã hoàn tất mọi bước."), -1);
}
} // namespace PartitionMover
