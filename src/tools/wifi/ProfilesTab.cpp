#include "ProfilesTab.h"

#include "WifiUiStyle.h"
#include "engine/ConnectionWatcher.h"
#include "engine/WlanProfileXml.h"
#include "tools/qr/QRCodec.h"
#include "tools/qr/QRPayload.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
// Mã bảo mật cho định dạng WIFI: của mã QR (chuẩn chung: WPA cho mọi WPA/WPA2/WPA3, WEP, nopass).
QString qrSecurityToken(WifiSecurity s)
{
    switch (s)
    {
        case WifiSecurity::Open: return "nopass";
        case WifiSecurity::Wep:  return "WEP";
        default:                 return "WPA";
    }
}
} // namespace

namespace
{
QTableWidgetItem* makeItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
} // namespace

ProfilesTab::ProfilesTab(WlanController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    // "Kết nối" chỉ GỬI yêu cầu (WlanConnect trả về ngay). Trước đây dòng trạng thái dừng mãi ở "Đang kết
    // nối..." dù kết nối đã xong hay đã thất bại.
    m_watcher = new ConnectionWatcher(m_controller, this);
    connect(m_watcher, &ConnectionWatcher::succeeded, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
    });
    connect(m_watcher, &ConnectionWatcher::failed, this, [this](const QString& name) {
        m_statusLabel->setText(QString("⚠ Không kết nối được tới \"%1\" (mạng ngoài tầm phủ sóng, hoặc mật khẩu đã lưu "
                                       "không còn đúng).").arg(name));
    });
}

void ProfilesTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // Checkbox nằm RIÊNG một hàng (không chung hàng với 5 nút bên dưới): ở độ rộng cửa sổ bình thường,
    // nhãn dài "Hiện mật khẩu đã lưu (cần quyền Administrator)" cộng 5 nút không đủ chỗ trên cùng 1
    // hàng - QCheckBox không tự rút gọn chữ (không giống QLabel/ô bảng), layout co nó lại nhỏ hơn chữ
    // cần thiết khiến chữ bị cắt cụt giữa chừng, rất khó đọc.
    auto* checkRow = new QHBoxLayout();
    m_showPasswordsCheck = new QCheckBox("👁 Hiện mật khẩu đã lưu (cần quyền Administrator)", this);
    connect(m_showPasswordsCheck, &QCheckBox::toggled, this, &ProfilesTab::onShowPasswordsToggled);
    checkRow->addWidget(m_showPasswordsCheck);
    checkRow->addStretch();
    root->addLayout(checkRow);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);

    m_connectBtn = new QPushButton("🔗 Kết nối", this);
    m_connectBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_copyPasswordBtn = new QPushButton("📋 Sao chép mật khẩu", this);
    m_shareQrBtn = new QPushButton("📱 Chia sẻ QR", this);
    m_exportBtn = new QPushButton("📤 Xuất XML...", this);
    m_importBtn = new QPushButton("📥 Nhập XML...", this);
    m_deleteBtn = new QPushButton("🗑 Xóa hồ sơ", this);
    for (QPushButton* b : {m_connectBtn, m_copyPasswordBtn, m_shareQrBtn, m_exportBtn, m_importBtn, m_deleteBtn})
    {
        b->setCursor(Qt::PointingHandCursor);
        bar->addWidget(b);
    }
    m_copyPasswordBtn->setStyleSheet(WifiUi::buttonStyle());
    m_shareQrBtn->setStyleSheet(WifiUi::buttonStyle());
    m_shareQrBtn->setToolTip("Hiện mã QR để điện thoại quét và tự vào mạng này (mã QR CHỨA mật khẩu WiFi dạng chữ).");
    m_exportBtn->setStyleSheet(WifiUi::buttonStyle());
    m_importBtn->setStyleSheet(WifiUi::buttonStyle());
    m_deleteBtn->setStyleSheet(WifiUi::dangerButtonStyle());
    root->addLayout(bar);

    connect(m_connectBtn, &QPushButton::clicked, this, &ProfilesTab::onConnectClicked);
    connect(m_copyPasswordBtn, &QPushButton::clicked, this, &ProfilesTab::onCopyPasswordClicked);
    connect(m_shareQrBtn, &QPushButton::clicked, this, &ProfilesTab::onShareQrClicked);
    connect(m_exportBtn, &QPushButton::clicked, this, &ProfilesTab::onExportClicked);
    connect(m_importBtn, &QPushButton::clicked, this, &ProfilesTab::onImportClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ProfilesTab::onDeleteClicked);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"Tên hồ sơ (SSID)", "Bảo mật", "Tự kết nối", "Mật khẩu", ""});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(WifiUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ProfilesTab::onSelectionChanged);
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText); // có chứa SSID/tên hồ sơ - xem WifiUi::plainMessage()
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(m_statusLabel);

    onSelectionChanged();
}

void ProfilesTab::setAdapter(const QString& guid)
{
    m_adapterGuid = guid;
    reload();
}

void ProfilesTab::reload()
{
    if (m_adapterGuid.isEmpty())
        return;

    QString error;
    m_profiles = m_controller->profiles(m_adapterGuid, m_passwordsVisible, &error);
    if (!error.isEmpty())
        m_statusLabel->setText("⚠ " + error);
    else
        m_statusLabel->setText(QString("%1 hồ sơ đã lưu trên máy này.").arg(m_profiles.size()));

    m_table->setRowCount(m_profiles.size());
    bool anyDenied = false;
    for (int i = 0; i < m_profiles.size(); ++i)
    {
        const WifiProfile& p = m_profiles[i];
        m_table->setItem(i, 0, makeItem(p.ssid.isEmpty() ? p.name : p.ssid));
        m_table->setItem(i, 1, makeItem(WifiSecurityUtil::displayName(p.security)));
        m_table->setItem(i, 2, makeItem(p.autoConnect ? "Có" : "Không"));

        QString pwText;
        if (!WifiSecurityUtil::requiresPassword(p.security))
            pwText = "—";
        else if (m_passwordsVisible && p.hasPassword)
            pwText = p.password;
        else if (m_passwordsVisible && p.passwordAccessDenied)
        {
            pwText = "(Cần quyền Admin)";
            anyDenied = true;
        }
        else
            pwText = "••••••••";
        m_table->setItem(i, 3, makeItem(pwText));
        m_table->setItem(i, 4, makeItem(QString()));
    }

    if (anyDenied)
        m_statusLabel->setText(m_statusLabel->text() + "  Một số mật khẩu cần chạy One for ALL với quyền Administrator mới xem được.");

    onSelectionChanged();
}

void ProfilesTab::onShowPasswordsToggled(bool on)
{
    m_passwordsVisible = on;
    reload();
}

int ProfilesTab::selectedRow() const
{
    return m_table->currentRow();
}

void ProfilesTab::onSelectionChanged()
{
    const int row = selectedRow();
    const bool has = row >= 0 && row < m_profiles.size();
    m_connectBtn->setEnabled(has);
    m_exportBtn->setEnabled(has);
    m_deleteBtn->setEnabled(has);
    m_copyPasswordBtn->setEnabled(has && m_passwordsVisible && m_profiles[qMax(row, 0)].hasPassword);
    // Chia sẻ QR: mọi mạng kết nối được bằng SSID+mật khẩu (kể cả mạng mở). Mạng Enterprise/không xác định
    // không chia sẻ kiểu này được.
    m_shareQrBtn->setEnabled(has && WifiSecurityUtil::isSupportedForQuickConnect(m_profiles[qMax(row, 0)].security));
}

void ProfilesTab::onShareQrClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;
    const WifiProfile profile = m_profiles[row];
    const QString ssid = profile.ssid.isEmpty() ? profile.name : profile.ssid;

    if (!WifiSecurityUtil::isSupportedForQuickConnect(profile.security))
    {
        WifiUi::plainMessage(this, QMessageBox::Information, "Chia sẻ QR",
                             "Mạng Enterprise/không xác định không chia sẻ được bằng mã QR SSID+mật khẩu.");
        return;
    }

    // Lấy mật khẩu dạng chữ để nhét vào mã QR. Mạng mở thì không cần. Nếu hồ sơ hiện chưa có mật khẩu dạng
    // chữ (chưa bật "Hiện mật khẩu"), xin lại ĐÚNG hồ sơ này - cần quyền Administrator mới đọc được.
    QString password;
    if (WifiSecurityUtil::requiresPassword(profile.security))
    {
        if (profile.hasPassword)
        {
            password = profile.password;
        }
        else
        {
            QString error;
            const QList<WifiProfile> fresh = m_controller->profiles(m_adapterGuid, true, &error);
            for (const WifiProfile& f : fresh)
                if (f.name == profile.name && f.hasPassword)
                {
                    password = f.password;
                    break;
                }
            if (password.isEmpty())
            {
                WifiUi::plainMessage(this, QMessageBox::Warning, "Chia sẻ QR",
                                     "Không đọc được mật khẩu dạng chữ của mạng này. Hãy chạy One for ALL với quyền "
                                     "Administrator (hoặc bật \"Hiện mật khẩu đã lưu\") rồi thử lại.");
                return;
            }
        }
    }

    WifiInfo info;
    info.ssid = ssid;
    info.password = password;
    info.security = qrSecurityToken(profile.security);
    const QString payload = QRPayload::makeWifi(info);

    QString error;
    const QRMatrix matrix = QRCodec::encode(payload, QREcc::Medium, &error);
    if (!matrix.isValid())
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Chia sẻ QR",
                             error.isEmpty() ? "Không tạo được mã QR." : error);
        return;
    }

    QRStyle style;
    style.targetSize = 360;
    const QImage image = QRCodec::render(matrix, style);

    // Hộp thoại hiển thị đơn giản, dựng tại chỗ (không thêm file mới). Mã QR chứa mật khẩu dạng chữ nên có
    // dòng nhắc rõ ràng.
    QDialog dlg(this);
    dlg.setWindowTitle("Chia sẻ WiFi qua QR");
    dlg.setStyleSheet("QDialog { background-color: #f6f8fa; } QLabel { color: #1f2328; background: transparent; }");
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto* ssidLabel = new QLabel(QString("Mạng: %1").arg(ssid), &dlg);
    ssidLabel->setTextFormat(Qt::PlainText);
    ssidLabel->setStyleSheet("font-weight: 700; font-size: 14px;");
    ssidLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(ssidLabel);

    auto* qrLabel = new QLabel(&dlg);
    qrLabel->setPixmap(QPixmap::fromImage(image));
    qrLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(qrLabel);

    auto* hint = new QLabel("Mở camera điện thoại quét mã này để tự vào mạng.\n"
                            "⚠ Mã QR chứa mật khẩu WiFi dạng chữ - đừng để người lạ chụp lại.",
                            &dlg);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setStyleSheet("color: #57606a; font-size: 12px;");
    layout->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(buttons);

    dlg.exec();
}

void ProfilesTab::onConnectClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    const WifiProfile& profile = m_profiles[row];

    // Đang nối sẵn bằng chính hồ sơ này: không gửi lại yêu cầu (và không báo "thành công" dựa trên một
    // kết nối vốn có từ trước).
    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);
    if (cur.isConnected && cur.profileName == profile.name)
    {
        m_watcher->cancel();
        m_statusLabel->setText("✓ Đang kết nối sẵn tới \"" + profile.ssid + "\".");
        return;
    }

    QString error;
    if (!m_controller->connectToSavedProfile(m_adapterGuid, profile.name, &error))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Không kết nối được", error);
        return;
    }
    m_statusLabel->setText("⏳ Đang kết nối tới \"" + profile.ssid + "\"...");
    m_watcher->watch(m_adapterGuid, profile.ssid, profile.name);
}

void ProfilesTab::onCopyPasswordClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size() || !m_profiles[row].hasPassword)
        return;
    QGuiApplication::clipboard()->setText(m_profiles[row].password);
    m_statusLabel->setText("✓ Đã sao chép mật khẩu vào clipboard.");
}

void ProfilesTab::onExportClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    const WifiProfile profile = m_profiles[row];

    // Hồ sơ có mật khẩu: hỏi TRƯỚC người dùng có muốn tệp chứa mật khẩu dạng chữ không. Trước đây luôn xin
    // mật khẩu dạng chữ và ghi thẳng ra một tệp XML thường (ai mở tệp cũng đọc được) mà không một lời
    // cảnh báo.
    bool includeKey = false;
    if (WifiSecurityUtil::requiresPassword(profile.security))
    {
        QMessageBox box(QMessageBox::Warning, "Xuất hồ sơ WiFi",
                        QString("Xuất hồ sơ \"%1\" ra tệp XML.\n\n"
                                "• KÈM mật khẩu: mật khẩu WiFi nằm trong tệp ở dạng CHỮ ĐỌC ĐƯỢC - bất kỳ ai mở tệp "
                                "đều thấy. Dùng khi cần chuyển hồ sơ sang máy khác; hãy giữ tệp cẩn thận và xóa sau khi dùng.\n\n"
                                "• KHÔNG kèm mật khẩu: khóa trong tệp ở dạng mã hóa, chỉ nhập lại được trên chính máy này.")
                            .arg(profile.ssid),
                        QMessageBox::NoButton, this);
        box.setTextFormat(Qt::PlainText);
        QPushButton* withoutKey = box.addButton("Không kèm mật khẩu", QMessageBox::AcceptRole);
        QPushButton* withKey = box.addButton("Kèm mật khẩu (dạng chữ)", QMessageBox::DestructiveRole);
        box.addButton("Hủy", QMessageBox::RejectRole);
        box.setDefaultButton(withoutKey);
        box.exec();
        if (box.clickedButton() == withKey)
            includeKey = true;
        else if (box.clickedButton() != withoutKey)
            return;
    }

    const QString suggested = profile.ssid + ".xml";
    const QString path = QFileDialog::getSaveFileName(this, "Xuất hồ sơ WiFi", suggested, "XML (*.xml)");
    if (path.isEmpty())
        return;

    QString error;
    const QString xml = m_controller->exportProfileXml(m_adapterGuid, profile.name, includeKey, &error);
    if (xml.isEmpty())
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", error.isEmpty() ? "Không xuất được hồ sơ." : error);
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", "Không ghi được file: " + f.errorString());
        return;
    }
    const QByteArray bytes = xml.toUtf8();
    if (f.write(bytes) != bytes.size() || !f.flush())
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", "Không ghi được file: " + f.errorString());
        return;
    }
    f.close();

    // Nói rõ tệp vừa lưu CÓ hay KHÔNG chứa mật khẩu đọc được (dựa trên chính nội dung đã ghi).
    WifiProfile written;
    const bool hasPlainKey = WlanProfileXml::parse(xml, &written) && written.hasPassword;
    QString message = "Đã lưu: " + path;
    if (hasPlainKey)
        message += "\n\n⚠ Tệp này CHỨA MẬT KHẨU WiFi ở dạng chữ đọc được. Đừng chia sẻ/để ở nơi người khác mở được.";
    if (!error.isEmpty())
        message += "\n\n⚠ " + error;
    WifiUi::plainMessage(this, hasPlainKey || !error.isEmpty() ? QMessageBox::Warning : QMessageBox::Information,
                         "Xuất hồ sơ", message);
}

void ProfilesTab::onImportClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, "Nhập hồ sơ WiFi", QString(), "XML (*.xml)");
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", "Không đọc được file: " + f.errorString());
        return;
    }
    const QString xml = QString::fromUtf8(f.readAll());

    // Đọc tên hồ sơ trong tệp TRƯỚC khi nhập: WlanSetProfile ghi đè hồ sơ trùng tên (kèm mật khẩu đã lưu
    // của nó) không hỏi han gì - trước đây chọn nhầm tệp là mất hồ sơ đang dùng.
    WifiProfile incoming;
    if (!WlanProfileXml::parse(xml, &incoming))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi",
                             "Tệp này không phải hồ sơ WiFi hợp lệ (không đọc được XML hoặc thiếu tên hồ sơ).");
        return;
    }
    if (m_controller->hasProfile(m_adapterGuid, incoming.name))
    {
        if (WifiUi::plainMessage(this, QMessageBox::Warning, "Nhập hồ sơ",
                                 QString("Máy này đã có hồ sơ tên \"%1\".\n\nNhập tệp sẽ GHI ĐÈ hồ sơ đó (kể cả mật khẩu "
                                         "đang lưu). Tiếp tục?").arg(incoming.name),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
    }

    QString error;
    if (!m_controller->importProfileXml(m_adapterGuid, xml, &error))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", error);
        return;
    }
    reload();
    WifiUi::plainMessage(this, QMessageBox::Information, "Nhập hồ sơ",
                         QString("Đã nhập hồ sơ \"%1\" thành công.").arg(incoming.name));
}

void ProfilesTab::onDeleteClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    if (WifiUi::plainMessage(this, QMessageBox::Question, "Xóa hồ sơ",
                             QString("Xóa hồ sơ \"%1\"? Mật khẩu đã lưu sẽ bị xóa khỏi máy này.").arg(m_profiles[row].ssid),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!m_controller->deleteProfile(m_adapterGuid, m_profiles[row].name, &error))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", error);
        return;
    }
    reload();
}
