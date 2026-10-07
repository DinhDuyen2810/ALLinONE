#include "CleanupScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
/// Trên Windows, QFileInfo coi tệp lối tắt (.lnk/.pif) như một "symlink" và TỰ ĐỘNG đi theo nó:
/// isDir()/entryList() phản ánh THƯ MỤC ĐÍCH chứ không phải chính tệp .lnk (vài trăm byte). Hạng mục
/// "Danh sách tệp gần đây" (%APPDATA%/.../Recent) toàn là lối tắt - nếu không chặn việc này, một lối
/// tắt trỏ tới "Downloads" hay cả ổ D: sẽ bị cộng nhầm HÀNG CHỤC GB vào kích thước hạng mục, dù xóa
/// lối tắt chỉ giải phóng đúng kích thước của chính nó. Dùng GetFileAttributesExW (API Win32 thô, đọc
/// thẳng thuộc tính NTFS của chính tệp .lnk - không hề biết/đi theo định dạng Shell Link) để lấy đúng
/// loại + kích thước thật của chính đường dẫn đó, bỏ qua việc Qt "giải mã hộ" lối tắt.
bool rawFileInfo(const QString& path, bool* isDirOut, qint64* sizeOut)
{
#ifdef Q_OS_WIN
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(reinterpret_cast<const wchar_t*>(path.utf16()), GetFileExInfoStandard, &data))
        return false;
    *isDirOut = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    *sizeOut = (static_cast<qint64>(data.nFileSizeHigh) << 32) | static_cast<qint64>(data.nFileSizeLow);
    return true;
#else
    Q_UNUSED(path);
    Q_UNUSED(isDirOut);
    Q_UNUSED(sizeOut);
    return false;
#endif
}

/// Loại + kích thước thật của một mục, không bị Qt đi theo lối tắt (xem rawFileInfo). Dùng cho MỌI
/// mục được liệt kê, không chỉ riêng hạng mục Recent Items - an toàn vì luôn trả về dữ liệu đúng hơn
/// hoặc bằng QFileInfo (rawFileInfo chỉ thay thế khi entry thực sự là symlink/lối tắt).
void realEntryInfo(const QFileInfo& entry, bool* isDirOut, qint64* sizeOut)
{
    if (entry.isSymLink())
    {
        bool rawIsDir = false;
        qint64 rawSize = 0;
        if (rawFileInfo(entry.absoluteFilePath(), &rawIsDir, &rawSize))
        {
            *isDirOut = rawIsDir;
            *sizeOut = rawSize;
            return;
        }
    }
    *isDirOut = entry.isDir();
    *sizeOut = entry.isDir() ? 0 : entry.size(); // thư mục: gọi directorySize() riêng, không dùng size() ở đây
}

/// Tính tổng kích thước một thư mục (đệ quy). Kiểm tra cờ dừng định kỳ vì thư mục có thể rất lớn
/// (Windows.old có thể hàng chục GB). Bỏ qua lối tắt/symlink (QDir::NoSymLinks) để không vô tình đệ
/// quy lạc sang một thư mục đích hoàn toàn khác với thư mục gốc đang quét - xem rawFileInfo() ở trên.
qint64 directorySize(const QString& path, const std::atomic_bool& stopFlag)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::System | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
    int counter = 0;
    while (it.hasNext())
    {
        if ((++counter & 0xFF) == 0 && stopFlag.load())
            break;
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}
} // namespace

CleanupScanner::CleanupScanner(QObject* parent)
    : QThread(parent)
{
}

CleanupScanner::~CleanupScanner()
{
    requestStop();
    wait(5000);
}

void CleanupScanner::setCategories(const QList<CleanupCategory>& categories)
{
    m_categories = categories;
}

void CleanupScanner::requestStop()
{
    m_stopRequested = true;
}

void CleanupScanner::run()
{
    m_stopRequested = false;
    qint64 grandTotalBytes = 0;
    int grandTotalItems = 0;

    for (const CleanupCategory& category : m_categories)
    {
        if (m_stopRequested)
            break;
        if (category.rootPaths.isEmpty())
            continue;

        emit categoryStarted(category.name);
        qint64 categoryBytes = 0;
        int categoryCount = 0;
        scanCategory(category, &categoryBytes, &categoryCount);

        if (m_stopRequested)
            break;

        grandTotalBytes += categoryBytes;
        grandTotalItems += categoryCount;
        emit categoryFinished(category.id, categoryBytes, categoryCount);
    }

    if (m_stopRequested)
        emit scanStopped();
    else
        emit scanFinished(grandTotalBytes, grandTotalItems);
}

void CleanupScanner::scanCategory(const CleanupCategory& category, qint64* outBytes, int* outCount)
{
    for (const QString& rootPath : category.rootPaths)
    {
        if (m_stopRequested)
            return;

        if (!category.keepRootFolder)
        {
            // Toàn bộ rootPath là MỘT mục cần xóa (vd: Windows.old)
            const QFileInfo info(rootPath);
            if (!info.exists())
                continue;
            bool isDir = false;
            qint64 size = 0;
            realEntryInfo(info, &isDir, &size);
            if (isDir)
                size = directorySize(rootPath, m_stopRequested);
            if (m_stopRequested)
                return;

            CleanupItem item;
            item.path = rootPath;
            item.sizeBytes = size;
            item.isDirectory = isDir;
            item.categoryId = category.id;
            item.selected = (category.risk == CleanupRisk::Safe);
            emit itemFound(item);
            *outBytes += size;
            *outCount += 1;
            continue;
        }

        QDir dir(rootPath);
        if (!dir.exists())
            continue;

        QStringList nameFilters = category.namePatterns;
        const QFileInfoList entries =
            nameFilters.isEmpty()
                ? dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden)
                : dir.entryInfoList(nameFilters, QDir::Files | QDir::System | QDir::Hidden);

        for (const QFileInfo& entry : entries)
        {
            if (m_stopRequested)
                return;

            bool isDir = false;
            qint64 size = 0;
            realEntryInfo(entry, &isDir, &size);
            if (isDir)
                size = directorySize(entry.absoluteFilePath(), m_stopRequested);
            if (m_stopRequested)
                return;

            CleanupItem item;
            item.path = entry.absoluteFilePath();
            item.sizeBytes = size;
            item.isDirectory = isDir;
            item.categoryId = category.id;
            item.selected = (category.risk == CleanupRisk::Safe);
            emit itemFound(item);
            *outBytes += size;
            *outCount += 1;
        }
    }
}
