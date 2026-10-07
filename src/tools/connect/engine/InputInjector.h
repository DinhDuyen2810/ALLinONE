#pragma once

#include <QPoint>

/**
 * @brief Lớp duy nhất "phát lại" sự kiện chuột/bàn phím nhận được từ máy đã ghép đôi lên MÁY NÀY
 * (qua Win32 SendInput/SetCursorPos) - tuân thủ nguyên tắc cô lập Win32 của dự án, giống hệt
 * InputController của module Auto Click nhưng độc lập (mỗi module tự chứa, không phụ thuộc chéo).
 *
 * Chỉ được gọi khi máy này đang NHẬN quyền điều khiển từ một máy đã ghép đôi hợp lệ
 * (ConnectSessionController đảm bảo điều đó, lớp này không tự kiểm tra ai gọi).
 */
class InputInjector
{
public:
    InputInjector() = default;
    ~InputInjector() = default;

    void moveRelative(int dx, int dy);
    void moveAbsolute(int x, int y);

    /// Vị trí con trỏ vật lý hiện tại (GetCursorPos) - dùng để theo dõi chạm biên màn hình.
    static QPoint currentCursorPos();

    /// button: 0=Left, 1=Right, 2=Middle, 3=X1, 4=X2
    void mouseButton(int button, bool pressed);
    void mouseWheel(int deltaY, int deltaX);

    void keyEvent(int vkCode, int scanCode, bool pressed, bool extended);
};
