#pragma once

#include <QObject>
#include <QString>

class QTimer;

/**
 * @brief Bắt một thao tác KÉO CHUỘT THẬT (nhấn-giữ-kéo-thả) qua hook chuột toàn cục tạm thời
 * (WH_MOUSE_LL) - KHÁC HotkeyCapture: hook này CHỈ QUAN SÁT (không bao giờ "nuốt" sự kiện), để thao tác
 * kéo thật của người dùng vẫn tác động lên ứng dụng/màn hình thật phía dưới như bình thường trong lúc
 * ứng dụng này đã tự ẩn đi - đúng yêu cầu "màn hình vẫn nhận tương tác". Ứng dụng chỉ ghi lại tọa độ
 * điểm nhấn chuột xuống và điểm thả chuột ra để lưu thành một Action kiểu Kéo chuột.
 *
 * Luôn có LỐI THOÁT: Esc (hook bàn phím tạm thời, chỉ nuốt đúng phím Esc) hoặc tự hủy sau 30 giây.
 * Trước đây không có cả hai - cửa sổ Auto Click đã ẩn đi để chờ, người dùng đổi ý không kéo nữa thì cửa
 * sổ ẩn luôn và hook chuột toàn cục treo vô thời hạn.
 */
class DragGestureCapture : public QObject
{
    Q_OBJECT

public:
    explicit DragGestureCapture(QObject* parent = nullptr);
    ~DragGestureCapture() override;

    /// Trả về false nếu KHÔNG cài được hook chuột (không có gì để chờ) - nơi gọi phải tự hiện lại cửa
    /// sổ và báo lỗi, không có tín hiệu nào phát ra trong trường hợp này.
    bool startCapture();
    void cancelCapture();
    bool isCapturing() const { return m_mouseHook != nullptr; }

signals:
    void dragCaptured(int startX, int startY, int endX, int endY);
    /// Người dùng bấm Esc, hết 30 giây chờ, hoặc cancelCapture() được gọi trong lúc đang bắt.
    void captureCancelled();

public:
    // Chỉ dành cho hàm callback Win32 tĩnh trong .cpp gọi (xem InputHook.h cho quy ước tương tự).
    void onRawMouse(int message, long x, long y);
    /// Trả về true nếu đã "nuốt" phím (chỉ phím Esc).
    bool onRawKey(int vkCode, bool pressed);

private:
    void uninstallHook();

    void* m_mouseHook{nullptr};
    void* m_keyboardHook{nullptr};
    bool m_pressed{false};
    long m_startX{0};
    long m_startY{0};
    QTimer* m_timeoutTimer{nullptr};
};
