#pragma once

#include "../model/Action.h"
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
};
