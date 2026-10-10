#include "InputController.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <thread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace
{
using Clock = std::chrono::steady_clock;

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

// Map ModifierKey -> VK_* CHỈ nằm ở đây (nơi duy nhất gọi Win32 Input API, đúng Rule 3 của dự án) -
// ActionRunner/UI không cần biết gì về mã phím Windows.
int vkForModifier(ModifierKey m)
{
    switch (m)
    {
        case ModifierKey::Ctrl:  return VK_CONTROL;
        case ModifierKey::Alt:   return VK_MENU;
        case ModifierKey::Shift: return VK_SHIFT;
        case ModifierKey::Win:   return VK_LWIN;
    }
    return 0;
}

bool stopRequested(const std::atomic_bool* stopFlag)
{
    return stopFlag && stopFlag->load();
}

/// Ngủ tới MỐC THỜI GIAN `deadline` theo từng lát ngắn (tối đa 50ms), kiểm tra cờ dừng giữa các lát - trả
/// về false nếu bị yêu cầu dừng giữa chừng. Hai lỗi thật của cách ngủ cũ (`sleep_for(cả khoảng)` một lần):
///  - KHÔNG NGẮT ĐƯỢC: gõ 2 ký tự trong 60 giây = mỗi ký tự ngủ liền 30 giây, cuộn 1 nấc với thời lượng
///    1 giờ = ngủ liền 1 giờ - bấm Dừng/đóng cửa sổ không có tác dụng cho tới khi ngủ xong.
///  - TRÔI THỜI GIAN: cộng dồn nhiều lần Sleep(n) (mỗi lần Windows đều ngủ DƯ tới ~15ms theo độ phân giải
///    timer mặc định) làm thời lượng thật dài hơn cấu hình; ngủ theo mốc tuyệt đối thì sai số không cộng dồn.
bool sleepUntil(Clock::time_point deadline, const std::atomic_bool* stopFlag)
{
    constexpr auto kSlice = std::chrono::milliseconds(50);
    for (;;)
    {
        if (stopRequested(stopFlag))
            return false;
        const auto now = Clock::now();
        if (now >= deadline)
            return true;
        std::this_thread::sleep_for(std::min<Clock::duration>(kSlice, deadline - now));
    }
}

bool sleepFor(std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    return sleepUntil(Clock::now() + duration, stopFlag);
}
}

void InputController::noteError(const char* what)
{
    if (!m_lastError.isEmpty())
        return; // chỉ giữ lỗi ĐẦU TIÊN - các lỗi sau thường chỉ là hệ quả của nó

    const DWORD code = GetLastError();
    m_lastError = QString("%1 bị Windows từ chối (mã lỗi %2)").arg(QString::fromUtf8(what)).arg(code);
    if (code == ERROR_ACCESS_DENIED)
        m_lastError += " - cửa sổ đích có thể đang chạy với quyền Administrator, hoặc máy đang ở màn hình "
                       "khóa/hộp thoại UAC";
}

bool InputController::sendInputs(void* inputs, unsigned int count, const char* what)
{
    SetLastError(0);
    const UINT sent = SendInput(count, static_cast<INPUT*>(inputs), sizeof(INPUT));
    if (sent == count)
        return true;
    noteError(what);
    return false;
}

void InputController::moveMouse(int x, int y)
{
    SetLastError(0);
    if (!SetCursorPos(x, y))
        noteError("Di chuyển chuột (SetCursorPos)");
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

    sendInputs(&input, 1, "Nhấn nút chuột (SendInput)");
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

    sendInputs(&input, 1, "Nhả nút chuột (SendInput)");
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

    // Mốc thời gian của từng bước tính từ lúc BẮT ĐẦU kéo (không cộng dồn sleep 20ms mỗi bước) để tổng
    // thời gian kéo đúng bằng thời lượng đã cấu hình - xem sleepUntil().
    const auto dragStart = Clock::now();
    bool stopped = false;
    for (int i = 1; i <= steps; ++i)
    {
        if (stopRequested(stopFlag))
        {
            stopped = true;
            break;
        }

        double t = static_cast<double>(i) / steps;
        int currentX = startX + static_cast<int>((endX - startX) * t);
        int currentY = startY + static_cast<int>((endY - startY) * t);
        moveMouse(currentX, currentY);

        const auto stepDeadline = dragStart + std::chrono::milliseconds(static_cast<long long>(totalMs) * i / steps);
        if (!sleepUntil(stepDeadline, stopFlag))
        {
            stopped = true;
            break;
        }
    }

    if (!stopped)
    {
        moveMouse(endX, endY);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // LUÔN nhả nút chuột kể cả khi bị dừng giữa chừng - không để nút trái "kẹt" ở trạng thái nhấn.
    mouseUp(MouseButtonType::Left);
}

void InputController::hold(int x, int y, MouseButtonType button, std::chrono::milliseconds duration, const std::atomic_bool* stopFlag)
{
    moveMouse(x, y);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mouseDown(button);

    sleepFor(duration, stopFlag);

    // LUÔN nhả nút chuột kể cả khi bị dừng giữa chừng.
    mouseUp(button);
}

void InputController::pressKey(int keyCode)
{
    INPUT inputs[2] = { makeKeyInput(keyCode, false), makeKeyInput(keyCode, true) };
    sendInputs(inputs, 2, "Nhấn phím (SendInput)");
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

    sendInputs(downInputs.data(), static_cast<UINT>(downInputs.size()), "Nhấn tổ hợp phím (SendInput)");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // Vẫn gửi lệnh NHẢ kể cả khi lệnh nhấn ở trên báo lỗi - SendInput có thể đã chèn được một phần (vd
    // chỉ các phím bổ trợ), không nhả thì Ctrl/Alt/Shift/Win kẹt ở trạng thái nhấn.
    sendInputs(upInputs.data(), static_cast<UINT>(upInputs.size()), "Nhả tổ hợp phím (SendInput)");
}

void InputController::hotkey(const std::vector<ModifierKey>& orderedMods, int keyCode)
{
    std::vector<INPUT> downInputs;
    for (ModifierKey m : orderedMods)
        downInputs.push_back(makeKeyInput(vkForModifier(m), false));
    downInputs.push_back(makeKeyInput(keyCode, false));

    std::vector<INPUT> upInputs;
    upInputs.push_back(makeKeyInput(keyCode, true));
    for (auto it = orderedMods.rbegin(); it != orderedMods.rend(); ++it)
        upInputs.push_back(makeKeyInput(vkForModifier(*it), true));

    sendInputs(downInputs.data(), static_cast<UINT>(downInputs.size()), "Nhấn tổ hợp phím (SendInput)");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    sendInputs(upInputs.data(), static_cast<UINT>(upInputs.size()), "Nhả tổ hợp phím (SendInput)");
}

void InputController::pressModifiersOrdered(const std::vector<ModifierKey>& orderedMods)
{
    if (orderedMods.empty())
        return;
    std::vector<INPUT> downInputs;
    for (ModifierKey m : orderedMods)
        downInputs.push_back(makeKeyInput(vkForModifier(m), false));
    sendInputs(downInputs.data(), static_cast<UINT>(downInputs.size()), "Giữ phím bổ trợ (SendInput)");
}

void InputController::releaseModifiersOrdered(const std::vector<ModifierKey>& orderedMods)
{
    if (orderedMods.empty())
        return;
    std::vector<INPUT> upInputs;
    for (auto it = orderedMods.rbegin(); it != orderedMods.rend(); ++it)
        upInputs.push_back(makeKeyInput(vkForModifier(*it), true));
    sendInputs(upInputs.data(), static_cast<UINT>(upInputs.size()), "Nhả phím bổ trợ (SendInput)");
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

    // Mốc thời gian của ký tự kế tiếp cộng dồn từ lúc BẮT ĐẦU gõ (không phải "ngủ charDelay sau mỗi ký
    // tự") - vừa ngắt được giữa chừng, vừa không trôi thời gian, xem sleepUntil().
    auto nextCharAt = Clock::now();

    for (wchar_t ch : wstr)
    {
        if (stopRequested(stopFlag))
            break;

        if (ch == 13) // CR
            continue;
        if (ch == 10 || ch == 9) // LF -> Enter, TAB -> Tab
        {
            pressKey(ch == 10 ? VK_RETURN : VK_TAB);
        }
        else
        {
            INPUT inputs[2] = {0};
            inputs[0].type = INPUT_KEYBOARD;
            inputs[0].ki.wScan = ch;
            inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

            inputs[1].type = INPUT_KEYBOARD;
            inputs[1].ki.wScan = ch;
            inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

            sendInputs(inputs, 2, "Gõ ký tự (SendInput)");
        }

        // Windows đã từ chối thì các ký tự sau cũng sẽ bị từ chối y hệt - dừng luôn thay vì "gõ" tiếp
        // vào hư không cho hết chuỗi.
        if (hasError())
            break;

        if (charDelay > 0)
        {
            nextCharAt += std::chrono::milliseconds(charDelay);
            if (!sleepUntil(nextCharAt, stopFlag))
                break;
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

    auto nextStepAt = Clock::now(); // mốc cộng dồn từ lúc bắt đầu cuộn - xem typeText()

    for (int i = 0; i < wheelClicks; ++i)
    {
        if (stopRequested(stopFlag))
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

        if (!sendInputs(&input, 1, "Cuộn chuột (SendInput)"))
            break;

        if (stepDuration > 0)
        {
            nextStepAt += std::chrono::milliseconds(stepDuration);
            if (!sleepUntil(nextStepAt, stopFlag))
                break;
        }
    }
}
