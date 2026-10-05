#include "NetworksTab.h"

#include "ConnectDialog.h"
#include "WifiUiStyle.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
QString signalIcon(int quality)
{
    if (quality >= 75) return "📶";
    if (quality >= 50) return "📶";
    if (quality >= 25) return "📵";
    return "📵";
}

QTableWidgetItem* makeItem(const QString& text)
{
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
} // namespace

NetworksTab::NetworksTab(WlanController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    m_scanSettleTimer = new QTimer(this);
    m_scanSettleTimer->setSingleShot(true);
    m_scanSettleTimer->setInterval(2200);
    connect(m_scanSettleTimer, &QTimer::timeout, this, [this] {
        m_scanning = false;
        m_scanBtn->setEnabled(true);
        reloadNetworks();
    });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(4000);
    connect(m_pollTimer, &QTimer::timeout, this, &NetworksTab::onPollTick);
}

void NetworksTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_banner = new QLabel(this);
    m_banner->setStyleSheet(WifiUi::bannerStyle(false));
    m_banner->setText("Chưa kết nối WiFi nào.");
    root->addWidget(m_banner);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);
    m_scanBtn = new QPushButton("🔄 Quét lại", this);
    m_connectBtn = new QPushButton("🔗 Kết nối", this);
    m_disconnectBtn = new QPushButton("⛔ Ngắt kết nối", this);
    m_forgetBtn = new QPushButton("🗑 Quên mạng này", this);
    m_hiddenBtn = new QPushButton("➕ Kết nối mạng ẩn...", this);
    for (QPushButton* b : {m_scanBtn, m_connectBtn, m_disconnectBtn, m_forgetBtn, m_hiddenBtn})
    {
        b->setStyleSheet(WifiUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        bar->addWidget(b);
    }
    m_connectBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_connectBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(false);
    m_forgetBtn->setEnabled(false);
    bar->addStretch();
    root->addLayout(bar);

    connect(m_scanBtn, &QPushButton::clicked, this, &NetworksTab::onScanClicked);
    connect(m_connectBtn, &QPushButton::clicked, this, &NetworksTab::onConnectClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &NetworksTab::onDisconnectClicked);
    connect(m_forgetBtn, &QPushButton::clicked, this, &NetworksTab::onForgetClicked);
    connect(m_hiddenBtn, &QPushButton::clicked, this, &NetworksTab::onHiddenClicked);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"", "Tên mạng (SSID)", "Bảo mật", "Tín hiệu", "Trạng thái"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
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
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &NetworksTab::onSelectionChanged);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onConnectClicked(); });
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(m_statusLabel);
}

void NetworksTab::setAdapter(const QString& guid)
{
    m_adapterGuid = guid;
    reloadNetworks();
    reloadCurrentConnectionBanner();
}

void NetworksTab::refreshNow()
{
    reloadNetworks();
    reloadCurrentConnectionBanner();
}

void NetworksTab::startActivePolling()
{
    if (!m_adapterGuid.isEmpty())
        m_pollTimer->start();
}

void NetworksTab::stopActivePolling()
{
    m_pollTimer->stop();
}

void NetworksTab::onPollTick()
{
    reloadCurrentConnectionBanner();
}

void NetworksTab::onScanClicked()
{
    if (m_adapterGuid.isEmpty() || m_scanning)
        return;

    QString error;
    if (!m_controller->requestScan(m_adapterGuid, &error))
    {
        m_statusLabel->setText("⚠ " + error);
        reloadNetworks(); // vẫn hiện danh sách từ lần quét gần nhất của hệ thống
        return;
    }

    m_scanning = true;
    m_scanBtn->setEnabled(false);
    m_statusLabel->setText("⏳ Đang quét mạng WiFi xung quanh...");
    m_scanSettleTimer->start();
}

void NetworksTab::reloadNetworks()
{
    if (m_adapterGuid.isEmpty())
        return;

    const int prevRow = m_table->currentRow();
    const QString prevSsid = (prevRow >= 0 && prevRow < m_networks.size()) ? m_networks[prevRow].ssid : QString();

    QString error;
    m_networks = m_controller->availableNetworks(m_adapterGuid, &error);
    if (!error.isEmpty())
    {
        m_statusLabel->setText("⚠ " + error);
    }
    else if (!m_scanning)
    {
        m_statusLabel->setText(QString("Tìm thấy %1 mạng.").arg(m_networks.size()));
    }

    m_table->setRowCount(m_networks.size());
    for (int i = 0; i < m_networks.size(); ++i)
    {
        const WifiNetwork& n = m_networks[i];
        m_table->setItem(i, 0, makeItem(signalIcon(n.signalQuality)));
        m_table->setItem(i, 1, makeItem(n.ssid));
        m_table->setItem(i, 2, makeItem(WifiSecurityUtil::displayName(n.security)));
        m_table->setItem(i, 3, makeItem(QString("%1%").arg(n.signalQuality)));
        QStringList tags;
        if (n.connected) tags << "Đang dùng";
        if (n.hasProfile) tags << "Đã lưu";
        m_table->setItem(i, 4, makeItem(tags.join(", ")));
    }

    int restoreRow = -1;
    if (!prevSsid.isEmpty())
        for (int i = 0; i < m_networks.size(); ++i)
            if (m_networks[i].ssid == prevSsid) { restoreRow = i; break; }
    if (restoreRow >= 0)
        m_table->selectRow(restoreRow);

    onSelectionChanged();
}

void NetworksTab::reloadCurrentConnectionBanner()
{
    if (m_adapterGuid.isEmpty())
        return;

    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);
    if (cur.isConnected)
    {
        m_banner->setStyleSheet(WifiUi::bannerStyle(true));
        m_banner->setText(QString("✓ Đang kết nối: %1  •  %2  •  Tín hiệu %3%  •  %4  •  ↓%5 Mbps ↑%6 Mbps")
                              .arg(cur.ssid, WifiSecurityUtil::displayName(cur.security))
                              .arg(cur.signalQuality)
                              .arg(cur.phyType)
                              .arg(cur.rxRateMbps, 0, 'f', 0).arg(cur.txRateMbps, 0, 'f', 0));
    }
    else
    {
        m_banner->setStyleSheet(WifiUi::bannerStyle(false));
        m_banner->setText("○ Chưa kết nối WiFi nào.");
    }
}

int NetworksTab::selectedRow() const
{
    return m_table->currentRow();
}

WifiNetwork NetworksTab::selectedNetwork() const
{
    const int row = selectedRow();
    if (row >= 0 && row < m_networks.size())
        return m_networks[row];
    return WifiNetwork();
}

void NetworksTab::onSelectionChanged()
{
    const int row = selectedRow();
    const bool has = row >= 0 && row < m_networks.size();
    m_connectBtn->setEnabled(has && !m_networks[row].connected);
    m_disconnectBtn->setEnabled(has && m_networks[row].connected);
    m_forgetBtn->setEnabled(has && m_networks[row].hasProfile);
}

void NetworksTab::onConnectClicked()
{
    const WifiNetwork net = selectedNetwork();
    if (net.ssid.isEmpty())
        return;

    auto* dlg = new ConnectDialog(m_controller, m_adapterGuid, net, this);
    connect(dlg, &ConnectDialog::connected, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
        reloadNetworks();
        reloadCurrentConnectionBanner();
        emit profilesMayHaveChanged();
    });
    dlg->exec();
    dlg->deleteLater();
}

void NetworksTab::onHiddenClicked()
{
    auto* dlg = ConnectDialog::forHiddenNetwork(m_controller, m_adapterGuid, this);
    connect(dlg, &ConnectDialog::connected, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
        reloadNetworks();
        reloadCurrentConnectionBanner();
        emit profilesMayHaveChanged();
    });
    dlg->exec();
    dlg->deleteLater();
}

void NetworksTab::onDisconnectClicked()
{
    QString error;
    if (!m_controller->disconnect(m_adapterGuid, &error))
    {
        QMessageBox::critical(this, "Lỗi", error);
        return;
    }
    QTimer::singleShot(400, this, [this] {
        reloadNetworks();
        reloadCurrentConnectionBanner();
    });
}

void NetworksTab::onForgetClicked()
{
    const WifiNetwork net = selectedNetwork();
    if (net.profileName.isEmpty())
        return;

    if (QMessageBox::question(this, "Quên mạng", QString("Xóa hồ sơ đã lưu cho \"%1\"? Mật khẩu đã lưu sẽ bị xóa khỏi máy này.").arg(net.ssid),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!m_controller->deleteProfile(m_adapterGuid, net.profileName, &error))
    {
        QMessageBox::critical(this, "Lỗi", error);
        return;
    }
    reloadNetworks();
    emit profilesMayHaveChanged();
}
