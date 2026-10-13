#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

/// Một dòng lịch sử tải (một tệp/video đã tải xong, thất bại hoặc bị hủy).
struct DownloadHistoryEntry
{
    QDateTime time;
    QString title;      ///< Tên hiển thị
    QString fileName;
    QString path;       ///< Đường dẫn tệp đã lưu (rỗng nếu thất bại hoặc chưa biết)
    QString folder;     ///< Thư mục chứa tệp (để mở thư mục kể cả khi không biết đúng tên tệp)
    QString sourceUrl;  ///< Link người dùng dán / trang nguồn
    QString mediaUrl;   ///< Link tệp thật
    QString status;     ///< "Hoàn tất" / "Thất bại" / "Đã hủy"
    QString error;
    qint64 size{-1};
    QString kind;       ///< "http" / "yt-dlp"
};

/// Lịch sử tải của công cụ Downloader, lưu ở `AppPaths::profileFile("downloader_history.json")` - tức
/// `%LOCALAPPDATA%\OneForAll\profiles\downloader_history.json` (tối đa 2000 mục, mới nhất đứng đầu). Một đối tượng
/// do DownloaderWindow sở hữu và chia sẻ cho hàng đợi, tab Lịch sử, tab Video nền tảng.
class DownloadHistoryStore : public QObject
{
    Q_OBJECT

public:
    static constexpr int kMaxEntries = 2000;

    explicit DownloadHistoryStore(QObject* parent = nullptr);
    explicit DownloadHistoryStore(const QString& path, QObject* parent = nullptr);

    const QList<DownloadHistoryEntry>& entries() const { return m_entries; }
    void add(const DownloadHistoryEntry& entry);
    void removeAt(int index);
    void clear();
    QString filePath() const { return m_path; }

signals:
    void changed();

private:
    void load();
    bool save();

    QString m_path;
    QList<DownloadHistoryEntry> m_entries;
};
