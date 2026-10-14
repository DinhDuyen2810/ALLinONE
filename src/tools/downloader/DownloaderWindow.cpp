#include "DownloaderWindow.h"

#include "DownloadHistoryStore.h"
#include "DownloadHistoryTab.h"
#include "DownloadTab.h"
#include "DownloaderSettings.h"
#include "DownloaderSettingsTab.h"
#include "engine/DownloadQueue.h"
#include "engine/FileDownloader.h"
#include "core/IconHelper.h"
#include "core/Logger.h"
#include "ui/widgets/ComboBoxStyle.h"

#include <QCloseEvent>
#include <QTabWidget>
#include <QVBoxLayout>

DownloaderWindow::DownloaderWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Downloader");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/download.png", 48, 10, 4));
    resize(920, 700);
    setMinimumSize(760, 620); // dưới mức này phần điều khiển phía trên chiếm gần hết chiều cao, bảng chỉ còn thấy ~1 dòng
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

    Logger::instance().info("Downloader", "Mở cửa sổ Downloader");

    ComboBoxStyle::applyCursor(this);
}

DownloaderWindow::~DownloaderWindow() = default;

void DownloaderWindow::closeEvent(QCloseEvent* event)
{
    // QUAN TRỌNG: cửa sổ này được DownloaderTool giữ qua QPointer và TÁI DÙNG ở lần mở sau (không có
    // WA_DeleteOnClose) - đóng cửa sổ (bấm X) mặc định CHỈ ẨN đi, KHÔNG hủy đối tượng. Nếu không tự dừng
    // ở đây, một phiên tải video nền tảng (yt-dlp.exe, có thể tự sinh ffmpeg.exe lúc ghép), một lượt kiểm tra link
    // trong hàng đợi (yt-dlp.exe / Edge-Chrome ẩn) sẽ tiếp tục chạy ngầm vô thời hạn dù người dùng tưởng đã "đóng"
    // công cụ này - đúng lớp lỗi đã gặp với Android (xem AndroidControlWindow::closeEvent/ScrcpyLauncher.cpp).
    // Tải tệp HTTP (FileDownloader, thuần QNetworkAccessManager) không có tiến trình ngoài nào có thể mồ côi nên
    // không cần dừng ở đây.
    if (m_downloadTab)
        m_downloadTab->cancelBackgroundWork(); // quét trang (yt-dlp/trình duyệt ẩn) + lấy danh sách chất lượng
    if (m_queue)
        m_queue->stopProcesses();
    event->accept();
}

void DownloaderWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_settings = new DownloaderSettings(this);
    m_history = new DownloadHistoryStore(this);
    m_downloader = new FileDownloader(this);
    m_queue = new DownloadQueue(m_settings, m_history, m_downloader, this);

    m_tabs = new QTabWidget(this);
    m_downloadTab = new DownloadTab(m_queue, m_settings, this);
    m_historyTab = new DownloadHistoryTab(m_history, this);
    m_settingsTab = new DownloaderSettingsTab(m_settings, this);
    m_tabs->addTab(m_downloadTab, "⬇️  Tải xuống");
    m_tabs->addTab(m_historyTab, "🕘  Lịch sử");
    m_tabs->addTab(m_settingsTab, "⚙️  Cài đặt");
    root->addWidget(m_tabs, 1);
}
