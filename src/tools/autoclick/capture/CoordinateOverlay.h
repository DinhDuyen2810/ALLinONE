#pragma once

#include <QWidget>
#include <QPoint>

class CoordinateOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit CoordinateOverlay(QWidget* parent = nullptr);

signals:
    void pointCaptured(int x, int y);
    void captureCancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPoint m_currentPos;  // pixel vật lý (Win32)
    QPoint m_localPos;    // pixel logic trong widget, chỉ để vẽ
};
