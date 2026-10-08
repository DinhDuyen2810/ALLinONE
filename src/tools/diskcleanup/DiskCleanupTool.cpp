#include "DiskCleanupTool.h"

#include "DiskCleanupWindow.h"
#include "core/IconHelper.h"

QString DiskCleanupTool::description() const
{
    return "Dọn dẹp rác hệ thống theo hạng mục (tệp tạm, cache, thùng rác...), tìm và xóa tệp lớn, "
           "xem tổng quan dung lượng từng ổ đĩa.";
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
