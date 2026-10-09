#include "ProfilesTab.h"

#include "WifiUiStyle.h"
#include "engine/ConnectionWatcher.h"
#include "engine/WlanProfileXml.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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
    m_exportBtn = new QPushButton("📤 Xuất XML...", this);
    m_importBtn = new QPushButton("📥 Nhập XML...", this);
    m_deleteBtn = new QPushButton("🗑 Xóa hồ sơ", this);
    for (QPushButton* b : {m_connectBtn, m_copyPasswordBtn, m_exportBtn, m_importBtn, m_deleteBtn})
    {
        b->setCursor(Qt::PointingHandCursor);
        bar->addWidget(b);
    }
    m_copyPasswordBtn->setStyleSheet(WifiUi::buttonStyle());
    m_exportBtn->setStyleSheet(WifiUi::buttonStyle());
    m_importBtn->setStyleSheet(WifiUi::buttonStyle());
    m_deleteBtn->setStyleSheet(WifiUi::dangerButtonStyle());
    root->addLayout(bar);

    connect(m_connectBtn, &QPushButton::clicked, this, &ProfilesTab::onConnectClicked);
    connect(m_copyPasswordBtn, &QPushButton::clicked, this, &ProfilesTab::onCopyPasswordClicked);
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
