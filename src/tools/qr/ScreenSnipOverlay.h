#pragma once

#include <QImage>
#include <QList>
#include <QPixmap>
#include <QPoint>
#include <QWidget>

/// Chụp một vùng màn hình: đóng băng màn hình, kéo chuột để chọn vùng, ESC để hủy.
class ScreenSnipOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenSnipOverlay(QWidget* parent = nullptr);

signals:
    void captured(const QImage& image);
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    struct ScreenShot
    {
        QRect logicalRect; // tọa độ logic toàn desktop
        QPixmap pixmap;    // ảnh pixel vật lý
        qreal dpr{1.0};
    };

    QImage cropSelection(const QRect& globalLogicalRect) const;

    QList<ScreenShot> m_shots;
    QPoint m_start;
    QPoint m_current;
    bool m_dragging{false};
    bool m_done{false}; // đã phát captured()/cancelled() rồi - closeEvent() không phát thêm lần nữa
};
