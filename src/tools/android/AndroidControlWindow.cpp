#include "AndroidControlWindow.h"

#include "DevicesTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QCloseEvent>
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

void AndroidControlWindow::closeEvent(QCloseEvent* event)
{
    // QUAN TRỌNG: cửa sổ này được AndroidControlTool giữ qua QPointer và TÁI DÙNG ở lần mở sau (không
    // có WA_DeleteOnClose) - đóng cửa sổ (bấm X) mặc định CHỈ ẨN đi, KHÔNG hủy đối tượng. Nếu không tự
    // dừng phiên scrcpy ở đây, phiên gương/điều khiển đang chạy (nếu có) sẽ tiếp tục chạy ngầm vô thời
    // hạn dù người dùng tưởng đã "đóng" công cụ này - đúng như thói quen đóng cửa sổ của các tool khác
    // trong ứng dụng (xem ConnectWindow::closeEvent/VpnControlWindow::closeEvent).
    if (m_devicesTab)
        m_devicesTab->stopActiveSession();
    event->accept();
}

void AndroidControlWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_devicesTab = new DevicesTab(this);
    root->addWidget(m_devicesTab, 1);
}
