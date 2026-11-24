#include "CleanupScanner.h"

#include "CategoryRegistry.h"
#include "CleanupSystem.h"
#include "FsSafety.h"
#include "LeftoverFinder.h"
#include "RecycleBinOps.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <climits>

namespace
{
/// Tổng kích thước một thư mục (đệ quy) + thời điểm sửa MỚI NHẤT của các tệp bên trong. Dùng
/// FsSafety::walkFiles: kiểm tra cờ dừng ở từng mục (thư mục có thể rất lớn - Windows.old hàng chục GB)
/// và KHÔNG đi vào junction/symlink thư mục, bỏ qua lối tắt - không bao giờ cộng nhầm kích thước của một
/// thư mục đích nằm ngoài thư mục gốc đang quét.
qint64 directorySize(const QString& path, const std::atomic_bool& stopFlag, qint64* newestWriteTime = nullptr,
                     int* fileCount = nullptr)
{
    qint64 total = 0;
    qint64 newest = 0;
    int files = 0;
    FsSafety::walkFiles(path, stopFlag, [&](const FsSafety::WalkEntry& e) {
        // Tệp chỉ có trên mây (OneDrive): chiếm ~0 byte trên đĩa - không phải dung lượng giải phóng được.
        if (!e.cloudOnly())
            total += e.sizeBytes;
        ++files;
        if (e.lastWriteTime > newest)
            newest = e.lastWriteTime;
    });
    if (newestWriteTime)
        *newestWriteTime = newest;
    if (fileCount)
        *fileCount = files;
    return total;
}

constexpr qint64 kFileTimeDay = 24LL * 3600LL * 10000000LL;

/// "3 ngày trước" / "5 tháng trước" / "2 năm trước" từ một FILETIME.
QString agoText(qint64 now, qint64 then)
{
    const qint64 days = then > 0 && now > then ? (now - then) / kFileTimeDay : 0;
    if (days < 60)
        return QString("%1 ngày trước").arg(days);
    if (days < 730)
        return QString("%1 tháng trước").arg(days / 30);
    return QString("%1 năm trước").arg(days / 365);
}

bool hasSystemAttribute(quint32 attrs)
{
    return (attrs & 0x4u) != 0; // FILE_ATTRIBUTE_SYSTEM
}

const QSet<QString>& installerSuffixes()
{
    static const QSet<QString> s{"exe", "msi", "msix", "msixbundle", "appx", "appxbundle", "iso"};
    return s;
}
} // namespace

CleanupScanner::CleanupScanner(QObject* parent)
    : QThread(parent)
{
}

CleanupScanner::~CleanupScanner()
{
    // Chờ KHÔNG giới hạn: vòng quét kiểm tra cờ dừng ở từng mục nên thoát nhanh, còn hủy một QThread
    // đang thực sự chạy là hành vi KHÔNG XÁC ĐỊNH (crash) - tệ hơn nhiều so với chờ thêm một nhịp.
    requestStop();
    wait();
}

void CleanupScanner::setCategories(const QList<CleanupCategory>& categories)
{
    m_categories = categories;
}

void CleanupScanner::requestStop()
{
    m_stopRequested = true;
}

void CleanupScanner::startScan(QThread::Priority priority)
{
    // Đặt lại cờ dừng TRƯỚC khi start(), không phải ở đầu run(): nếu đặt lại trong run() thì một
    // requestStop() gọi ngay sau start() (luồng mới chưa kịp chạy) sẽ bị chính run() xóa mất.
    if (isRunning())
        return; // lượt quét trước chưa thoát hẳn - không xóa cờ dừng đang chờ nó
    m_stopRequested = false;
    start(priority);
}

void CleanupScanner::run()
{
    qint64 grandTotalBytes = 0;
    int grandTotalItems = 0;

    for (const CleanupCategory& category : m_categories)
    {
        if (m_stopRequested)
            break;
        if (!CategoryRegistry::isAvailable(category))
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
    switch (category.kind)
    {
        case CleanupKind::RecycleBin:
            scanRecycleBin(category, outBytes, outCount);
            return;
        case CleanupKind::Hibernation:
            scanHibernation(category, outBytes, outCount);
            return;
        case CleanupKind::OldInstallers:
        case CleanupKind::BigOldDownloads:
            scanDownloads(category, outBytes, outCount);
            return;
        case CleanupKind::AppDataLeftovers:
            scanLeftovers(category, outBytes, outCount);
            return;
        case CleanupKind::FolderContents:
            break;
    }

    const qint64 newestAllowed =
        category.minAgeSeconds > 0 ? FsSafety::nowFileTime() - static_cast<qint64>(category.minAgeSeconds) * 10000000LL : 0;

    // Trả về false nếu mục này KHÔNG được liệt kê (liên kết thư mục, quá mới, bị dừng giữa chừng...).
    auto emitItem = [&](const QString& path, const QString& root) -> bool {
        // Thông tin của CHÍNH mục này, không để Qt đi theo lối tắt/symlink hộ (QFileInfo coi .lnk là
        // "symlink" và trả về kích thước/loại của ĐÍCH - lỗi thật đã gặp với hạng mục Recent Items).
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists)
            return false;
        // Junction/symlink THƯ MỤC nằm trong thư mục gốc: bỏ qua hẳn (không tính kích thước, không đưa
        // vào danh sách xóa) - đích của nó là dữ liệu ở NƠI KHÁC, không phải rác của hạng mục này, và
        // không thể bảo đảm từ đây rằng Shell chỉ gỡ liên kết mà không đụng tới nội dung đích.
        if (info.isDir && info.isLink)
            return false;

        qint64 size = FsSafety::WalkEntry::isCloudOnlyAttributes(info.attributes) ? 0 : info.sizeBytes;
        qint64 newestWrite = info.lastWriteTime;
        int files = info.isDir ? 0 : 1;
        if (info.isDir)
        {
            qint64 newestInside = 0;
            size = directorySize(path, m_stopRequested, &newestInside, &files);
            if (newestInside > newestWrite)
                newestWrite = newestInside;
        }
        if (m_stopRequested)
            return false;
        if (newestAllowed > 0 && newestWrite > newestAllowed)
            return false; // vừa được sửa - nhiều khả năng đang được một chương trình khác dùng

        CleanupItem item;
        item.path = path;
        item.sizeBytes = size;
        item.isDirectory = info.isDir;
        item.categoryId = category.id;
        item.selected = (category.risk == CleanupRisk::Safe);
        item.root = root;
        item.fileCount = files;
        emit itemFound(item);
        *outBytes += size;
        *outCount += 1;
        return true;
    };

    for (const QString& rootPath : category.rootPaths)
    {
        if (m_stopRequested)
            return;

        // Lớp chặn cuối cùng ngay tại nơi quét (CategoryRegistry đã lọc một lần): không bao giờ coi một
        // đường dẫn rỗng/tương đối/gốc ổ đĩa là "thư mục rác".
        if (!FsSafety::unsafeCleanupRootReason(rootPath, {}).isEmpty())
            continue;

        // Toàn bộ rootPath là MỘT mục cần xóa (vd: Windows.old), hoặc rootPath là một TỆP (vd %WINDIR%\MEMORY.DMP).
        const FsSafety::RawInfo rootInfo = FsSafety::rawInfo(rootPath);
        if (!category.keepRootFolder || (rootInfo.exists && !rootInfo.isDir))
        {
            emitItem(rootPath, rootPath);
            if (m_stopRequested)
                return;
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
            if (category.excludeNames.contains(entry.fileName(), Qt::CaseInsensitive))
                continue;
            // Ghép tên thủ công thay vì entry.absoluteFilePath(): giữ đúng đường dẫn của CHÍNH mục này.
            emitItem(dir.absoluteFilePath(entry.fileName()), rootPath);
        }
    }
}

void CleanupScanner::emitSpecial(const CleanupCategory& category, CleanupItem item, qint64* outBytes, int* outCount)
{
    item.categoryId = category.id;
    item.selected = (category.risk == CleanupRisk::Safe);
    emit itemFound(item);
    *outBytes += item.sizeBytes;
    *outCount += 1;
}

void CleanupScanner::scanRecycleBin(const CleanupCategory& category, qint64* outBytes, int* outCount)
{
    const RecycleBinOps::Status status = RecycleBinOps::queryStatus();
    if (status.itemCount <= 0 && status.totalBytes <= 0)
        return;
    CleanupItem item;
    item.path = QStringLiteral("::RecycleBin");
    item.sizeBytes = status.totalBytes;
    item.fileCount = static_cast<int>(qMin<qint64>(status.itemCount, INT_MAX));
    item.detail = QString("%1 mục").arg(status.itemCount);
    emitSpecial(category, item, outBytes, outCount);
}

void CleanupScanner::scanHibernation(const CleanupCategory& category, qint64* outBytes, int* outCount)
{
    for (const QString& path : category.searchRoots)
    {
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists || info.isDir || info.sizeBytes <= 0)
            continue;
        CleanupItem item;
        item.path = path;
        item.sizeBytes = info.sizeBytes;
        item.fileCount = 1;
        emitSpecial(category, item, outBytes, outCount);
    }
}

void CleanupScanner::scanDownloads(const CleanupCategory& category, qint64* outBytes, int* outCount)
{
    const qint64 now = FsSafety::nowFileTime();
    const bool installers = category.kind == CleanupKind::OldInstallers;
    for (const QString& root : category.searchRoots)
    {
        if (m_stopRequested)
            return;
        if (installers)
        {
            // CHỈ tệp nằm ngay trong Downloads: .exe trong một thư mục đã giải nén thường là ứng dụng chạy liền,
            // không phải bộ cài.
            QDir dir(root);
            for (const QString& name : dir.entryList(QDir::Files | QDir::Hidden))
            {
                if (m_stopRequested)
                    return;
                if (!installerSuffixes().contains(QFileInfo(name).suffix().toLower()))
                    continue;
                const QString path = root + QLatin1Char('/') + name;
                const FsSafety::RawInfo info = FsSafety::rawInfo(path);
                if (!info.exists || info.isDir || info.isLink || hasSystemAttribute(info.attributes) ||
                    FsSafety::WalkEntry::isCloudOnlyAttributes(info.attributes))
                    continue;
                const qint64 touched = qMax(info.creationTime, info.lastWriteTime);
                if (touched > now - 7 * kFileTimeDay)
                    continue;
                CleanupItem item;
                item.path = path;
                item.sizeBytes = info.sizeBytes;
                item.fileCount = 1;
                item.detail = "tải về " + agoText(now, touched);
                emitSpecial(category, item, outBytes, outCount);
            }
            continue;
        }
        // Tệp lớn lâu không đụng tới: cả thư mục con; lấy mốc MỚI NHẤT của tạo/sửa/mở (thận trọng: tệp vừa mở
        // tuần trước không phải "bị bỏ quên").
        constexpr qint64 kMinBig = 250LL * 1024 * 1024;
        QList<CleanupItem> found;
        FsSafety::walkFiles(root, m_stopRequested, [&](const FsSafety::WalkEntry& e) {
            if (e.sizeBytes < kMinBig || e.cloudOnly() || hasSystemAttribute(e.attributes))
                return;
            if (installerSuffixes().contains(QFileInfo(e.path).suffix().toLower()) &&
                QFileInfo(e.path).path().compare(root, Qt::CaseInsensitive) == 0)
                return; // bộ cài ngay trong Downloads: đã thuộc hạng mục "Bộ cài đặt cũ"
            const qint64 touched = qMax(qMax(e.creationTime, e.lastWriteTime), e.lastAccessTime);
            if (touched > now - 30 * kFileTimeDay)
                return;
            CleanupItem item;
            item.path = e.path;
            item.sizeBytes = e.sizeBytes;
            item.fileCount = 1;
            item.detail = "không đụng tới từ " + agoText(now, touched);
            found << item;
        });
        if (m_stopRequested)
            return;
        std::sort(found.begin(), found.end(), [](const CleanupItem& a, const CleanupItem& b) { return a.sizeBytes > b.sizeBytes; });
        for (const CleanupItem& item : found)
            emitSpecial(category, item, outBytes, outCount);
    }
}

void CleanupScanner::scanLeftovers(const CleanupCategory& category, qint64* outBytes, int* outCount)
{
    const QString localAppData = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
    const QSet<QString> keys = LeftoverFinder::nameKeys(LeftoverFinder::installedRawNames(localAppData));
    const qint64 now = FsSafety::nowFileTime();
    const QList<LeftoverFinder::Leftover> found =
        LeftoverFinder::find(category.searchRoots, keys, now, m_stopRequested, FsSafety::systemProtectedDirs());
    for (const LeftoverFinder::Leftover& l : found)
    {
        if (m_stopRequested)
            return;
        CleanupItem item;
        item.path = l.path;
        item.sizeBytes = l.sizeBytes;
        item.isDirectory = true;
        item.fileCount = l.fileCount;
        item.detail = QString("%1 tệp, không thay đổi từ %2").arg(l.fileCount).arg(agoText(now, l.newestWriteTime));
        emitSpecial(category, item, outBytes, outCount);
    }
}
