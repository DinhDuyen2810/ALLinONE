#include "FsSafety.h"

#include <QDir>
#include <QRegularExpression>
#include <QStandardPaths>

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
/// Dạng đường dẫn mở rộng "\\?\C:\..." - cho phép vượt MAX_PATH (260 ký tự), vốn rất hay gặp trong
/// cache trình duyệt/node_modules. Dạng này KHÔNG được Windows chuẩn hóa hộ nên phải tự làm sạch trước.
std::wstring toExtendedPath(const QString& path)
{
    QString native = QDir::toNativeSeparators(QDir::cleanPath(QDir::isRelativePath(path) ? QDir::current().absoluteFilePath(path) : path));
    if (native.startsWith(QLatin1String("\\\\?\\")))
        return native.toStdWString();
    if (native.startsWith(QLatin1String("\\\\")))
        return (QStringLiteral("\\\\?\\UNC\\") + native.mid(2)).toStdWString();
    if (native.size() == 2 && native[1] == QLatin1Char(':'))
        native += QLatin1Char('\\'); // cleanPath("C:/") có thể bỏ dấu gạch cuối
    return (QStringLiteral("\\\\?\\") + native).toStdWString();
}

constexpr DWORD kNameSurrogateBit = 0x20000000; // IsReparseTagNameSurrogate()

bool isLinkLike(DWORD attributes, DWORD reparseTag)
{
    return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 && (reparseTag & kNameSurrogateBit) != 0;
}

qint64 toInt64(const FILETIME& ft)
{
    return (static_cast<qint64>(ft.dwHighDateTime) << 32) | static_cast<qint64>(ft.dwLowDateTime);
}

bool walkDirectory(const QString& dir, const std::atomic_bool& stopFlag,
                   const std::function<void(const FsSafety::WalkEntry&)>& onFile)
{
    QString pattern = dir;
    if (!pattern.endsWith(QLatin1Char('/')))
        pattern += QLatin1Char('/');
    const QString base = pattern;
    pattern += QLatin1Char('*');

    WIN32_FIND_DATAW data;
    const HANDLE h = FindFirstFileExW(toExtendedPath(pattern).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                      nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE)
        return true; // không đọc được (quyền truy cập...) - bỏ qua thư mục này, không phải "bị dừng"

    QStringList subDirs;
    bool keepGoing = true;
    do
    {
        if (stopFlag.load())
        {
            keepGoing = false;
            break;
        }
        const wchar_t* name = data.cFileName;
        if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0)))
            continue;

        const bool link = isLinkLike(data.dwFileAttributes, data.dwReserved0);
        const QString fullPath = base + QString::fromWCharArray(name);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!link) // junction/symlink thư mục: KHÔNG đi vào
                subDirs << fullPath;
            continue;
        }
        if (link || fullPath.endsWith(QLatin1String(".lnk"), Qt::CaseInsensitive))
            continue;

        FsSafety::WalkEntry e;
        e.path = fullPath;
        e.sizeBytes = (static_cast<qint64>(data.nFileSizeHigh) << 32) | static_cast<qint64>(data.nFileSizeLow);
        e.lastWriteTime = toInt64(data.ftLastWriteTime);
        onFile(e);
    } while (FindNextFileW(h, &data));
    FindClose(h);

    if (!keepGoing)
        return false;
    for (const QString& sub : subDirs)
        if (!walkDirectory(sub, stopFlag, onFile))
            return false;
    return true;
}
#endif

QString normalized(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
}
} // namespace

namespace FsSafety
{

RawInfo rawInfo(const QString& path)
{
    RawInfo info;
#ifdef Q_OS_WIN
    if (path.isEmpty())
        return info;
    WIN32_FIND_DATAW data;
    const HANDLE h = FindFirstFileExW(toExtendedPath(path).c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (h == INVALID_HANDLE_VALUE)
    {
        // FindFirstFile không dùng được cho gốc ổ đĩa ("C:\") - hỏi thuộc tính trực tiếp.
        const DWORD attrs = GetFileAttributesW(toExtendedPath(path).c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return info;
        info.exists = true;
        info.isDir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        return info;
    }
    FindClose(h);
    info.exists = true;
    info.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.isLink = isLinkLike(data.dwFileAttributes, data.dwReserved0);
    info.sizeBytes = (static_cast<qint64>(data.nFileSizeHigh) << 32) | static_cast<qint64>(data.nFileSizeLow);
    info.lastWriteTime = toInt64(data.ftLastWriteTime);
#else
    Q_UNUSED(path);
#endif
    return info;
}

bool existsNoFollow(const QString& path)
{
    return rawInfo(path).exists;
}

bool fileIdentity(const QString& path, FileIdentity* out)
{
#ifdef Q_OS_WIN
    // Quyền truy cập 0 (chỉ đọc siêu dữ liệu) + chia sẻ toàn phần: mở được cả tệp đang bị chương trình
    // khác khóa ghi. Không đặt FILE_FLAG_OPEN_REPARSE_POINT: nếu đường dẫn là symlink thì lấy danh tính
    // của tệp ĐÍCH - đúng thứ cần so để biết "hai đường dẫn này có phải cùng một tệp không".
    const HANDLE h = CreateFileW(toExtendedPath(path).c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION info;
    const bool ok = GetFileInformationByHandle(h, &info) != 0;
    CloseHandle(h);
    if (!ok)
        return false;
    out->volumeSerial = info.dwVolumeSerialNumber;
    out->fileIndex = (static_cast<quint64>(info.nFileIndexHigh) << 32) | static_cast<quint64>(info.nFileIndexLow);
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(out);
    return false;
#endif
}

bool walkFiles(const QString& root, const std::atomic_bool& stopFlag, const std::function<void(const WalkEntry&)>& onFile)
{
#ifdef Q_OS_WIN
    if (root.isEmpty())
        return true;
    const QString clean = QDir::cleanPath(QDir(root).absolutePath());
    return walkDirectory(clean, stopFlag, onFile);
#else
    Q_UNUSED(root);
    Q_UNUSED(stopFlag);
    Q_UNUSED(onFile);
    return true;
#endif
}

qint64 nowFileTime()
{
#ifdef Q_OS_WIN
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return toInt64(ft);
#else
    return 0;
#endif
}

QString windowsDirectory()
{
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH + 1] = {};
    const UINT len = GetWindowsDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(buf, static_cast<int>(len)));
#endif
    return {};
}

QStringList systemProtectedDirs()
{
    QStringList dirs;
    auto add = [&](const QString& p) {
        const QString n = normalized(p);
        if (!n.isEmpty() && !dirs.contains(n, Qt::CaseInsensitive))
            dirs << n;
    };

    for (const char* var : {"USERPROFILE", "PUBLIC", "APPDATA", "LOCALAPPDATA", "ProgramFiles", "ProgramFiles(x86)",
                            "ProgramW6432", "ProgramData", "OneDrive"})
        add(qEnvironmentVariable(var));

    for (QStandardPaths::StandardLocation loc :
         {QStandardPaths::HomeLocation, QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
          QStandardPaths::DownloadLocation, QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,
          QStandardPaths::MoviesLocation})
        add(QStandardPaths::writableLocation(loc));

    const QString winDir = windowsDirectory();
    if (!winDir.isEmpty())
    {
        add(winDir);
        add(winDir + "/System32");
    }
    return dirs;
}

QString unsafeCleanupRootReason(const QString& path, const QStringList& protectedDirs)
{
    if (path.trimmed().isEmpty())
        return "đường dẫn rỗng";

    const QString p = normalized(path);
    // Bắt buộc dạng "X:/..." hoặc UNC "//máy/chia sẻ/..." - "/Temp" (sinh ra khi biến môi trường rỗng)
    // hay "Windows.old" (tương đối) sẽ bị Windows hiểu theo ổ/thư mục làm việc hiện hành.
    static const QRegularExpression driveForm(QStringLiteral("^[A-Za-z]:/"));
    const bool isUnc = p.startsWith(QLatin1String("//"));
    if (!isUnc && !driveForm.match(p + "/").hasMatch())
        return "không phải đường dẫn tuyệt đối có ký tự ổ đĩa";

    static const QRegularExpression driveRoot(QStringLiteral("^[A-Za-z]:/?$"));
    if (driveRoot.match(p).hasMatch())
        return "là thư mục gốc của ổ đĩa";
    if (isUnc && p.count(QLatin1Char('/')) < 4) // "//máy/chia sẻ" - gốc của một thư mục chia sẻ
        return "là thư mục gốc của một thư mục chia sẻ mạng";

    const QString withSlash = p + "/";
    for (const QString& raw : protectedDirs)
    {
        const QString prot = normalized(raw);
        if (prot.isEmpty())
            continue;
        if (prot.compare(p, Qt::CaseInsensitive) == 0)
            return "trùng với thư mục được bảo vệ: " + prot;
        if (prot.startsWith(withSlash, Qt::CaseInsensitive))
            return "chứa thư mục được bảo vệ: " + prot;
    }
    return {};
}

} // namespace FsSafety
