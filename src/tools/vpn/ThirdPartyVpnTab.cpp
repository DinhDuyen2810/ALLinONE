#include "ThirdPartyVpnTab.h"

#include "VpnUiStyle.h"
#include "core/AppPaths.h"
#include "direct/DirectVpnSession.h"
#include "direct/WintunAdapter.h"
#include "engine/ThirdPartyVpnScanner.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
// Chỉ nhớ TÊN ĐĂNG NHẬP theo máy chủ cho tiện (không phải bí mật) - mật khẩu không bao giờ được lưu.
const QString kUsersFile = QStringLiteral("vpn_direct_users.json");

QString rememberedUser(const QString& server)
{
    QFile f(AppPaths::profileFile(kUsersFile));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().value(server).toString();
}

void rememberUser(const QString& server, const QString& user)
{
    QJsonObject o;
    QFile in(AppPaths::profileFile(kUsersFile));
    if (in.open(QIODevice::ReadOnly))
        o = QJsonDocument::fromJson(in.readAll()).object();
    in.close();
    o.insert(server, user);
    QSaveFile out(AppPaths::profileFile(kUsersFile));
    if (out.open(QIODevice::WriteOnly))
    {
        out.write(QJsonDocument(o).toJson());
        out.commit();
    }
}

/// "ab12cd..." -> "AB:12:CD:..." (dễ đối chiếu với vân tay quản trị viên cung cấp).
QString prettyFingerprint(const QString& hex)
{
    QString out;
    for (int i = 0; i + 1 < hex.size(); i += 2)
    {
        if (!out.isEmpty())
            out += ':';
        out += hex.mid(i, 2).toUpper();
    }
    return out;
}
} // namespace

ThirdPartyVpnTab::ThirdPartyVpnTab(QWidget* parent)
    : QWidget(parent)
    , m_session(new DirectVpnSession(this))
{
    buildUi();

    connect(m_session, &DirectVpnSession::statusChanged, this, [this](const QString& s) { m_directLabel->setText("⏳ " + s); });
    connect(m_session, &DirectVpnSession::otpRequested, this, [this](const QString& prompt, bool push) {
        bool ok = false;
        QString text = prompt.isEmpty() ? QStringLiteral("Nhập mã xác thực 2 lớp (OTP):") : prompt;
        if (push)
            text += QStringLiteral("\n\n(Để TRỐNG rồi bấm OK để nhận thông báo đẩy FortiToken trên điện thoại.)");
        const QString code = QInputDialog::getText(this, QStringLiteral("Mã xác thực (OTP)"), text, QLineEdit::Normal, QString(), &ok);
        if (!ok)
        {
            m_session->disconnectVpn();
            return;
        }
        m_session->sendOtp(code.trimmed());
    });
    connect(m_session, &DirectVpnSession::certificateUntrusted, this,
            [this](const QString& hostPort, const QString& sha256, const QString& subject, const QString& issuer) {
                const auto ans = QMessageBox::warning(
                    this, QStringLiteral("Chứng chỉ máy chủ chưa được tin cậy"),
                    QStringLiteral("Windows không xác thực được chứng chỉ của máy chủ VPN %1 (thường gặp khi FortiGate dùng "
                                   "chứng chỉ mặc định tự ký - FortiClient cũng hỏi đúng câu này).\n\n"
                                   "Chủ thể: %2\nCấp bởi: %3\n\nVân tay SHA-256:\n%4\n\n"
                                   "Chỉ tin cậy nếu vân tay này KHỚP với vân tay bộ phận IT của bạn cung cấp (hoặc với "
                                   "vân tay FortiClient từng hiện). Tin cậy chứng chỉ này và tiếp tục kết nối?")
                        .arg(hostPort, subject, issuer, prettyFingerprint(sha256)),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                if (ans == QMessageBox::Yes)
                    m_session->trustAndRetry(hostPort, sha256);
                else
                    m_session->disconnectVpn();
            });
    connect(m_session, &DirectVpnSession::connected, this, [this](const QString& summary) {
        m_directLabel->setText(QStringLiteral("✓ Đã kết nối trực tiếp \"%1\" - %2").arg(m_activeName, summary));
        updateButtons();
    });
    connect(m_session, &DirectVpnSession::ended, this, [this](bool wasConnected, const QString& message) {
        m_directLabel->setText(wasConnected ? QStringLiteral("Đã ngắt kết nối \"%1\". %2").arg(m_activeName, message)
                                            : QStringLiteral("⚠ Không kết nối được \"%1\": %2").arg(m_activeName, message));
        m_activeName.clear();
        updateButtons();
    });

    reload();
}

ThirdPartyVpnTab::~ThirdPartyVpnTab()
{
    shutdownDirect(5000);
}

bool ThirdPartyVpnTab::isDirectActive() const
{
    return m_session && m_session->isActive();
}

void ThirdPartyVpnTab::shutdownDirect(int waitMs)
{
    if (m_session)
        m_session->shutdown(waitMs);
}

void ThirdPartyVpnTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Phát hiện các kết nối VPN đã cấu hình sẵn trên máy (FortiClient, GlobalProtect, OpenVPN, Cisco Secure Client). "
        "Hồ sơ FortiClient SSL-VPN và GlobalProtect có thể KẾT NỐI TRỰC TIẾP ngay tại đây (nhập tài khoản do cơ quan "
        "cấp - mật khẩu chỉ dùng cho phiên này, không lưu; cần bấm Yes ở hộp thoại Administrator để tạo adapter mạng). "
        "Ứng dụng KHÔNG đọc/giải mã mật khẩu đã lưu trong các phần mềm đó. Hồ sơ khác: \"Mở phần mềm\" để kết nối.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Quét lại", this);
    m_refreshBtn->setStyleSheet(VpnUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);
    topRow->addStretch();

    m_directBtn = new QPushButton("🔌 Kết nối trực tiếp", this);
    m_directBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_directBtn->setCursor(Qt::PointingHandCursor);
    connect(m_directBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onDirectConnectClicked);
    topRow->addWidget(m_directBtn);

    m_directStopBtn = new QPushButton("⛔ Ngắt kết nối", this);
    m_directStopBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_directStopBtn->setCursor(Qt::PointingHandCursor);
    m_directStopBtn->setVisible(false);
    connect(m_directStopBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onDirectDisconnectClicked);
    topRow->addWidget(m_directStopBtn);

    m_openBtn = new QPushButton("▶ Mở phần mềm", this);
    m_openBtn->setStyleSheet(VpnUi::buttonStyle());
    m_openBtn->setCursor(Qt::PointingHandCursor);
    m_openBtn->setEnabled(false);
    connect(m_openBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onOpenClicked);
    topRow->addWidget(m_openBtn);
    root->addLayout(topRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Phần mềm", "Tên hồ sơ", "Máy chủ / Portal", "Kết nối trực tiếp"});
    m_table->setStyleSheet(VpnUi::tableStyle());
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ThirdPartyVpnTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    m_directLabel = new QLabel("", this);
    m_directLabel->setWordWrap(true);
    m_directLabel->setStyleSheet("color: #1f2328; font-size: 12px; font-weight: bold;");
    root->addWidget(m_directLabel);

    m_statusLabel = new QLabel("", this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);
}

void ThirdPartyVpnTab::reload()
{
    m_profiles = ThirdPartyVpnScanner::scan();
    m_table->setRowCount(m_profiles.size());
    for (int i = 0; i < m_profiles.size(); ++i)
    {
        const ThirdPartyVpnProfile& p = m_profiles[i];
        auto set = [&](int col, const QString& text, const QString& tip = QString()) {
            auto* item = new QTableWidgetItem(text);
            if (!tip.isEmpty())
                item->setToolTip(tip);
            m_table->setItem(i, col, item);
        };
        set(0, p.clientName, p.canLaunch() ? p.exePath : "Không tìm thấy chương trình để mở");
        set(1, p.name);
        set(2, p.server.isEmpty() ? "(không rõ)" : p.server, p.source);
        set(3, p.canConnectDirectly() ? "✓ " + DirectVpn::protocolName(p.direct) : QStringLiteral("— chỉ mở phần mềm"),
            p.canConnectDirectly() ? QString()
                                   : QStringLiteral("Loại kết nối này (vd FortiClient IPsec, OpenVPN, Cisco) chưa hỗ trợ "
                                                    "kết nối trực tiếp - dùng \"Mở phần mềm\"."));
    }
    if (m_profiles.isEmpty())
        m_statusLabel->setText("Không tìm thấy hồ sơ VPN nào từ FortiClient/GlobalProtect/OpenVPN/Cisco trên máy này. "
                               "Nếu bạn đã cài một trong số đó và đã cấu hình kết nối, hãy bấm \"Quét lại\".");
    else
        m_statusLabel->setText(QString("Tìm thấy %1 hồ sơ. Chọn một dòng rồi bấm \"Kết nối trực tiếp\" (nếu hỗ trợ) hoặc "
                                       "\"Mở phần mềm\".")
                                   .arg(m_profiles.size()));
    if (!WintunAdapter::isAvailable())
        m_statusLabel->setText(m_statusLabel->text() +
                               QStringLiteral(" ⚠ Thiếu wintun\\wintun.dll trong thư mục cài đặt - chưa kết nối trực tiếp được."));
    updateButtons();
}

const ThirdPartyVpnProfile* ThirdPartyVpnTab::selected() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_profiles.size())
        return nullptr;
    return &m_profiles[row];
}

void ThirdPartyVpnTab::updateButtons()
{
    const ThirdPartyVpnProfile* p = selected();
    const bool active = isDirectActive();
    m_openBtn->setEnabled(p != nullptr && p->canLaunch());
    m_directBtn->setVisible(!active);
    m_directBtn->setEnabled(!active && p != nullptr && p->canConnectDirectly() && WintunAdapter::isAvailable());
    m_directStopBtn->setVisible(active);
    m_refreshBtn->setEnabled(!active);
}

void ThirdPartyVpnTab::onRowSelectionChanged()
{
    updateButtons();
}

void ThirdPartyVpnTab::onRefreshClicked()
{
    reload();
}

void ThirdPartyVpnTab::onOpenClicked()
{
    const ThirdPartyVpnProfile* p = selected();
    if (!p)
        return;
    QString error;
    if (ThirdPartyVpnScanner::launch(*p, &error))
        m_statusLabel->setText("✓ Đã mở " + p->clientName + ". Hãy đăng nhập/kết nối trong cửa sổ của phần mềm đó.");
    else
        QMessageBox::warning(this, "Không mở được", error);
}

bool ThirdPartyVpnTab::askCredentials(const ThirdPartyVpnProfile& p, QString* user, QString* pass)
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Đăng nhập VPN - %1").arg(p.name));
    auto* lay = new QVBoxLayout(&dlg);
    auto* info = new QLabel(QStringLiteral("Kết nối trực tiếp tới %1 (%2).\nNhập tài khoản VPN do cơ quan cấp - mật khẩu chỉ "
                                           "dùng cho phiên này, KHÔNG được lưu.")
                                .arg(p.server, DirectVpn::protocolName(p.direct)),
                            &dlg);
    info->setWordWrap(true);
    lay->addWidget(info);
    auto* form = new QFormLayout();
    auto* userEdit = new QLineEdit(rememberedUser(p.server), &dlg);
    auto* passEdit = new QLineEdit(&dlg);
    passEdit->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("Tên đăng nhập:"), userEdit);
    form->addRow(QStringLiteral("Mật khẩu:"), passEdit);
    lay->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Kết nối"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Hủy"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);
    (userEdit->text().isEmpty() ? userEdit : passEdit)->setFocus();
    dlg.resize(420, dlg.sizeHint().height());
    if (dlg.exec() != QDialog::Accepted || userEdit->text().trimmed().isEmpty())
    {
        passEdit->clear();
        return false;
    }
    *user = userEdit->text().trimmed();
    *pass = passEdit->text();
    passEdit->clear();
    return true;
}

void ThirdPartyVpnTab::onDirectConnectClicked()
{
    const ThirdPartyVpnProfile* p = selected();
    if (!p || !p->canConnectDirectly() || isDirectActive())
        return;
    const ThirdPartyVpnProfile profile = *p; // bản sao: danh sách có thể được quét lại
    QString user, pass;
    if (!askCredentials(profile, &user, &pass))
        return;
    rememberUser(profile.server, user);
    m_activeName = profile.name;
    DirectVpnSession::Request req;
    req.protocol = profile.direct;
    req.server = profile.server;
    req.username = user;
    req.password = pass;
    pass.fill(QChar(0));
    m_directLabel->setText(QStringLiteral("⏳ Đang chuẩn bị kết nối \"%1\"...").arg(profile.name));
    m_session->connectTo(req);
    req.password.fill(QChar(0));
    updateButtons();
}

void ThirdPartyVpnTab::onDirectDisconnectClicked()
{
    m_directLabel->setText(QStringLiteral("⏳ Đang ngắt kết nối..."));
    m_session->disconnectVpn();
}
