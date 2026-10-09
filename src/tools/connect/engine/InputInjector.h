#pragma once

#include <QPoint>
#include <QRect>

/**
 * @brief Lớp duy nhất "phát lại" sự kiện chuột/bàn phím nhận được từ máy đã ghép đôi lên MÁY NÀY
 * (qua Win32 SendInput/SetCursorPos) - tuân thủ nguyên tắc cô lập Win32 của dự án, giống hệt
 * InputController của module Auto Click nhưng độc lập (mỗi module tự chứa, không phụ thuộc chéo).
 *
 * Chỉ được gọi khi máy này đang NHẬN quyền điều khiển từ một máy đã ghép đôi hợp lệ
 * (ConnectSessionController đảm bảo điều đó, lớp này không tự kiểm tra ai gọi).
 *
 * Các hàm tiêm input là virtual CHỈ để bộ kiểm thử thay bằng một bản ghi nhận (không đụng chuột/bàn phím
 * thật của người đang ngồi máy chạy test) - xem ConnectSessionController::TestConfig.
 */
class InputInjector
{
public:
    InputInjector() = default;
    virtual ~InputInjector() = default;

    virtual void moveRelative(int dx, int dy);
    virtual void moveAbsolute(int x, int y);

    /// Vị trí con trỏ vật lý hiện tại (GetCursorPos) - dùng để theo dõi chạm biên màn hình.
    static QPoint currentCursorPos();

    /// Hình chữ nhật bao toàn bộ màn hình ảo (mọi màn hình gộp lại), CÙNG hệ tọa độ pixel vật lý với
    /// GetCursorPos/SetCursorPos (GetSystemMetrics(SM_*VIRTUALSCREEN)). Không tự quy đổi từ tọa độ logic
    /// của Qt nhân devicePixelRatio của màn hình chính như trước - phép quy đổi đó sai khi các màn hình
    /// có tỉ lệ phóng (DPI) khác nhau.
    static QRect virtualScreenBounds();

    /// Có nút chuột vật lý nào đang được giữ không (GetAsyncKeyState - trạng thái thật của hệ điều hành).
    /// Đang kéo-thả mà chạm biên thì KHÔNG trao quyền điều khiển: thao tác kéo không thể tiếp tục sang máy
    /// khác, và máy này sẽ không bao giờ thấy lần nhả nút.
    static bool anyMouseButtonDown();

    /// button: 0=Left, 1=Right, 2=Middle, 3=X1, 4=X2
    virtual void mouseButton(int button, bool pressed);
    virtual void mouseWheel(int deltaY, int deltaX);

    virtual void keyEvent(int vkCode, int scanCode, bool pressed, bool extended);
};
