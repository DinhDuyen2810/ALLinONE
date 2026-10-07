#include "SecurityGatewayTool.h"

#include "SecurityGatewayWindow.h"
#include "core/IconHelper.h"

QString SecurityGatewayTool::description() const
{
    return "Kiểm tra lệnh PowerShell trước khi chạy, chặn kết nối tới trang/máy chủ độc hại, quét và "
           "xử lý mã độc trên máy (dựa trên Windows Defender có sẵn).";
}

QIcon SecurityGatewayTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/gateway.png", 36, 8, 3);
}

QWidget* SecurityGatewayTool::createWindow()
{
    if (!m_window)
        m_window = new SecurityGatewayWindow();
    return m_window;
}
