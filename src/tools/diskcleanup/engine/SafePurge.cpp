#include "SafePurge.h"

#include "FsSafety.h"

#include <QDir>
#include <QStringList>

#include <algorithm>
#include <vector>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#endif

namespace
{
#ifdef Q_OS_WIN
/// "\\?\C:\..." - vượt MAX_PATH (cache trình duyệt/node_modules hay sâu hơn 260 ký tự).
std::wstring extendedPath(const QString& path)
{
    QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (native.startsWith(QLatin1String("\\\\?\\")))
        return native.toStdWString();
    if (native.startsWith(QLatin1String("\\\\")))
        return (QStringLiteral("\\\\?\\UNC\\") + native.mid(2)).toStdWString();
    if (native.size() == 2 && native[1] == QLatin1Char(':'))
        native += QLatin1Char('\\');
    return (QStringLiteral("\\\\?\\") + native).toStdWString();
}

QString finalPathOfHandle(HANDLE h)
{
    wchar_t small[512];
    DWORD n = GetFinalPathNameByHandleW(h, small, 512, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n == 0)
        return {};
    QString s;
    if (n < 512)
    {
        s = QString::fromWCharArray(small, static_cast<int>(n));
    }
    else
    {
        std::vector<wchar_t> big(static_cast<size_t>(n) + 1);
        n = GetFinalPathNameByHandleW(h, big.data(), static_cast<DWORD>(big.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (n == 0 || n >= big.size())
            return {};
        s = QString::fromWCharArray(big.data(), static_cast<int>(n));
    }
    if (s.startsWith(QLatin1String("\\\\?\\UNC\\")))
        s = QStringLiteral("\\\\") + s.mid(8);
    else if (s.startsWith(QLatin1String("\\\\?\\")))
        s = s.mid(4);
    s = QDir::fromNativeSeparators(s);
    if (s.size() > 3 && s.endsWith(QLatin1Char('/')))
        s.chop(1);
    return s;
}

constexpr DWORD kNameSurrogateBit = 0x20000000;
constexpr int kFileDispositionInfoEx = 21; // FILE_INFO_BY_HANDLE_CLASS::FileDispositionInfoEx (Win10 1607+)
constexpr ULONG kDispositionDelete = 0x1;
constexpr ULONG kDispositionPosix = 0x2;
constexpr ULONG kDispositionIgnoreReadOnly = 0x10;

struct DispositionInfoEx
{
    ULONG Flags;
};

enum class Outcome
{
    Deleted,
    Gone,     // đã tự biến mất - mục tiêu coi như đạt, không tính byte
    InUse,
    Refused,  // nằm ngoài thư mục gốc tin cậy / không xác định được đường dẫn thật
    NotEmpty, // thư mục còn nội dung (thứ bị giữ lại bên trong)
    Failed,
};

Outcome classify(DWORD error)
{
    switch (error)
    {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return Outcome::Gone;
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
        case ERROR_USER_MAPPED_FILE:
            return Outcome::InUse;
        case ERROR_DIR_NOT_EMPTY:
            return Outcome::NotEmpty;
        default:
            return Outcome::Failed;
    }
}

/// Xóa MỘT tệp hoặc MỘT thư mục rỗng, chỉ khi đường dẫn thật của chính handle nằm trong rootReal.
/// allowRootItself: cho phép xóa chính rootReal (chỉ dùng khi gốc tin cậy là một TỆP).
Outcome deleteOne(const QString& path, const QString& rootReal, bool allowRootItself)
{
    const HANDLE h = CreateFileW(extendedPath(path).c_str(), DELETE | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return classify(GetLastError());

    const QString real = finalPathOfHandle(h);
    const bool sameAsRoot = real.compare(rootReal, Qt::CaseInsensitive) == 0;
    if (real.isEmpty() || !SafePurge::isInside(real, rootReal) || (sameAsRoot && !allowRootItself))
    {
        CloseHandle(h);
        return Outcome::Refused;
    }

    DispositionInfoEx ex{kDispositionDelete | kDispositionPosix | kDispositionIgnoreReadOnly};
    BOOL ok = SetFileInformationByHandle(h, static_cast<FILE_INFO_BY_HANDLE_CLASS>(kFileDispositionInfoEx), &ex, sizeof(ex));
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    if (!ok && (error == ERROR_INVALID_PARAMETER || error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED))
    {
        // Hệ thống tệp không biết FileDispositionInfoEx (FAT/exFAT, ổ mạng): cách cổ điển - phải tự bỏ cờ chỉ đọc.
        FILE_BASIC_INFO basic{};
        bool clearedReadOnly = false;
        if (GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof(basic)) &&
            (basic.FileAttributes & FILE_ATTRIBUTE_READONLY))
        {
            FILE_BASIC_INFO changed = basic;
            changed.FileAttributes &= ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY);
            if (changed.FileAttributes == 0)
                changed.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            clearedReadOnly = SetFileInformationByHandle(h, FileBasicInfo, &changed, sizeof(changed)) != 0;
        }
        FILE_DISPOSITION_INFO classic{TRUE};
        ok = SetFileInformationByHandle(h, FileDispositionInfo, &classic, sizeof(classic));
        error = ok ? ERROR_SUCCESS : GetLastError();
        if (!ok && clearedReadOnly)
            SetFileInformationByHandle(h, FileBasicInfo, &basic, sizeof(basic)); // không xóa được: trả lại cờ như cũ
    }
    CloseHandle(h);
    return ok ? Outcome::Deleted : classify(error);
}

struct Entry
{
    QString path;
    bool isDir{false};
    bool link{false};
    bool cloudOnly{false};
    qint64 size{0};
};

/// Liệt kê MỘT thư mục (không đệ quy) rồi đóng handle tìm kiếm ngay - xóa trong lúc đang duyệt chính thư
/// mục đó là không cần thiết.
bool listDirectory(const QString& dir, std::vector<Entry>* out)
{
    QString pattern = dir;
    if (!pattern.endsWith(QLatin1Char('/')))
        pattern += QLatin1Char('/');
    const QString base = pattern;
    pattern += QLatin1Char('*');
    WIN32_FIND_DATAW data;
    const HANDLE h = FindFirstFileExW(extendedPath(pattern).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr,
                                      FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    do
    {
        const wchar_t* name = data.cFileName;
        if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0)))
            continue;
        Entry e;
        e.path = base + QString::fromWCharArray(name);
        e.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        e.link = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (data.dwReserved0 & kNameSurrogateBit) != 0;
        e.cloudOnly = FsSafety::WalkEntry::isCloudOnlyAttributes(data.dwFileAttributes);
        e.size = (static_cast<qint64>(data.nFileSizeHigh) << 32) | static_cast<qint64>(data.nFileSizeLow);
        out->push_back(e);
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return true;
}
#endif
} // namespace

namespace SafePurge
{

QString finalPath(const QString& path)
{
#ifdef Q_OS_WIN
    if (path.isEmpty())
        return {};
    const HANDLE h = CreateFileW(extendedPath(path).c_str(), FILE_READ_ATTRIBUTES,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                 FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    const QString real = finalPathOfHandle(h);
    CloseHandle(h);
    return real;
#else
    Q_UNUSED(path);
    return {};
#endif
}

bool isInside(const QString& real, const QString& root)
{
    if (real.isEmpty() || root.isEmpty())
        return false;
    if (real.compare(root, Qt::CaseInsensitive) == 0)
        return true;
    const QString prefix = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    return real.startsWith(prefix, Qt::CaseInsensitive);
}

Result purge(const QString& path, const QString& trustedRoot, const std::atomic_bool& stopFlag,
             const std::function<void(qint64)>& onFreed)
{
    Result r;
#ifdef Q_OS_WIN
    const QString rootReal = finalPath(trustedRoot);
    if (rootReal.isEmpty() || rootReal.size() <= 3 /* "C:/" - gốc ổ đĩa không bao giờ là gốc tin cậy */)
    {
        r.rootRefused = true;
        return r;
    }
    const QString cleanPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
    const FsSafety::RawInfo info = FsSafety::rawInfo(cleanPath);
    if (!info.exists)
    {
        r.removedItself = true;
        return r;
    }
    if (info.isLink)
    {
        ++r.refusedFiles; // liên kết: không đi theo, không gỡ
        return r;
    }

    auto account = [&](Outcome o, qint64 size) {
        switch (o)
        {
            case Outcome::Deleted:
                r.freedBytes += size;
                ++r.deletedFiles;
                if (onFreed)
                    onFreed(size);
                break;
            case Outcome::Gone:
                break;
            case Outcome::InUse:
                ++r.inUseFiles;
                break;
            case Outcome::Refused:
                ++r.refusedFiles;
                break;
            case Outcome::NotEmpty:
            case Outcome::Failed:
                ++r.failedFiles;
                break;
        }
    };

    if (!info.isDir)
    {
        if (FsSafety::WalkEntry::isCloudOnlyAttributes(info.attributes))
        {
            ++r.refusedFiles; // tệp giữ chỗ OneDrive: xóa là xóa cả bản trên mây
            return r;
        }
        const bool rootIsThisFile = QDir::cleanPath(QDir::fromNativeSeparators(trustedRoot)).compare(cleanPath, Qt::CaseInsensitive) == 0;
        const Outcome o = deleteOne(cleanPath, rootReal, rootIsThisFile);
        account(o, info.sizeBytes);
        r.removedItself = (o == Outcome::Deleted || o == Outcome::Gone);
        return r;
    }

    // Thư mục: duyệt theo chiều sâu bằng ngăn xếp (không đệ quy - cây cache có thể rất sâu), xóa tệp ngay khi
    // gặp, ghi nhận thư mục để gỡ SAU (sâu trước).
    const bool keepTop = QDir::cleanPath(QDir::fromNativeSeparators(trustedRoot)).compare(cleanPath, Qt::CaseInsensitive) == 0;
    std::vector<QString> dirs;
    std::vector<QString> stack{cleanPath};
    while (!stack.empty())
    {
        if (stopFlag.load())
        {
            r.stopped = true;
            return r;
        }
        const QString dir = stack.back();
        stack.pop_back();
        dirs.push_back(dir);
        std::vector<Entry> entries;
        if (!listDirectory(dir, &entries))
            continue; // không đọc được (quyền) - thư mục này sẽ không gỡ được, không sao
        for (const Entry& e : entries)
        {
            if (stopFlag.load())
            {
                r.stopped = true;
                return r;
            }
            if (e.link)
            {
                ++r.refusedFiles; // junction/symlink: không đi vào, không gỡ
                continue;
            }
            if (e.isDir)
            {
                stack.push_back(e.path);
                continue;
            }
            if (e.cloudOnly)
            {
                ++r.refusedFiles;
                continue;
            }
            account(deleteOne(e.path, rootReal, false), e.size);
        }
    }

    std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.count('/') > b.count('/'); });
    for (const QString& d : dirs)
    {
        if (keepTop && d.compare(cleanPath, Qt::CaseInsensitive) == 0)
            continue;
        const Outcome o = deleteOne(d, rootReal, false);
        Q_UNUSED(o); // thư mục còn nội dung bị giữ lại (tệp đang mở...) thì để nguyên - không phải lỗi riêng
    }
    r.removedItself = !keepTop && !FsSafety::existsNoFollow(cleanPath);
#else
    Q_UNUSED(path);
    Q_UNUSED(trustedRoot);
    Q_UNUSED(stopFlag);
    Q_UNUSED(onFreed);
    r.rootRefused = true;
#endif
    return r;
}
} // namespace SafePurge
