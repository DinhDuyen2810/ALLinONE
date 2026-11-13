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
    // được hiện hộp thoại giữa lúc ứng dụng đang thoát. YÊU CẦU dừng rồi chờ tối đa 3 giây: luồng quét kết
    // thúc powershell.exe con của nó trong ~200ms. Trước v1.28.1 chỉ yêu cầu mà không chờ - tiến trình ứng
    // dụng thoát trước khi luồng kịp làm việc đó, Start-MpScan chạy mồ côi (cửa sổ công cụ không bao giờ bị hủy).
    if (auto* window = qobject_cast<SecurityGatewayWindow*>(m_window.data()))
        window->requestCancelScanForQuit();
}
