#include "CoordinateOverlay.h"
#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QCursor>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
// Tọa độ vật lý khớp với SetCursorPos (process là Per-Monitor DPI aware),
// khác với tọa độ logic của Qt khi màn hình scale != 100%.
QPoint nativeCursorPos()
{
    POINT pt;
    if (GetCursorPos(&pt))
        return QPoint(pt.x, pt.y);
    return QCursor::pos();
}

// Cùng mẫu với DragGestureCapture: hook bàn phím toàn cục TẠM THỜI, chỉ sống trong lúc overlay đang mở,
// chỉ nuốt đúng phím Esc - không ảnh hưởng gì tới phím khác hay hook khác đang chạy trong cùng tiến trình.
CoordinateOverlay* g_instance = nullptr;

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_instance)
    {
        const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        if (!(data->flags & LLKHF_INJECTED) && g_instance->onRawKey(static_cast<int>(data->vkCode), pressed))
            return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
}

CoordinateOverlay::CoordinateOverlay(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);

    // Cover all screens / virtual desktop
    QRect fullGeometry;
    for (QScreen* screen : QGuiApplication::screens())
    {
        fullGeometry = fullGeometry.united(screen->geometry());
    }
    setGeometry(fullGeometry);
    m_currentPos = nativeCursorPos();
    m_localPos = mapFromGlobal(QCursor::pos());
    setFocusPolicy(Qt::StrongFocus);
    installEscapeHook();
}

CoordinateOverlay::~CoordinateOverlay()
{
    uninstallEscapeHook();
}

void CoordinateOverlay::installEscapeHook()
{
    g_instance = this;
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!m_keyboardHook)
        g_instance = nullptr; // không cài được - vẫn còn keyPressEvent() dự phòng
}

void CoordinateOverlay::uninstallEscapeHook()
{
    if (m_keyboardHook)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyboardHook));
        m_keyboardHook = nullptr;
    }
    if (g_instance == this)
        g_instance = nullptr;
}

bool CoordinateOverlay::onRawKey(int vkCode, bool pressed)
{
    if (vkCode != VK_ESCAPE || !pressed)
        return false;
    // Phát QUA HÀNG ĐỢI sự kiện: có thể đang chạy BÊN TRONG callback hook bàn phím - không nên đóng
    // cửa sổ/emit signal khi Windows còn đang chờ callback trả về (cùng lý do đã ghi trong
    // DragGestureCapture::cancelCapture()).
    QMetaObject::invokeMethod(this, &CoordinateOverlay::requestCancel, Qt::QueuedConnection);
    return true;
}

void CoordinateOverlay::requestCancel()
{
    if (m_finished)
        return;
    m_finished = true;
    uninstallEscapeHook();
    emit captureCancelled();
    close(); // WA_DeleteOnClose đã đặt trong constructor
}

void CoordinateOverlay::requestCapture(int x, int y)
{
    if (m_finished)
        return;
    m_finished = true;
    uninstallEscapeHook();
    emit pointCaptured(x, y);
    close();
}

void CoordinateOverlay::mouseMoveEvent(QMouseEvent* event)
{
    m_currentPos = nativeCursorPos();
    m_localPos = event->position().toPoint();
    update();
}

void CoordinateOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        QPoint pt = nativeCursorPos();
        requestCapture(pt.x(), pt.y());
    }
}

void CoordinateOverlay::keyPressEvent(QKeyEvent* event)
{
    // Lớp dự phòng nếu hook toàn cục cài thất bại (Windows từ chối) - bình thường ESC đã được hook ở
    // trên xử lý trước khi tới đây.
    if (event->key() == Qt::Key_Escape)
        requestCancel();
}

void CoordinateOverlay::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Subtle dark semi-transparent overlay
    painter.fillRect(rect(), QColor(0, 0, 0, 40));

    // Draw crosshair around current mouse position
    QPoint localPos = m_localPos;
    painter.setPen(QPen(QColor(0, 200, 255, 200), 1, Qt::DashLine));
    painter.drawLine(0, localPos.y(), width(), localPos.y());
    painter.drawLine(localPos.x(), 0, localPos.x(), height());

    // Coordinate tooltip box
    QString text = QString("X: %1, Y: %2\nClick để chọn | ESC để hủy").arg(m_currentPos.x()).arg(m_currentPos.y());
    QRect textRect = painter.fontMetrics().boundingRect(QRect(0, 0, 200, 60), Qt::AlignCenter, text);
    textRect.adjust(-10, -5, 10, 5);

    // Position tooltip offset from cursor
    int boxX = localPos.x() + 20;
    int boxY = localPos.y() + 20;
    if (boxX + textRect.width() > width()) boxX = localPos.x() - textRect.width() - 20;
    if (boxY + textRect.height() > height()) boxY = localPos.y() - textRect.height() - 20;
    textRect.moveTo(boxX, boxY);

    painter.setPen(QPen(QColor(9, 105, 218), 1.5));
    painter.setBrush(QColor(255, 255, 255, 245));
    painter.drawRoundedRect(textRect, 10, 10);

    painter.setPen(QColor(31, 35, 40));
    painter.drawText(textRect, Qt::AlignCenter, text);
}
