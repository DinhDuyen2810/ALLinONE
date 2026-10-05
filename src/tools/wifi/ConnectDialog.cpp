#include "ConnectDialog.h"

#include "WifiUiStyle.h"
#include "engine/WlanController.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// Thứ tự hiển thị trong combo; index khớp WifiSecurity bằng userData.
const QList<WifiSecurity> kSelectableSecurities = {
    WifiSecurity::Wpa2Psk, WifiSecurity::Wpa3Sae, WifiSecurity::WpaPsk, WifiSecurity::Wep, WifiSecurity::Open};
}

ConnectDialog::ConnectDialog(WlanController* controller, const QString& adapterGuid, const WifiNetwork& network, QWidget* parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_adapterGuid(adapterGuid)
    , m_network(network)
    , m_hiddenMode(false)
{
    buildUi();
    m_ssidEdit->setText(network.ssid);
    m_ssidEdit->setReadOnly(true);

    int idx = m_securityCombo->findData(static_cast<int>(network.security));
    if (idx < 0)
        idx = m_securityCombo->findData(static_cast<int>(WifiSecurity::Wpa2Psk));
    m_securityCombo->setCurrentIndex(idx);

    if (network.security == WifiSecurity::Open)
    {
        m_passwordEdit->setEnabled(false);
        m_passwordEdit->setPlaceholderText("Mạng mở, không cần mật khẩu");
    }
}

ConnectDialog* ConnectDialog::forHiddenNetwork(WlanController* controller, const QString& adapterGuid, QWidget* parent)
{
    WifiNetwork n;
    n.security = WifiSecurity::Wpa2Psk;
    auto* dlg = new ConnectDialog(controller, adapterGuid, n, parent);
    dlg->setWindowTitle("Kết nối mạng ẩn");
    dlg->m_hiddenMode = true;
    dlg->m_ssidEdit->setReadOnly(false);
    dlg->m_ssidEdit->setPlaceholderText("Nhập tên mạng (SSID)");
    dlg->m_ssidEdit->clear();
    return dlg;
}

void ConnectDialog::buildUi()
{
    setWindowTitle("Kết nối WiFi");
    setMinimumWidth(380);
    setStyleSheet(
        "QDialog { background-color: #f6f8fa; }"
        "QLabel { color: #1f2328; background: transparent; }" + WifiUi::inputStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    m_ssidEdit = new QLineEdit(this);
    form->addRow("Tên mạng (SSID):", m_ssidEdit);

    m_securityCombo = new QComboBox(this);
    for (WifiSecurity s : kSelectableSecurities)
        m_securityCombo->addItem(WifiSecurityUtil::displayName(s), static_cast<int>(s));
    connect(m_securityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        const auto sec = static_cast<WifiSecurity>(m_securityCombo->currentData().toInt());
        const bool needPw = WifiSecurityUtil::requiresPassword(sec);
        m_passwordEdit->setEnabled(needPw);
        m_passwordEdit->setPlaceholderText(needPw ? "Nhập mật khẩu" : "Mạng mở, không cần mật khẩu");
        if (!needPw)
            m_passwordEdit->clear();
    });
    form->addRow("Bảo mật:", m_securityCombo);

    auto* pwRow = new QWidget(this);
    auto* pwLayout = new QHBoxLayout(pwRow);
    pwLayout->setContentsMargins(0, 0, 0, 0);
    pwLayout->setSpacing(6);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Nhập mật khẩu");
    m_toggleVisBtn = new QPushButton("👁", this);
    m_toggleVisBtn->setFixedWidth(36);
    m_toggleVisBtn->setCursor(Qt::PointingHandCursor);
    m_toggleVisBtn->setStyleSheet(WifiUi::buttonStyle());
    connect(m_toggleVisBtn, &QPushButton::clicked, this, &ConnectDialog::onTogglePasswordVisibility);
    pwLayout->addWidget(m_passwordEdit, 1);
    pwLayout->addWidget(m_toggleVisBtn);
    form->addRow("Mật khẩu:", pwRow);

    root->addLayout(form);

    m_autoConnectCheck = new QCheckBox("Tự động kết nối khi trong tầm phủ sóng", this);
    m_autoConnectCheck->setChecked(true);
    root->addWidget(m_autoConnectCheck);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(m_statusLabel);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_cancelBtn = new QPushButton("Hủy", this);
    m_cancelBtn->setStyleSheet(WifiUi::buttonStyle());
    m_connectBtn = new QPushButton("Kết nối", this);
    m_connectBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_connectBtn->setDefault(true);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_connectBtn, &QPushButton::clicked, this, &ConnectDialog::onConnectClicked);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_connectBtn);
    root->addLayout(btnRow);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &ConnectDialog::pollConnectionStatus);
}

void ConnectDialog::onTogglePasswordVisibility()
{
    const bool hidden = m_passwordEdit->echoMode() == QLineEdit::Password;
    m_passwordEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
    m_toggleVisBtn->setText(hidden ? "🙈" : "👁");
}

void ConnectDialog::setBusy(bool busy, const QString& message)
{
    m_connectBtn->setEnabled(!busy);
    m_ssidEdit->setEnabled(!busy && m_hiddenMode);
    m_securityCombo->setEnabled(!busy);
    m_passwordEdit->setEnabled(!busy && WifiSecurityUtil::requiresPassword(
                                            static_cast<WifiSecurity>(m_securityCombo->currentData().toInt())));
    m_statusLabel->setText(message);
}

void ConnectDialog::onConnectClicked()
{
    const QString ssid = m_ssidEdit->text().trimmed();
    if (ssid.isEmpty())
    {
        QMessageBox::warning(this, "Thiếu thông tin", "Vui lòng nhập tên mạng (SSID).");
        return;
    }

    WifiNetwork net = m_network;
    net.ssid = ssid;
    if (m_hiddenMode)
        net.ssidBytes = ssid.toUtf8();

    const auto security = static_cast<WifiSecurity>(m_securityCombo->currentData().toInt());
    const QString password = m_passwordEdit->text();

    QString error;
    if (!m_controller->connectWithPassword(m_adapterGuid, net, password, security, m_autoConnectCheck->isChecked(), &error))
    {
        QMessageBox::critical(this, "Không kết nối được", error);
        return;
    }

    m_pollAttempts = 0;
    setBusy(true, "⏳ Đang kết nối tới \"" + ssid + "\"...");
    m_pollTimer->start();
}

void ConnectDialog::pollConnectionStatus()
{
    ++m_pollAttempts;
    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);

    if (cur.isConnected && cur.ssid == m_ssidEdit->text().trimmed())
    {
        m_pollTimer->stop();
        emit connected(cur.ssid);
        accept();
        return;
    }

    if (m_pollAttempts >= 15)
    {
        m_pollTimer->stop();
        setBusy(false, "⚠ Không kết nối được sau 15 giây. Có thể sai mật khẩu, mạng ngoài tầm phủ sóng, hoặc chọn sai loại bảo mật.");
    }
}
