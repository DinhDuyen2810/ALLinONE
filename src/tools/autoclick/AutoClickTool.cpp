#include "AutoClickTool.h"
#include "AutoClickWindow.h"
#include "core/IconHelper.h"

QString AutoClickTool::description() const
{
    return "Tự động hóa chuỗi thao tác chuột và bàn phím chuyên nghiệp (Click, Drag, Hold, Type, Hotkey, Scroll) kèm HUD Overlay thông minh.";
}

QIcon AutoClickTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/autoclicker.jpg", 36, 8, 3);
}

QWidget* AutoClickTool::createWindow()
{
    return new AutoClickWindow();
}
