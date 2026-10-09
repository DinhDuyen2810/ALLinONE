#include "ScreenSnipOverlay.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>

ScreenSnipOverlay::ScreenSnipOverlay(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);

    QRect full;
    for (QScreen* screen : QGuiApplication::screens())
    {
        ScreenShot s;
        s.logicalRect = screen->geometry();
        s.pixmap = screen->grabWindow(0);
        s.dpr = screen->devicePixelRatio();
        m_shots.push_back(s);
        full = full.united(s.logicalRect);
    }
    setGeometry(full);
}

void ScreenSnipOverlay::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    for (const ScreenShot& s : m_shots)
        p.drawPixmap(s.logicalRect.translated(-geometry().topLeft()), s.pixmap);

    p.fillRect(rect(), QColor(0, 0, 0, 110));

    if (m_dragging || !m_current.isNull())
    {
        const QRect sel = QRect(m_start, m_current).normalized();
        if (m_dragging && sel.width() > 0 && sel.height() > 0)
        {
            // Hiện lại vùng đang chọn ở độ sáng gốc
            for (const ScreenShot& s : m_shots)
            {
                const QRect local = s.logicalRect.translated(-geometry().topLeft());
                const QRect inter = local.intersected(sel);
                if (inter.isEmpty())
                    continue;
                const QRectF src((inter.x() - local.x()) * s.dpr, (inter.y() - local.y()) * s.dpr,
                                 inter.width() * s.dpr, inter.height() * s.dpr);
                p.drawPixmap(QRectF(inter), s.pixmap, src);
            }
            p.setPen(QPen(QColor(9, 105, 218), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(sel);

            const QString label = QString("%1 x %2").arg(sel.width()).arg(sel.height());
            p.setPen(Qt::white);
            p.fillRect(QRect(sel.left(), std::max(0, sel.top() - 22), 90, 20), QColor(9, 105, 218));
            p.drawText(QRect(sel.left(), std::max(0, sel.top() - 22), 90, 20), Qt::AlignCenter, label);
        }
    }

    p.setPen(Qt::white);
    p.drawText(QRect(0, 12, width(), 24), Qt::AlignHCenter, "Kéo chuột để chọn vùng chứa mã QR  |  ESC để hủy");
}

void ScreenSnipOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_start = event->position().toPoint();
        m_current = m_start;
        m_dragging = true;
        update();
    }
    else if (event->button() == Qt::RightButton)
    {
        m_done = true;
        emit cancelled();
        close();
    }
}

void ScreenSnipOverlay::mouseMoveEvent(QMouseEvent* event)
{
    m_current = event->position().toPoint();
    if (m_dragging)
        update();
}

void ScreenSnipOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_dragging)
        return;

    m_dragging = false;
    const QRect sel = QRect(m_start, event->position().toPoint()).normalized();
    m_done = true;

    if (sel.width() < 8 || sel.height() < 8)
    {
        emit cancelled();
        close();
        return;
    }

    const QImage img = cropSelection(sel.translated(geometry().topLeft()));
    if (img.isNull())
        emit cancelled();
    else
        emit captured(img);
    close();
}

void ScreenSnipOverlay::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        m_done = true;
        emit cancelled();
        close();
    }
}

void ScreenSnipOverlay::closeEvent(QCloseEvent* event)
{
    // Lớp phủ bị đóng mà CHƯA phát tín hiệu nào (Alt+F4, hệ thống đóng cửa sổ khi tắt máy/đăng xuất...):
    // nơi gọi đã ẩn cửa sổ QR Tools và chỉ hiện lại khi nhận captured()/cancelled() - không báo hủy ở đây
    // thì cửa sổ đó bị ẩn luôn.
    if (!m_done)
    {
        m_done = true;
        emit cancelled();
    }
    QWidget::closeEvent(event);
}

QImage ScreenSnipOverlay::cropSelection(const QRect& globalRect) const
{
    // Chọn màn hình có diện tích giao lớn nhất (vùng chọn trải nhiều màn hình sẽ bị cắt theo màn hình đó)
    const ScreenShot* best = nullptr;
    qint64 bestArea = 0;
    for (const ScreenShot& s : m_shots)
    {
        const QRect inter = s.logicalRect.intersected(globalRect);
        const qint64 area = static_cast<qint64>(inter.width()) * inter.height();
        if (area > bestArea)
        {
            bestArea = area;
            best = &s;
        }
    }
    if (!best)
        return QImage();

    const QRect inter = best->logicalRect.intersected(globalRect);
    const QRect local = inter.translated(-best->logicalRect.topLeft());
    const QRect phys(qRound(local.x() * best->dpr), qRound(local.y() * best->dpr),
                     qRound(local.width() * best->dpr), qRound(local.height() * best->dpr));
    return best->pixmap.toImage().copy(phys);
}
