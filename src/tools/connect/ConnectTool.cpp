#include "ConnectTool.h"

#include "ConnectWindow.h"
#include "core/IconHelper.h"

QString ConnectTool::description() const
{
    return "Dùng chung một chuột và bàn phím điều khiển nhiều máy tính qua mạng LAN (tham khảo Mouse "
           "without Borders). Ghép đôi bằng mã bảo mật hiển thị trên màn hình, kênh truyền mã hóa "
           "AES-256-GCM, chỉ hoạt động trong mạng nội bộ.";
}

QIcon ConnectTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/con_device.png", 36, 8, 3);
}

QWidget* ConnectTool::createWindow()
{
    if (!m_window)
        m_window = new ConnectWindow();
    return m_window;
}
