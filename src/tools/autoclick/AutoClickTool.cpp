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
    if (!m_window)
        m_window = new AutoClickWindow();
    return m_window;
}

void AutoClickTool::stopBackgroundWorkForQuit()
{
    if (auto* window = qobject_cast<AutoClickWindow*>(m_window.data()))
        window->stopForQuit();
}
