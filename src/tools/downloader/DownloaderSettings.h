#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

/// Cài đặt của công cụ Downloader, lưu bền ở `%LOCALAPPDATA%\OneForAll\profiles\downloader_settings.json` (qua
/// AppPaths - không dùng đường dẫn tương đối). MỌI tab dùng CHUNG một đối tượng này nên đổi thư mục tải ở một nơi là
/// áp dụng cho tất cả. Mỗi setter tự lưu và phát changed().
class DownloaderSettings : public QObject
{
    Q_OBJECT

public:
    enum class CookieMode { None, Browser, File };

    /// Đọc từ tệp mặc định của người dùng.
    explicit DownloaderSettings(QObject* parent = nullptr);
    /// Dùng một tệp cụ thể (test).
    explicit DownloaderSettings(const QString& filePath, QObject* parent = nullptr);

    static QString defaultDownloadFolder();

    QString downloadFolder() const { return m_folder; }
    void setDownloadFolder(const QString& folder);

    /// Số tệp HTTP tải đồng thời (1-8, mặc định 3).
    int maxConcurrent() const { return m_maxConcurrent; }
    void setMaxConcurrent(int n);

    /// Số mục tối đa lấy từ một hồ sơ/kênh/danh sách phát (1-1000, mặc định 100).
    int profileMaxItems() const { return m_profileMaxItems; }
    void setProfileMaxItems(int n);

    /// Cho phép dùng Edge/Chrome ẩn để quét sâu trang (mặc định bật).
    bool useBrowserFallback() const { return m_useBrowser; }
    void setUseBrowserFallback(bool on);

    /// Cookie cho yt-dlp (Instagram/Facebook cần đăng nhập): lấy từ trình duyệt hoặc từ tệp cookies.txt.
    CookieMode cookieMode() const { return m_cookieMode; }
    QString cookieBrowser() const { return m_cookieBrowser; }
    QString cookieFile() const { return m_cookieFile; }
    void setCookies(CookieMode mode, const QString& browser, const QString& file);
    /// Đối số yt-dlp ứng với cài đặt cookie (rỗng nếu không dùng / tệp không tồn tại).
    QStringList ytDlpExtraArguments() const;

    /// Trình duyệt hợp lệ cho `--cookies-from-browser`.
    static QStringList supportedCookieBrowsers();

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void changed();

private:
    void load();
    void save() const;

    QString m_filePath;
    QString m_folder;
    int m_maxConcurrent{3};
    int m_profileMaxItems{100};
    bool m_useBrowser{true};
    CookieMode m_cookieMode{CookieMode::None};
    QString m_cookieBrowser{"firefox"};
    QString m_cookieFile;
};
