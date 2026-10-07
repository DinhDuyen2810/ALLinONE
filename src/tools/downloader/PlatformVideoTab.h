#pragma once

#include "model/VideoInfo.h"

#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class YtDlpInfoWorker;
class YtDlpDownloadWorker;

/// Tab "Video nền tảng": tải video từ YouTube/Facebook/TikTok/... (hơn 1000 trang yt-dlp hỗ trợ) qua
/// `yt-dlp.exe` đóng gói kèm theo ứng dụng - xem YtDlpController.h để biết vì sao không tự viết lại.
class PlatformVideoTab : public QWidget
{
    Q_OBJECT

public:
    explicit PlatformVideoTab(QWidget* parent = nullptr);
    ~PlatformVideoTab() override;

private slots:
    void onFetchInfoClicked();
    void onInfoFetched(bool ok, VideoInfo info, QString error);
    void onChooseFolderClicked();
    void onDownloadClicked();
    void onDownloadProgress(qint64 downloaded, qint64 total, qint64 speed, qint64 eta);
    void onDownloadFinished(bool ok, QString error);

private:
    void buildUi();
    void updateBundleBanner();
    void setBusyFetching(bool busy);
    void setBusyDownloading(bool busy);

    QLabel* m_bundleBanner{nullptr};
    QLineEdit* m_urlEdit{nullptr};
    QPushButton* m_fetchBtn{nullptr};
    QLabel* m_disclaimerLabel{nullptr};

    QLabel* m_titleLabel{nullptr};
    QComboBox* m_formatCombo{nullptr};
    QLabel* m_folderLabel{nullptr};
    QPushButton* m_chooseFolderBtn{nullptr};
    QPushButton* m_downloadBtn{nullptr};

    QProgressBar* m_progressBar{nullptr};
    QLabel* m_progressLabel{nullptr};

    YtDlpInfoWorker* m_infoWorker{nullptr};
    YtDlpDownloadWorker* m_downloadWorker{nullptr};

    VideoInfo m_currentInfo;
    QString m_saveFolder;
};
