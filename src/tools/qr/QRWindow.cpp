#include "QRWindow.h"

#include "QRGenerateTab.h"
#include "QRHistoryTab.h"
#include "QRScanTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QCloseEvent>
#include <QTabWidget>
#include <QVBoxLayout>

QRWindow::QRWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - QR Tools");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/QR.jpg", 48, 10, 4));
    resize(1020, 660);
    setMinimumSize(860, 560);
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

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 14);

    m_tabs = new QTabWidget(this);
    m_generate = new QRGenerateTab(this);
    m_scan = new QRScanTab(this);
    m_history = new QRHistoryTab(this);
    m_tabs->addTab(m_generate, "✚  Tạo mã QR");
    m_tabs->addTab(m_scan, "🔍  Quét mã QR");
    m_tabs->addTab(m_history, "🕘  Lịch sử");
    layout->addWidget(m_tabs);

    connect(m_scan, &QRScanTab::recreateRequested, this, &QRWindow::recreateFromText);
    connect(m_history, &QRHistoryTab::recreateRequested, this, &QRWindow::recreateFromText);
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) {
        if (m_tabs->currentWidget() != m_scan)
            m_scan->stopCamera();
    });

    Logger::instance().info("QR", "Mở cửa sổ QR Tools");
}

void QRWindow::recreateFromText(const QString& text)
{
    m_generate->setRawText(text);
    m_tabs->setCurrentWidget(m_generate);
}

void QRWindow::closeEvent(QCloseEvent* event)
{
    m_scan->stopCamera();
    event->accept();
}
