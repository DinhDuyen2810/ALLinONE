#pragma once

#include <QWidget>
#include <QPoint>

/// Bắt 1 điểm tọa độ chuột cho nút "Lấy tọa độ" - ESC hủy, click trái chọn điểm.
///
/// ESC trước đây chỉ xử lý qua Qt keyPressEvent() thường (phụ thuộc focus cửa sổ thật của hệ thống) -
/// không đáng tin cậy bằng hook toàn cục WH_KEYBOARD_LL mà DragGestureCapture/HotkeyCapture đã dùng đúng:
/// nếu overlay (dù WindowStaysOnTopHint) không giữ được focus bàn phím đúng lúc (ứng dụng khác giành lại,
/// race condition lúc show()/activateWindow()), ESC gửi tới cửa sổ khác thay vì tới đây - người dùng báo
/// "ấn ESC không hủy được" (xác nhận v1.19.7). Giờ thêm hook ESC-only giống DragGestureCapture::onRawKey
/// (chỉ nuốt đúng phím Esc, không ảnh hưởng gì khác) - không phụ thuộc focus hệ thống. keyPressEvent() cũ
/// GIỮ LẠI làm lớp dự phòng (không hại gì) nếu Windows từ chối cài hook.
class CoordinateOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit CoordinateOverlay(QWidget* parent = nullptr);
    ~CoordinateOverlay() override;

signals:
    void pointCaptured(int x, int y);
    void captureCancelled();

public:
    // Chỉ dành cho hàm callback Win32 tĩnh trong .cpp gọi (cùng quy ước với DragGestureCapture/HotkeyCapture).
    bool onRawKey(int vkCode, bool pressed);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void installEscapeHook();
    void uninstallEscapeHook();
    void requestCancel();
    void requestCapture(int x, int y);

    QPoint m_currentPos;  // pixel vật lý (Win32)
    QPoint m_localPos;    // pixel logic trong widget, chỉ để vẽ
    void* m_keyboardHook{nullptr};
    bool m_finished{false}; // chặn emit/close lặp lại (hook ESC + keyPressEvent dự phòng có thể cùng kích hoạt)
};
