#include "WifiTool.h"

#include "WifiWindow.h"
#include "core/IconHelper.h"

QString WifiTool::description() const
{
    return "Quét mạng WiFi xung quanh, xem và quản lý hồ sơ/mật khẩu WiFi đã lưu trên máy, "
           "kết nối bằng mật khẩu bạn nhập, xuất/nhập hồ sơ XML.";
}

QIcon WifiTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/wifi.png", 36, 8, 3);
}

QWidget* WifiTool::createWindow()
{
    if (!m_window)
        m_window = new WifiWindow();
    return m_window;
}
