#include "VpnTab.h"

#include "AddVpnProfileDialog.h"
#include "VpnUiStyle.h"
#include "engine/DirectProfileStore.h"
#include "engine/VpnConnector.h"

#include <QCheckBox>
#include <QColor>
#include <QGroupBox>
#include <QHostInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

VpnTab::VpnTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_connector = new VpnConnector(this);
    connect(m_connector, &VpnConnector::operationFinished, this, &VpnTab::onConnectorFinished);

    m_ipChecker = new PublicIpChecker(this);
    connect(m_ipChecker, &PublicIpChecker::result, this, &VpnTab::onIpResult);
    connect(m_ipChecker, &PublicIpChecker::errorOccurred, this, &VpnTab::onIpError);

    m_ipv6Checker = new PublicIpv6Checker(this);
    connect(m_ipv6Checker, &PublicIpv6Checker::result, this, &VpnTab::onIpv6Result);
    m_ipv6Guard = new Ipv6GuardSession(this);
    connect(m_ipv6Guard, &Ipv6GuardSession::ready, this, &VpnTab::onGuardReady);
    connect(m_ipv6Guard, &Ipv6GuardSession::stateChanged, this, &VpnTab::onGuardStateChanged);

    reloadConnections();
    onCheckLocationClicked();
}

VpnTab::~VpnTab()
{
    // KHÔNG chỉ wait(3000) suông - bắt tay VPN thật có thể mất tới 45 giây (máy chủ chậm/xa), 3 giây
    // gần như chắc chắn không đủ trong trường hợp đó, khiến QThread bị hủy đối tượng trong lúc vẫn đang
    // thực sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt). Phải HỦY (an toàn) rồi mới chờ.
    cancelAndWait(3000);
    // Luồng nền đang chạy một lệnh PowerShell (liệt kê/thêm/xóa hồ sơ, tối đa ~20 giây) cũng phải thoát
    // hẳn trước khi đối tượng QThread của nó bị hủy cùng tab này.
    if (m_backgroundThread)
        m_backgroundThread->wait();
}

bool VpnTab::isBusy() const
{
    return (m_connector && m_connector->isRunning()) || m_waitingGuard;
}

void VpnTab::abortGuardWait()
{
    if (!m_waitingGuard)
        return;
    // Chưa quay số: xóa trắng mật khẩu đã nạp vào VpnConnector và bỏ việc chặn IPv6 đang chuẩn bị.
    m_waitingGuard = false;
    m_connector->setDisconnectTarget(m_guardedConnection);
    m_guardedConnection.clear();
    m_pendingConnectionName.clear();
    m_pendingUsername.clear();
    m_ipv6Guard->release();
}

void VpnTab::cancelAndWait(int waitMs)
{
    abortGuardWait();
    if (!isBusy())
        return;
    m_connector->requestCancel();
    if (!m_connector->wait(waitMs))
        m_connector->wait(); // vẫn chưa thoát - đợi thêm, không bao giờ để nơi gọi hủy đối tượng lúc còn chạy
}

void VpnTab::requestCancelNoWait()
{
    abortGuardWait();
    if (isBusy())
        m_connector->requestCancel();
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
    m_locationIpv6Label = new QLabel("", this);
    m_locationIpv6Label->setStyleSheet("color: #57606a; font-size: 12px;");
    m_locationIpv6Label->setWordWrap(true);
    m_locationIpv6Label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    locTextLayout->addWidget(m_locationIpLabel);
    locTextLayout->addWidget(m_locationPlaceLabel);
    locTextLayout->addWidget(m_locationIpv6Label);
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
    bottomRow->addSpacing(12);
    m_ipv6GuardCheck = new QCheckBox("🛡 Chặn rò rỉ IPv6", this);
    m_ipv6GuardCheck->setChecked(true);
    m_ipv6GuardCheck->setToolTip(
        "Áp dụng cho kết nối \"toàn bộ lưu lượng qua VPN\" (không chia tuyến). Nhiều VPN chỉ đổi IPv4 - nếu mạng của bạn có "
        "IPv6, IPv6 thật vẫn ra Internet trực tiếp và trang web thấy vị trí thật.\nBật ô này: chặn IPv6 ra Internet trong "
        "suốt phiên (trừ khi chính VPN cấp IPv6 - lúc đó IPv6 đi qua VPN). Cần quyền Administrator MỘT lần lúc kết nối, chỉ "
        "khi mạng thật sự có IPv6.");
    bottomRow->addWidget(m_ipv6GuardCheck);
    m_ipv6GuardLabel = new QLabel("", this);
    m_ipv6GuardLabel->setWordWrap(true);
    m_ipv6GuardLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    bottomRow->addWidget(m_ipv6GuardLabel, 1);
    root->addLayout(bottomRow);
}

void VpnTab::runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done)
{
    // Get-VpnConnection/Add-VpnConnection/Remove-VpnConnection qua PowerShell mất 1-3 giây (tối đa 20 giây
    // theo PowerShellRunner) - trước đây chạy thẳng trên luồng giao diện, làm cửa sổ đứng hình ngay lúc
    // mở và sau mỗi lần thêm/xóa/kết nối.
    m_statusLabel->setText(statusText);
    QThread* thread = QThread::create(std::move(work));
    thread->setParent(this);
    m_backgroundThread = thread;
    connect(thread, &QThread::finished, this, [this, thread, done = std::move(done)]() {
        if (m_backgroundThread == thread)
            m_backgroundThread = nullptr;
        thread->deleteLater();
        done();
        updateButtons();
    });
    thread->start();
    updateButtons();
}

void VpnTab::reloadConnections()
{
    if (m_backgroundThread)
        return;

    auto connections = std::make_shared<QList<VpnConnectionStatus>>();
    auto error = std::make_shared<QString>();
    // Giữ nguyên dòng thông báo đang hiện (vd kết quả kết nối vừa xong) nếu có - chỉ ghi "đang tải" khi trống.
    const QString keepStatus = m_statusLabel->text();
    runInBackground(
        keepStatus.isEmpty() ? QString("⏳ Đang đọc danh sách hồ sơ VPN...") : keepStatus,
        [connections, error]() { *connections = VpnController::listConnections(error.get()); },
        [this, connections, error, keepStatus]() {
            populateConnections(*connections, *error);
            if (error->isEmpty() && !keepStatus.isEmpty() && !keepStatus.startsWith("Có "))
                m_statusLabel->setText(keepStatus);
        });
}

void VpnTab::populateConnections(const QList<VpnConnectionStatus>& connections, const QString& error)
{
    // Nhớ tên đang chọn để chọn lại sau khi nạp (danh sách được dựng lại từ đầu).
    const auto* previous = selectedConnection();
    const QString previousName = previous ? previous->name : QString();

    m_table->clearSelection();
    m_connections = connections;
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
        if (!previousName.isEmpty() && c.name == previousName)
            m_table->selectRow(i);
    }

    m_statusLabel->setText(error.isEmpty() ? QString("Có %1 hồ sơ VPN.").arg(m_connections.size()) : ("⚠ " + error));
    updateButtons();
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

void VpnTab::updateButtons()
{
    const auto* c = selectedConnection();
    // Bận = đang kết nối/ngắt kết nối HOẶC đang chạy một lệnh PowerShell nền. Trong lúc bận khóa cả Làm
    // mới/Thêm/Xóa và bảng: trước đây chỉ nút Kết nối bị tắt, nên vẫn bấm Xóa được (Remove-VpnConnection
    // -Force) đúng lúc hồ sơ đó đang quay số.
    const bool busy = m_connector->isRunning() || m_backgroundThread != nullptr || m_waitingGuard;
    m_refreshBtn->setEnabled(!busy);
    m_addBtn->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_removeBtn->setEnabled(c != nullptr && !busy && !c->isConnected());
    m_connectBtn->setEnabled(c != nullptr && !c->isConnected() && !busy);
    m_connectBtn->setVisible(!(c && c->isConnected()));
    m_disconnectBtn->setVisible(c && c->isConnected());
    m_disconnectBtn->setEnabled(!busy);
    m_ipv6GuardCheck->setEnabled(!busy);
}

void VpnTab::onRowSelectionChanged()
{
    updateButtons();
}

void VpnTab::onRefreshClicked()
{
    m_statusLabel->clear();
    reloadConnections();
}

void VpnTab::onAddClicked()
{
    if (m_backgroundThread || m_connector->isRunning())
        return;

    AddVpnProfileDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // Chế độ GlobalProtect/FortiClient: không phải kết nối VPN của Windows - lưu vào kho hồ sơ riêng của ứng dụng, hiện ở
    // tab "VPN đã cài sẵn" (kết nối trực tiếp, tài khoản hỏi lúc kết nối).
    if (dlg.mode() != AddVpnProfileDialog::Mode::Normal)
    {
        const DirectVpnProfile direct = dlg.directProfile();
        QString error;
        if (!DirectProfileStore::add(direct, &error))
        {
            QMessageBox::warning(this, "Không thêm được", error);
            return;
        }
        m_statusLabel->setText(QString("✓ Đã thêm hồ sơ \"%1\" (%2). Xem và kết nối ở tab \"VPN đã cài sẵn\".")
                                   .arg(direct.name, DirectProfileStore::clientName(direct.protocol)));
        emit directProfileAdded(direct.name, static_cast<int>(direct.protocol));
        return;
    }

    const VpnProfile profile = dlg.profile();
    if (profile.name.isEmpty() || profile.serverAddress.isEmpty())
    {
        QMessageBox::warning(this, "Thiếu thông tin", "Vui lòng nhập tên hồ sơ và địa chỉ máy chủ.");
        return;
    }

    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang thêm hồ sơ \"" + profile.name + "\"...",
        [profile, ok, error]() { *ok = VpnController::addConnection(profile, error.get()); },
        [this, ok, error]() {
            if (!*ok)
            {
                m_statusLabel->clear();
                QMessageBox::critical(this, "Không thêm được", *error);
            }
            else
            {
                // Thêm thành công; *error (nếu có) chỉ là cảnh báo không lưu được nhãn/tên đăng nhập.
                m_statusLabel->setText(error->isEmpty() ? QString() : ("⚠ " + *error));
            }
            reloadConnections();
        });
}

void VpnTab::onRemoveClicked()
{
    const auto* c = selectedConnection();
    if (!c || m_backgroundThread || m_connector->isRunning())
        return;

    const QString name = c->name;
    if (QMessageBox::question(this, "Xóa hồ sơ VPN",
                              QString("Xóa hồ sơ \"%1\" khỏi Windows?\n\nĐây là kết nối VPN của hệ thống (cũng hiện trong "
                                      "Cài đặt > Mạng > VPN của Windows), không chỉ riêng trong ứng dụng này.")
                                  .arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    if (m_backgroundThread || m_connector->isRunning())
        return; // hộp thoại chạy vòng lặp sự kiện riêng - trạng thái có thể đã đổi

    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang xóa hồ sơ \"" + name + "\"...",
        [name, ok, error]() { *ok = VpnController::removeConnection(name, error.get()); },
        [this, ok, error]() {
            m_statusLabel->clear();
            if (!*ok)
                QMessageBox::critical(this, "Không xóa được", *error);
            else if (!error->isEmpty())
                m_statusLabel->setText("⚠ " + *error);
            reloadConnections();
        });
}

void VpnTab::onConnectClicked()
{
    const auto* c = selectedConnection();
    if (!c || m_backgroundThread || m_connector->isRunning())
        return;
    const QString name = c->name;
    const QString savedUsername = c->username;

    // Điền sẵn tên đăng nhập đã lưu cho hồ sơ này (nhập lúc thêm hồ sơ hoặc ở lần kết nối thành công
    // trước) - trước đây ô "Tên đăng nhập" của hộp thoại thêm hồ sơ bị bỏ đi, lần nào cũng phải gõ lại.
    bool ok = false;
    const QString username =
        QInputDialog::getText(this, "Kết nối VPN", "Tên đăng nhập (để trống = dùng thông tin Windows đã nhớ):",
                              QLineEdit::Normal, savedUsername, &ok).trimmed();
    if (!ok)
        return;
    QString password;
    if (!username.isEmpty())
    {
        password = QInputDialog::getText(this, "Kết nối VPN", "Mật khẩu:", QLineEdit::Password, QString(), &ok);
        if (!ok)
            return;
    }
    if (m_backgroundThread || m_connector->isRunning())
        return;

    m_pendingConnectionName = name;
    m_pendingUsername = username;
    // Mật khẩu đi thẳng vào bộ nhớ của VpnConnector (không qua dòng lệnh tiến trình con nào) - lớp đó tự
    // xóa trắng vùng nhớ chứa mật khẩu ngay sau khi gọi RAS API, xem VpnConnector.h.
    m_connector->setConnectTarget(name, username, password);

    // Kết nối "toàn bộ lưu lượng qua VPN" = đang đổi IP: chặn IPv6 thật TRƯỚC khi quay số (VPN chia tuyến kiểu cơ quan
    // thì không - Internet vốn đi thẳng, chặn IPv6 chỉ làm mất IPv6 vô ích).
    if (m_ipv6GuardCheck->isChecked() && !c->splitTunneling)
    {
        m_waitingGuard = true;
        m_guardedConnection = name;
        m_statusLabel->setText("⏳ Đang kiểm tra IPv6 của máy trước khi kết nối \"" + name + "\"...");
        updateButtons();
        QHostInfo::lookupHost(c->serverAddress, this, [this, name](const QHostInfo& info) {
            if (!m_waitingGuard || m_guardedConnection != name)
                return;
            QStringList v6;
            for (const QHostAddress& a : info.addresses())
                if (a.protocol() == QAbstractSocket::IPv6Protocol)
                    v6 << a.toString();
            m_ipv6Guard->begin(name, v6);
        });
        return;
    }
    m_statusLabel->setText("⏳ Đang kết nối \"" + name + "\"...");
    m_connector->start();
    updateButtons();
}

void VpnTab::onGuardReady(bool ok, QString message)
{
    if (!m_waitingGuard)
        return;
    m_waitingGuard = false;
    if (!ok)
    {
        const QString name = m_guardedConnection;
        m_guardedConnection.clear();
        m_pendingConnectionName.clear();
        m_pendingUsername.clear();
        m_connector->setDisconnectTarget(name); // xóa trắng mật khẩu đã nạp - KHÔNG start()
        m_ipv6Guard->release();
        m_statusLabel->setText("⚠ " + message + " Chưa kết nối để IPv6 thật không bị lộ. Bỏ tích \"Chặn rò rỉ IPv6\" nếu "
                               "vẫn muốn kết nối không chặn.");
        updateButtons();
        return;
    }
    m_statusLabel->setText("⏳ Đang kết nối \"" + m_guardedConnection + "\"...");
    m_connector->start();
    updateButtons();
}

void VpnTab::onGuardStateChanged(Ipv6GuardSession::State state, QString text)
{
    m_ipv6GuardLabel->setText(text);
    const char* color = state == Ipv6GuardSession::State::Failed   ? "#cf222e"
                        : state == Ipv6GuardSession::State::Active ? "#1a7f37"
                                                                   : "#57606a";
    m_ipv6GuardLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(color));
}

void VpnTab::onDisconnectClicked()
{
    const auto* c = selectedConnection();
    if (!c || m_backgroundThread || m_connector->isRunning())
        return;
    m_pendingConnectionName.clear();
    m_pendingUsername.clear();
    m_disconnectingName = c->name;
    m_connector->setDisconnectTarget(c->name);
    m_statusLabel->setText("⏳ Đang ngắt kết nối \"" + c->name + "\"...");
    m_connector->start();
    updateButtons();
}

void VpnTab::onConnectorFinished(bool success, QString message)
{
    // Kết nối thành công với một tên đăng nhập mới -> nhớ lại cho lần sau (chỉ tên, KHÔNG có mật khẩu).
    if (success && !m_pendingConnectionName.isEmpty() && !m_pendingUsername.isEmpty())
    {
        VpnController::VpnProfileMeta meta = VpnController::profileMeta(m_pendingConnectionName);
        if (meta.username != m_pendingUsername)
        {
            meta.username = m_pendingUsername;
            VpnController::setProfileMeta(m_pendingConnectionName, meta, nullptr);
        }
    }
    m_pendingConnectionName.clear();
    m_pendingUsername.clear();

    // Chặn IPv6 gắn với kết nối đang được bảo vệ: nối thất bại, hoặc chính kết nối đó vừa ngắt xong -> gỡ chặn. Ngắt
    // THẤT BẠI thì giữ (VPN có thể vẫn nối; tiến trình phụ chỉ thôi chặn khi VPN ngắt thật - xem Ipv6GuardHelper.h).
    if (!m_guardedConnection.isEmpty())
    {
        const bool wasDisconnect = !m_disconnectingName.isEmpty();
        if ((!wasDisconnect && !success) ||
            (wasDisconnect && success && m_disconnectingName.compare(m_guardedConnection, Qt::CaseInsensitive) == 0))
        {
            m_ipv6Guard->release();
            m_guardedConnection.clear();
        }
    }
    m_disconnectingName.clear();

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
    if (!m_ipv6Checker->isRunning())
    {
        m_locationIpv6Label->setStyleSheet("color: #57606a; font-size: 12px;");
        m_locationIpv6Label->setText("IPv6: ⏳ đang kiểm tra...");
        m_ipv6Checker->check();
    }
}

void VpnTab::onIpv6Result(PublicIpv6Info info)
{
    // "Đang nối VPN" = có một kết nối toàn-bộ-lưu-lượng đang Connected (VPN chia tuyến không đổi IP).
    bool fullTunnelUp = false;
    for (const VpnConnectionStatus& c : std::as_const(m_connections))
        if (c.isConnected() && !c.splitTunneling)
            fullTunnelUp = true;
    bool warn = false;
    m_locationIpv6Label->setText(Ipv6GuardSession::describePublicIpv6(info, fullTunnelUp, m_ipv6Guard->state(),
                                                                     m_ipv6Guard->tunnelHasIpv6(), &warn));
    m_locationIpv6Label->setStyleSheet(QString("color: %1; font-size: 12px;").arg(warn ? "#cf222e" : "#57606a"));
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
