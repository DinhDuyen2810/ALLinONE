#include "DiskCleanupWindow.h"

#include "CleanupTab.h"
#include "LargeFilesTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QTabWidget>
#include <QVBoxLayout>

DiskCleanupWindow::DiskCleanupWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Disk Cleanup");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/cleaner.png", 48, 10, 4));
    resize(920, 620);
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

    buildUi();

    Logger::instance().info("DiskCleanup", "Mở cửa sổ Disk Cleanup");
}

DiskCleanupWindow::~DiskCleanupWindow() = default;

void DiskCleanupWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_tabs = new QTabWidget(this);
    m_cleanupTab = new CleanupTab(this);
    m_largeFilesTab = new LargeFilesTab(this);
    m_tabs->addTab(m_cleanupTab, "🧹  Dọn dẹp theo hạng mục");
    m_tabs->addTab(m_largeFilesTab, "📦  Tìm tệp lớn");
    root->addWidget(m_tabs, 1);
}
