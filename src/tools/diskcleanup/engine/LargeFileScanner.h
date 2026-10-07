#pragma once

#include <QDateTime>
#include <QString>
#include <QThread>
#include <atomic>

/// Một tệp lớn tìm thấy khi quét một thư mục gốc theo yêu cầu người dùng (khác CleanupItem - đây là
/// công cụ "tìm tệp lớn" tự do, không gắn với hạng mục dọn dẹp định sẵn nào).
struct LargeFileEntry
{
    QString path;
    qint64 sizeBytes{0};
    QDateTime lastModified;
};

/**
 * @brief Quét một thư mục gốc (do người dùng chọn, ví dụ cả ổ C:) tìm các tệp lớn hơn ngưỡng cho
 * trước. Chạy trên QThread riêng vì có thể quét rất nhiều tệp.
 */
class LargeFileScanner : public QThread
{
    Q_OBJECT

public:
    explicit LargeFileScanner(QObject* parent = nullptr);
    ~LargeFileScanner() override;

    void setRootPath(const QString& path) { m_rootPath = path; }
    void setMinSizeBytes(qint64 bytes) { m_minSizeBytes = bytes; }
    void setMaxResults(int max) { m_maxResults = max; }
    void requestStop();

signals:
    void progressTick(qint64 filesScanned, QString currentPath);
    void scanFinished(QList<LargeFileEntry> results);
    void scanStopped();

protected:
    void run() override;

private:
    QString m_rootPath;
    qint64 m_minSizeBytes{100 * 1024 * 1024}; // mặc định 100MB
    int m_maxResults{200};
    std::atomic_bool m_stopRequested{false};
};
