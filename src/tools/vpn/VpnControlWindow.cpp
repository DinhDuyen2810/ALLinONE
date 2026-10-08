#include "VpnControlWindow.h"

#include "VpnTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QVBoxLayout>

VpnControlWindow::VpnControlWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - VPN & Location");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/vpn.jpg", 48, 10, 4));
    resize(820, 700);
    setMinimumSize(680, 560);
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

    Logger::instance().info("Vpn", "Mở cửa sổ VPN & Location");
}

VpnControlWindow::~VpnControlWindow() = default;

void VpnControlWindow::forceStopNow()
{
    // requestCancelNoWait() - KHÔNG dùng cancelAndWait(): ở đây không hủy đối tượng gì (chỉ báo
    // rasdial.exe dừng sớm), cancelAndWait() có thể chặn tới 45 giây nếu may rủi trúng lúc rasdial.exe
    // đang chờ mạng - vô nghĩa và phản tác dụng khi gọi từ một luồng thoát ứng dụng đang cố NHANH.
    if (m_vpnTab)
        m_vpnTab->requestCancelNoWait();
}

void VpnControlWindow::closeEvent(QCloseEvent* event)
{
    if (m_vpnTab && m_vpnTab->isBusy())
    {
        const auto reply = QMessageBox::question(
            this, "Đang xử lý VPN",
            "Đang kết nối/ngắt kết nối VPN. Đóng cửa sổ sẽ HỦY thao tác này giữa chừng (an toàn). Bạn "
            "có muốn tiếp tục đóng?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_vpnTab->cancelAndWait();
    }
    event->accept();
}

void VpnControlWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_vpnTab = new VpnTab(this);
    root->addWidget(m_vpnTab, 1);
}
