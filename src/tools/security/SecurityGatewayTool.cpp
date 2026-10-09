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

void SecurityGatewayTool::stopBackgroundWorkForQuit()
{
    // KHÔNG gọi close() như DownloaderTool: closeEvent() của cửa sổ này hỏi xác nhận nếu đang quét - không
    // được hiện hộp thoại giữa lúc ứng dụng đang thoát. Chỉ YÊU CẦU dừng (không chờ): luồng quét tự kết
    // thúc powershell.exe con của nó trong ~200ms thay vì bỏ lại nó chạy ngầm sau khi ứng dụng đã thoát.
    if (auto* window = qobject_cast<SecurityGatewayWindow*>(m_window.data()))
        window->requestCancelScanForQuit();
}
