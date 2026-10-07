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
            if ((++scanned & 0x3FF) == 0) // cứ mỗi 1024 tệp thì kiểm tra dừng + báo tiến độ, tránh gọi quá dày
            {
                if (m_stopRequested)
                {
                    emit scanStopped();
                    return;
                }
                emit progressTick(scanned);
            }

            const QFileInfo info = it.nextFileInfo();
            if (info.size() >= m_minSizeBytes)
            {
                LargeFileEntry e;
                e.path = info.absoluteFilePath();
                e.sizeBytes = info.size();
                e.lastModified = info.lastModified();
                results.push_back(e);
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
