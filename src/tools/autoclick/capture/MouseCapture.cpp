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
        restoreParentWindow();
        emit dragGestureCaptured(startX, startY, endX, endY);
    });
    // Esc / hết 30 giây chờ: hiện lại cửa sổ (trước đây không có đường nào quay lại - xem
    // DragGestureCapture.h).
    connect(m_dragGestureCapture, &DragGestureCapture::captureCancelled, this, [this]() {
        restoreParentWindow();
        emit captureCancelled();
    });
}

void MouseCapture::restoreParentWindow()
{
    if (m_parentWindow)
    {
        m_parentWindow->showNormal();
        m_parentWindow->raise();
        m_parentWindow->activateWindow();
    }
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
        if (!m_dragGestureCapture->startCapture())
        {
            // Không cài được hook thì không có gì để chờ - trước đây cửa sổ cứ thế ẩn luôn, không một
            // lời báo lỗi.
            restoreParentWindow();
            emit captureFailed("Không bắt được thao tác kéo: Windows từ chối cài đặt hook chuột toàn cục. "
                               "Hãy nhập tọa độ thủ công hoặc dùng \"Lấy tọa độ\" cho từng điểm.");
        }
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
