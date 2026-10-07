#include "VpnTab.h"

#include "AddVpnProfileDialog.h"
#include "VpnUiStyle.h"
#include "engine/VpnConnector.h"

#include <QColor>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

VpnTab::VpnTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_connector = new VpnConnector(this);
    connect(m_connector, &VpnConnector::operationFinished, this, &VpnTab::onConnectorFinished);

    m_ipChecker = new PublicIpChecker(this);
    connect(m_ipChecker, &PublicIpChecker::result, this, &VpnTab::onIpResult);
    connect(m_ipChecker, &PublicIpChecker::errorOccurred, this, &VpnTab::onIpError);

    reloadConnections();
    onCheckLocationClicked();
}

VpnTab::~VpnTab()
{
    // KHÔNG chỉ wait(3000) suông - bắt tay VPN thật có thể mất tới 45 giây (máy chủ chậm/xa), 3 giây
    // gần như chắc chắn không đủ trong trường hợp đó, khiến QThread bị hủy đối tượng trong lúc vẫn đang
    // thực sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt). Phải HỦY (an toàn) rồi mới chờ.
    cancelAndWait(3000);
}

bool VpnTab::isBusy() const
{
    return m_connector && m_connector->isRunning();
}

void VpnTab::cancelAndWait(int waitMs)
{
    if (!isBusy())
        return;
    m_connector->requestCancel();
    if (!m_connector->wait(waitMs))
        m_connector->wait(); // vẫn chưa thoát - đợi thêm, không bao giờ để nơi gọi hủy đối tượng lúc còn chạy
}

void VpnTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Quản lý kết nối VPN hệ thống của Windows (IKEv2/L2TP/SSTP/PPTP - tích hợp sẵn, không cần cài "
        "phần mềm nào khác). Bạn tự khai báo máy chủ + tài khoản từ nhà cung cấp VPN đã đăng ký (hoặc "
        "VPN cơ quan) - ứng dụng không đi kèm máy chủ VPN nào.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    // ---- Vị trí hiện tại (dựa trên IP - xem giải thích đầy đủ trong VpnTab.h) ----
    auto* locGroup = new QGroupBox("Vị trí hiện tại (theo IP)", this);
    locGroup->setStyleSheet(VpnUi::groupStyle());
    auto* locLayout = new QHBoxLayout(locGroup);
    locLayout->setContentsMargins(14, 10, 14, 10);

    auto* locTextLayout = new QVBoxLayout();
    m_locationIpLabel = new QLabel("Đang kiểm tra...", this);
    m_locationIpLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 14px;");
    m_locationIpLabel->setWordWrap(true);
    m_locationPlaceLabel = new QLabel("", this);
    m_locationPlaceLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_locationPlaceLabel->setWordWrap(true);
    locTextLayout->addWidget(m_locationIpLabel);
    locTextLayout->addWidget(m_locationPlaceLabel);
    locLayout->addLayout(locTextLayout, 1);

    m_refreshLocationBtn = new QPushButton("🔄 Kiểm tra lại", this);
    m_refreshLocationBtn->setStyleSheet(VpnUi::buttonStyle());
    m_refreshLocationBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshLocationBtn, &QPushButton::clicked, this, &VpnTab::onCheckLocationClicked);
    locLayout->addWidget(m_refreshLocationBtn);
    root->addWidget(locGroup);

    // ---- Danh sách hồ sơ VPN ----
    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Làm mới", this);
    m_refreshBtn->setStyleSheet(VpnUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &VpnTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);

    m_addBtn = new QPushButton("+ Thêm hồ sơ...", this);
    m_addBtn->setStyleSheet(VpnUi::buttonStyle());
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &VpnTab::onAddClicked);
    topRow->addWidget(m_addBtn);

    m_removeBtn = new QPushButton("🗑 Xóa", this);
    m_removeBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_removeBtn->setCursor(Qt::PointingHandCursor);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &VpnTab::onRemoveClicked);
    topRow->addWidget(m_removeBtn);
    topRow->addStretch();
    root->addLayout(topRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Tên hồ sơ", "Quốc gia", "Giao thức", "Trạng thái"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(VpnUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &VpnTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_connectBtn = new QPushButton("🔌 Kết nối", this);
    m_connectBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    m_connectBtn->setEnabled(false);
    connect(m_connectBtn, &QPushButton::clicked, this, &VpnTab::onConnectClicked);
    bottomRow->addWidget(m_connectBtn);

    m_disconnectBtn = new QPushButton("⏹ Ngắt kết nối", this);
    m_disconnectBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_disconnectBtn->setCursor(Qt::PointingHandCursor);
    m_disconnectBtn->setVisible(false);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &VpnTab::onDisconnectClicked);
    bottomRow->addWidget(m_disconnectBtn);
    bottomRow->addStretch();
    root->addLayout(bottomRow);
}

void VpnTab::reloadConnections()
{
    QString error;
    m_connections = VpnController::listConnections(&error);

    m_table->setRowCount(m_connections.size());
    for (int i = 0; i < m_connections.size(); ++i)
    {
        const auto& c = m_connections[i];
        auto setItem = [&](int col, const QString& text, const QString& color = QString()) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (!color.isEmpty())
                item->setForeground(QColor(color));
            m_table->setItem(i, col, item);
        };
        setItem(0, c.name);
        setItem(1, c.countryLabel.isEmpty() ? "—" : c.countryLabel);
        setItem(2, c.tunnelType);
        setItem(3, c.isConnected() ? "Đã kết nối" : "Chưa kết nối", VpnUi::statusColor(c.isConnected()));
    }

    m_statusLabel->setText(error.isEmpty() ? QString("Có %1 hồ sơ VPN.").arg(m_connections.size()) : ("⚠ " + error));
    onRowSelectionChanged();
}

const VpnConnectionStatus* VpnTab::selectedConnection() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_connections.size())
        return nullptr;
    return &m_connections[row];
}

void VpnTab::onRowSelectionChanged()
{
    const auto* c = selectedConnection();
    const bool busy = m_connector->isRunning();
    m_removeBtn->setEnabled(c != nullptr && !busy);
    m_connectBtn->setEnabled(c != nullptr && !c->isConnected() && !busy);
    m_connectBtn->setVisible(!(c && c->isConnected()));
    m_disconnectBtn->setVisible(c && c->isConnected());
    m_disconnectBtn->setEnabled(!busy);
}

void VpnTab::onRefreshClicked()
{
    reloadConnections();
}

void VpnTab::onAddClicked()
{
    AddVpnProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const VpnProfile profile = dlg.profile();
    if (profile.name.isEmpty() || profile.serverAddress.isEmpty())
    {
        QMessageBox::warning(this, "Thiếu thông tin", "Vui lòng nhập tên hồ sơ và địa chỉ máy chủ.");
        return;
    }

    QString error;
    if (!VpnController::addConnection(profile, &error))
    {
        QMessageBox::critical(this, "Không thêm được", error);
        return;
    }
    reloadConnections();
}

void VpnTab::onRemoveClicked()
{
    const auto* c = selectedConnection();
    if (!c)
        return;

    if (QMessageBox::question(this, "Xóa hồ sơ VPN", QString("Xóa hồ sơ \"%1\"?").arg(c->name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!VpnController::removeConnection(c->name, &error))
    {
        QMessageBox::critical(this, "Không xóa được", error);
        return;
    }
    reloadConnections();
}

void VpnTab::onConnectClicked()
{
    const auto* c = selectedConnection();
    if (!c)
        return;

    bool ok = false;
    const QString username = QInputDialog::getText(this, "Kết nối VPN", "Tên đăng nhập:", QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    const QString password = QInputDialog::getText(this, "Kết nối VPN", "Mật khẩu:", QLineEdit::Password, QString(), &ok);
    if (!ok)
        return;

    m_connector->setConnectTarget(c->name, username, password);
    m_connectBtn->setEnabled(false);
    m_statusLabel->setText("⏳ Đang kết nối \"" + c->name + "\"...");
    m_connector->start();
}

void VpnTab::onDisconnectClicked()
{
    const auto* c = selectedConnection();
    if (!c)
        return;
    m_connector->setDisconnectTarget(c->name);
    m_disconnectBtn->setEnabled(false);
    m_statusLabel->setText("⏳ Đang ngắt kết nối \"" + c->name + "\"...");
    m_connector->start();
}

void VpnTab::onConnectorFinished(bool success, QString message)
{
    m_statusLabel->setText((success ? "✓ " : "⚠ ") + (message.isEmpty() ? (success ? "Xong." : "Thất bại.") : message));
    reloadConnections();
    if (success)
        onCheckLocationClicked(); // vị trí IP đổi ngay sau khi kết nối/ngắt kết nối VPN
}

void VpnTab::onCheckLocationClicked()
{
    m_locationIpLabel->setText("⏳ Đang kiểm tra...");
    m_locationPlaceLabel->clear();
    m_ipChecker->check();
}

void VpnTab::onIpResult(PublicIpInfo info)
{
    m_locationIpLabel->setText("IP: " + info.ip);
    QString place = info.city.isEmpty() ? info.country : (info.city + ", " + info.country);
    m_locationPlaceLabel->setText(place.isEmpty() ? "(không xác định được quốc gia)" : ("📍 " + place));
}

void VpnTab::onIpError(QString message)
{
    m_locationIpLabel->setText("⚠ Không kiểm tra được");
    m_locationPlaceLabel->setText(message);
}
