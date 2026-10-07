#pragma once

#include <QDialog>

#include "model/VpnProfile.h"

class QComboBox;
class QLineEdit;

/// Hộp thoại thêm một hồ sơ VPN - người dùng tự nhập địa chỉ máy chủ + tên tài khoản từ chính nhà
/// cung cấp VPN họ đã đăng ký (hoặc VPN cơ quan/tự dựng riêng). Không có máy chủ nào được điền sẵn.
class AddVpnProfileDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AddVpnProfileDialog(QWidget* parent = nullptr);

    VpnProfile profile() const;

private:
    QLineEdit* m_nameEdit{nullptr};
    QLineEdit* m_countryEdit{nullptr};
    QLineEdit* m_serverEdit{nullptr};
    QComboBox* m_tunnelTypeCombo{nullptr};
    QLineEdit* m_usernameEdit{nullptr};
};
