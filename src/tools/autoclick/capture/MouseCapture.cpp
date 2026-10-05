#include "MouseCapture.h"
#include "CoordinateOverlay.h"
#include <QTimer>

MouseCapture::MouseCapture(QWidget* parentWindow, QObject* parent)
    : QObject(parent)
    , m_parentWindow(parentWindow)
{
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
    });
}
