#include "RuntimeOverlay.h"
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <QHBoxLayout>

RuntimeOverlay::RuntimeOverlay(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // HUD không được cướp focus của cửa sổ đích, nếu không phím gõ sẽ vào HUD
    setAttribute(Qt::WA_ShowWithoutActivating);
    setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
    setFixedSize(320, 190);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 14, 16, 14);
    mainLayout->setSpacing(6);

    // Header layout
    auto* headerLayout = new QHBoxLayout();
    m_chainLabel = new QLabel("Auto Click đang chạy", this);
    m_chainLabel->setStyleSheet("color: #0969da; font-weight: bold; font-size: 13px;");
    m_roundLabel = new QLabel("Vòng 1/1", this);
    m_roundLabel->setStyleSheet("color: #57606a; font-size: 11px; font-weight: 500;");
    headerLayout->addWidget(m_chainLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(m_roundLabel);
    mainLayout->addLayout(headerLayout);

    // Action info
    m_actionLabel = new QLabel("Hành động: Sẵn sàng", this);
    m_actionLabel->setStyleSheet("color: #1f2328; font-size: 12px; font-weight: bold;");
    m_actionLabel->setWordWrap(true);
    mainLayout->addWidget(m_actionLabel);

    m_nextActionLabel = new QLabel("Tiếp theo: -", this);
    m_nextActionLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_nextActionLabel->setWordWrap(true);
    mainLayout->addWidget(m_nextActionLabel);

    // Countdown & Progress
    m_countdownLabel = new QLabel("Đếm ngược: 0.0s", this);
    m_countdownLabel->setStyleSheet("color: #9a6700; font-size: 12px; font-family: monospace; font-weight: 600;");
    mainLayout->addWidget(m_countdownLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #0969da; border-radius: 3px; }"
    );
    mainLayout->addWidget(m_progressBar);

    // Bottom stop button
    auto* bottomLayout = new QHBoxLayout();
    m_hotkeyHintLabel = new QLabel(this);
    m_hotkeyHintLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    bottomLayout->addWidget(m_hotkeyHintLabel);
    bottomLayout->addStretch();
    m_stopButton = new QPushButton("■ Dừng lại (Stop)", this);
    m_stopButton->setFixedSize(130, 28);
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setStyleSheet(
        "QPushButton { background-color: #cf222e; color: white; border: none; border-radius: 8px; font-weight: bold; font-size: 11px; }"
        "QPushButton:hover { background-color: #a40e26; }"
        "QPushButton:pressed { background-color: #82071e; }"
    );
    connect(m_stopButton, &QPushButton::clicked, this, &RuntimeOverlay::stopClicked);
    bottomLayout->addWidget(m_stopButton);
    mainLayout->addLayout(bottomLayout);

    // Default position: top right
    QScreen* screen = QGuiApplication::primaryScreen();
    if (screen)
    {
        QRect screenGeom = screen->availableGeometry();
        m_homePos = QPoint(screenGeom.right() - width() - 20, screenGeom.top() + 40);
        move(m_homePos);
    }
}

void RuntimeOverlay::setRoundInfo(int currentRound, int totalRounds)
{
    QString totalStr = (totalRounds > 0) ? QString::number(totalRounds) : "∞";
    m_roundLabel->setText(QString("Vòng %1 / %2").arg(currentRound).arg(totalStr));
}

void RuntimeOverlay::setStopHotkeyHint(const QString& hotkeyLabel)
{
    m_hotkeyHintLabel->setText(hotkeyLabel.isEmpty() ? QString() : QString("Dừng nhanh: %1").arg(hotkeyLabel));
}

void RuntimeOverlay::setActionInfo(int actionIndex, int totalActions, const QString& currentDesc, const QString& nextDesc, int targetX, int targetY)
{
    m_totalCountdownMs = 0;
    m_lastRemainingMs = 0;
    m_progressBar->setValue(0);
    m_actionLabel->setText(QString("Hành động %1/%2: %3").arg(actionIndex).arg(totalActions).arg(currentDesc));
    m_nextActionLabel->setText(QString("Tiếp theo: %1").arg(nextDesc));

    updateOverlayPosition(targetX, targetY);
}

void RuntimeOverlay::setCountdown(qint64 remainingMs, const QString& phase)
{
    double sec = remainingMs / 1000.0;
    m_countdownLabel->setText(QString("%1: %2 s").arg(phase).arg(sec, 0, 'f', 2));

    // Tick mới có remaining lớn hơn tick trước => bắt đầu một pha chờ mới
    if (remainingMs > m_lastRemainingMs)
        m_totalCountdownMs = static_cast<int>(remainingMs);
    m_lastRemainingMs = remainingMs;

    if (m_totalCountdownMs > 0)
    {
        int progress = static_cast<int>((1.0 - (static_cast<double>(remainingMs) / m_totalCountdownMs)) * 100);
        m_progressBar->setValue(progress);
    }
}

void RuntimeOverlay::updateOverlayPosition(int targetX, int targetY)
{
    m_targetX = targetX;
    m_targetY = targetY;

    // Tọa độ đích là pixel vật lý (Win32); Qt dùng pixel logic nên quy đổi theo DPR
    QScreen* primary = QGuiApplication::primaryScreen();
    const qreal dpr = primary ? primary->devicePixelRatio() : 1.0;
    const int logicalX = static_cast<int>(targetX / dpr);
    const int logicalY = static_cast<int>(targetY / dpr);

    QScreen* screen = QGuiApplication::screenAt(QPoint(logicalX, logicalY));
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;

    QRect screenGeom = screen->availableGeometry();
    QRect overlayRect = geometry();

    // Check collision: if target point is inside overlay rect with 60px padding
    QRect dangerRect = overlayRect.adjusted(-60, -60, 60, 60);
    if (dangerRect.contains(logicalX, logicalY))
    {
        // Reposition to another corner to avoid collision
        int left = screenGeom.left() + 30;
        int right = screenGeom.right() - width() - 30;
        int top = screenGeom.top() + 40;
        int bottom = screenGeom.bottom() - height() - 40;

        // If target is in top half, move to bottom, and vice versa
        int newX = (logicalX > screenGeom.center().x()) ? left : right;
        int newY = (logicalY < screenGeom.center().y()) ? bottom : top;

        move(newX, newY);
        m_isDisplaced = true;
        return;
    }

    // Không còn va chạm ở vị trí hiện tại. Nếu đang né (không ở home) và HOME giờ cũng an toàn cho đích
    // này thì quay về - trước đây không có nhánh này, HUD đứng yên vĩnh viễn ở góc đã né dù không còn lý
    // do gì phải tránh nữa (xác nhận qua yêu cầu người dùng, v1.19.7).
    if (m_isDisplaced)
    {
        QRect dangerRectHome = QRect(m_homePos, size()).adjusted(-60, -60, 60, 60);
        if (!dangerRectHome.contains(logicalX, logicalY))
        {
            move(m_homePos);
            m_isDisplaced = false;
        }
        // home vẫn nguy hiểm với đích này nhưng chỗ né hiện tại đang an toàn - đứng yên, không có lý do di chuyển.
    }
}

void RuntimeOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Light sleek container background with rounded corners
    painter.setBrush(QColor(255, 255, 255, 248));
    painter.setPen(QPen(QColor(9, 105, 218, 180), 1.5));
    painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 16, 16);
}
