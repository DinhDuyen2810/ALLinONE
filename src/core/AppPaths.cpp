#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QThread>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
QMutex g_mutex;
QString g_override;
QString g_envRaw;      // giá trị ONEFORALL_DATA_DIR đã phân giải lần gần nhất
QString g_envResolved; // ...và đường dẫn TUYỆT ĐỐI tương ứng

QString ensureDir(const QString& path)
{
    QDir().mkpath(path);
    return path;
}

/// Đường dẫn tuyệt đối cho giá trị ONEFORALL_DATA_DIR. Giá trị TƯƠNG ĐỐI (vd "." hay "data" - kiểu chạy
/// portable, dữ liệu nằm cạnh exe) được phân giải theo thư mục làm việc ở LẦN ĐẦU dùng tới rồi giữ nguyên:
/// trước đây nó được trả về nguyên dạng tương đối, tức đổi nghĩa mỗi khi tiến trình đổi thư mục làm việc,
/// và làm removeLegacyProfileCopies() không còn nhận ra thư mục đang dùng (xem isSameDirectory()).
QString resolveEnvDataDir(const QString& raw)
{
    QMutexLocker locker(&g_mutex);
    if (raw != g_envRaw)
    {
        g_envRaw = raw;
        g_envResolved = QDir(QDir::fromNativeSeparators(raw)).absolutePath();
    }
    return g_envResolved;
}

/// Hai đường dẫn có trỏ tới CÙNG một thư mục vật lý không. So bằng danh tính tệp do hệ thống cấp (số
/// sê-ri ổ đĩa + chỉ số tệp), không so chuỗi: cùng một thư mục có thể được viết theo tên ngắn 8.3
/// ("C:\PROGRA~1\..."), qua một junction/liên kết tượng trưng, hay ổ đĩa `subst` - so chuỗi thì ra "khác
/// nhau" (đã xác nhận thật bằng core_stress_tests với tên ngắn 8.3 và junction: removeLegacyProfileCopies()
/// xóa luôn bản đang dùng). Không mở được một trong hai (không tồn tại, không có quyền) thì lùi về so chuỗi
/// không phân biệt hoa/thường.
bool isSameDirectory(const QString& a, const QString& b)
{
    const QString absA = QDir::cleanPath(QDir(a).absolutePath());
    const QString absB = QDir::cleanPath(QDir(b).absolutePath());
#ifdef Q_OS_WIN
    const auto identity = [](const QString& path, BY_HANDLE_FILE_INFORMATION* info) {
        // FILE_FLAG_BACKUP_SEMANTICS: bắt buộc để mở THƯ MỤC; quyền truy cập 0 = chỉ hỏi thông tin.
        HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        const bool ok = GetFileInformationByHandle(h, info) != 0;
        CloseHandle(h);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION infoA{}, infoB{};
    if (identity(absA, &infoA) && identity(absB, &infoB))
    {
        return infoA.dwVolumeSerialNumber == infoB.dwVolumeSerialNumber &&
               infoA.nFileIndexHigh == infoB.nFileIndexHigh && infoA.nFileIndexLow == infoB.nFileIndexLow;
    }
#endif
    return absA.compare(absB, Qt::CaseInsensitive) == 0;
}

QString systemDirectory()
{
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH + 1] = {};
    const UINT len = GetSystemDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(buf, static_cast<int>(len)));
#endif
    return QStringLiteral("C:/Windows/System32");
}
} // namespace

namespace AppPaths
{

QString dataDir()
{
    QString overrideDir;
    {
        QMutexLocker locker(&g_mutex);
        overrideDir = g_override;
    }
    if (!overrideDir.isEmpty())
        return ensureDir(overrideDir);

    const QString fromEnv = qEnvironmentVariable("ONEFORALL_DATA_DIR");
    if (!fromEnv.trimmed().isEmpty())
        return ensureDir(resolveEnvDataDir(fromEnv.trimmed()));

    // GenericDataLocation (không phải AppLocalDataLocation): không phụ thuộc tên ứng dụng/tổ chức đặt
    // qua QCoreApplication - các bộ test dựng QApplication riêng không đặt hai giá trị đó.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return ensureDir(base + "/OneForAll");
}

QString profilesDir()
{
    return ensureDir(dataDir() + "/profiles");
}

QString logsDir()
{
    return ensureDir(dataDir() + "/logs");
}

namespace
{
/// Các vị trí CŨ có thể còn một bản của tệp hồ sơ (không gồm vị trí hiện tại).
QStringList legacyProfileCandidates(const QString& fileName)
{
    QStringList candidates;
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath() + "/profiles/" + fileName;
    candidates << QDir::current().absoluteFilePath("profiles/" + fileName);

    const QString currentDir = profilesDir();
    QStringList result;
    for (const QString& candidate : std::as_const(candidates))
    {
        const QFileInfo info(candidate);
        const QString path = QDir::cleanPath(info.absoluteFilePath());
        // isSameDirectory() chỉ được gọi khi ứng viên THẬT SỰ là một tệp (đa số lần gọi không có tệp cũ nào).
        if (info.isFile() && !isSameDirectory(info.absolutePath(), currentDir) && !result.contains(path, Qt::CaseInsensitive))
            result << path;
    }
    return result;
}

/// Chép tệp cũ sang vị trí mới sao cho tệp đích chỉ XUẤT HIỆN khi đã đủ nội dung: chép ra một tệp tạm
/// cùng thư mục rồi đổi tên. Chép thẳng vào tên đích (như trước) thì trong suốt lúc chép, tệp đích đã "tồn
/// tại" nhưng đang bị khóa/chưa đủ - luồng hoặc bản sao khác của ứng dụng gọi profileFile() đúng lúc đó
/// nhận về một tệp không mở được và coi nó là hồ sơ hỏng (đã xác nhận thật bằng core_stress_tests: 845 lần
/// mở thất bại trong lúc chép một tệp 96 MB).
void copyIntoPlace(const QString& source, const QString& target)
{
    // Tên tạm riêng cho từng tiến trình + luồng. Nối chuỗi, KHÔNG dùng QString::arg() nối tiếp: đường dẫn
    // có thể chứa "%1"/"%2" (tên thư mục người dùng) và sẽ bị lần arg() kế tiếp thay nhầm.
    const QString temp = target + QChar('.') + QString::number(QCoreApplication::applicationPid()) + QChar('-') +
                         QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16) +
                         QStringLiteral(".migrating");
    QFile::remove(temp); // sót lại từ một lần chép dở trước đó của chính luồng này
    // QFile::rename không ghi đè tệp đã có - nếu một luồng/tiến trình khác vừa tạo target thì giữ nguyên
    // bản đó và bỏ bản tạm của ta.
    if (!QFile::copy(source, temp) || !QFile::rename(temp, target))
        QFile::remove(temp);
}

bool overrideActive()
{
    QMutexLocker locker(&g_mutex);
    return !g_override.isEmpty();
}
} // namespace

QString profileFile(const QString& fileName)
{
    const QString target = profilesDir() + "/" + fileName;
    if (QFileInfo::exists(target))
        return target;

    // Khi bộ test đã ép thư mục dữ liệu, KHÔNG di trú: nếu không, test sẽ nạp dữ liệu thật của người
    // phát triển (vd profiles\qr_history.json cạnh exe test) vào thư mục tạm và không còn độc lập.
    if (overrideActive())
        return target;

    const QStringList legacy = legacyProfileCandidates(fileName);
    if (!legacy.isEmpty())
        copyIntoPlace(legacy.first(), target);
    return target;
}

void removeLegacyProfileCopies(const QString& fileName)
{
    if (overrideActive())
        return;
    for (const QString& path : legacyProfileCandidates(fileName))
        QFile::remove(path);
}

QString logFile(const QString& fileName)
{
    return logsDir() + "/" + fileName;
}

void setDataDirOverride(const QString& dir)
{
    QMutexLocker locker(&g_mutex);
    g_override = dir.isEmpty() ? QString() : QDir(dir).absolutePath();
}

QString systemExecutable(const QString& fileName)
{
    return systemDirectory() + "/" + fileName;
}

QString powershellExecutable()
{
    return systemDirectory() + "/WindowsPowerShell/v1.0/powershell.exe";
}

} // namespace AppPaths
