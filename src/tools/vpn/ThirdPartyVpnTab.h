#pragma once

#include <QList>
#include <QWidget>

#include "model/ThirdPartyVpnProfile.h"

class QLabel;
class QPushButton;
class QTableWidget;
class DirectVpnSession;

/// Tab "VPN đã cài sẵn": phát hiện các hồ sơ VPN đã cấu hình bởi phần mềm bên thứ ba (FortiClient,
/// GlobalProtect, OpenVPN, Cisco Secure Client). Hồ sơ FortiClient SSL-VPN và GlobalProtect KẾT NỐI TRỰC TIẾP
/// được ngay trong ứng dụng (nhập tài khoản ở đây, không cần mở phần mềm của họ - xem direct/); mọi hồ sơ
/// vẫn "Mở phần mềm" được. Chỉ đọc tên + địa chỉ máy chủ, KHÔNG đọc/giải mã mật khẩu đã lưu của phần mềm đó.
class ThirdPartyVpnTab : public QWidget
{
    Q_OBJECT

public:
    explicit ThirdPartyVpnTab(QWidget* parent = nullptr);
    ~ThirdPartyVpnTab() override;

    /// Đang có phiên kết nối trực tiếp (đang nối hoặc đã nối).
    bool isDirectActive() const;
    /// Ngắt phiên trực tiếp và chờ tiến trình phụ gỡ tuyến/adapter (đóng cửa sổ/thoát ứng dụng).
    void shutdownDirect(int waitMs);

private slots:
    void onRefreshClicked();
    void onOpenClicked();
    void onDirectConnectClicked();
    void onDirectDisconnectClicked();
    void onForgetCredentialsClicked();
    void onRowSelectionChanged();

private:
    void buildUi();
    void reload();
    void updateButtons();
    const ThirdPartyVpnProfile* selected() const;
    bool askCredentials(const ThirdPartyVpnProfile& p, QString* user, QString* pass, QString* psk, bool* save);
    void clearPendingCredentials();

    QTableWidget* m_table{nullptr}; // [Phần mềm][Tên hồ sơ][Máy chủ][Kết nối trực tiếp]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_openBtn{nullptr};
    QPushButton* m_directBtn{nullptr};
    QPushButton* m_directStopBtn{nullptr};
    QPushButton* m_forgetBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_directLabel{nullptr};

    QList<ThirdPartyVpnProfile> m_profiles;
    DirectVpnSession* m_session{nullptr};
    QString m_activeName;   ///< tên hồ sơ của phiên trực tiếp đang chạy
    QString m_activeServer; ///< máy chủ của phiên đang chạy (khóa của tài khoản đã lưu)
    bool m_usingSaved{false};  ///< phiên này dùng tài khoản đã lưu trong Windows Credential Manager
    /// Người dùng tích "Lưu": CHỈ lưu sau khi đăng nhập THÀNH CÔNG (không lưu mật khẩu sai) - giữ tạm trong bộ nhớ
    /// tới lúc đó rồi xóa trắng.
    bool m_pendingSave{false};
    QString m_pendingUser;
    QString m_pendingPass;
    QString m_pendingPsk;       ///< PSK chờ lưu (FortiGate IPsec)
    bool m_activeIsIpsec{false}; ///< phiên đang chạy là IPsec (cần lưu/đọc PSK)
};
