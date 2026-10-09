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
    // QDir::cleanPath để SÓT dấu gạch kép khi bỏ một thành phần "." đứng giữa hai dấu gạch kép
    // ("C://.//Windows" -> "C://Windows", đã xác nhận bằng chạy thử), trong khi Win32 coi đó chính là
    // "C:/Windows" - so chuỗi với thư mục được bảo vệ sẽ trượt. Gộp nốt dấu gạch lặp (giữ "//" đầu của UNC)
    // rồi làm sạch lại cho tới khi chuỗi ổn định.
    QString p = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    static const QRegularExpression repeatedSlashes(QStringLiteral("/{2,}"));
    for (int round = 0; round < 8; ++round)
    {
        const bool unc = p.startsWith(QLatin1String("//"));
        QString collapsed = unc ? p.mid(2) : p;
        collapsed.replace(repeatedSlashes, QStringLiteral("/"));
        if (unc)
            collapsed = QLatin1String("//") + (collapsed.startsWith(QLatin1Char('/')) ? collapsed.mid(1) : collapsed);
        const QString cleaned = QDir::cleanPath(collapsed);
        if (cleaned == p)
            break;
        p = cleaned;
    }
    return p;
}

/// Tên ngắn 8.3 ("C:/PROGRA~1", "C:/Users/NGUYEN~1") là BÍ DANH của tên dài - và biến môi trường TEMP của
/// Windows rất hay ở dạng này. So chuỗi thuần sẽ không nhận ra "C:/Users/NGUYEN~1" chính là thư mục hồ sơ
/// đang được bảo vệ, nên đổi về tên dài thật trước khi so (chỉ khi đường dẫn có '~' và tồn tại; chỉ với
/// đường dẫn có ký tự ổ đĩa - không bao giờ chạm tới mạng). Không đổi được thì trả về nguyên trạng.
QString expandShortNames(const QString& cleanDrivePath)
{
#ifdef Q_OS_WIN
    if (!cleanDrivePath.contains(QLatin1Char('~')) || cleanDrivePath.startsWith(QLatin1String("//")))
        return cleanDrivePath;
    const std::wstring extended = toExtendedPath(cleanDrivePath);
    DWORD len = GetLongPathNameW(extended.c_str(), nullptr, 0);
    if (len == 0)
        return cleanDrivePath;
    std::wstring buffer(len, L'\0');
    len = GetLongPathNameW(extended.c_str(), buffer.data(), len);
    if (len == 0 || len >= buffer.size())
        return cleanDrivePath;
    buffer.resize(len);
    QString out = QDir::fromNativeSeparators(QString::fromStdWString(buffer));
    if (out.startsWith(QLatin1String("//?/")))
        out = out.mid(4);
    return QDir::cleanPath(out);
#else
    return cleanDrivePath;
#endif
}

bool isAsciiLetter(QChar c)
{
    return (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('a') && c <= QLatin1Char('z'));
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
    // FindFirstFile coi '*'/'?' ở thành phần cuối là ký tự ĐẠI DIỆN và trả về thông tin của một tệp KHÁC
    // khớp mẫu - một đường dẫn như vậy không phải là "chính đường dẫn được hỏi" nên coi như không tồn tại.
    const QString body = (path.startsWith(QLatin1String("\\\\?\\")) || path.startsWith(QLatin1String("//?/"))) ? path.mid(4) : path;
    if (body.contains(QLatin1Char('*')) || body.contains(QLatin1Char('?')))
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

bool lockedAgainstDelete(const QString& path)
{
#ifdef Q_OS_WIN
    // Mở chính mục này (không đi theo liên kết) với quyền DELETE và chia sẻ toàn phần: thất bại vì vi phạm
    // chia sẻ/khóa/quyền = lúc này không ai xóa được nó. Không đọc, không sửa gì.
    const HANDLE h = CreateFileW(toExtendedPath(path).c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h != INVALID_HANDLE_VALUE)
    {
        CloseHandle(h);
        return false;
    }
    const DWORD error = GetLastError();
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED;
#else
    Q_UNUSED(path);
    return false;
#endif
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
    if (path.contains(QChar(0)))
        return "đường dẫn chứa ký tự NUL";

    QString p = normalized(path);
    // Bắt buộc dạng "X:/..." hoặc UNC "//máy/chia sẻ/..." - "/Temp" (sinh ra khi biến môi trường rỗng)
    // hay "Windows.old" (tương đối) sẽ bị Windows hiểu theo ổ/thư mục làm việc hiện hành.
    static const QRegularExpression driveForm(QStringLiteral("^[A-Za-z]:/"));
    const bool isUnc = p.startsWith(QLatin1String("//"));
    if (!isUnc && !driveForm.match(p + "/").hasMatch())
        return "không phải đường dẫn tuyệt đối có ký tự ổ đĩa";

    // "\\.\C:\Users" trông như UNC ("//./C:/Users") nhưng là CHÍNH ổ C: - nếu coi là "máy ./chia sẻ C:"
    // thì mọi phép so với thư mục được bảo vệ bên dưới đều trượt. (Tiền tố "\\?\" đã được
    // QDir::fromNativeSeparators bỏ sẵn; vẫn chặn cả dạng đó phòng khi còn sót sau khi làm sạch.)
    if (isUnc && p.size() > 2 && (p[2] == QLatin1Char('?') || p[2] == QLatin1Char('.')) &&
        (p.size() == 3 || p[3] == QLatin1Char('/')))
        return "là đường dẫn dạng thiết bị (\\\\?\\ hoặc \\\\.\\)";

    // Ký tự không thể có trong tên tệp thật: '*'/'?' được Shell hiểu là KÝ TỰ ĐẠI DIỆN (xóa mọi tệp khớp
    // mẫu), ':' sau ký tự ổ đĩa là luồng dữ liệu phụ của một tệp/thư mục khác.
    static const QRegularExpression invalidChars(QStringLiteral("[*?\"<>|:]"));
    if (invalidChars.match(isUnc ? p : p.mid(2)).hasMatch())
        return "chứa ký tự không hợp lệ trong tên tệp (* ? \" < > | :)";

    // Win32 tự BỎ dấu chấm/khoảng trắng ở cuối tên: "C:/Windows." chính là "C:/Windows", và "x/tệp." bị
    // Shell hiểu thành "x/tệp" - một tệp KHÁC (tên kiểu này chỉ tạo được qua \\?\, WSL, một số trình giải nén).
    const QStringList components = p.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : components)
        if (component.endsWith(QLatin1Char('.')) || component.endsWith(QLatin1Char(' ')))
            return "có tên kết thúc bằng dấu chấm/khoảng trắng (Windows sẽ hiểu thành một đường dẫn khác)";

    static const QRegularExpression driveRoot(QStringLiteral("^[A-Za-z]:/?$"));
    if (driveRoot.match(p).hasMatch())
        return "là thư mục gốc của ổ đĩa";
    if (isUnc)
    {
        if (components.size() < 3) // "//máy/chia sẻ" - gốc của một thư mục chia sẻ
            return "là thư mục gốc của một thư mục chia sẻ mạng";
        // Chia sẻ quản trị: "//máy/C$/Users" là bí danh của "C:/Users", "//máy/ADMIN$" là thư mục Windows.
        const QString& share = components[1];
        if (share.compare(QLatin1String("admin$"), Qt::CaseInsensitive) == 0)
            return "nằm trong thư mục Windows (chia sẻ quản trị ADMIN$)";
        if (share.size() == 2 && share[1] == QLatin1Char('$') && isAsciiLetter(share[0]))
            p = QString(share[0]) + QLatin1String(":/") + components.mid(2).join(QLatin1Char('/'));
    }
    p = expandShortNames(p);

    const QString withSlash = p + "/";
    for (const QString& raw : protectedDirs)
    {
        const QString prot = expandShortNames(normalized(raw));
        if (prot.isEmpty())
            continue;
        if (prot.compare(p, Qt::CaseInsensitive) == 0)
            return "trùng với thư mục được bảo vệ: " + prot;
        if (prot.startsWith(withSlash, Qt::CaseInsensitive))
            return "chứa thư mục được bảo vệ: " + prot;
    }
    return {};
}

QString unsafeShellPathReason(const QString& path)
{
    if (path != path.trimmed())
        return "đường dẫn có khoảng trắng ở đầu/cuối";
    // Tiền tố "\\?\" chưa được bỏ (nơi gọi chưa qua QDir::cleanPath): Shell không nhận dạng đường dẫn này.
    if (QDir::fromNativeSeparators(path) != QString(path).replace(QLatin1Char('\\'), QLatin1Char('/')))
        return "đường dẫn còn tiền tố \\\\?\\";
    const QString reason = unsafeCleanupRootReason(path, {});
    if (!reason.isEmpty())
        return reason;
#ifdef Q_OS_WIN
    // Lớp chặn tổng quát: GetFullPathNameW áp ĐÚNG phép chuẩn hóa Win32 mà Shell sẽ áp lên chuỗi này (chỉ
    // xử lý chuỗi, không chạm đĩa). Kết quả khác đầu vào = Shell sẽ thao tác trên một đối tượng KHÁC với
    // thứ đã được quét/kiểm (vd tên thiết bị dành riêng "x/NUL" -> "\\.\NUL"). So trên CHÍNH chuỗi được
    // truyền vào (chỉ đổi dấu gạch) - đó là chuỗi nơi gọi sẽ giao cho Shell.
    const QString native = QString(path).replace(QLatin1Char('/'), QLatin1Char('\\'));
    const std::wstring wide = native.toStdWString();
    DWORD len = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
    if (len == 0)
        return "Windows không chuẩn hóa được đường dẫn";
    std::wstring buffer(len, L'\0');
    len = GetFullPathNameW(wide.c_str(), len, buffer.data(), nullptr);
    if (len == 0 || len >= buffer.size())
        return "Windows không chuẩn hóa được đường dẫn";
    buffer.resize(len);
    if (QString::fromStdWString(buffer).compare(native, Qt::CaseInsensitive) != 0)
        return "Windows hiểu đường dẫn này thành một đường dẫn khác";
#endif
    return {};
}

} // namespace FsSafety
