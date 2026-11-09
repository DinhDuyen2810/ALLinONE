#include "DiskCleanupTool.h"

#include "DiskCleanupWindow.h"
#include "core/IconHelper.h"

QString DiskCleanupTool::description() const
{
    return "Dọn dẹp rác hệ thống theo hạng mục (tệp tạm, cache trình duyệt, Windows Update...), tìm tệp lớn, "
           "tìm tệp trùng lặp, đổi kích thước/dời phân vùng (kéo mép trái, đổi thứ tự), quét cả ổ thành cây thư mục theo dung lượng (xem và "
           "xóa từng thư mục/tệp, kể cả tệp nhỏ) và xem tiến trình/RAM/CPU đang chạy.";
}

QIcon DiskCleanupTool::icon() const
{
    return IconHelper::makeBadgedIcon(":/icons/cleaner.png", 36, 8, 3);
}

QWidget* DiskCleanupTool::createWindow()
{
    if (!m_window)
        m_window = new DiskCleanupWindow();
    return m_window;
}

void DiskCleanupTool::stopBackgroundWorkForQuit()
{
    if (m_window)
        static_cast<DiskCleanupWindow*>(m_window.data())->stopSafelyCancellableWorkForQuit();
}

bool DiskCleanupTool::isWindowBusy() const
{
    if (!m_window)
        return false;
    return static_cast<DiskCleanupWindow*>(m_window.data())->hasUnsafeOperationInProgress();
}
