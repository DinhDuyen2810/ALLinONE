#include "ConnectWindow.h"

#include "ConnectUiStyle.h"
#include "PairingTab.h"
#include "PeersTab.h"
#include "core/IconHelper.h"
#include "core/Logger.h"
#include "engine/ConnectSessionController.h"

#include <QCloseEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QShowEvent>
#include <QTabWidget>
#include <QTime>
#include <QVBoxLayout>

ConnectWindow::ConnectWindow(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("One for ALL - Connect Together");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/con_device.png", 48, 10, 4));
    resize(760, 680);
    setMinimumSize(620, 560);
    setStyleSheet(
        "QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: #eaeef2; color: #57606a; padding: 9px 20px; margin-right: 4px; border-top-left-radius: 10px; border-top-right-radius: 10px; font-weight: 600; }"
        "QTabBar::tab:selected { background: #ffffff; color: #0969da; }"
        "QTabBar::tab:hover:!selected { background: #e0e4e9; }"
        "QLabel { background: transparent; }"
        "QToolTip { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; }");

    m_controller = new ConnectSessionController(this);

    buildUi();

    connect(m_controller, &ConnectSessionController::logMessage, this, &ConnectWindow::appendLog);
    connect(m_controller, &ConnectSessionController::roleChanged, this, &ConnectWindow::updateRoleBanner);
    connect(m_controller, &ConnectSessionController::pairingFailed, this,
           [this](const QString& reason) { QMessageBox::warning(this, "Ghép đôi thất bại", reason); });

    startController(true);

    Logger::instance().info("Connect", "Mở cửa sổ Connect Together");
}

ConnectWindow::~ConnectWindow() = default;

void ConnectWindow::startController(bool showErrorDialog)
{
    // start() là idempotent (đang chạy thì trả true ngay) - gọi được từ cả constructor lẫn showEvent.
    QString err;
    if (!m_controller->start(&err))
    {
        appendLog("⚠ Không khởi động được Connect Together: " + err);
        if (showErrorDialog)
            QMessageBox::critical(this, "Lỗi", "Không khởi động được Connect Together:\n" + err);
    }

    m_identityLabel->setText(QString("Tên máy: %1  •  Cổng TCP: %2")
                                 .arg(m_controller->localIdentity().machineName)
                                 .arg(m_controller->listenPort()));
    updateRoleBanner();
}

void ConnectWindow::showEvent(QShowEvent* event)
{
    // Cửa sổ này được ConnectTool giữ qua QPointer và TÁI DÙNG ở lần mở sau (không có WA_DeleteOnClose):
    // closeEvent() đã stop() controller, nên mở lại mà không start() thì cửa sổ hiện ra bình thường nhưng
    // không nghe cổng, không khám phá, không hook - "chết" cho tới khi khởi động lại cả ứng dụng. Bỏ qua
    // sự kiện show tự phát của hệ thống (khôi phục từ thu nhỏ) - khi đó controller vẫn đang chạy.
    QWidget::showEvent(event);
    if (!event->spontaneous() && !m_controller->isRunning())
        startController(false);
}

void ConnectWindow::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 14);
    root->setSpacing(10);

    m_identityLabel = new QLabel(this);
    m_identityLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    root->addWidget(m_identityLabel);

    m_roleBanner = new QLabel(this);
    m_roleBanner->setWordWrap(true);
    root->addWidget(m_roleBanner);

    m_tabs = new QTabWidget(this);
    m_pairingTab = new PairingTab(m_controller, this);
    m_peersTab = new PeersTab(m_controller, this);
    m_tabs->addTab(m_pairingTab, "🔑  Ghép đôi");
    m_tabs->addTab(m_peersTab, "💻  Máy đã ghép đôi");
    root->addWidget(m_tabs, 1);

    auto* logLabel = new QLabel("NHẬT KÝ", this);
    logLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px;");
    root->addWidget(logLabel);

    m_logView = new QPlainTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setMaximumHeight(110);
    m_logView->setStyleSheet(
        "QPlainTextEdit { background-color: #ffffff; color: #57606a; border: 1px solid #d0d7de; "
        "border-radius: 8px; font-family: 'Consolas', monospace; font-size: 11px; padding: 6px; }");
    root->addWidget(m_logView);
}

void ConnectWindow::appendLog(const QString& text)
{
    const QString stamped = QTime::currentTime().toString("HH:mm:ss") + "  " + text;
    m_logView->appendPlainText(stamped);
}

void ConnectWindow::updateRoleBanner()
{
    switch (m_controller->currentRole())
    {
        case ControlRole::Idle:
            m_roleBanner->setStyleSheet(ConnectUi::bannerStyle("idle"));
            m_roleBanner->setText("✓ Sẵn sàng. Đưa chuột chạm mép màn hình đã xếp vị trí để chia sẻ điều khiển.");
            break;
        case ControlRole::Controlling:
            m_roleBanner->setStyleSheet(ConnectUi::bannerStyle("active"));
            m_roleBanner->setText("🖱 Đang điều khiển " + m_controller->activePeerName() +
                                  "  •  Nhấn Ctrl+Alt+Home để quay lại máy này.");
            break;
        case ControlRole::BeingControlled:
            m_roleBanner->setStyleSheet(ConnectUi::bannerStyle("warn"));
            m_roleBanner->setText("⚠ Một máy khác (" + m_controller->activePeerName() +
                                  ") đang điều khiển máy này.");
            break;
    }
}

void ConnectWindow::closeEvent(QCloseEvent* event)
{
    m_controller->stop();
    event->accept();
}
