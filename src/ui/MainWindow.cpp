#include "MainWindow.h"
#include "../tools/autoclick/AutoClickTool.h"
#include "../tools/qr/QRTool.h"
#include "../tools/wifi/WifiTool.h"
#include "../tools/connect/ConnectTool.h"
#include "../tools/diskcleanup/DiskCleanupTool.h"
#include "../tools/android/AndroidControlTool.h"
#include "../tools/vpn/VpnControlTool.h"
#include "../tools/security/SecurityGatewayTool.h"
#include "../tools/downloader/DownloaderTool.h"
#include "core/IconHelper.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QIcon>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    registerTools();

    // Select first tool (Auto Click)
    if (m_toolListWidget->count() > 0)
    {
        m_toolListWidget->setCurrentRow(0);
    }
}

void MainWindow::registerTools()
{
    auto& tm = ToolManager::instance();

    // 1. Auto Click (Main tool implemented)
    tm.registerTool(std::make_unique<AutoClickTool>());

    tm.registerTool(std::make_unique<ConnectTool>());

    tm.registerTool(std::make_unique<QRTool>());

    tm.registerTool(std::make_unique<DiskCleanupTool>());

    tm.registerTool(std::make_unique<AndroidControlTool>());

    tm.registerTool(std::make_unique<DownloaderTool>());

    tm.registerTool(std::make_unique<SecurityGatewayTool>());

    tm.registerTool(std::make_unique<VpnControlTool>());

    tm.registerTool(std::make_unique<WifiTool>());

    // Populate sidebar list
    m_toolListWidget->clear();
    for (const auto& tool : tm.getAllTools())
    {
        auto* item = new QListWidgetItem(tool->icon(), "  " + tool->name());
        item->setSizeHint(QSize(210, 52));
        m_toolListWidget->addItem(item);
    }
}

void MainWindow::setupUi()
{
    setWindowTitle("ONE FOR ALL - Desktop Suite v1.0");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/autoclicker.jpg", 48, 10, 4));
    resize(960, 580);
    setMinimumSize(850, 520);
    setStyleSheet(
        "QMainWindow { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );

    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left Sidebar
    auto* sidebarFrame = new QFrame(this);
    sidebarFrame->setFixedWidth(270);
    sidebarFrame->setStyleSheet("QFrame { background-color: #f0f2f5; border-right: 1px solid #d0d7de; }");

    auto* sidebarLayout = new QVBoxLayout(sidebarFrame);
    sidebarLayout->setContentsMargins(14, 18, 14, 18);
    sidebarLayout->setSpacing(10);

    auto* brandLabel = new QLabel("ONE FOR ALL", this);
    QFont brandFont = brandLabel->font();
    brandFont.setPointSize(15);
    brandFont.setBold(true);
    brandLabel->setFont(brandFont);
    brandLabel->setStyleSheet("color: #0969da; letter-spacing: 2px;");
    sidebarLayout->addWidget(brandLabel);

    auto* subtitleLabel = new QLabel("BỘ CÔNG CỤ ĐA NĂNG DESKTOP", this);
    subtitleLabel->setStyleSheet("color: #57606a; font-size: 10px; font-weight: bold; margin-bottom: 6px;");
    sidebarLayout->addWidget(subtitleLabel);

    m_toolListWidget = new QListWidget(this);
    m_toolListWidget->setIconSize(QSize(32, 32));
    m_toolListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_toolListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_toolListWidget->setTextElideMode(Qt::ElideRight);
    m_toolListWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; padding: 2px; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 10px; margin-bottom: 4px; color: #1f2328; font-size: 13px; font-weight: 500; }"
        "QListWidget::item:hover { background-color: #e4e7eb; color: #1f2328; }"
        "QListWidget::item:selected { background-color: #0969da; color: #ffffff; font-weight: bold; }"
    );
    sidebarLayout->addWidget(m_toolListWidget, 1);

    auto* versionLabel = new QLabel("Version 1.0.0 (C++20 & Qt 6)", this);
    versionLabel->setStyleSheet("color: #8c959f; font-size: 11px;");
    versionLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(versionLabel);

    mainLayout->addWidget(sidebarFrame);

    // Right Content Area (Tool Details & Launcher Card)
    auto* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: #f6f8fa;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(40, 30, 40, 30);
    contentLayout->setSpacing(0);

    contentLayout->addStretch();

    // Modern Rounded Floating Card
    auto* cardFrame = new QFrame(this);
    cardFrame->setStyleSheet(
        "QFrame { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 18px; }"
    );
    auto* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(36, 36, 36, 36);
    cardLayout->setSpacing(16);

    m_toolIconLabel = new QLabel(this);
    m_toolIconLabel->setAlignment(Qt::AlignCenter);
    m_toolIconLabel->setStyleSheet("border: none; background: transparent;");
    cardLayout->addWidget(m_toolIconLabel);

    m_toolTitleLabel = new QLabel(this);
    QFont titleFont = m_toolTitleLabel->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    m_toolTitleLabel->setFont(titleFont);
    m_toolTitleLabel->setAlignment(Qt::AlignCenter);
    m_toolTitleLabel->setStyleSheet("color: #1f2328; border: none; background: transparent;");
    cardLayout->addWidget(m_toolTitleLabel);

    auto* statusContainer = new QHBoxLayout();
    statusContainer->addStretch();
    m_toolStatusLabel = new QLabel(this);
    m_toolStatusLabel->setAlignment(Qt::AlignCenter);
    m_toolStatusLabel->setStyleSheet(
        "background-color: rgba(31, 136, 61, 0.1); color: #1a7f37; "
        "border: 1px solid rgba(31, 136, 61, 0.3); border-radius: 12px; "
        "padding: 4px 14px; font-size: 11px; font-weight: bold;"
    );
    statusContainer->addWidget(m_toolStatusLabel);
    statusContainer->addStretch();
    cardLayout->addLayout(statusContainer);

    m_toolDescLabel = new QLabel(this);
    m_toolDescLabel->setWordWrap(true);
    m_toolDescLabel->setAlignment(Qt::AlignCenter);
    m_toolDescLabel->setStyleSheet("color: #57606a; font-size: 13px; line-height: 1.6; border: none; background: transparent; padding: 0 20px;");
    cardLayout->addWidget(m_toolDescLabel);

    cardLayout->addSpacing(10);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_openToolButton = new QPushButton("Khởi Chạy Công Cụ (Open Tool)", this);
    m_openToolButton->setFixedSize(260, 46);
    m_openToolButton->setCursor(Qt::PointingHandCursor);
    m_openToolButton->setStyleSheet(
        "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #1a7f37; }"
        "QPushButton:pressed { background-color: #116329; }"
        "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }"
    );
    connect(m_openToolButton, &QPushButton::clicked, this, &MainWindow::onOpenToolClicked);
    btnRow->addWidget(m_openToolButton);
    btnRow->addStretch();
    cardLayout->addLayout(btnRow);

    contentLayout->addWidget(cardFrame);
    contentLayout->addStretch();

    mainLayout->addWidget(contentWidget, 1);

    connect(m_toolListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onToolSelected);
    connect(m_toolListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onToolDoubleClicked);

    statusBar()->setStyleSheet("background-color: #ffffff; color: #57606a; border-top: 1px solid #d0d7de;");
    statusBar()->showMessage("Sẵn sàng. Chọn Auto Click để bắt đầu xây dựng chuỗi tự động hóa.");
}

void MainWindow::onToolSelected(int row)
{
    const auto& tools = ToolManager::instance().getAllTools();
    if (row >= 0 && row < static_cast<int>(tools.size()))
    {
        m_selectedTool = tools[row].get();
        m_toolIconLabel->setPixmap(IconHelper::makeBadgedPixmap(m_selectedTool->iconPath(), 108, 22, 12));
        m_toolTitleLabel->setText(m_selectedTool->name());
        m_toolDescLabel->setText(m_selectedTool->description());

        if (m_selectedTool->isAvailable())
        {
            m_toolStatusLabel->setText("● ĐÃ SẴN SÀNG HOẠT ĐỘNG (READY)");
            m_toolStatusLabel->setStyleSheet(
                "background-color: rgba(31, 136, 61, 0.1); color: #1a7f37; "
                "border: 1px solid rgba(31, 136, 61, 0.3); border-radius: 12px; "
                "padding: 4px 14px; font-size: 11px; font-weight: bold;"
            );
            m_openToolButton->setEnabled(true);
            m_openToolButton->setText("Mở Cửa Sổ " + m_selectedTool->name());
            m_openToolButton->setStyleSheet(
                "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; }"
                "QPushButton:hover { background-color: #1a7f37; }"
                "QPushButton:pressed { background-color: #116329; }"
            );
        }
        else
        {
            m_toolStatusLabel->setText("○ ĐANG PHÁT TRIỂN (COMING SOON)");
            m_toolStatusLabel->setStyleSheet(
                "background-color: rgba(154, 103, 0, 0.1); color: #9a6700; "
                "border: 1px solid rgba(154, 103, 0, 0.3); border-radius: 12px; "
                "padding: 4px 14px; font-size: 11px; font-weight: bold;"
            );
            m_openToolButton->setEnabled(true);
            m_openToolButton->setText("Xem Thông Tin " + m_selectedTool->name());
            m_openToolButton->setStyleSheet(
                "QPushButton { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; font-weight: bold; font-size: 13px; }"
                "QPushButton:hover { background-color: #eaeef2; color: #0969da; border-color: #0969da; }"
                "QPushButton:pressed { background-color: #d0d7de; }"
            );
        }
    }
}

void MainWindow::onToolDoubleClicked(QListWidgetItem* /*item*/)
{
    onOpenToolClicked();
}

void MainWindow::onOpenToolClicked()
{
    if (m_selectedTool)
    {
        QWidget* win = m_selectedTool->createWindow();
        if (win)
        {
            win->showNormal();
            win->raise();
            win->activateWindow();
        }
    }
}
