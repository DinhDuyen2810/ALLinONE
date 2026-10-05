#include "QRImageView.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QPainter>
#include <QUrl>

QRImageView::QRImageView(QWidget* parent)
    : QWidget(parent)
    , m_placeholder("Kéo thả ảnh vào đây, hoặc dùng các nút phía trên")
{
    setAcceptDrops(true);
    setMinimumSize(260, 200);
}

void QRImageView::setImage(const QImage& image)
{
    m_image = image;
    m_highlights.clear();
    update();
}

void QRImageView::setHighlights(const QList<QPolygon>& polygons)
{
    m_highlights = polygons;
    update();
}

void QRImageView::setPlaceholder(const QString& text)
{
    m_placeholder = text;
    update();
}

QRectF QRImageView::imageRect() const
{
    if (m_image.isNull())
        return QRectF();
    const QRectF area = QRectF(rect()).adjusted(8, 8, -8, -8);
    const QSizeF fit = QSizeF(m_image.size()).scaled(area.size(), Qt::KeepAspectRatio);
    return QRectF(area.center().x() - fit.width() / 2, area.center().y() - fit.height() / 2, fit.width(), fit.height());
}

void QRImageView::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    p.setPen(QPen(QColor("#d0d7de"), 1, m_image.isNull() ? Qt::DashLine : Qt::SolidLine));
    p.setBrush(QColor("#ffffff"));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);

    if (m_image.isNull())
    {
        p.setPen(QColor("#8c959f"));
        p.drawText(rect().adjusted(16, 16, -16, -16), Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }

    const QRectF target = imageRect();
    p.drawImage(target, m_image);

    const double sx = target.width() / m_image.width();
    const double sy = target.height() / m_image.height();
    p.setPen(QPen(QColor(31, 136, 61), 3));
    p.setBrush(QColor(31, 136, 61, 22));
    for (const QPolygon& poly : m_highlights)
    {
        QPolygonF mapped;
        for (const QPoint& pt : poly)
            mapped << QPointF(target.left() + pt.x() * sx, target.top() + pt.y() * sy);
        p.drawPolygon(mapped);
    }
}

void QRImageView::dragEnterEvent(QDragEnterEvent* event)
{
    const QMimeData* md = event->mimeData();
    if (md->hasImage() || md->hasUrls())
        event->acceptProposedAction();
}

void QRImageView::dropEvent(QDropEvent* event)
{
    const QMimeData* md = event->mimeData();
    if (md->hasUrls())
    {
        for (const QUrl& url : md->urls())
        {
            if (!url.isLocalFile())
                continue;
            QImage img(url.toLocalFile());
            if (!img.isNull())
            {
                emit imageDropped(img, url.toLocalFile());
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (md->hasImage())
    {
        const QImage img = qvariant_cast<QImage>(md->imageData());
        if (!img.isNull())
        {
            emit imageDropped(img, "Ảnh kéo thả");
            event->acceptProposedAction();
        }
    }
}
