#include "NetworksTab.h"

#include "ConnectDialog.h"
#include "WifiUiStyle.h"
#include "engine/ConnectionWatcher.h"
#include "ui/widgets/FlowLayout.h"

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

    m_watcher = new ConnectionWatcher(m_controller, this);
    connect(m_watcher, &ConnectionWatcher::succeeded, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
        reloadNetworks();
        reloadCurrentConnectionBanner();
    });
    connect(m_watcher, &ConnectionWatcher::failed, this, [this](const QString& name) {
        m_statusLabel->setText(QString("⚠ Không kết nối được tới \"%1\" bằng hồ sơ đã lưu (mạng ngoài tầm phủ sóng, hoặc "
                                       "mật khẩu của mạng đã đổi - khi đó chọn mạng rồi bấm \"Mật khẩu khác...\").").arg(name));
        reloadNetworks();
        reloadCurrentConnectionBanner();
    });
}

void NetworksTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_banner = new QLabel(this);
    // Banner và dòng trạng thái đều chứa SSID (dữ liệu không tin cậy) - xem WifiUi::plainMessage().
    m_banner->setTextFormat(Qt::PlainText);
    m_banner->setWordWrap(true);
    m_banner->setStyleSheet(WifiUi::bannerStyle(false));
    m_banner->setText("Chưa kết nối WiFi nào.");
    root->addWidget(m_banner);

    // 7 nút full-text không rút gọn - có thể vượt chiều rộng cửa sổ kể cả ở kích thước mặc định (đã xác
    // nhận thật bằng ảnh chụp ở DPI 125%). FlowLayout tự xuống dòng thay vì cắt mất nút ngoài tầm nhìn.
    auto* bar = new FlowLayout(nullptr, 0, 8, 8);
    m_scanBtn = new QPushButton("🔄 Quét lại", this);
    m_connectBtn = new QPushButton("🔗 Kết nối", this);
    m_newPasswordBtn = new QPushButton("🔑 Mật khẩu khác...", this);
    m_newPasswordBtn->setToolTip("Mạng này đã có hồ sơ lưu sẵn. Bấm để nhập một mật khẩu khác (vd mật khẩu của mạng vừa "
                                 "được đổi); hồ sơ cũ được giữ lại nếu mật khẩu mới không kết nối được.");
    m_disconnectBtn = new QPushButton("⛔ Ngắt kết nối", this);
    m_forgetBtn = new QPushButton("🗑 Quên mạng này", this);
    m_hiddenBtn = new QPushButton("➕ Kết nối mạng ẩn...", this);
    m_loveBtn = new QPushButton("💜 Love WiFi", this);
    for (QPushButton* b : {m_scanBtn, m_connectBtn, m_newPasswordBtn, m_disconnectBtn, m_forgetBtn, m_hiddenBtn, m_loveBtn})
    {
        b->setStyleSheet(WifiUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        bar->addWidget(b);
    }
    m_connectBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_connectBtn->setEnabled(false);
    m_newPasswordBtn->setEnabled(false);
    m_disconnectBtn->setEnabled(false);
    m_forgetBtn->setEnabled(false);
    m_loveBtn->setEnabled(false);
    m_loveBtn->setToolTip("Chọn một mạng để bày tỏ tình cảm 💜");
    root->addLayout(bar);

    connect(m_scanBtn, &QPushButton::clicked, this, &NetworksTab::onScanClicked);
    connect(m_connectBtn, &QPushButton::clicked, this, &NetworksTab::onConnectClicked);
    connect(m_newPasswordBtn, &QPushButton::clicked, this, &NetworksTab::onNewPasswordClicked);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &NetworksTab::onDisconnectClicked);
    connect(m_forgetBtn, &QPushButton::clicked, this, &NetworksTab::onForgetClicked);
    connect(m_hiddenBtn, &QPushButton::clicked, this, &NetworksTab::onHiddenClicked);
    connect(m_loveBtn, &QPushButton::clicked, this, &NetworksTab::onLoveClicked);

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
    // Nháy đúp = bấm nút "Kết nối", nên phải tôn trọng trạng thái nút: trước đây nháy đúp vào mạng ĐANG
    // DÙNG (nút Kết nối bị khóa) vẫn mở hộp thoại nhập mật khẩu và ghi đè hồ sơ đang dùng.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) {
        if (m_connectBtn->isEnabled())
            onConnectClicked();
    });
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
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
        m_table->setItem(i, 1, makeItem(n.displayName()));
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
    else if (prevRow >= 0)
    {
        // Mạng đang chọn đã biến mất khỏi danh sách: bỏ chọn hẳn, thay vì để vùng chọn rơi vào một mạng
        // KHÁC tình cờ nằm cùng số hàng (các nút sẽ tác động lên mạng mà người dùng chưa hề chọn).
        m_table->setCurrentCell(-1, -1);
        m_table->clearSelection();
    }

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

bool NetworksTab::hasSelection() const
{
    const int row = selectedRow();
    return row >= 0 && row < m_networks.size();
}

WifiNetwork NetworksTab::selectedNetwork() const
{
    return hasSelection() ? m_networks[selectedRow()] : WifiNetwork();
}

void NetworksTab::onSelectionChanged()
{
    const int row = selectedRow();
    const bool has = row >= 0 && row < m_networks.size();
    m_connectBtn->setEnabled(has && !m_networks[row].connected);
    // "Mật khẩu khác...": chỉ khi mạng đã có hồ sơ (chưa có thì "Kết nối" đã hỏi mật khẩu) và biết SSID.
    m_newPasswordBtn->setEnabled(has && !m_networks[row].connected && m_networks[row].hasProfile &&
                                 !m_networks[row].ssid.isEmpty());
    m_disconnectBtn->setEnabled(has && m_networks[row].connected);
    m_forgetBtn->setEnabled(has && m_networks[row].hasProfile);
    m_loveBtn->setEnabled(has);
}

void NetworksTab::onLoveClicked()
{
    if (!hasSelection())
        return;
    const WifiNetwork net = selectedNetwork();

    WifiUi::plainMessage(this, QMessageBox::Information, "💜 Love WiFi",
                         QString("Yes, \"%1\" love you too! 💜").arg(net.displayName()));
}

void NetworksTab::onConnectClicked()
{
    if (!hasSelection() || m_adapterGuid.isEmpty())
        return;
    const WifiNetwork net = selectedNetwork();

    // Mạng ĐÃ có hồ sơ lưu sẵn: kết nối bằng chính hồ sơ đó, như Windows. Trước đây lúc nào cũng mở hộp
    // thoại bắt nhập lại mật khẩu rồi GHI ĐÈ hồ sơ đang có - gõ nhầm là hỏng hồ sơ đang dùng tốt. Muốn
    // nhập mật khẩu khác thì bấm "Mật khẩu khác..." (người dùng chủ động).
    if (net.hasProfile && !net.profileName.isEmpty())
    {
        QString error;
        if (!m_controller->connectToSavedProfile(m_adapterGuid, net.profileName, &error))
        {
            WifiUi::plainMessage(this, QMessageBox::Critical, "Không kết nối được", error);
            return;
        }
        m_statusLabel->setText("⏳ Đang kết nối tới \"" + net.displayName() + "\" bằng hồ sơ đã lưu...");
        m_watcher->watch(m_adapterGuid, net.ssid, net.profileName);
        return;
    }

    // Mạng ẩn chưa có hồ sơ: danh sách quét không cho biết SSID - người dùng phải tự nhập.
    if (net.ssid.isEmpty())
    {
        openHiddenDialog(WifiSecurityUtil::isSupportedForQuickConnect(net.security) ? net.security : WifiSecurity::Wpa2Psk);
        return;
    }

    openConnectDialog(net);
}

void NetworksTab::onNewPasswordClicked()
{
    if (!hasSelection() || m_adapterGuid.isEmpty())
        return;
    const WifiNetwork net = selectedNetwork();
    if (net.ssid.isEmpty() || net.connected)
        return;
    openConnectDialog(net);
}

void NetworksTab::openConnectDialog(const WifiNetwork& net)
{
    m_watcher->cancel(); // người dùng chuyển sang nhập mật khẩu: kết quả lần "kết nối bằng hồ sơ" trước không còn ý nghĩa
    auto* dlg = new ConnectDialog(m_controller, m_adapterGuid, net, this);
    connect(dlg, &ConnectDialog::connected, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
    });
    dlg->exec();
    dlg->deleteLater();
    // Dù thành công hay không, hồ sơ có thể đã đổi (tạo mới / khôi phục / xóa bản tạm) - đọc lại cả hai tab.
    reloadNetworks();
    reloadCurrentConnectionBanner();
    emit profilesMayHaveChanged();
}

void NetworksTab::openHiddenDialog(WifiSecurity security)
{
    if (m_adapterGuid.isEmpty())
        return;
    m_watcher->cancel();
    auto* dlg = ConnectDialog::forHiddenNetwork(m_controller, m_adapterGuid, this, security);
    connect(dlg, &ConnectDialog::connected, this, [this](const QString& ssid) {
        m_statusLabel->setText("✓ Đã kết nối: " + ssid);
    });
    dlg->exec();
    dlg->deleteLater();
    reloadNetworks();
    reloadCurrentConnectionBanner();
    emit profilesMayHaveChanged();
}

void NetworksTab::onHiddenClicked()
{
    openHiddenDialog(WifiSecurity::Wpa2Psk);
}

void NetworksTab::onDisconnectClicked()
{
    m_watcher->cancel();
    QString error;
    if (!m_controller->disconnect(m_adapterGuid, &error))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", error);
        return;
    }
    QTimer::singleShot(400, this, [this] {
        reloadNetworks();
        reloadCurrentConnectionBanner();
    });
}

void NetworksTab::onForgetClicked()
{
    if (!hasSelection())
        return;
    const WifiNetwork net = selectedNetwork();
    if (net.profileName.isEmpty())
    {
        // Không được thoát im lặng khi nút đang sáng: nói rõ vì sao không làm được và làm ở đâu.
        m_statusLabel->setText("⚠ Không xác định được tên hồ sơ của mạng này. Hãy xóa nó ở tab \"Hồ sơ đã lưu\".");
        return;
    }

    if (WifiUi::plainMessage(this, QMessageBox::Question, "Quên mạng",
                             QString("Xóa hồ sơ đã lưu cho \"%1\"? Mật khẩu đã lưu sẽ bị xóa khỏi máy này.").arg(net.displayName()),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!m_controller->deleteProfile(m_adapterGuid, net.profileName, &error))
    {
        WifiUi::plainMessage(this, QMessageBox::Critical, "Lỗi", error);
        return;
    }
    reloadNetworks();
    emit profilesMayHaveChanged();
}
