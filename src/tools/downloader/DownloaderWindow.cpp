#include "DownloaderWindow.h"

#include "DirectDownloadTab.h"
#include "PageScanTab.h"
#include "PlatformVideoTab.h"
#include "engine/FileDownloader.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QTabWidget>
#include <QVBoxLayout>

DownloaderWindow::DownloaderWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Downloader");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/download.png", 48, 10, 4));
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

    Logger::instance().info("Downloader", "Mở cửa sổ Downloader");
}

DownloaderWindow::~DownloaderWindow() = default;

void DownloaderWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_downloader = new FileDownloader(this);

    m_tabs = new QTabWidget(this);
    m_directTab = new DirectDownloadTab(m_downloader, this);
    m_pageScanTab = new PageScanTab(m_downloader, this);
    m_platformTab = new PlatformVideoTab(this);
    m_tabs->addTab(m_directTab, "⬇️  Tải trực tiếp");
    m_tabs->addTab(m_pageScanTab, "🔍  Quét trang web");
    m_tabs->addTab(m_platformTab, "🎬  Video nền tảng");
    root->addWidget(m_tabs, 1);
}
