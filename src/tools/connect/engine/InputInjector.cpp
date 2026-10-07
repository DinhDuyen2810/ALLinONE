#include "InputInjector.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

void InputInjector::moveRelative(int dx, int dy)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    input.mi.dx = dx;
    input.mi.dy = dy;
    SendInput(1, &input, sizeof(INPUT));
}

void InputInjector::moveAbsolute(int x, int y)
{
    // SetCursorPos chính xác tuyệt đối theo pixel vật lý, đơn giản hơn chuẩn hóa tọa độ cho
    // SendInput(MOUSEEVENTF_ABSOLUTE) - cùng cách MouseCapture của module Auto Click dùng để đặt vị trí.
    SetCursorPos(x, y);
}

void InputInjector::mouseButton(int button, bool pressed)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE;

    switch (button)
    {
        case 0: input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
        case 1: input.mi.dwFlags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
        case 2: input.mi.dwFlags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
        case 3:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON1;
            break;
        case 4:
            input.mi.dwFlags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
            input.mi.mouseData = XBUTTON2;
            break;
        default:
            return;
    }
    SendInput(1, &input, sizeof(INPUT));
}

void InputInjector::mouseWheel(int deltaY, int deltaX)
{
    if (deltaY != 0)
    {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaY);
        SendInput(1, &input, sizeof(INPUT));
    }
    if (deltaX != 0)
    {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        input.mi.mouseData = static_cast<DWORD>(deltaX);
        SendInput(1, &input, sizeof(INPUT));
    }
}

void InputInjector::keyEvent(int vkCode, int scanCode, bool pressed, bool extended)
{
    INPUT input = {0};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(vkCode);
    input.ki.wScan = static_cast<WORD>(scanCode);
    input.ki.dwFlags = extended ? KEYEVENTF_EXTENDEDKEY : 0;
    if (!pressed)
        input.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}
