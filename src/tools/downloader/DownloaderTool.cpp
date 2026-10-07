#include "DownloaderTool.h"

#include "DownloaderWindow.h"
#include "core/IconHelper.h"

QString DownloaderTool::description() const
{
    return "Tải trực tiếp tệp từ URL, quét trang web tìm ảnh/video/tài liệu, và tải video từ YouTube/"
           "Facebook/TikTok cùng hơn 1000 nền tảng khác.";
}

QIcon DownloaderTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/download.png", 36, 8, 3);
}

QWidget* DownloaderTool::createWindow()
{
    if (!m_window)
        m_window = new DownloaderWindow();
    return m_window;
}
