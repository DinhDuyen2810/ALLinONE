#include "AddVpnProfileDialog.h"

#include "VpnUiStyle.h"
#include "engine/DirectProfileStore.h"
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
    setMinimumWidth(440);
    setStyleSheet(
        "QDialog { background-color: #f6f8fa; }"
        "QLabel { color: #1f2328; background: transparent; }" + VpnUi::inputStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    m_modeHint = new QLabel(this);
    m_modeHint->setWordWrap(true);
    m_modeHint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(m_modeHint);

    m_form = new QFormLayout();
    m_form->setSpacing(10);

    // 3 chế độ (hiện tại chỉ có 3): Thường = VPN hệ thống Windows; GlobalProtect; FortiClient.
    m_modeCombo = new QComboBox(this);
    m_modeCombo->setObjectName("modeCombo");
    m_modeCombo->addItem("Thường (VPN của Windows)", static_cast<int>(Mode::Normal));
    m_modeCombo->addItem("GlobalProtect", static_cast<int>(Mode::GlobalProtect));
    m_modeCombo->addItem("FortiClient", static_cast<int>(Mode::Forticlient));
    m_form->addRow("Chế độ:", m_modeCombo);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setObjectName("nameEdit");
    m_form->addRow("Tên hồ sơ:", m_nameEdit);

    // ---- Chế độ Thường: VPN hệ thống Windows ----
    m_countryEdit = new QLineEdit(this);
    m_countryEdit->setPlaceholderText("vd: Hà Lan (chỉ để hiển thị)");
    m_form->addRow("Quốc gia:", m_countryEdit);

    m_serverEdit = new QLineEdit(this);
    m_serverEdit->setPlaceholderText("vd: nl123.vpnprovider.com hoặc IP");
    m_form->addRow("Địa chỉ máy chủ:", m_serverEdit);

    m_tunnelTypeCombo = new QComboBox(this);
    m_tunnelTypeCombo->addItem("Tự động", static_cast<int>(VpnTunnelType::Automatic));
    m_tunnelTypeCombo->addItem("IKEv2 (khuyến nghị)", static_cast<int>(VpnTunnelType::Ikev2));
    m_tunnelTypeCombo->addItem("L2TP/IPsec", static_cast<int>(VpnTunnelType::L2tp));
    m_tunnelTypeCombo->addItem("SSTP", static_cast<int>(VpnTunnelType::Sstp));
    m_tunnelTypeCombo->addItem("PPTP (cũ, kém an toàn)", static_cast<int>(VpnTunnelType::Pptp));
    m_tunnelTypeCombo->setCurrentIndex(1); // IKEv2 mặc định - tương thích rộng, an toàn
    m_form->addRow("Giao thức:", m_tunnelTypeCombo);

    // Chỉ có nghĩa với L2TP/IPsec - trước đây không có ô này nên máy chủ L2TP dùng khóa chia sẻ trước (loại
    // phổ biến nhất) không bao giờ kết nối được.
    m_pskEdit = new QLineEdit(this);
    m_pskEdit->setObjectName("l2tpPskEdit");
    m_pskEdit->setEchoMode(QLineEdit::Password);
    m_pskEdit->setPlaceholderText("Khóa chia sẻ trước (PSK) - chỉ cho L2TP/IPsec");
    m_form->addRow("Khóa L2TP (PSK):", m_pskEdit);
    const auto updatePsk = [this]() {
        const bool l2tp = static_cast<VpnTunnelType>(m_tunnelTypeCombo->currentData().toInt()) == VpnTunnelType::L2tp;
        m_pskEdit->setEnabled(l2tp);
    };
    connect(m_tunnelTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updatePsk);
    updatePsk();

    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("Tên đăng nhập VPN (không bắt buộc)");
    m_form->addRow("Tên đăng nhập:", m_usernameEdit);

    // ---- Chế độ GlobalProtect ----
    m_gpPortalEdit = new QLineEdit(this);
    m_gpPortalEdit->setObjectName("gpPortalEdit");
    m_gpPortalEdit->setPlaceholderText("vd: vpn.congty.vn (portal GlobalProtect)");
    m_form->addRow("Portal GlobalProtect:", m_gpPortalEdit);

    // ---- Chế độ FortiClient ----
    m_fortiTypeCombo = new QComboBox(this);
    m_fortiTypeCombo->setObjectName("fortiTypeCombo");
    m_fortiTypeCombo->addItem("SSL-VPN", static_cast<int>(DirectProtocol::FortinetSsl));
    m_fortiTypeCombo->addItem("IPsec (IKEv1 + PSK)", static_cast<int>(DirectProtocol::FortinetIpsec));
    m_form->addRow("Loại kết nối:", m_fortiTypeCombo);

    m_fortiServerEdit = new QLineEdit(this);
    m_fortiServerEdit->setObjectName("fortiServerEdit");
    m_form->addRow("Máy chủ FortiGate:", m_fortiServerEdit);
    const auto updateFortiPlaceholder = [this]() {
        const bool ipsec = static_cast<DirectProtocol>(m_fortiTypeCombo->currentData().toInt()) == DirectProtocol::FortinetIpsec;
        m_fortiServerEdit->setPlaceholderText(ipsec ? "vd: 119.17.206.59 hoặc vpn.congty.vn"
                                                    : "vd: vpn.congty.vn:10443 (cổng mặc định 443)");
    };
    connect(m_fortiTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, updateFortiPlaceholder);
    updateFortiPlaceholder();

    root->addLayout(m_form);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName("errorLabel");
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet(VpnUi::bannerStyle("danger"));
    m_errorLabel->hide();
    root->addWidget(m_errorLabel);

    m_noteLabel = new QLabel(this);
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(m_noteLabel);

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

    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { updateModeUi(); });
    updateModeUi();

    ComboBoxStyle::applyCursor(this);
}

AddVpnProfileDialog::Mode AddVpnProfileDialog::mode() const
{
    return static_cast<Mode>(m_modeCombo->currentData().toInt());
}

void AddVpnProfileDialog::updateModeUi()
{
    const Mode m = mode();
    const bool normal = (m == Mode::Normal);
    const bool gp = (m == Mode::GlobalProtect);
    const bool forti = (m == Mode::Forticlient);
    // Mỗi chế độ chỉ hiện các ô của nó (QFormLayout::setRowVisible ẩn cả nhãn lẫn ô nhập).
    for (QWidget* w : {static_cast<QWidget*>(m_countryEdit), static_cast<QWidget*>(m_serverEdit),
                       static_cast<QWidget*>(m_tunnelTypeCombo), static_cast<QWidget*>(m_pskEdit),
                       static_cast<QWidget*>(m_usernameEdit)})
        m_form->setRowVisible(w, normal);
    m_form->setRowVisible(m_gpPortalEdit, gp);
    m_form->setRowVisible(m_fortiTypeCombo, forti);
    m_form->setRowVisible(m_fortiServerEdit, forti);

    if (normal)
    {
        m_modeHint->setText(
            "Nhập thông tin máy chủ VPN từ chính nhà cung cấp bạn đã đăng ký (hoặc VPN cơ quan). Ứng dụng "
            "KHÔNG cung cấp sẵn máy chủ VPN nào - đây chỉ là nơi quản lý/kết nối các hồ sơ bạn tự khai báo. "
            "Hồ sơ này là kết nối VPN của Windows (IKEv2/L2TP/SSTP/PPTP).");
        m_nameEdit->setPlaceholderText("vd: Hà Lan - Mullvad");
        m_noteLabel->setText("Mật khẩu sẽ được hỏi riêng mỗi lần kết nối - không lưu trong hồ sơ này.");
    }
    else if (gp)
    {
        m_modeHint->setText(
            "Kết nối TRỰC TIẾP tới portal GlobalProtect (Palo Alto) ngay trong ứng dụng - không cần cài GlobalProtect. "
            "Hồ sơ được lưu trong ứng dụng và hiện ở tab \"VPN đã cài sẵn\".");
        m_nameEdit->setPlaceholderText("vd: VPN công ty (GlobalProtect)");
        m_noteLabel->setText("Tài khoản (và mã OTP nếu có) được hỏi lúc kết nối; có thể lưu - mã hóa bởi Windows - để lần sau "
                             "chỉ bấm Kết nối. Không lưu trong hồ sơ này.");
    }
    else
    {
        m_modeHint->setText(
            "Kết nối TRỰC TIẾP tới FortiGate ngay trong ứng dụng - không cần cài FortiClient. SSL-VPN: nhập máy chủ (kèm "
            ":cổng nếu khác 443). IPsec: nhập địa chỉ máy chủ (IKEv1, cần khóa chia sẻ trước PSK). Hồ sơ hiện ở tab "
            "\"VPN đã cài sẵn\".");
        m_nameEdit->setPlaceholderText("vd: VPN_CVTLT");
        m_noteLabel->setText("Tài khoản (và PSK với IPsec) được hỏi lúc kết nối; có thể lưu - mã hóa bởi Windows - để lần sau "
                             "chỉ bấm Kết nối. Không lưu trong hồ sơ này.");
    }
    m_errorLabel->hide();
    adjustSize();
}

bool AddVpnProfileDialog::validateInput(QString* error) const
{
    if (mode() == Mode::Normal)
    {
        if (m_nameEdit->text().trimmed().isEmpty() || m_serverEdit->text().trimmed().isEmpty())
        {
            if (error)
                *error = "Vui lòng nhập tên hồ sơ và địa chỉ máy chủ.";
            return false;
        }
        return true;
    }
    return DirectProfileStore::validate(directProfile(), error);
}

void AddVpnProfileDialog::accept()
{
    QString error;
    if (!validateInput(&error))
    {
        m_errorLabel->setText("⚠ " + error);
        m_errorLabel->show();
        return;
    }
    QDialog::accept();
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

DirectVpnProfile AddVpnProfileDialog::directProfile() const
{
    DirectVpnProfile p;
    p.name = m_nameEdit->text().trimmed();
    switch (mode())
    {
        case Mode::GlobalProtect:
            p.protocol = DirectProtocol::GlobalProtect;
            p.server = m_gpPortalEdit->text().trimmed();
            break;
        case Mode::Forticlient:
            p.protocol = static_cast<DirectProtocol>(m_fortiTypeCombo->currentData().toInt());
            p.server = m_fortiServerEdit->text().trimmed();
            break;
        case Mode::Normal:
            break; // không phải hồ sơ trực tiếp - protocol None
    }
    return p;
}
