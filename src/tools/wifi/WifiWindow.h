#pragma once

#include <QWidget>

class QCloseEvent;
class QShowEvent;
class QComboBox;
class QLabel;
class QTabWidget;
class NetworksTab;
class ProfilesTab;
class SpeedTestTab;
class WlanController;

/// Cửa sổ WiFi Connection: quét/kết nối mạng, quản lý hồ sơ & mật khẩu đã lưu trên máy.
class WifiWindow : public QWidget
{
    Q_OBJECT

public:
    explicit WifiWindow(QWidget* parent = nullptr);
    ~WifiWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void onAdapterChanged(int index);
    void onTabChanged(int index);

private:
    void buildUi();
    void reloadAdapters();

    WlanController* m_controller{nullptr};
    bool m_serviceAvailable{false};
    bool m_shownOnce{false}; // lần hiện đầu tiên: constructor vừa nạp adapter xong, showEvent không nạp lại

    QComboBox* m_adapterCombo{nullptr};
    QLabel* m_warningLabel{nullptr};
    QTabWidget* m_tabs{nullptr};
    NetworksTab* m_networksTab{nullptr};
    ProfilesTab* m_profilesTab{nullptr};
    SpeedTestTab* m_speedTestTab{nullptr};
};
