#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class DownloaderSettings;

/// Tab "Cài đặt" của Downloader: thư mục tải xuống (dùng chung cho mọi tab), số tệp tải đồng thời, giới hạn số mục khi
/// tải cả hồ sơ/kênh, bật/tắt trình duyệt ẩn để quét sâu, và cookie đăng nhập cho yt-dlp (Instagram/Facebook).
/// Mọi thay đổi được lưu NGAY (xem DownloaderSettings) - không có nút "Lưu".
class DownloaderSettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit DownloaderSettingsTab(DownloaderSettings* settings, QWidget* parent = nullptr);

private slots:
    void onChooseFolder();
    void onOpenFolder();
    void onResetFolder();
    void onChooseCookieFile();
    void onCookieControlsChanged();
    void refreshFromSettings();

private:
    void buildUi();

    DownloaderSettings* m_settings{nullptr};
    QLabel* m_folderLabel{nullptr};
    QSpinBox* m_concurrentSpin{nullptr};
    QSpinBox* m_maxItemsSpin{nullptr};
    QCheckBox* m_browserCheck{nullptr};
    QLabel* m_browserLabel{nullptr};
    QRadioButton* m_cookieNone{nullptr};
    QRadioButton* m_cookieBrowser{nullptr};
    QRadioButton* m_cookieFile{nullptr};
    QComboBox* m_cookieBrowserCombo{nullptr};
    QLineEdit* m_cookieFileEdit{nullptr};
    QPushButton* m_cookieFileBtn{nullptr};
    bool m_refreshing{false};
};
