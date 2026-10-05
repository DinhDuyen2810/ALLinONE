#include "QRTool.h"

#include "QRWindow.h"
#include "core/IconHelper.h"

QString QRTool::description() const
{
    return "Tạo mã QR (văn bản, URL, WiFi, Email, SMS, Điện thoại, Vị trí, vCard) với màu sắc, logo, xuất PNG/JPG/SVG; "
           "quét mã QR từ ảnh, clipboard, màn hình hoặc camera; lưu lịch sử.";
}

QIcon QRTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/QR.jpg", 36, 8, 3);
}

QWidget* QRTool::createWindow()
{
    if (!m_window)
        m_window = new QRWindow();
    return m_window;
}
