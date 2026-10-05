#include "ProfilesTab.h"

#include "WifiUiStyle.h"

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
}

void ProfilesTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);

    m_showPasswordsCheck = new QCheckBox("👁 Hiện mật khẩu đã lưu (cần quyền Administrator)", this);
    connect(m_showPasswordsCheck, &QCheckBox::toggled, this, &ProfilesTab::onShowPasswordsToggled);
    bar->addWidget(m_showPasswordsCheck);
    bar->addStretch();

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

    QString error;
    if (!m_controller->connectToSavedProfile(m_adapterGuid, m_profiles[row].name, &error))
    {
        QMessageBox::critical(this, "Không kết nối được", error);
        return;
    }
    m_statusLabel->setText("⏳ Đang kết nối tới \"" + m_profiles[row].ssid + "\"...");
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

    const QString suggested = m_profiles[row].ssid + ".xml";
    const QString path = QFileDialog::getSaveFileName(this, "Xuất hồ sơ WiFi", suggested, "XML (*.xml)");
    if (path.isEmpty())
        return;

    QString error;
    const QString xml = m_controller->exportProfileXml(m_adapterGuid, m_profiles[row].name, &error);
    if (xml.isEmpty())
    {
        QMessageBox::critical(this, "Lỗi", error.isEmpty() ? "Không xuất được hồ sơ." : error);
        return;
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, "Lỗi", "Không ghi được file: " + f.errorString());
        return;
    }
    f.write(xml.toUtf8());

    if (!error.isEmpty())
        QMessageBox::information(this, "Xuất hồ sơ", "Đã lưu: " + path + "\n\n⚠ " + error);
    else
        QMessageBox::information(this, "Xuất hồ sơ", "Đã lưu: " + path);
}

void ProfilesTab::onImportClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, "Nhập hồ sơ WiFi", QString(), "XML (*.xml)");
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, "Lỗi", "Không đọc được file: " + f.errorString());
        return;
    }
    const QString xml = QString::fromUtf8(f.readAll());

    QString error;
    if (!m_controller->importProfileXml(m_adapterGuid, xml, &error))
    {
        QMessageBox::critical(this, "Lỗi", error);
        return;
    }
    reload();
    QMessageBox::information(this, "Nhập hồ sơ", "Đã nhập hồ sơ thành công.");
}

void ProfilesTab::onDeleteClicked()
{
    const int row = selectedRow();
    if (row < 0 || row >= m_profiles.size())
        return;

    if (QMessageBox::question(this, "Xóa hồ sơ", QString("Xóa hồ sơ \"%1\"? Mật khẩu đã lưu sẽ bị xóa khỏi máy này.").arg(m_profiles[row].ssid),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!m_controller->deleteProfile(m_adapterGuid, m_profiles[row].name, &error))
    {
        QMessageBox::critical(this, "Lỗi", error);
        return;
    }
    reload();
}
