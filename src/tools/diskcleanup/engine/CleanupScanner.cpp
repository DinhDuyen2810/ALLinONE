#include "CleanupScanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace
{
/// Tính tổng kích thước một thư mục (đệ quy). Kiểm tra cờ dừng định kỳ vì thư mục có thể rất lớn
/// (Windows.old có thể hàng chục GB).
qint64 directorySize(const QString& path, const std::atomic_bool& stopFlag)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::System | QDir::Hidden, QDirIterator::Subdirectories);
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
            const qint64 size = info.isDir() ? directorySize(rootPath, m_stopRequested) : info.size();
            if (m_stopRequested)
                return;

            CleanupItem item;
            item.path = rootPath;
            item.sizeBytes = size;
            item.isDirectory = info.isDir();
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

            const qint64 size = entry.isDir() ? directorySize(entry.absoluteFilePath(), m_stopRequested)
                                              : entry.size();
            if (m_stopRequested)
                return;

            CleanupItem item;
            item.path = entry.absoluteFilePath();
            item.sizeBytes = size;
            item.isDirectory = entry.isDir();
            item.categoryId = category.id;
            item.selected = (category.risk == CleanupRisk::Safe);
            emit itemFound(item);
            *outBytes += size;
            *outCount += 1;
        }
    }
}
