#include "WifiWindow.h"

#include "NetworksTab.h"
#include "ProfilesTab.h"
#include "SpeedTestTab.h"
#include "WifiUiStyle.h"
#include "core/IconHelper.h"
#include "core/Logger.h"
#include "engine/WlanController.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
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
    m_adapterCombo->clear();
    for (const WifiAdapterInfo& a : list)
        m_adapterCombo->addItem(a.description, a.guidString);

    if (list.isEmpty())
    {
        m_warningLabel->setText("⚠ Không tìm thấy adapter WiFi nào trên máy này" + (error.isEmpty() ? QString() : (": " + error)));
        m_warningLabel->setVisible(true);
        m_tabs->setEnabled(false);
    }
    else
    {
        m_tabs->setEnabled(true);
        onAdapterChanged(0);
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
