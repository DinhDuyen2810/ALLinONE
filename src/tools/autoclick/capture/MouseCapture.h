#pragma once

#include <QObject>
#include <QWidget>

class DragGestureCapture;

class MouseCapture : public QObject
{
    Q_OBJECT

public:
    explicit MouseCapture(QWidget* parentWindow, QObject* parent = nullptr);

    void startCapture();

    /// Ẩn cửa sổ chính rồi chờ một thao tác KÉO CHUỘT THẬT (nhấn-kéo-thả) - khác startCapture() (chỉ
    /// bắt 1 điểm bằng cú click): không chặn thao tác thật lên màn hình/ứng dụng phía dưới, chỉ quan
    /// sát tọa độ nhấn xuống/thả ra. Xem DragGestureCapture.h.
    void startDragGestureCapture();

signals:
    void pointCaptured(int x, int y);
    void captureCancelled();
    void dragGestureCaptured(int startX, int startY, int endX, int endY);
    /// Không vào được chế độ bắt (vd Windows từ chối cài hook chuột) - cửa sổ chính ĐÃ được hiện lại
    /// trước khi tín hiệu này phát ra, nơi nhận chỉ cần báo cho người dùng.
    void captureFailed(const QString& message);

private:
    void restoreParentWindow();

    QWidget* m_parentWindow;
    DragGestureCapture* m_dragGestureCapture;
};
