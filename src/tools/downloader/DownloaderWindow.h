#pragma once

#include <QWidget>

class QTabWidget;
class DirectDownloadTab;
class PageScanTab;
class PlatformVideoTab;
class FileDownloader;

/// Cửa sổ Downloader: tải trực tiếp (dán link tệp), quét trang web (tìm media/tài liệu trong 1 trang),
/// và tải video nền tảng (YouTube/Facebook/TikTok... qua yt-dlp).
class DownloaderWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DownloaderWindow(QWidget* parent = nullptr);
    ~DownloaderWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    FileDownloader* m_downloader{nullptr}; ///< Dùng CHUNG giữa DirectDownloadTab và PageScanTab
    DirectDownloadTab* m_directTab{nullptr};
    PageScanTab* m_pageScanTab{nullptr};
    PlatformVideoTab* m_platformTab{nullptr};
};
