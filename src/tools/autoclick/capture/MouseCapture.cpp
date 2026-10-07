#include "MouseCapture.h"
#include "CoordinateOverlay.h"
#include "DragGestureCapture.h"
#include <QTimer>

MouseCapture::MouseCapture(QWidget* parentWindow, QObject* parent)
    : QObject(parent)
    , m_parentWindow(parentWindow)
    , m_dragGestureCapture(new DragGestureCapture(this))
{
    connect(m_dragGestureCapture, &DragGestureCapture::dragCaptured, this,
            [this](int startX, int startY, int endX, int endY) {
        if (m_parentWindow)
        {
            m_parentWindow->showNormal();
            m_parentWindow->raise();
            m_parentWindow->activateWindow();
        }
        emit dragGestureCaptured(startX, startY, endX, endY);
    });
}

void MouseCapture::startDragGestureCapture()
{
    if (m_parentWindow)
    {
        m_parentWindow->hide();
    }

    // Đợi cửa sổ ẩn hẳn rồi mới cài hook - tránh trường hợp cú nhấn chuột bắt đầu thao tác kéo lại rơi
    // ngay trúng vị trí cửa sổ (đang trong quá trình ẩn) thay vì ứng dụng/màn hình thật phía dưới.
    QTimer::singleShot(200, this, [this]() {
        m_dragGestureCapture->startCapture();
    });
}

void MouseCapture::startCapture()
{
    if (m_parentWindow)
    {
        m_parentWindow->hide();
    }

    // Delay 200ms to let window hide smoothly before displaying overlay
    QTimer::singleShot(200, this, [this]() {
        auto* overlay = new CoordinateOverlay();

        connect(overlay, &CoordinateOverlay::pointCaptured, this, [this](int x, int y) {
            if (m_parentWindow)
            {
                m_parentWindow->showNormal();
                m_parentWindow->raise();
                m_parentWindow->activateWindow();
            }
            emit pointCaptured(x, y);
        });

        connect(overlay, &CoordinateOverlay::captureCancelled, this, [this]() {
            if (m_parentWindow)
            {
                m_parentWindow->showNormal();
                m_parentWindow->raise();
                m_parentWindow->activateWindow();
            }
            emit captureCancelled();
        });

        overlay->show();
        overlay->raise();
        overlay->activateWindow();
        overlay->setFocus();
    });
}
