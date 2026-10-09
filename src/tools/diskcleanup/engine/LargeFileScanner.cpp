#include "LargeFileScanner.h"

#include "FsSafety.h"

#include <algorithm>

LargeFileScanner::LargeFileScanner(QObject* parent)
    : QThread(parent)
{
}

LargeFileScanner::~LargeFileScanner()
{
    // Chờ KHÔNG giới hạn - xem CleanupScanner::~CleanupScanner (cờ dừng được kiểm tra ở từng mục).
    requestStop();
    wait();
}

void LargeFileScanner::requestStop()
{
    m_stopRequested = true;
}

void LargeFileScanner::startScan(QThread::Priority priority)
{
    if (isRunning())
        return;
    m_stopRequested = false;
    start(priority);
}

void LargeFileScanner::run()
{
    QList<LargeFileEntry> results;

    if (!m_rootPath.isEmpty())
    {
        qint64 scanned = 0;
        // FsSafety::walkFiles: không đi vào junction/symlink thư mục, bỏ qua lối tắt - kết quả chỉ gồm
        // tệp THẬT nằm trong cây thư mục đã chọn (một junction trỏ sang ổ khác từng có thể đưa tệp ở nơi
        // hoàn toàn khác vào danh sách "xóa vào Thùng rác").
        FsSafety::walkFiles(m_rootPath, m_stopRequested, [&](const FsSafety::WalkEntry& file) {
            if (file.sizeBytes >= m_minSizeBytes)
            {
                LargeFileEntry e;
                e.path = file.path;
                e.sizeBytes = file.sizeBytes;
                // FILETIME (100ns từ 1601-01-01 UTC) -> QDateTime giờ địa phương.
                e.lastModified = QDateTime::fromMSecsSinceEpoch(file.lastWriteTime / 10000 - 11644473600000LL);
                results.push_back(e);
            }

            // Cứ mỗi 200 tệp thì báo tiến độ (kèm đường dẫn hiện tại) - quét cả ổ đĩa hệ thống có thể gặp
            // hàng trăm nghìn tệp nên cần phản hồi đủ dày để người dùng thấy ứng dụng vẫn đang chạy,
            // không bị tưởng nhầm là treo ("Không phản hồi").
            if ((++scanned % 200) == 0)
                emit progressTick(scanned, file.path);
        });
    }

    if (m_stopRequested)
    {
        emit scanStopped();
        return;
    }

    std::sort(results.begin(), results.end(),
             [](const LargeFileEntry& a, const LargeFileEntry& b) { return a.sizeBytes > b.sizeBytes; });
    if (results.size() > m_maxResults)
        results.resize(m_maxResults);

    emit scanFinished(results);
}
