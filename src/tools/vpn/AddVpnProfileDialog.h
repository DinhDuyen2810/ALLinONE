#pragma once

#include <QDialog>

#include "model/DirectVpnProfile.h"
#include "model/VpnProfile.h"

class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;

/// Hộp thoại thêm một hồ sơ VPN, có 3 CHẾ ĐỘ (chọn ở ô "Chế độ"):
///  - Thường: VPN hệ thống của Windows (IKEv2/L2TP/SSTP/PPTP) - người dùng tự nhập địa chỉ máy chủ + tên tài khoản từ chính
///    nhà cung cấp VPN họ đã đăng ký (hoặc VPN cơ quan/tự dựng riêng). Không có máy chủ nào được điền sẵn.
///  - GlobalProtect: hồ sơ kết nối TRỰC TIẾP tới portal GlobalProtect (Palo Alto) - tài khoản/OTP nhập lúc kết nối.
///  - FortiClient: hồ sơ kết nối TRỰC TIẾP tới FortiGate - SSL-VPN (máy chủ[:cổng]) hoặc IPsec (IKEv1 + PSK, XAuth).
/// Hai chế độ sau được lưu vào kho hồ sơ riêng của ứng dụng (engine/DirectProfileStore.h) và hiện ở tab "VPN đã cài sẵn".
class AddVpnProfileDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Mode
    {
        Normal,
        GlobalProtect,
        Forticlient,
    };

    explicit AddVpnProfileDialog(QWidget* parent = nullptr);

    Mode mode() const;
    /// Chế độ Thường: hồ sơ VPN hệ thống Windows.
    VpnProfile profile() const;
    /// Chế độ GlobalProtect/FortiClient: hồ sơ kết nối trực tiếp (chưa chuẩn hóa/kiểm định - DirectProfileStore::add làm việc đó).
    DirectVpnProfile directProfile() const;

    /// Chỉ chấp nhận (đóng hộp thoại) khi thông tin của chế độ đang chọn hợp lệ; sai thì hiện lỗi ngay trong hộp thoại.
    void accept() override;

private:
    void updateModeUi();
    bool validateInput(QString* error) const;

    QComboBox* m_modeCombo{nullptr};
    QLabel* m_modeHint{nullptr};
    QLabel* m_errorLabel{nullptr};
    QLabel* m_noteLabel{nullptr};
    QFormLayout* m_form{nullptr};
    QLineEdit* m_nameEdit{nullptr};
    // Chế độ Thường
    QLineEdit* m_countryEdit{nullptr};
    QLineEdit* m_serverEdit{nullptr};
    QComboBox* m_tunnelTypeCombo{nullptr};
    QLineEdit* m_usernameEdit{nullptr};
    QLineEdit* m_pskEdit{nullptr};
    // Chế độ GlobalProtect
    QLineEdit* m_gpPortalEdit{nullptr};
    // Chế độ FortiClient
    QComboBox* m_fortiTypeCombo{nullptr};
    QLineEdit* m_fortiServerEdit{nullptr};
};
