#include "InputController.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
bool isExtendedKey(int vk)
{
    switch (vk)
    {
        case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_INSERT: case VK_DELETE: case VK_LWIN: case VK_RWIN:
            return true;
        default:
            return false;
    }
}

INPUT makeKeyInput(int vk, bool keyUp)
{
    INPUT in = {0};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = static_cast<WORD>(vk);
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
    if (isExtendedKey(vk))
        in.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (keyUp)
        in.ki.dwFlags |= KEYEVENTF_KEYUP;
    return in;
}
}

void InputController::moveMouse(int x, int y)
{
    SetCursorPos(x, y);
}

void InputController::mouseDown(MouseButtonType button)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;

    switch (button)
    {
        case MouseButtonType::Left:
            input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
            break;
        case MouseButtonType::Right:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
            break;
        case MouseButtonType::Middle:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEDOWN;
            break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void InputController::mouseUp(MouseButtonType button)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;

    switch (button)
    {
        case MouseButtonType::Left:
            input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
            break;
        case MouseButtonType::Right:
            input.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
            break;
        case MouseButtonType::Middle:
            input.mi.dwFlags = MOUSEEVENTF_MIDDLEUP;
            break;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void InputController::click(MouseButtonType button)
{
    mouseDown(button);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mouseUp(button);
}

void InputController::drag(int startX, int startY, int endX, int endY, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    moveMouse(startX, startY);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    mouseDown(MouseButtonType::Left);

    int totalMs = std::max(50, static_cast<int>(duration.count()));
    int stepInterval = 20; // 20ms per step
    int steps = std::max(1, totalMs / stepInterval);

    bool stopped = false;
    for (int i = 1; i <= steps; ++i)
    {
        if (stopFlag && stopFlag->load())
        {
            stopped = true;
            break;
        }

        double t = static_cast<double>(i) / steps;
        int currentX = startX + static_cast<int>((endX - startX) * t);
        int currentY = startY + static_cast<int>((endY - startY) * t);
        moveMouse(currentX, currentY);

        std::this_thread::sleep_for(std::chrono::milliseconds(stepInterval));
    }

    if (!stopped)
    {
        moveMouse(endX, endY);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    mouseUp(MouseButtonType::Left);
}

void InputController::hold(int x, int y, MouseButtonType button, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    moveMouse(x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mouseDown(button);

    int remainingMs = static_cast<int>(duration.count());
    int interval = 50;

    while (remainingMs > 0)
    {
        if (stopFlag && stopFlag->load())
            break;

        int sleepTime = std::min(interval, remainingMs);
        std::this_thread::sleep_for(std::chrono::milliseconds(sleepTime));
        remainingMs -= sleepTime;
    }

    mouseUp(button);
}

void InputController::pressKey(int keyCode)
{
    INPUT inputs[2] = { makeKeyInput(keyCode, false), makeKeyInput(keyCode, true) };
    SendInput(2, inputs, sizeof(INPUT));
}

void InputController::hotkey(bool ctrl, bool alt, bool shift, bool win, int keyCode)
{
    std::vector<int> mods;
    if (ctrl)  mods.push_back(VK_CONTROL);
    if (alt)   mods.push_back(VK_MENU);
    if (shift) mods.push_back(VK_SHIFT);
    if (win)   mods.push_back(VK_LWIN);

    std::vector<INPUT> downInputs;
    for (int vk : mods)
        downInputs.push_back(makeKeyInput(vk, false));
    downInputs.push_back(makeKeyInput(keyCode, false));

    // Nhả phím chính trước, sau đó nhả modifier theo thứ tự ngược
    std::vector<INPUT> upInputs;
    upInputs.push_back(makeKeyInput(keyCode, true));
    for (auto it = mods.rbegin(); it != mods.rend(); ++it)
        upInputs.push_back(makeKeyInput(*it, true));

    SendInput(static_cast<UINT>(downInputs.size()), downInputs.data(), sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SendInput(static_cast<UINT>(upInputs.size()), upInputs.data(), sizeof(INPUT));
}

void InputController::typeText(const std::string& text, TextTypeMode mode, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    if (text.empty())
        return;

    // Convert UTF-8 std::string to UTF-16 std::wstring for Win32 SendInput KEYEVENTF_UNICODE
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (wlen <= 1)
        return;

    std::wstring wstr(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wstr[0], wlen);

    int charDelay = 0;
    if (mode == TextTypeMode::CharByChar && !wstr.empty())
    {
        charDelay = static_cast<int>(duration.count()) / static_cast<int>(wstr.size());
        if (charDelay <= 0) charDelay = 10;
    }

    for (wchar_t ch : wstr)
    {
        if (stopFlag && stopFlag->load())
            break;

        if (ch == 13) // CR
            continue;
        if (ch == 10 || ch == 9) // LF -> Enter, TAB -> Tab
        {
            pressKey(ch == 10 ? VK_RETURN : VK_TAB);
            if (charDelay > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(charDelay));
            continue;
        }

        INPUT inputs[2] = {0};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = ch;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = ch;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));

        if (charDelay > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(charDelay));
        }
    }
}

void InputController::scroll(ScrollDirection direction, int amount, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    int wheelClicks = std::max(1, amount);
    int stepDuration = 50;
    if (duration.count() > 0)
    {
        stepDuration = static_cast<int>(duration.count()) / wheelClicks;
        if (stepDuration <= 0) stepDuration = 10;
    }

    for (int i = 0; i < wheelClicks; ++i)
    {
        if (stopFlag && stopFlag->load())
            break;

        INPUT input = {0};
        input.type = INPUT_MOUSE;

        switch (direction)
        {
            case ScrollDirection::Down:
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);
                break;
            case ScrollDirection::Up:
                input.mi.dwFlags = MOUSEEVENTF_WHEEL;
                input.mi.mouseData = static_cast<DWORD>(WHEEL_DELTA);
                break;
            case ScrollDirection::Left:
                input.mi.dwFlags = 0x01000; // MOUSEEVENTF_HWHEEL
                input.mi.mouseData = static_cast<DWORD>(-WHEEL_DELTA);
                break;
            case ScrollDirection::Right:
                input.mi.dwFlags = 0x01000; // MOUSEEVENTF_HWHEEL
                input.mi.mouseData = static_cast<DWORD>(WHEEL_DELTA);
                break;
        }

        SendInput(1, &input, sizeof(INPUT));

        if (stepDuration > 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(stepDuration));
        }
    }
}
