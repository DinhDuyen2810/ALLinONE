#include "DiskCleanupWindow.h"

#include "CleanupTab.h"
#include "DuplicateFilesTab.h"
#include "LargeFilesTab.h"
#include "PartitionTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"

#include <QCloseEvent>
#include <QMessageBox>
#include <QTabWidget>
#include <QVBoxLayout>

DiskCleanupWindow::DiskCleanupWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Disk Cleanup");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/cleaner.png", 48, 10, 4));
    resize(920, 620);
    setMinimumSize(760, 480);
    setStyleSheet(
        "QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #eaeef2; color: #57606a; padding: 9px 22px; margin-right: 4px; border-top-left-radius: 10px; border-top-right-radius: 10px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0969da; }"
        "QTabBar::tab:hover:!selected { background: #e0e4e9; }"
        "QLabel { background: transparent; }"
        "QToolTip { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; }");

    buildUi();

    Logger::instance().info("DiskCleanup", "Mở cửa sổ Disk Cleanup");
}

DiskCleanupWindow::~DiskCleanupWindow() = default;

bool DiskCleanupWindow::hasUnsafeOperationInProgress() const
{
    return (m_partitionTab && m_partitionTab->isResizingNow()) ||
           (m_cleanupTab && m_cleanupTab->isCleaningNow());
}

void DiskCleanupWindow::stopSafelyCancellableWorkForQuit()
{
    if (m_cleanupTab)
        m_cleanupTab->stopScanIfRunning(); // quét hạng mục chỉ ĐỌC - an toàn để dừng, khác với xóa thật
    if (m_largeFilesTab)
        m_largeFilesTab->stopScanIfRunning();
    if (m_duplicateFilesTab)
        m_duplicateFilesTab->stopScanIfRunning();
}

bool DiskCleanupWindow::isAnyOtherTabBusy(const QWidget* exceptTab) const
{
    if (m_cleanupTab && m_cleanupTab != exceptTab && m_cleanupTab->isBusy())
        return true;
    if (m_largeFilesTab && m_largeFilesTab != exceptTab && m_largeFilesTab->isScanningNow())
        return true;
    if (m_duplicateFilesTab && m_duplicateFilesTab != exceptTab && m_duplicateFilesTab->isScanningNow())
        return true;
    if (m_partitionTab && m_partitionTab != exceptTab && m_partitionTab->isResizingNow())
        return true;
    return false;
}

void DiskCleanupWindow::closeEvent(QCloseEvent* event)
{
    if (m_partitionTab && m_partitionTab->isResizingNow())
    {
        QMessageBox::warning(
            this, "Đang đổi kích thước phân vùng",
            "Không thể đóng cửa sổ trong lúc đang đổi kích thước phân vùng - hủy giữa chừng có thể làm "
            "hỏng hệ thống tệp. Vui lòng đợi quá trình hoàn tất rồi đóng lại.");
        event->ignore();
        return;
    }
    if (m_cleanupTab && m_cleanupTab->isCleaningNow())
    {
        QMessageBox::warning(
            this, "Đang dọn dẹp",
            "Không thể đóng cửa sổ trong lúc đang xóa tệp - vui lòng đợi quá trình dọn dẹp hoàn tất rồi "
            "đóng lại.");
        event->ignore();
        return;
    }
    // Quét tệp lớn/tệp trùng lặp AN TOÀN để hủy giữa chừng (khác resize/xóa thật ở trên) - không cần
    // chặn đóng cửa sổ, nhưng vẫn phải tự dừng ở đây: cửa sổ này được DiskCleanupTool giữ qua QPointer và
    // TÁI DÙNG ở lần mở sau (không WA_DeleteOnClose) - đóng chỉ ẨN đi, các tab con không bị hủy nên
    // destructor của chúng (vốn đã requestStop()+wait() đúng) không bao giờ chạy, QThread quét tiếp tục
    // chạy ngầm vô ích nếu không chủ động dừng ở đây. Lượt quét theo hạng mục của CleanupTab cũng vậy.
    if (m_cleanupTab)
        m_cleanupTab->stopScanIfRunning();
    if (m_largeFilesTab)
        m_largeFilesTab->stopScanIfRunning();
    if (m_duplicateFilesTab)
        m_duplicateFilesTab->stopScanIfRunning();
    event->accept();
}

void DiskCleanupWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_tabs = new QTabWidget(this);
    m_cleanupTab = new CleanupTab(this);
    m_largeFilesTab = new LargeFilesTab(this);
    m_duplicateFilesTab = new DuplicateFilesTab(this);
    m_partitionTab = new PartitionTab(this);
    m_tabs->addTab(m_cleanupTab, "🧹  Dọn dẹp theo hạng mục");
    m_tabs->addTab(m_largeFilesTab, "📦  Tìm tệp lớn");
    m_tabs->addTab(m_duplicateFilesTab, "👯  Tìm tệp trùng lặp");
    m_tabs->addTab(m_partitionTab, "💽  Quản lý phân vùng");
    root->addWidget(m_tabs, 1);
}
