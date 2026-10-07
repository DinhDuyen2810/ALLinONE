#include "AndroidControlTool.h"

#include "AndroidControlWindow.h"
#include "core/IconHelper.h"

QString AndroidControlTool::description() const
{
    return "Gương và điều khiển màn hình điện thoại Android theo thời gian thực (chạm/phím/chuột) qua "
           "cáp USB hoặc Wi-Fi, dựa trên adb + scrcpy.";
}

QIcon AndroidControlTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/pcTOphone.png", 36, 8, 3);
}

QWidget* AndroidControlTool::createWindow()
{
    if (!m_window)
        m_window = new AndroidControlWindow();
    return m_window;
}
