#pragma once

#include "../model/Action.h"
#include <QString>
#include <string>
#include <vector>
#include <atomic>

/**
 * @brief Đóng gói tương tác với Windows Input API (SendInput, SetCursorPos).
 * Tuân thủ Rule 1 và Rule 3: Chỉ tầng này được gọi Win32 Input API.
 */
class InputController
{
public:
    InputController() = default;
    ~InputController() = default;

    void moveMouse(int x, int y);
    void mouseDown(MouseButtonType button);
    void mouseUp(MouseButtonType button);
    void click(MouseButtonType button);

    void drag(int startX, int startY, int endX, int endY, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag = nullptr);
    void hold(int x, int y, MouseButtonType button, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag = nullptr);

    void pressKey(int keyCode);
    void hotkey(bool ctrl, bool alt, bool shift, bool win, int keyCode);
    void typeText(const std::string& text, TextTypeMode mode, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag = nullptr);
    void scroll(ScrollDirection direction, int amount, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag = nullptr);

    /// Windows có từ chối thao tác nào kể từ lần clearError() gần nhất không (SendInput chèn thiếu sự
    /// kiện / SetCursorPos trả FALSE - thường gặp khi cửa sổ đích chạy quyền Administrator còn ứng dụng
    /// này thì không (UIPI), hoặc đang ở màn hình khóa/hộp thoại UAC). Trước đây mọi giá trị trả về của
    /// Win32 đều bị bỏ qua nên chuỗi hành động "chạy" tiếp mà không có gì xảy ra trên màn hình. Chỉ GHI
    /// NHẬN lỗi đầu tiên; các hàm thao tác ở trên vẫn giữ kiểu trả về void như cũ (CommandLauncher của
    /// Security Gateway cũng gọi hotkey()).
    bool hasError() const { return !m_lastError.isEmpty(); }
    QString lastError() const { return m_lastError; }
    void clearError() { m_lastError.clear(); }

private:
    bool sendInputs(void* inputs, unsigned int count, const char* what);
    void noteError(const char* what);

    QString m_lastError;
};
