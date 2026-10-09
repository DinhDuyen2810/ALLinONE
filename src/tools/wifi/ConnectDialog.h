#pragma once

#include <QDialog>

#include "engine/WlanController.h"
#include "model/WifiNetwork.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;

/**
 * @brief Hộp thoại kết nối WiFi: nhập ĐÚNG MỘT mật khẩu rồi kết nối, giống Windows.
 * Không có bất kỳ chế độ thử nhiều mật khẩu nào: mỗi lần bấm "Kết nối" gọi
 * WlanController::connectWithPassword() đúng một lần với mật khẩu đang nằm trong ô nhập, và nút bị khóa
 * cho tới khi lần đó có kết quả (thành công / hết giờ / người dùng hủy).
 *
 * Lần kết nối không thành công (hết giờ, bấm Hủy, đóng hộp thoại giữa chừng) được HOÀN TÁC: hồ sơ cũ của
 * mạng (nếu có) được khôi phục, hồ sơ mới tạo thì bị xóa - xem WlanController::rollbackProfile().
 */
class ConnectDialog : public QDialog
{
    Q_OBJECT

public:
    /// network: mạng đã quét được (ssid/security điền sẵn). adapterGuid: adapter dùng để kết nối.
    ConnectDialog(WlanController* controller, const QString& adapterGuid, const WifiNetwork& network, QWidget* parent = nullptr);

    /// Dựng hộp thoại cho "Kết nối mạng ẩn" (người dùng tự nhập SSID). security: loại bảo mật chọn sẵn.
    static ConnectDialog* forHiddenNetwork(WlanController* controller, const QString& adapterGuid, QWidget* parent = nullptr,
                                           WifiSecurity security = WifiSecurity::Wpa2Psk);

    /// Mọi đường đóng hộp thoại (Hủy, Esc, nút X, accept) đều qua đây - nơi hoàn tác lần kết nối còn dở.
    void done(int result) override;

signals:
    void connected(QString ssid);

private slots:
    void onTogglePasswordVisibility();
    void onConnectClicked();
    void pollConnectionStatus();

private:
    enum class Phase
    {
        Idle,              // chưa gửi gì / lần trước đã có kết quả
        WaitingDisconnect, // đang chờ ngắt khỏi chính SSID này trước khi kết nối lại bằng mật khẩu mới
        WaitingConnect     // đã gửi yêu cầu kết nối, chờ kết quả
    };

    void buildUi();
    void setBusy(bool busy, const QString& message = QString());
    void startConnect();
    /// Hoàn tác thay đổi của lần kết nối đang dở; trả về câu mô tả việc đã làm để ghép vào thông báo.
    QString rollbackPending();

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
    int m_elapsedMs{0};
    Phase m_phase{Phase::Idle};
    QString m_attemptSsid;             // SSID của lần kết nối đang chờ kết quả
    WifiProfileBackup m_backup;        // hồ sơ cũ để khôi phục nếu lần này thất bại
    QString m_previousProfileName;     // hồ sơ đang dùng mà hộp thoại đã chủ động ngắt (để nối lại nếu thất bại)
};
