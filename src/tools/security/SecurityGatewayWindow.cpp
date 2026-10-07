#include "SecurityGatewayWindow.h"

#include "CommandGatewayTab.h"
#include "MalwareScanTab.h"
#include "WebProtectionTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QTabWidget>
#include <QVBoxLayout>

SecurityGatewayWindow::SecurityGatewayWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Security Gateway");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/gateway.png", 48, 10, 4));
    resize(920, 680);
    setMinimumSize(760, 540);
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

    buildUi();

    Logger::instance().info("Security", "Mở cửa sổ Security Gateway");
}

SecurityGatewayWindow::~SecurityGatewayWindow() = default;

void SecurityGatewayWindow::closeEvent(QCloseEvent* event)
{
    if (m_scanTab && m_scanTab->isScanningNow())
    {
        const auto reply = QMessageBox::question(
            this, "Đang quét mã độc",
            "Đang quét mã độc (có thể là quét toàn bộ máy, mất nhiều thời gian). Đóng cửa sổ sẽ HỦY "
            "quét giữa chừng (an toàn - giống bấm Hủy trên Windows Security). Bạn có muốn tiếp tục đóng?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_scanTab->cancelScanAndWait();
    }
    event->accept();
}

void SecurityGatewayWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_tabs = new QTabWidget(this);
    m_commandTab = new CommandGatewayTab(this);
    m_webTab = new WebProtectionTab(this);
    m_scanTab = new MalwareScanTab(this);
    m_tabs->addTab(m_commandTab, "💻  Cổng lệnh PowerShell");
    m_tabs->addTab(m_webTab, "🌐  Bảo vệ Web");
    m_tabs->addTab(m_scanTab, "🦠  Quét mã độc");
    root->addWidget(m_tabs, 1);
}
