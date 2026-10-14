#pragma once

#include <QWidget>

class QTabWidget;
class DownloadTab;
class DownloadHistoryStore;
class DownloadHistoryTab;
class DownloadQueue;
class DownloaderSettings;
class DownloaderSettingsTab;
class FileDownloader;

/// Cửa sổ Downloader: tab Tải xuống (hàng đợi hợp nhất: dán link/quét trang -> kiểm tra OK/ERR -> chọn và tải; gộp tải trực
/// tiếp, quét trang web và video nền tảng YouTube/Facebook/TikTok... qua yt-dlp), lịch sử tải và cài đặt (thư mục tải...).
class DownloaderWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DownloaderWindow(QWidget* parent = nullptr);
    ~DownloaderWindow() override;

    // Truy cập cho kiểm thử giao diện.
    DownloadQueue* queue() const { return m_queue; }
    DownloaderSettings* settings() const { return m_settings; }
    DownloadHistoryStore* history() const { return m_history; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    DownloaderSettings* m_settings{nullptr};      ///< Cài đặt CHUNG cho mọi tab
    DownloadHistoryStore* m_history{nullptr};     ///< Lịch sử CHUNG
    FileDownloader* m_downloader{nullptr};        ///< Tải tệp HTTP, dùng chung qua hàng đợi
    DownloadQueue* m_queue{nullptr};              ///< Hàng đợi hợp nhất
    DownloadTab* m_downloadTab{nullptr};
    DownloadHistoryTab* m_historyTab{nullptr};
    DownloaderSettingsTab* m_settingsTab{nullptr};
};
