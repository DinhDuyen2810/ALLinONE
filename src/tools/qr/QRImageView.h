#pragma once

#include <QImage>
#include <QList>
#include <QPolygon>
#include <QWidget>

/// Khung xem ảnh co giãn giữ tỷ lệ, vẽ viền quanh mã QR đã nhận diện, nhận kéo-thả ảnh/file.
class QRImageView : public QWidget
{
    Q_OBJECT

public:
    explicit QRImageView(QWidget* parent = nullptr);

    void setImage(const QImage& image);
    const QImage& image() const { return m_image; }
    void setHighlights(const QList<QPolygon>& polygons);
    void setPlaceholder(const QString& text);

signals:
    void imageDropped(const QImage& image, const QString& sourceName);

protected:
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    QSize sizeHint() const override { return {420, 320}; }

private:
    QRectF imageRect() const;

    QImage m_image;
    QList<QPolygon> m_highlights;
    QString m_placeholder;
};
