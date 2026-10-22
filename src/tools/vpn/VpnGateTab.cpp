#include "VpnGateTab.h"

#include "VpnUiStyle.h"
#include "engine/VpnConnector.h"
#include "engine/VpnController.h"
#include "engine/VpnGateCache.h"
#include "engine/VpnGateClient.h"
#include "model/VpnProfile.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

#include <QDateTime>

namespace
{
// Hồ sơ VPN TẠM dùng cho mọi kết nối VPN Gate - luôn ghi đè (một tên duy nhất), tự xóa khi ngắt kết nối.
const QString kTempProfileName = QStringLiteral("OneForAll-VPNGate");
// Tài khoản CÔNG KHAI của VPN Gate cho L2TP/IPsec (công bố trên trang hướng dẫn của họ - không phải bí mật).
const QString kVpnGateUser = QStringLiteral("vpn");
const QString kVpnGatePass = QStringLiteral("vpn");
const QString kVpnGatePsk = QStringLiteral("vpn");

QString speedText(double mbps)
{
    if (mbps <= 0)
        return QStringLiteral("—");
    return QString::number(mbps, 'f', mbps >= 10 ? 0 : 1) + " Mbps";
}
} // namespace

VpnGateTab::VpnGateTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_client = new VpnGateClient(this);
    connect(m_client, &VpnGateClient::finished, this, &VpnGateTab::onServersFetched);

    m_connector = new VpnConnector(this);
    connect(m_connector, &VpnConnector::operationFinished, this, &VpnGateTab::onConnectorFinished);

    m_ipChecker = new PublicIpChecker(this);
    connect(m_ipChecker, &PublicIpChecker::result, this, &VpnGateTab::onIpResult);
    connect(m_ipChecker, &PublicIpChecker::errorOccurred, this, &VpnGateTab::onIpError);

    // Nạp danh sách đã TÍCH LŨY từ những lần tải trước (API chỉ trả ~100 máy chủ/lần - gộp lại để có nhiều hơn).
    m_allServers = VpnGateCache::load();
    if (!m_allServers.isEmpty())
    {
        rebuildCountryFilter();
        applyFilter();
        m_countLabel->setText(QString("Đã có %1 máy chủ (gộp từ các lần tải trước). Bấm \"Tải thêm máy chủ\" để cập nhật.")
                                  .arg(m_allServers.size()));
    }

    onCheckLocationClicked();
    updateButtons();
}

VpnGateTab::~VpnGateTab()
{
    // Hủy (an toàn) phiên RAS rồi chờ - hủy QThread đang chạy là hành vi KHÔNG XÁC ĐỊNH (xem VpnTab.cpp).
    cancelAndWait(3000);
    if (m_backgroundThread)
        m_backgroundThread->wait();
}

bool VpnGateTab::isBusy() const
{
    return (m_connector && m_connector->isRunning()) || m_backgroundThread != nullptr;
}

void VpnGateTab::cancelAndWait(int waitMs)
{
    if (m_connector && m_connector->isRunning())
    {
        m_connector->requestCancel();
        if (!m_connector->wait(waitMs))
            m_connector->wait();
    }
}

void VpnGateTab::requestCancelNoWait()
{
    if (m_connector && m_connector->isRunning())
        m_connector->requestCancel();
}

void VpnGateTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Kết nối tới máy chủ CÔNG CỘNG MIỄN PHÍ của VPN Gate (dự án nghiên cứu của ĐH Tsukuba, Nhật Bản) để "
        "đổi IP ra quốc gia bạn chọn. Đây là relay do tình nguyện viên chia sẻ: IP đổi theo thời gian, tốc độ/"
        "ổn định KHÔNG được bảo đảm, bạn tự chịu trách nhiệm khi dùng. Kết nối bằng VPN tích hợp sẵn của "
        "Windows (mặc định SSTP qua cổng 443 - không cần cài thêm gì, không cần quyền Administrator). Không phải "
        "máy chủ nào cũng bật đủ giao thức - nếu lỗi hãy thử máy chủ khác.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("warn"));
    root->addWidget(hint);

    // Sửa lỗi L2TP sau NAT (AssumeUDPEncapsulationContextOnSendRule) - nguyên nhân phổ biến khiến L2TP tới
    // máy chủ công cộng (thường nằm sau NAT) thất bại trên Windows. Nút này có quyền Admin đặt khóa registry
    // Microsoft tài liệu hóa, rồi cần khởi động lại máy.
    auto* natRow = new QHBoxLayout();
    auto* natHint = new QLabel("Chỉ cho cách L2TP/IPsec - nếu hay báo lỗi \"handle/khởi tạo\": máy chủ công cộng thường nằm sau NAT -", this);
    natHint->setStyleSheet("color: #57606a; font-size: 11px;");
    natHint->setWordWrap(true);
    natRow->addWidget(natHint, 1);
    m_natFixBtn = new QPushButton("🔧 Sửa lỗi L2TP sau NAT (Admin)", this);
    m_natFixBtn->setStyleSheet(VpnUi::buttonStyle());
    m_natFixBtn->setCursor(Qt::PointingHandCursor);
    m_natFixBtn->setToolTip("Đặt khóa registry AssumeUDPEncapsulationContextOnSendRule=2 (Microsoft tài liệu hóa) để "
                            "L2TP/IPsec kết nối được tới máy chủ nằm sau NAT. Cần quyền Administrator và KHỞI ĐỘNG LẠI máy.");
    connect(m_natFixBtn, &QPushButton::clicked, this, &VpnGateTab::onNatFixClicked);
    natRow->addWidget(m_natFixBtn);
    root->addLayout(natRow);

    // ---- Vị trí hiện tại (theo IP) ----
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
    connect(m_refreshLocationBtn, &QPushButton::clicked, this, &VpnGateTab::onCheckLocationClicked);
    locLayout->addWidget(m_refreshLocationBtn);
    root->addWidget(locGroup);

    // ---- Hàng điều khiển: tải danh sách + lọc quốc gia + kết nối/ngắt ----
    auto* topRow = new QHBoxLayout();
    m_fetchBtn = new QPushButton("🔄 Tải thêm máy chủ", this);
    m_fetchBtn->setStyleSheet(VpnUi::buttonStyle());
    m_fetchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_fetchBtn, &QPushButton::clicked, this, &VpnGateTab::onFetchClicked);
    topRow->addWidget(m_fetchBtn);

    auto* filterLabel = new QLabel("Quốc gia:", this);
    topRow->addWidget(filterLabel);
    m_countryFilter = new QComboBox(this);
    m_countryFilter->setStyleSheet(VpnUi::inputStyle());
    m_countryFilter->setMinimumWidth(220);
    m_countryFilter->addItem("Tất cả quốc gia");
    connect(m_countryFilter, &QComboBox::currentIndexChanged, this, &VpnGateTab::onCountryFilterChanged);
    topRow->addWidget(m_countryFilter, 1);

    auto* methodLabel = new QLabel("Cách kết nối:", this);
    topRow->addWidget(methodLabel);
    m_methodCombo = new QComboBox(this);
    m_methodCombo->setStyleSheet(VpnUi::inputStyle());
    // SSTP (TLS/443 tới <HostName>.opengw.net) không đụng tầng IPsec nên chạy được cả khi L2TP native của
    // Windows hỏng/bị chặn (đã xác nhận thật: L2TP lỗi 791 nhưng SSTP đổi được IP trên cùng máy) - mặc định.
    m_methodCombo->addItem("SSTP - cổng 443 (khuyến nghị)", "sstp");
    m_methodCombo->addItem("L2TP/IPsec", "l2tp");
    m_methodCombo->setToolTip("SSTP: kết nối TLS qua cổng 443 tới tên miền <máy chủ>.opengw.net - vượt được mạng chặn L2TP.\n"
                              "L2TP/IPsec: qua UDP 500/4500/1701 tới IP máy chủ.");
    topRow->addWidget(m_methodCombo);

    m_connectBtn = new QPushButton("🔌 Kết nối", this);
    m_connectBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_connectBtn, &QPushButton::clicked, this, &VpnGateTab::onConnectClicked);
    topRow->addWidget(m_connectBtn);

    m_disconnectBtn = new QPushButton("⛔ Ngắt kết nối", this);
    m_disconnectBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_disconnectBtn->setCursor(Qt::PointingHandCursor);
    m_disconnectBtn->setVisible(false);
    connect(m_disconnectBtn, &QPushButton::clicked, this, &VpnGateTab::onDisconnectClicked);
    topRow->addWidget(m_disconnectBtn);

    m_checkBtn = new QPushButton("🔎 Kiểm tra kết nối", this);
    m_checkBtn->setStyleSheet(VpnUi::buttonStyle());
    m_checkBtn->setCursor(Qt::PointingHandCursor);
    m_checkBtn->setToolTip("Kiểm tra máy chủ đang chọn có còn \"chạm\" được không (TCP) trước khi kết nối VPN.");
    connect(m_checkBtn, &QPushButton::clicked, this, &VpnGateTab::onCheckConnectivityClicked);
    topRow->addWidget(m_checkBtn);
    root->addLayout(topRow);

    m_countLabel = new QLabel("Bấm \"Tải danh sách máy chủ\" để lấy danh sách máy chủ công cộng mới nhất.", this);
    m_countLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_countLabel->setWordWrap(true);
    root->addWidget(m_countLabel);

    // ---- Bảng máy chủ ----
    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"Quốc gia", "Ping (ms)", "Tốc độ", "Số phiên", "IP máy chủ"});
    m_table->setStyleSheet(VpnUi::tableStyle());
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &VpnGateTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel("", this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);
}

void VpnGateTab::onFetchClicked()
{
    if (isBusy())
        return;
    m_fetchBtn->setEnabled(false);
    m_countLabel->setText("⏳ Đang tải danh sách máy chủ công cộng từ VPN Gate (có thể mất vài giây)...");
    m_client->fetch();
}

void VpnGateTab::onServersFetched(QList<VpnGateServer> servers, QString error)
{
    m_fetchBtn->setEnabled(!isBusy());
    if (!error.isEmpty())
    {
        m_countLabel->setText("⚠ " + error);
        return;
    }
    // GỘP vào danh sách đã tích lũy (dedup theo IP, loại máy chủ quá cũ) rồi lưu lại - API chỉ trả ~100
    // máy chủ "tốt nhất" mỗi lần (xoay vòng theo thời gian), gộp qua nhiều lần tải để có nhiều hơn.
    const int before = m_allServers.size();
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    m_allServers = VpnGateCache::merge(m_allServers, servers, now);
    VpnGateCache::save(m_allServers);
    rebuildCountryFilter();
    applyFilter();
    const int added = m_allServers.size() - before;
    m_countLabel->setText(QString("Lần này nhận %1 máy chủ; tổng đã gộp: %2 máy chủ%3. Chọn một dòng rồi bấm \"Kết nối\" "
                                  "(bấm \"Tải thêm máy chủ\" định kỳ để tích lũy thêm).")
                              .arg(servers.size())
                              .arg(m_allServers.size())
                              .arg(added > 0 ? QString(" (+%1 mới)").arg(added) : QString()));
}

void VpnGateTab::rebuildCountryFilter()
{
    const QString previous = m_countryFilter->currentIndex() > 0 ? m_countryFilter->currentText() : QString();

    // Đếm số máy chủ theo quốc gia.
    QMap<QString, int> counts; // QMap tự sắp theo tên quốc gia (A-Z)
    for (const VpnGateServer& s : m_allServers)
        counts[s.countryLong]++;

    QSignalBlocker block(m_countryFilter); // không kích hoạt onCountryFilterChanged khi dựng lại
    m_countryFilter->clear();
    m_countryFilter->addItem(QString("Tất cả quốc gia (%1)").arg(m_allServers.size()), QString());
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
        m_countryFilter->addItem(QString("%1 (%2)").arg(it.key()).arg(it.value()), it.key());

    // Giữ lựa chọn quốc gia cũ nếu vẫn còn.
    if (!previous.isEmpty())
    {
        const int idx = m_countryFilter->findText(previous);
        if (idx >= 0)
            m_countryFilter->setCurrentIndex(idx);
    }
}

void VpnGateTab::applyFilter()
{
    const QString country = m_countryFilter->currentData().toString(); // rỗng = tất cả

    m_shown.clear();
    for (const VpnGateServer& s : m_allServers)
        if (country.isEmpty() || s.countryLong == country)
            m_shown.push_back(s);

    m_table->setRowCount(m_shown.size());
    for (int i = 0; i < m_shown.size(); ++i)
    {
        const VpnGateServer& s = m_shown[i];
        auto setItem = [&](int col, const QString& text, int align = Qt::AlignLeft) {
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(static_cast<Qt::Alignment>(align) | Qt::AlignVCenter);
            m_table->setItem(i, col, item);
        };
        const QString flagName = s.countryShort.isEmpty() ? s.countryLong : (s.countryLong + "  [" + s.countryShort + "]");
        setItem(0, flagName);
        setItem(1, s.ping > 0 ? QString::number(s.ping) : QStringLiteral("—"), Qt::AlignRight);
        setItem(2, speedText(s.speedMbps()), Qt::AlignRight);
        setItem(3, QString::number(s.sessions), Qt::AlignRight);
        setItem(4, s.ip, Qt::AlignLeft);
    }
    updateButtons();
}

void VpnGateTab::onCountryFilterChanged()
{
    applyFilter();
}

const VpnGateServer* VpnGateTab::selectedServer() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_shown.size())
        return nullptr;
    return &m_shown[row];
}

void VpnGateTab::onRowSelectionChanged()
{
    updateButtons();
}

void VpnGateTab::updateButtons()
{
    const bool busy = isBusy();
    const bool connected = m_phase == Phase::Connected;
    const VpnGateServer* s = selectedServer();

    m_fetchBtn->setEnabled(!busy && !connected);
    m_countryFilter->setEnabled(!busy);
    m_table->setEnabled(!busy);
    m_connectBtn->setVisible(!connected);
    m_connectBtn->setEnabled(!busy && !connected && s != nullptr);
    m_disconnectBtn->setVisible(connected);
    m_disconnectBtn->setEnabled(!busy && connected);
    m_checkBtn->setEnabled(s != nullptr);
    if (m_natFixBtn)
        m_natFixBtn->setEnabled(!busy);
}

void VpnGateTab::runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done)
{
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

void VpnGateTab::onConnectClicked()
{
    if (isBusy() || m_phase != Phase::Idle)
        return;
    const VpnGateServer* s = selectedServer();
    if (!s)
        return;

    // Dựng hồ sơ VPN TẠM (ghi đè nếu đã có) cho máy chủ đã chọn, tài khoản công khai của VPN Gate. Tạo hồ
    // sơ chạy nền (PowerShell ~1-3 giây), xong mới quay số RAS.
    VpnProfile p;
    p.name = kTempProfileName;
    p.countryLabel = s->countryLong;
    p.username = kVpnGateUser;
    const bool useSstp = !m_methodCombo || m_methodCombo->currentData().toString() != "l2tp";
    if (useSstp)
    {
        // SSTP phải tới đúng TÊN MIỀN (chứng chỉ là *.opengw.net) - kết nối bằng IP sẽ bị Windows từ chối.
        const QString host = s->sstpHost();
        if (host.isEmpty())
        {
            m_statusLabel->setText("⚠ Máy chủ này thiếu tên miền hợp lệ cho SSTP - chọn máy chủ khác hoặc dùng L2TP/IPsec.");
            return;
        }
        p.serverAddress = host;
        p.tunnelType = VpnTunnelType::Sstp;
    }
    else
    {
        p.serverAddress = s->ip;
        p.tunnelType = VpnTunnelType::L2tp;
        p.l2tpPsk = kVpnGatePsk;
    }

    const QString country = s->countryLong;
    m_connectedCountry = country;
    m_connectedMethod = useSstp ? QStringLiteral("SSTP") : QStringLiteral("L2TP");
    m_phase = Phase::Connecting;

    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang chuẩn bị kết nối tới " + country + "...",
        [p, ok, error]() { *ok = VpnController::addConnection(p, error.get()); },
        [this, ok, error, country]() {
            if (!*ok)
            {
                m_phase = Phase::Idle;
                m_statusLabel->setText("⚠ Không tạo được kết nối tạm: " + *error);
                return;
            }
            // Quay số RAS tới hồ sơ tạm với tài khoản công khai của VPN Gate (mật khẩu đi thẳng vào bộ
            // nhớ VpnConnector, tự xóa trắng sau khi dùng - xem VpnConnector.h).
            m_connector->setConnectTarget(kTempProfileName, kVpnGateUser, kVpnGatePass);
            m_statusLabel->setText("⏳ Đang kết nối tới máy chủ " + country + " qua " + m_connectedMethod + " (tối đa 45 giây)...");
            m_connector->start();
            updateButtons();
        });
}

void VpnGateTab::onDisconnectClicked()
{
    if (isBusy() || m_phase != Phase::Connected)
        return;
    m_phase = Phase::Disconnecting;
    m_statusLabel->setText("⏳ Đang ngắt kết nối...");
    m_connector->setDisconnectTarget(kTempProfileName);
    m_connector->start();
    updateButtons();
}

void VpnGateTab::onConnectorFinished(bool success, QString message)
{
    if (m_phase == Phase::Connecting)
    {
        if (success)
        {
            m_phase = Phase::Connected;
            m_statusLabel->setText("✓ Đã kết nối qua " + m_connectedCountry + " (" + m_connectedMethod +
                                   "). IP của bạn giờ hiển thị ở quốc gia này.");
            onCheckLocationClicked();
        }
        else
        {
            m_phase = Phase::Idle;
            m_statusLabel->setText(
                "⚠ Không kết nối được máy chủ này qua " + m_connectedMethod + " (" +
                (message.isEmpty() ? QString("thất bại") : message) + "). Máy chủ có thể không bật " +
                m_connectedMethod + " hoặc bị tường lửa chặn - hãy chọn máy chủ khác"
                + (m_connectedMethod == "SSTP" ? QString() : QString(" hoặc đổi sang SSTP")) + " rồi thử lại.");
            removeTempProfileInBackground(); // dọn hồ sơ tạm (không còn kết nối nên xóa được)
        }
    }
    else if (m_phase == Phase::Disconnecting)
    {
        m_phase = Phase::Idle;
        m_statusLabel->setText(success ? "Đã ngắt kết nối." : ("⚠ " + (message.isEmpty() ? QString("Ngắt kết nối thất bại.") : message)));
        onCheckLocationClicked();
        removeTempProfileInBackground();
    }
    updateButtons();
}

void VpnGateTab::removeTempProfileInBackground()
{
    // Dọn hồ sơ VPN tạm khỏi Windows (best-effort) - chỉ gọi khi KHÔNG còn kết nối (Remove-VpnConnection
    // từ chối xóa hồ sơ đang kết nối). Bỏ qua kết quả: hồ sơ cùng tên sẽ bị ghi đè ở lần kết nối sau.
    if (m_backgroundThread)
        return;
    runInBackground(
        m_statusLabel->text(), []() { VpnController::removeConnection(kTempProfileName, nullptr); }, [this]() {});
}

void VpnGateTab::onNatFixClicked()
{
    if (isBusy())
        return;
    const auto ans = QMessageBox::question(
        this, "Sửa lỗi L2TP sau NAT",
        "L2TP/IPsec tới máy chủ nằm sau NAT (đa số máy chủ công cộng/miễn phí) thường THẤT BẠI trên Windows "
        "cho tới khi bật một khóa registry được Microsoft tài liệu hóa:\n\n"
        "HKLM\\SYSTEM\\CurrentControlSet\\Services\\PolicyAgent\\AssumeUDPEncapsulationContextOnSendRule = 2\n\n"
        "Thao tác này cần quyền Administrator (sẽ hiện hộp thoại UAC) và CHỈ CÓ HIỆU LỰC SAU KHI KHỞI ĐỘNG LẠI "
        "MÁY. Tiếp tục?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes)
        return;

    auto ok = std::make_shared<bool>(false);
    auto error = std::make_shared<QString>();
    auto state = std::make_shared<int>(0); // 0=Unknown,1=Disabled,2=Enabled
    runInBackground(
        "⏳ Đang áp dụng (chờ xác nhận quyền Administrator)...",
        [ok, error, state]() {
            *ok = VpnController::enableAssumeUdpEncapsulationElevated(error.get());
            *state = static_cast<int>(VpnController::assumeUdpEncapsulationState());
        },
        [this, ok, error, state]() {
            if (!*ok)
            {
                m_statusLabel->setText("⚠ Chưa áp dụng được: " + *error);
                return;
            }
            if (*state == static_cast<int>(VpnController::NatTState::Enabled))
                m_statusLabel->setText("✓ Đã bật sửa lỗi L2TP sau NAT. Hãy KHỞI ĐỘNG LẠI máy rồi thử kết nối lại.");
            else
                m_statusLabel->setText("Đã chạy lệnh nhưng chưa xác nhận được khóa đã đặt. Hãy khởi động lại máy rồi thử lại.");
        });
}

void VpnGateTab::onCheckConnectivityClicked()
{
    const VpnGateServer* s = selectedServer();
    if (!s)
        return;
    // Thử cổng 443 (nhiều máy chủ VPN Gate có bật) làm chỉ báo máy chủ còn sống - xem ConnectivityTab.
    emit checkConnectivityRequested(s->ip, 443);
}

void VpnGateTab::onCheckLocationClicked()
{
    m_locationIpLabel->setText("⏳ Đang kiểm tra...");
    m_locationPlaceLabel->clear();
    m_ipChecker->check();
}

void VpnGateTab::onIpResult(PublicIpInfo info)
{
    m_locationIpLabel->setText("IP: " + info.ip);
    QString place = info.city.isEmpty() ? info.country : (info.city + ", " + info.country);
    m_locationPlaceLabel->setText(place.isEmpty() ? "(không xác định được quốc gia)" : ("📍 " + place));
}

void VpnGateTab::onIpError(QString message)
{
    m_locationIpLabel->setText("⚠ Không kiểm tra được");
    m_locationPlaceLabel->setText(message);
}
