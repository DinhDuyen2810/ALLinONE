#include "LargeFileScanner.h"

#include <QDirIterator>
#include <algorithm>

LargeFileScanner::LargeFileScanner(QObject* parent)
    : QThread(parent)
{
}

LargeFileScanner::~LargeFileScanner()
{
    requestStop();
    wait(5000);
}

void LargeFileScanner::requestStop()
{
    m_stopRequested = true;
}

void LargeFileScanner::run()
{
    m_stopRequested = false;
    QList<LargeFileEntry> results;

    if (!m_rootPath.isEmpty())
    {
        QDirIterator it(m_rootPath, QDir::Files | QDir::System | QDir::Hidden | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
        qint64 scanned = 0;
        while (it.hasNext())
        {
            const QFileInfo info = it.nextFileInfo(); // phải gọi trước: filePath()/fileInfo() chỉ phản ánh đúng mục VỪA next() xong
            if (info.size() >= m_minSizeBytes)
            {
                LargeFileEntry e;
                e.path = info.absoluteFilePath();
                e.sizeBytes = info.size();
                e.lastModified = info.lastModified();
                results.push_back(e);
            }

            // Cứ mỗi 200 tệp thì kiểm tra dừng + báo tiến độ (kèm đường dẫn hiện tại) - quét cả ổ đĩa
            // hệ thống có thể gặp hàng trăm nghìn tệp nên cần phản hồi đủ dày để người dùng thấy ứng
            // dụng vẫn đang chạy, không bị tưởng nhầm là treo ("Không phản hồi").
            if ((++scanned % 200) == 0)
            {
                if (m_stopRequested)
                {
                    emit scanStopped();
                    return;
                }
                emit progressTick(scanned, info.absoluteFilePath());
            }
        }
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
