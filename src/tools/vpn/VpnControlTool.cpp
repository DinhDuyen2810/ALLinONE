#include "VpnControlTool.h"

#include "VpnControlWindow.h"
#include "core/IconHelper.h"

QString VpnControlTool::description() const
{
    return "Quản lý kết nối VPN hệ thống Windows (IKEv2/L2TP/SSTP/PPTP) theo nhiều máy chủ/quốc gia "
           "bạn tự khai báo, kèm hiển thị vị trí IP hiện tại.";
}

QIcon VpnControlTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/vpn.jpg", 36, 8, 3);
}

QWidget* VpnControlTool::createWindow()
{
    if (!m_window)
        m_window = new VpnControlWindow();
    return m_window;
}
