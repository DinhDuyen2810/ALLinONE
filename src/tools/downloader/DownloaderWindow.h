#pragma once

#include <QWidget>

class QTabWidget;
class DirectDownloadTab;
class DownloadHistoryStore;
class DownloadHistoryTab;
class DownloadQueue;
class DownloaderSettings;
class DownloaderSettingsTab;
class PageScanTab;
class PlatformVideoTab;
class FileDownloader;

/// Cửa sổ Downloader: tải trực tiếp (hàng đợi hợp nhất: dán link -> kiểm tra OK/ERR -> chọn và tải), quét trang web,
/// tải video nền tảng (YouTube/Facebook/TikTok... qua yt-dlp), lịch sử tải và cài đặt (thư mục tải...).
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
    DirectDownloadTab* m_directTab{nullptr};
    PageScanTab* m_pageScanTab{nullptr};
    PlatformVideoTab* m_platformTab{nullptr};
    DownloadHistoryTab* m_historyTab{nullptr};
    DownloaderSettingsTab* m_settingsTab{nullptr};
};
