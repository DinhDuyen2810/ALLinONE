#include "CleanupScanner.h"

#include "FsSafety.h"

#include <QDir>
#include <QFileInfo>

namespace
{
/// Tổng kích thước một thư mục (đệ quy) + thời điểm sửa MỚI NHẤT của các tệp bên trong. Dùng
/// FsSafety::walkFiles: kiểm tra cờ dừng ở từng mục (thư mục có thể rất lớn - Windows.old hàng chục GB)
/// và KHÔNG đi vào junction/symlink thư mục, bỏ qua lối tắt - không bao giờ cộng nhầm kích thước của một
/// thư mục đích nằm ngoài thư mục gốc đang quét.
qint64 directorySize(const QString& path, const std::atomic_bool& stopFlag, qint64* newestWriteTime = nullptr)
{
    qint64 total = 0;
    qint64 newest = 0;
    FsSafety::walkFiles(path, stopFlag, [&](const FsSafety::WalkEntry& e) {
        total += e.sizeBytes;
        if (e.lastWriteTime > newest)
            newest = e.lastWriteTime;
    });
    if (newestWriteTime)
        *newestWriteTime = newest;
    return total;
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
    const qint64 newestAllowed =
        category.minAgeSeconds > 0 ? FsSafety::nowFileTime() - static_cast<qint64>(category.minAgeSeconds) * 10000000LL : 0;

    // Trả về false nếu mục này KHÔNG được liệt kê (liên kết thư mục, quá mới, bị dừng giữa chừng...).
    auto emitItem = [&](const QString& path) -> bool {
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

        qint64 size = info.sizeBytes;
        qint64 newestWrite = info.lastWriteTime;
        if (info.isDir)
        {
            qint64 newestInside = 0;
            size = directorySize(path, m_stopRequested, &newestInside);
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

        if (!category.keepRootFolder)
        {
            // Toàn bộ rootPath là MỘT mục cần xóa (vd: Windows.old)
            emitItem(rootPath);
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
            emitItem(dir.absoluteFilePath(entry.fileName()));
        }
    }
}
