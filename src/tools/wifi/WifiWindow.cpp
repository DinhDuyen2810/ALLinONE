#include "WifiWindow.h"

#include "NetworksTab.h"
#include "ProfilesTab.h"
#include "SpeedTestTab.h"
#include "WifiUiStyle.h"
#include "core/IconHelper.h"
#include "core/Logger.h"
#include "engine/WlanController.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QVBoxLayout>

WifiWindow::WifiWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - WiFi Connection");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/wifi.png", 48, 10, 4));
    resize(920, 600);
    setMinimumSize(760, 480);
    setStyleSheet(
        "QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #eaeef2; color: #57606a; padding: 9px 22px; margin-right: 4px; border-top-left-radius: 10px; border-top-right-radius: 10px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0969da; }"
        "QTabBar::tab:hover:!selected { background: #e0e4e9; }"
        "QLabel { background: transparent; }"
        "QToolTip { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; }");

    m_controller = new WlanController(this);
    QString openError;
    m_serviceAvailable = m_controller->open(&openError);

    buildUi();

    if (!m_serviceAvailable)
    {
        m_warningLabel->setText("⚠ " + openError);
        m_warningLabel->setVisible(true);
        m_adapterCombo->setEnabled(false);
        m_tabs->setEnabled(false);
    }
    else
    {
        reloadAdapters();
    }

    Logger::instance().info("WiFi", "Mở cửa sổ WiFi Connection");
}

WifiWindow::~WifiWindow() = default;

void WifiWindow::closeEvent(QCloseEvent* event)
{
    // QUAN TRỌNG: cửa sổ này được WifiTool giữ qua QPointer và TÁI DÙNG ở lần mở sau (không có
    // WA_DeleteOnClose) - đóng cửa sổ (bấm X) mặc định CHỈ ẨN đi, KHÔNG hủy đối tượng. Không có tiến
    // trình ngoài nào để mồ côi ở đây (quét WiFi/đo tốc độ đều thuần API Windows/QNetworkAccessManager
    // trong tiến trình), nhưng polling + đo tốc độ vẫn tiếp tục chạy ngầm vô ích nếu không tự dừng.
    if (m_networksTab)
        m_networksTab->stopActivePolling();
    if (m_speedTestTab)
        m_speedTestTab->stopIfRunning();
    event->accept();
}

void WifiWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (event->spontaneous())
        return; // chỉ là khôi phục từ thu nhỏ - không phải mở lại cửa sổ

    if (!m_shownOnce)
    {
        // Lần hiện đầu: constructor vừa mở dịch vụ + nạp adapter xong, chỉ còn thiếu polling - buildUi()
        // gọi onTabChanged(0) TRƯỚC khi có adapter nào (GUID còn rỗng) nên startActivePolling() lúc đó
        // không làm gì, banner trạng thái đứng yên cho tới khi người dùng đổi tab đi rồi quay lại.
        m_shownOnce = true;
        if (m_serviceAvailable && m_tabs->currentIndex() == 0)
            m_networksTab->startActivePolling();
        return;
    }

    // Cửa sổ được TÁI DÙNG sau khi đóng (xem closeEvent): closeEvent đã tắt polling, và danh sách adapter
    // chỉ được đọc một lần lúc tạo cửa sổ. Không làm lại ở đây thì lần mở sau banner không tự cập nhật,
    // adapter WiFi cắm thêm không hiện ra, và nếu dịch vụ WLAN chưa chạy ở lần mở đầu thì cửa sổ bị khóa
    // cho tới khi khởi động lại cả ứng dụng.
    if (!m_serviceAvailable)
    {
        QString openError;
        m_serviceAvailable = m_controller->open(&openError);
        if (!m_serviceAvailable)
        {
            m_warningLabel->setText("⚠ " + openError);
            m_warningLabel->setVisible(true);
            return;
        }
        m_adapterCombo->setEnabled(true);
    }

    reloadAdapters(); // setAdapter() của từng tab tự đọc lại danh sách mạng/hồ sơ
    if (m_tabs->currentIndex() == 0)
        m_networksTab->startActivePolling();
}

void WifiWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    auto* topBar = new QHBoxLayout();
    auto* adapterLabel = new QLabel("Adapter WiFi:", this);
    adapterLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_adapterCombo = new QComboBox(this);
    m_adapterCombo->setStyleSheet(WifiUi::inputStyle());
    m_adapterCombo->setMinimumWidth(260);
    connect(m_adapterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WifiWindow::onAdapterChanged);
    topBar->addWidget(adapterLabel);
    topBar->addWidget(m_adapterCombo);
    topBar->addStretch();
    root->addLayout(topBar);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(WifiUi::bannerStyle(false) + " color: #9a6700;");
    m_warningLabel->setVisible(false);
    root->addWidget(m_warningLabel);

    m_tabs = new QTabWidget(this);
    m_networksTab = new NetworksTab(m_controller, this);
    m_profilesTab = new ProfilesTab(m_controller, this);
    m_speedTestTab = new SpeedTestTab(m_controller, this);
    m_tabs->addTab(m_networksTab, "📶  Mạng xung quanh");
    m_tabs->addTab(m_profilesTab, "🔑  Hồ sơ đã lưu");
    m_tabs->addTab(m_speedTestTab, "🚀  Đo tốc độ mạng");
    connect(m_tabs, &QTabWidget::currentChanged, this, &WifiWindow::onTabChanged);
    connect(m_networksTab, &NetworksTab::profilesMayHaveChanged, m_profilesTab, &ProfilesTab::reload);
    root->addWidget(m_tabs, 1);

    onTabChanged(0);
}

void WifiWindow::reloadAdapters()
{
    QString error;
    const auto list = m_controller->adapters(&error);

    // Giữ adapter đang chọn nếu nó vẫn còn. Chặn tín hiệu trong lúc dựng lại combo rồi gọi
    // onAdapterChanged() đúng MỘT lần: trước đây addItem() đầu tiên tự phát currentIndexChanged(0) rồi
    // hàm này lại gọi onAdapterChanged(0) thêm lần nữa - mỗi tab đọc lại toàn bộ danh sách hai lần.
    const QString previousGuid = m_adapterCombo->currentData().toString();
    int index = 0;
    {
        QSignalBlocker blocker(m_adapterCombo);
        m_adapterCombo->clear();
        for (const WifiAdapterInfo& a : list)
            m_adapterCombo->addItem(a.description, a.guidString);
        const int previousIndex = previousGuid.isEmpty() ? -1 : m_adapterCombo->findData(previousGuid);
        if (previousIndex >= 0)
            index = previousIndex;
        if (!list.isEmpty())
            m_adapterCombo->setCurrentIndex(index);
    }

    if (list.isEmpty())
    {
        m_warningLabel->setText("⚠ Không tìm thấy adapter WiFi nào trên máy này" + (error.isEmpty() ? QString() : (": " + error)));
        m_warningLabel->setVisible(true);
        m_tabs->setEnabled(false);
    }
    else
    {
        m_warningLabel->setVisible(false);
        m_tabs->setEnabled(true);
        onAdapterChanged(index);
    }
}

void WifiWindow::onAdapterChanged(int index)
{
    if (index < 0 || !m_serviceAvailable)
        return;
    const QString guid = m_adapterCombo->itemData(index).toString();
    m_networksTab->setAdapter(guid);
    m_profilesTab->setAdapter(guid);
    m_speedTestTab->setAdapter(guid);
}

void WifiWindow::onTabChanged(int index)
{
    if (index == 0)
    {
        m_networksTab->startActivePolling();
        m_networksTab->refreshNow();
    }
    else
    {
        m_networksTab->stopActivePolling();
    }
    if (m_tabs->widget(index) == m_profilesTab)
        m_profilesTab->reload();
}
