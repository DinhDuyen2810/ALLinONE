#pragma once

#include <QObject>

/**
 * @brief Bắt một thao tác KÉO CHUỘT THẬT (nhấn-giữ-kéo-thả) qua hook chuột toàn cục tạm thời
 * (WH_MOUSE_LL) - KHÁC HotkeyCapture: hook này CHỈ QUAN SÁT (không bao giờ "nuốt" sự kiện), để thao tác
 * kéo thật của người dùng vẫn tác động lên ứng dụng/màn hình thật phía dưới như bình thường trong lúc
 * ứng dụng này đã tự ẩn đi - đúng yêu cầu "màn hình vẫn nhận tương tác". Ứng dụng chỉ ghi lại tọa độ
 * điểm nhấn chuột xuống và điểm thả chuột ra để lưu thành một Action kiểu Kéo chuột.
 */
class DragGestureCapture : public QObject
{
    Q_OBJECT

public:
    explicit DragGestureCapture(QObject* parent = nullptr);
    ~DragGestureCapture() override;

    void startCapture();
    void cancelCapture();
    bool isCapturing() const { return m_mouseHook != nullptr; }

signals:
    void dragCaptured(int startX, int startY, int endX, int endY);

public:
    // Chỉ dành cho hàm callback Win32 tĩnh trong .cpp gọi (xem InputHook.h cho quy ước tương tự).
    void onRawMouse(int message, long x, long y);

private:
    void uninstallHook();

    void* m_mouseHook{nullptr};
    bool m_pressed{false};
    long m_startX{0};
    long m_startY{0};
};
