#include "AddVpnProfileDialog.h"

#include "VpnUiStyle.h"
#include "ui/widgets/ComboBoxStyle.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

AddVpnProfileDialog::AddVpnProfileDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Thêm hồ sơ VPN");
    setMinimumWidth(420);
    setStyleSheet(
        "QDialog { background-color: #f6f8fa; }"
        "QLabel { color: #1f2328; background: transparent; }" + VpnUi::inputStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    auto* hint = new QLabel(
        "Nhập thông tin máy chủ VPN từ chính nhà cung cấp bạn đã đăng ký (hoặc VPN cơ quan). Ứng dụng "
        "KHÔNG cung cấp sẵn máy chủ VPN nào - đây chỉ là nơi quản lý/kết nối các hồ sơ bạn tự khai báo.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText("vd: Hà Lan - Mullvad");
    form->addRow("Tên hồ sơ:", m_nameEdit);

    m_countryEdit = new QLineEdit(this);
    m_countryEdit->setPlaceholderText("vd: Hà Lan (chỉ để hiển thị)");
    form->addRow("Quốc gia:", m_countryEdit);

    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText("vd: nl123.vpnprovider.com hoặc IP");
    form->addRow("Địa chỉ máy chủ:", m_serverEdit);

    m_tunnelTypeCombo = new QComboBox(this);
    m_tunnelTypeCombo->addItem("Tự động", static_cast<int>(VpnTunnelType::Automatic));
    m_tunnelTypeCombo->addItem("IKEv2 (khuyến nghị)", static_cast<int>(VpnTunnelType::Ikev2));
    m_tunnelTypeCombo->addItem("L2TP/IPsec", static_cast<int>(VpnTunnelType::L2tp));
    m_tunnelTypeCombo->addItem("SSTP", static_cast<int>(VpnTunnelType::Sstp));
    m_tunnelTypeCombo->addItem("PPTP (cũ, kém an toàn)", static_cast<int>(VpnTunnelType::Pptp));
    m_tunnelTypeCombo->setCurrentIndex(1); // IKEv2 mặc định - tương thích rộng, an toàn
    form->addRow("Giao thức:", m_tunnelTypeCombo);

    // Chỉ có nghĩa với L2TP/IPsec - trước đây không có ô này nên máy chủ L2TP dùng khóa chia sẻ trước (loại
    // phổ biến nhất) không bao giờ kết nối được.
    m_pskEdit = new QLineEdit(this);
    m_pskEdit->setObjectName("l2tpPskEdit");
    m_pskEdit->setEchoMode(QLineEdit::Password);
    m_pskEdit->setPlaceholderText("Khóa chia sẻ trước (PSK) - chỉ cho L2TP/IPsec");
    form->addRow("Khóa L2TP (PSK):", m_pskEdit);
    const auto updatePsk = [this]() {
        const bool l2tp = static_cast<VpnTunnelType>(m_tunnelTypeCombo->currentData().toInt()) == VpnTunnelType::L2tp;
        m_pskEdit->setEnabled(l2tp);
    };
    connect(m_tunnelTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updatePsk);
    updatePsk();

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("Tên đăng nhập VPN (không bắt buộc)");
    form->addRow("Tên đăng nhập:", m_usernameEdit);

    root->addLayout(form);

    auto* note = new QLabel("Mật khẩu sẽ được hỏi riêng mỗi lần kết nối - không lưu trong hồ sơ này.", this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(note);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Hủy", this);
    cancelBtn->setStyleSheet(VpnUi::buttonStyle());
    auto* addBtn = new QPushButton("Thêm", this);
    addBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    addBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(addBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(addBtn);
    root->addLayout(btnRow);

    ComboBoxStyle::applyCursor(this);
}

VpnProfile AddVpnProfileDialog::profile() const
{
    VpnProfile p;
    p.name = m_nameEdit->text().trimmed();
    p.countryLabel = m_countryEdit->text().trimmed();
    p.serverAddress = m_serverEdit->text().trimmed();
    p.tunnelType = static_cast<VpnTunnelType>(m_tunnelTypeCombo->currentData().toInt());
    p.username = m_usernameEdit->text().trimmed();
    if (p.tunnelType == VpnTunnelType::L2tp)
        p.l2tpPsk = m_pskEdit->text();
    return p;
}
