#pragma once

#include <QDialog>

#include "model/WifiNetwork.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class WlanController;

/**
 * @brief Hộp thoại kết nối WiFi: nhập ĐÚNG MỘT mật khẩu rồi kết nối, giống Windows.
 * Không có bất kỳ chế độ thử nhiều mật khẩu nào.
 */
class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    /// network: mạng đã quét được (ssid/security điền sẵn). adapterGuid: adapter dùng để kết nối.
    ConnectDialog(WlanController* controller, const QString& adapterGuid, const WifiNetwork& network, QWidget* parent = nullptr);

    /// Dựng hộp thoại cho "Kết nối mạng ẩn" (người dùng tự nhập SSID).
    static ConnectDialog* forHiddenNetwork(WlanController* controller, const QString& adapterGuid, QWidget* parent = nullptr);

signals:
    void connected(QString ssid);

private slots:
    void onTogglePasswordVisibility();
    void onConnectClicked();
    void pollConnectionStatus();

private:
    void buildUi();
    void setBusy(bool busy, const QString& message = QString());

    WlanController* m_controller;
    QString m_adapterGuid;
    WifiNetwork m_network;
    bool m_hiddenMode{false};

    QLineEdit* m_ssidEdit{nullptr};
    QComboBox* m_securityCombo{nullptr};
    QLineEdit* m_passwordEdit{nullptr};
    QPushButton* m_toggleVisBtn{nullptr};
    QCheckBox* m_autoConnectCheck{nullptr};
    QLabel* m_statusLabel{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};

    QTimer* m_pollTimer{nullptr};
    int m_pollAttempts{0};
};
