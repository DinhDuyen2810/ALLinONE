#include "AndroidControlWindow.h"

#include "DevicesTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QVBoxLayout>

AndroidControlWindow::AndroidControlWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Android Phone Control");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/pcTOphone.png", 48, 10, 4));
    resize(860, 720);
    setMinimumSize(700, 560);
    setStyleSheet(
        "QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QLabel { background: transparent; }"
        "QToolTip { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; }");

    buildUi();

    Logger::instance().info("Android", "Mở cửa sổ Android Phone Control");
}

AndroidControlWindow::~AndroidControlWindow() = default;

void AndroidControlWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_devicesTab = new DevicesTab(this);
    root->addWidget(m_devicesTab, 1);
}
