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

void DownloaderTool::stopBackgroundWorkForQuit()
{
    // close() gọi closeEvent() của DownloaderWindow, vốn chỉ tự dừng tải đang chạy rồi accept() - KHÔNG
    // có hộp thoại hỏi xác nhận nào ở đây, nên an toàn gọi từ lưới an toàn aboutToQuit() (khác VPN/
    // Security/Disk Cleanup/Auto Click - những cửa sổ CÓ hỏi xác nhận, không gọi close() ở đây để tránh
    // hiện hộp thoại giữa lúc ứng dụng đang thoát không thể hủy).
    if (m_window)
        m_window->close();
}
