#include "ConnectDialog.h"

#include "WifiUiStyle.h"
#include "engine/ConnectionWatcher.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
// Thứ tự hiển thị trong combo; index khớp WifiSecurity bằng userData.
const QList<WifiSecurity> kSelectableSecurities = {
    WifiSecurity::Wpa2Psk, WifiSecurity::Wpa3Sae, WifiSecurity::WpaPsk, WifiSecurity::Wep, WifiSecurity::Open};

constexpr int kDisconnectWaitMs = 10000;
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

ConnectDialog* ConnectDialog::forHiddenNetwork(WlanController* controller, const QString& adapterGuid, QWidget* parent,
                                               WifiSecurity security)
{
    WifiNetwork n;
    n.security = security;
    n.hidden = true;
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
    m_statusLabel->setTextFormat(Qt::PlainText); // dòng trạng thái có chứa SSID - xem WifiUi::plainMessage()
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
    m_pollTimer->setInterval(ConnectionWatcher::kPollIntervalMs);
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
    m_autoConnectCheck->setEnabled(!busy);
    m_passwordEdit->setEnabled(!busy && WifiSecurityUtil::requiresPassword(
                                            static_cast<WifiSecurity>(m_securityCombo->currentData().toInt())));
    m_statusLabel->setText(message);
}

void ConnectDialog::onConnectClicked()
{
    if (m_phase != Phase::Idle)
        return; // lần trước chưa có kết quả - không gửi thêm yêu cầu nào

    // Mạng quét được: dùng NGUYÊN SSID của mạng (trước đây bị trimmed() - SSID có khoảng trắng đầu/cuối
    // thành tên khác, so khớp trạng thái không bao giờ trùng). Mạng ẩn: người dùng tự gõ nên bỏ khoảng
    // trắng thừa hai đầu.
    const QString ssid = m_hiddenMode ? m_ssidEdit->text().trimmed() : m_network.ssid;
    if (ssid.isEmpty())
    {
        WifiUi::plainMessage(this, QMessageBox::Warning, "Thiếu thông tin", "Vui lòng nhập tên mạng (SSID).");
        return;
    }
    if (m_hiddenMode && ssid.toUtf8().size() > 32)
    {
        WifiUi::plainMessage(this, QMessageBox::Warning, "Tên mạng quá dài",
                             "Tên mạng (SSID) dài tối đa 32 byte (ký tự có dấu chiếm nhiều hơn 1 byte).");
        return;
    }

    m_attemptSsid = ssid;
    m_previousProfileName.clear();

    // Máy đang nối sẵn CHÍNH SSID này (vd nhập lại mật khẩu cho mạng đang dùng): nếu gửi yêu cầu kết nối
    // ngay, lần hỏi trạng thái đầu tiên vẫn thấy "đã kết nối <SSID>" của liên kết CŨ và hộp thoại báo thành
    // công dù mật khẩu mới sai. Ngắt trước, chờ adapter rời hẳn trạng thái đã kết nối rồi mới kết nối -
    // khi đó mọi trạng thái "đã kết nối" nhìn thấy về sau chắc chắn là của lần thử này.
    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);
    if (cur.isConnected && cur.ssid == ssid)
    {
        QString error;
        if (!m_controller->disconnect(m_adapterGuid, &error))
        {
            WifiUi::plainMessage(this, QMessageBox::Critical, "Không kết nối được", error);
            return;
        }
        m_previousProfileName = cur.profileName;
        m_phase = Phase::WaitingDisconnect;
        m_elapsedMs = 0;
        setBusy(true, "⏳ Đang ngắt kết nối hiện tại để kết nối lại bằng mật khẩu mới...");
        m_pollTimer->start();
        return;
    }

    startConnect();
}

void ConnectDialog::startConnect()
{
    WifiNetwork net = m_network;
    net.ssid = m_attemptSsid;
    if (m_hiddenMode)
    {
        net.ssidBytes = m_attemptSsid.toUtf8();
        net.hidden = true;
    }

    const auto security = static_cast<WifiSecurity>(m_securityCombo->currentData().toInt());
    const QString password = m_passwordEdit->text();

    // ĐÚNG MỘT lời gọi kết nối cho mỗi lần người dùng bấm nút, với đúng mật khẩu đang ở trong ô nhập.
    QString error;
    WifiProfileBackup backup;
    if (!m_controller->connectWithPassword(m_adapterGuid, net, password, security, m_autoConnectCheck->isChecked(),
                                           &backup, &error))
    {
        m_pollTimer->stop();
        m_phase = Phase::Idle;
        const QString undo = rollbackPending(); // nối lại hồ sơ cũ nếu chính hộp thoại vừa ngắt nó
        setBusy(false);
        WifiUi::plainMessage(this, QMessageBox::Critical, "Không kết nối được",
                             undo.isEmpty() ? error : error + "\n\n" + undo);
        return;
    }

    m_backup = backup;
    m_phase = Phase::WaitingConnect;
    m_elapsedMs = 0;
    setBusy(true, "⏳ Đang kết nối tới \"" + m_attemptSsid + "\"...");
    m_pollTimer->start();
}

QString ConnectDialog::rollbackPending()
{
    QStringList notes;
    if (m_backup.valid)
    {
        QString error;
        if (m_controller->rollbackProfile(m_backup, &error))
            notes << (m_backup.existed ? "Hồ sơ đã lưu trước đó của mạng này đã được khôi phục."
                                       : "Hồ sơ tạm vừa tạo đã được xóa.");
        else
            notes << "⚠ " + error;
        m_backup = WifiProfileBackup(); // bỏ luôn bản XML (có thể chứa mật khẩu cũ) khỏi bộ nhớ
    }
    if (!m_previousProfileName.isEmpty())
    {
        // Chính hộp thoại đã ngắt kết nối đang dùng (xem onConnectClicked) - nối lại bằng hồ sơ ĐÃ LƯU
        // (đã khôi phục ở trên), không liên quan gì tới mật khẩu vừa nhập.
        if (m_controller->connectToSavedProfile(m_adapterGuid, m_previousProfileName))
            notes << "Đang kết nối lại bằng hồ sơ cũ.";
        m_previousProfileName.clear();
    }
    return notes.join(" ");
}

void ConnectDialog::pollConnectionStatus()
{
    m_elapsedMs += m_pollTimer->interval();
    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);

    if (m_phase == Phase::WaitingDisconnect)
    {
        // "Đã rời SSID này" chứ không phải "không còn kết nối gì": ngay sau khi bị ngắt, Windows có thể tự nối
        // sang một mạng đã lưu KHÁC trong tầm. Trước đây điều kiện chỉ là !isConnected nên trường hợp đó bị
        // coi là "chưa ngắt được", chờ hết 10 giây rồi báo lỗi dù liên kết cũ tới SSID này đã mất hẳn.
        if (!cur.isConnected || cur.ssid != m_attemptSsid)
        {
            startConnect();
            return;
        }
        if (m_elapsedMs >= kDisconnectWaitMs)
        {
            // Vẫn đang kết nối như cũ: chưa đổi gì nên không có gì phải hoàn tác.
            m_pollTimer->stop();
            m_phase = Phase::Idle;
            m_previousProfileName.clear();
            setBusy(false, "⚠ Không ngắt được kết nối hiện tại. Hãy bấm \"Ngắt kết nối\" ở danh sách mạng rồi thử lại.");
        }
        return;
    }

    if (m_phase != Phase::WaitingConnect)
    {
        m_pollTimer->stop();
        return;
    }

    if (cur.isConnected && cur.ssid == m_attemptSsid)
    {
        m_pollTimer->stop();
        m_phase = Phase::Idle;
        m_backup = WifiProfileBackup(); // thành công: hồ sơ mới là hồ sơ đúng, không còn gì để hoàn tác
        m_previousProfileName.clear();
        emit connected(cur.ssid);
        accept();
        return;
    }

    if (ConnectionWatcher::shouldGiveUp(m_elapsedMs, cur.connecting))
    {
        m_pollTimer->stop();
        m_phase = Phase::Idle;
        const int seconds = m_elapsedMs / 1000;
        const QString undo = rollbackPending();
        setBusy(false, QString("⚠ Không kết nối được sau %1 giây. Có thể sai mật khẩu, mạng ngoài tầm phủ sóng, "
                               "hoặc chọn sai loại bảo mật. %2").arg(seconds).arg(undo));
    }
}

void ConnectDialog::done(int result)
{
    // Đóng hộp thoại khi lần kết nối còn dở (bấm Hủy/Esc/nút X giữa lúc đang chờ): hoàn tác như khi hết
    // giờ - nếu không, hồ sơ mang mật khẩu chưa được xác nhận sẽ nằm lại (và đè lên hồ sơ cũ đang dùng tốt).
    if (m_phase != Phase::Idle)
    {
        m_pollTimer->stop();
        m_phase = Phase::Idle;
        rollbackPending();
    }
    QDialog::done(result);
}
