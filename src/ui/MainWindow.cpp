#include "MainWindow.h"
#include "../tools/autoclick/AutoClickTool.h"
#include "../tools/qr/QRTool.h"
#include "../tools/wifi/WifiTool.h"
#include "../tools/connect/ConnectTool.h"
#include "../tools/diskcleanup/DiskCleanupTool.h"
#include "../tools/android/AndroidControlTool.h"
#include "../tools/vpn/VpnControlTool.h"
#include "../tools/security/SecurityGatewayTool.h"
#include "../tools/downloader/DownloaderTool.h"
#include "core/IconHelper.h"
#include "core/Version.h"
#include "core/update/UpdateInstaller.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QFrame>
#include <QIcon>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setupUi();
    registerTools();

    // Select first tool (Auto Click)
    if (m_toolListWidget->count() > 0)
    {
        m_toolListWidget->setCurrentRow(0);
    }

    // Tự kiểm tra cập nhật MỘT LẦN ngay lúc mở ứng dụng (UpdateChecker tự trễ vài giây rồi hỏi GitHub
    // Releases, không lặp định kỳ - theo đúng yêu cầu người dùng, xem UpdateChecker.h). Thất bại (mất
    // mạng, GitHub lỗi...) chỉ ghi log (xem UpdateChecker.cpp), không làm phiền bằng hộp thoại lỗi - đây
    // là việc chạy ngầm, không phải thao tác người dùng tự yêu cầu.
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);

    // Lưới an toàn CHUNG cho toàn bộ 9 tool: mỗi cửa sổ tool (AndroidControlWindow, DownloaderWindow...)
    // là một đối tượng KHÔNG cha (new Xxx() không gắn parent, giữ qua QPointer trong từng *Tool, tái
    // dùng ở lần mở sau) - khi ứng dụng thoát qua qApp->quit() GỌI TRỰC TIẾP (relaunch elevated ở Security/
    // Disk Cleanup, hoặc bản thân nút "Cập nhật ngay" của UpdateInstaller phía dưới), Qt KHÔNG tự gọi
    // closeEvent() của các cửa sổ tool KHÁC đang mở - việc dọn dẹp trong closeEvent() của chúng (nếu có)
    // sẽ không bao giờ chạy, để lại tiến trình ngoài (adb.exe/yt-dlp.exe...) mồ côi NGẦM vô thời hạn,
    // đúng lớp lỗi đã gặp với Android (xem ScrcpyLauncher.cpp). aboutToQuit() LUÔN phát ra đúng một lần
    // trước khi vòng lặp sự kiện kết thúc, bất kể thoát theo đường nào.
    connect(qApp, &QApplication::aboutToQuit, this, [] { ToolManager::instance().stopAllBackgroundWorkForQuit(); });
}

void MainWindow::registerTools()
{
    auto& tm = ToolManager::instance();

    // 1. Auto Click (Main tool implemented)
    tm.registerTool(std::make_unique<AutoClickTool>());

    tm.registerTool(std::make_unique<ConnectTool>());

    tm.registerTool(std::make_unique<QRTool>());

    tm.registerTool(std::make_unique<DiskCleanupTool>());

    tm.registerTool(std::make_unique<AndroidControlTool>());

    tm.registerTool(std::make_unique<DownloaderTool>());

    tm.registerTool(std::make_unique<SecurityGatewayTool>());

    tm.registerTool(std::make_unique<VpnControlTool>());

    tm.registerTool(std::make_unique<WifiTool>());

    // Populate sidebar list
    m_toolListWidget->clear();
    for (const auto& tool : tm.getAllTools())
    {
        auto* item = new QListWidgetItem(tool->icon(), "  " + tool->name());
        item->setSizeHint(QSize(210, 52));
        m_toolListWidget->addItem(item);
    }
}

void MainWindow::setupUi()
{
    setWindowTitle("ONE FOR ALL - Desktop Suite v1.0");
    // Icon đại diện CHO CẢ ỨNG DỤNG (cửa sổ chính/taskbar lúc đang chạy) - khác icon riêng từng tool
    // trong sidebar (mỗi tool vẫn giữ icon riêng, vd autoclicker.jpg cho Auto Click). Icon file .exe
    // (Explorer/shortcut/khi chưa chạy) nằm ở assets/app_icon.ico, cũng dựng từ icon/App.png.
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/App.png", 48, 10, 4));
    resize(960, 580);
    setMinimumSize(850, 520);
    setStyleSheet(
        "QMainWindow { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );

    auto* centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto* mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left Sidebar
    auto* sidebarFrame = new QFrame(this);
    sidebarFrame->setFixedWidth(270);
    sidebarFrame->setStyleSheet("QFrame { background-color: #f0f2f5; border-right: 1px solid #d0d7de; }");

    auto* sidebarLayout = new QVBoxLayout(sidebarFrame);
    sidebarLayout->setContentsMargins(14, 18, 14, 18);
    sidebarLayout->setSpacing(10);

    auto* brandLabel = new QLabel("ONE FOR ALL", this);
    QFont brandFont = brandLabel->font();
    brandFont.setPointSize(15);
    brandFont.setBold(true);
    brandLabel->setFont(brandFont);
    brandLabel->setStyleSheet("color: #0969da; letter-spacing: 2px;");
    sidebarLayout->addWidget(brandLabel);

    auto* subtitleLabel = new QLabel("BỘ CÔNG CỤ ĐA NĂNG DESKTOP", this);
    subtitleLabel->setStyleSheet("color: #57606a; font-size: 10px; font-weight: bold; margin-bottom: 6px;");
    sidebarLayout->addWidget(subtitleLabel);

    m_toolListWidget = new QListWidget(this);
    m_toolListWidget->setIconSize(QSize(32, 32));
    m_toolListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_toolListWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_toolListWidget->setTextElideMode(Qt::ElideRight);
    m_toolListWidget->setStyleSheet(
        "QListWidget { background: transparent; border: none; outline: none; padding: 2px; }"
        "QListWidget::item { padding: 8px 12px; border-radius: 10px; margin-bottom: 4px; color: #1f2328; font-size: 13px; font-weight: 500; }"
        "QListWidget::item:hover { background-color: #e4e7eb; color: #1f2328; }"
        "QListWidget::item:selected { background-color: #0969da; color: #ffffff; font-weight: bold; }"
    );
    sidebarLayout->addWidget(m_toolListWidget, 1);

    auto* versionLabel = new QLabel("Version 1.0.0 (C++20 & Qt 6)", this);
    versionLabel->setStyleSheet("color: #8c959f; font-size: 11px;");
    versionLabel->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(versionLabel);

    mainLayout->addWidget(sidebarFrame);

    // Right Content Area (Tool Details & Launcher Card)
    auto* contentWidget = new QWidget(this);
    contentWidget->setStyleSheet("background-color: #f6f8fa;");
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(40, 30, 40, 30);
    contentLayout->setSpacing(0);

    contentLayout->addStretch();

    // Modern Rounded Floating Card
    auto* cardFrame = new QFrame(this);
    cardFrame->setStyleSheet(
        "QFrame { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 18px; }"
    );
    auto* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(36, 36, 36, 36);
    cardLayout->setSpacing(16);

    m_toolIconLabel = new QLabel(this);
    m_toolIconLabel->setAlignment(Qt::AlignCenter);
    m_toolIconLabel->setStyleSheet("border: none; background: transparent;");
    cardLayout->addWidget(m_toolIconLabel);

    m_toolTitleLabel = new QLabel(this);
    QFont titleFont = m_toolTitleLabel->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    m_toolTitleLabel->setFont(titleFont);
    m_toolTitleLabel->setAlignment(Qt::AlignCenter);
    m_toolTitleLabel->setStyleSheet("color: #1f2328; border: none; background: transparent;");
    cardLayout->addWidget(m_toolTitleLabel);

    auto* statusContainer = new QHBoxLayout();
    statusContainer->addStretch();
    m_toolStatusLabel = new QLabel(this);
    m_toolStatusLabel->setAlignment(Qt::AlignCenter);
    m_toolStatusLabel->setStyleSheet(
        "background-color: rgba(31, 136, 61, 0.1); color: #1a7f37; "
        "border: 1px solid rgba(31, 136, 61, 0.3); border-radius: 12px; "
        "padding: 4px 14px; font-size: 11px; font-weight: bold;"
    );
    statusContainer->addWidget(m_toolStatusLabel);
    statusContainer->addStretch();
    cardLayout->addLayout(statusContainer);

    m_toolDescLabel = new QLabel(this);
    m_toolDescLabel->setWordWrap(true);
    m_toolDescLabel->setAlignment(Qt::AlignCenter);
    m_toolDescLabel->setStyleSheet("color: #57606a; font-size: 13px; line-height: 1.6; border: none; background: transparent; padding: 0 20px;");
    cardLayout->addWidget(m_toolDescLabel);

    cardLayout->addSpacing(10);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_openToolButton = new QPushButton("Khởi Chạy Công Cụ (Open Tool)", this);
    m_openToolButton->setFixedSize(260, 46);
    m_openToolButton->setCursor(Qt::PointingHandCursor);
    m_openToolButton->setStyleSheet(
        "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #1a7f37; }"
        "QPushButton:pressed { background-color: #116329; }"
        "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }"
    );
    connect(m_openToolButton, &QPushButton::clicked, this, &MainWindow::onOpenToolClicked);
    btnRow->addWidget(m_openToolButton);
    btnRow->addStretch();
    cardLayout->addLayout(btnRow);

    contentLayout->addWidget(cardFrame);
    contentLayout->addStretch();

    mainLayout->addWidget(contentWidget, 1);

    connect(m_toolListWidget, &QListWidget::currentRowChanged, this, &MainWindow::onToolSelected);
    connect(m_toolListWidget, &QListWidget::itemDoubleClicked, this, &MainWindow::onToolDoubleClicked);

    statusBar()->setStyleSheet("background-color: #ffffff; color: #57606a; border-top: 1px solid #d0d7de;");
    statusBar()->showMessage("Sẵn sàng. Chọn Auto Click để bắt đầu xây dựng chuỗi tự động hóa.");
}

void MainWindow::onToolSelected(int row)
{
    const auto& tools = ToolManager::instance().getAllTools();
    if (row >= 0 && row < static_cast<int>(tools.size()))
    {
        m_selectedTool = tools[row].get();
        m_toolIconLabel->setPixmap(IconHelper::makeBadgedPixmap(m_selectedTool->iconPath(), 108, 22, 12));
        m_toolTitleLabel->setText(m_selectedTool->name());
        m_toolDescLabel->setText(m_selectedTool->description());

        if (m_selectedTool->isAvailable())
        {
            m_toolStatusLabel->setText("● ĐÃ SẴN SÀNG HOẠT ĐỘNG (READY)");
            m_toolStatusLabel->setStyleSheet(
                "background-color: rgba(31, 136, 61, 0.1); color: #1a7f37; "
                "border: 1px solid rgba(31, 136, 61, 0.3); border-radius: 12px; "
                "padding: 4px 14px; font-size: 11px; font-weight: bold;"
            );
            m_openToolButton->setEnabled(true);
            m_openToolButton->setText("Mở Cửa Sổ " + m_selectedTool->name());
            m_openToolButton->setStyleSheet(
                "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; }"
                "QPushButton:hover { background-color: #1a7f37; }"
                "QPushButton:pressed { background-color: #116329; }"
            );
        }
        else
        {
            m_toolStatusLabel->setText("○ ĐANG PHÁT TRIỂN (COMING SOON)");
            m_toolStatusLabel->setStyleSheet(
                "background-color: rgba(154, 103, 0, 0.1); color: #9a6700; "
                "border: 1px solid rgba(154, 103, 0, 0.3); border-radius: 12px; "
                "padding: 4px 14px; font-size: 11px; font-weight: bold;"
            );
            m_openToolButton->setEnabled(true);
            m_openToolButton->setText("Xem Thông Tin " + m_selectedTool->name());
            m_openToolButton->setStyleSheet(
                "QPushButton { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; font-weight: bold; font-size: 13px; }"
                "QPushButton:hover { background-color: #eaeef2; color: #0969da; border-color: #0969da; }"
                "QPushButton:pressed { background-color: #d0d7de; }"
            );
        }
    }
}

void MainWindow::onToolDoubleClicked(QListWidgetItem* /*item*/)
{
    onOpenToolClicked();
}

void MainWindow::onOpenToolClicked()
{
    if (m_selectedTool)
    {
        QWidget* win = m_selectedTool->createWindow();
        if (win)
        {
            win->showNormal();
            win->raise();
            win->activateWindow();
        }
    }
}

void MainWindow::onUpdateAvailable(UpdateInfo info)
{
    const double mb = info.downloadSize > 0 ? info.downloadSize / 1024.0 / 1024.0 : 0.0;
    const QString sizeText = mb > 0 ? QString(" (%1 MB)").arg(mb, 0, 'f', 1) : QString();

    QMessageBox box(this);
    box.setWindowTitle("Có bản cập nhật mới");
    box.setIcon(QMessageBox::Information);
    box.setText(QString("Đã có bản mới One for ALL v%1%2 (đang dùng v%3).")
                    .arg(info.version, sizeText, APP_VERSION));
    if (!info.releaseNotes.trimmed().isEmpty())
        box.setDetailedText(info.releaseNotes);
    QPushButton* updateBtn = box.addButton("Cập nhật ngay", QMessageBox::AcceptRole);
    box.addButton("Để sau", QMessageBox::RejectRole);
    box.setDefaultButton(updateBtn);
    box.exec();

    if (box.clickedButton() != updateBtn)
        return; // "Để sau" - lần mở ứng dụng kế tiếp sẽ tự hỏi lại, không cần nhớ trạng thái "đã từ chối"

    auto* progress = new QProgressDialog("Đang tải bản cập nhật...", "Ẩn", 0, 100, this);
    progress->setWindowTitle("Đang cập nhật");
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    m_updateInstaller = new UpdateInstaller(this);

    connect(m_updateInstaller, &UpdateInstaller::progress, progress, [progress](qint64 received, qint64 total) {
        if (total > 0)
        {
            progress->setMaximum(static_cast<int>(total / 1024));
            progress->setValue(static_cast<int>(received / 1024));
        }
        else
        {
            progress->setMaximum(0); // GitHub không trả tổng dung lượng - hiện dạng "đang chạy" không xác định
        }
    });
    // Nút trên hộp thoại chỉ ẨN đi (tải vẫn tiếp tục ngầm) - UpdateInstaller/FileDownloader chưa có cách
    // hủy giữa chừng an toàn ở bản này; nếu thất bại/mất mạng, downloadFailed vẫn tự báo như bình thường.
    connect(progress, &QProgressDialog::canceled, progress, &QProgressDialog::hide);

    connect(m_updateInstaller, &UpdateInstaller::downloadFailed, this, [this, progress](QString error) {
        progress->close();
        progress->deleteLater();
        QMessageBox::warning(this, "Cập nhật thất bại", error);
    });
    connect(m_updateInstaller, &UpdateInstaller::aboutToRestart, this, [progress]() {
        progress->close();
        progress->deleteLater();
        // Trình cài đặt đã khởi chạy (âm thầm) và tự lo việc đóng + mở lại OneForAll.exe sau khi cài
        // xong (/CLOSEAPPLICATIONS /RESTARTAPPLICATIONS, xem UpdateInstaller.cpp) - tự đóng ngay ở đây
        // để không bị Restart Manager phải "ép" đóng giữa chừng.
        qApp->quit();
    });

    m_updateInstaller->downloadAndInstall(info.downloadUrl);
}
