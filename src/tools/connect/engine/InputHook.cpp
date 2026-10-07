#include "InputHook.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
// SetWindowsHookEx yêu cầu con trỏ hàm thường (không phải con trỏ hàm thành viên), nên chỉ có thể
// forward sự kiện tới MỘT instance đang hoạt động tại một thời điểm - đúng với cách ứng dụng này dùng
// (chỉ một InputHook sống trong suốt vòng đời ConnectSessionController).
InputHook* g_instance = nullptr;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_instance)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    const auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);

    // Bỏ qua sự kiện do chính ứng dụng này (hoặc bất kỳ ứng dụng nào khác) tiêm vào bằng SendInput -
    // tránh vòng lặp vô hạn giữa InputHook (bắt) và InputInjector (tiêm) nếu cả hai cùng chạy.
    if (data->flags & LLMHF_INJECTED)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    bool consumed = false;
    switch (wParam)
    {
        case WM_MOUSEMOVE:
            consumed = g_instance->onRawMouseMove(data->pt.x, data->pt.y);
            break;
        case WM_LBUTTONDOWN: consumed = g_instance->onRawMouseButton(0, true); break;
        case WM_LBUTTONUP:   consumed = g_instance->onRawMouseButton(0, false); break;
        case WM_RBUTTONDOWN: consumed = g_instance->onRawMouseButton(1, true); break;
        case WM_RBUTTONUP:   consumed = g_instance->onRawMouseButton(1, false); break;
        case WM_MBUTTONDOWN: consumed = g_instance->onRawMouseButton(2, true); break;
        case WM_MBUTTONUP:   consumed = g_instance->onRawMouseButton(2, false); break;
        case WM_XBUTTONDOWN:
            consumed = g_instance->onRawMouseButton(HIWORD(data->mouseData) == XBUTTON1 ? 3 : 4, true);
            break;
        case WM_XBUTTONUP:
            consumed = g_instance->onRawMouseButton(HIWORD(data->mouseData) == XBUTTON1 ? 3 : 4, false);
            break;
        case WM_MOUSEWHEEL:
            consumed = g_instance->onRawMouseWheel(static_cast<SHORT>(HIWORD(data->mouseData)), 0);
            break;
        case WM_MOUSEHWHEEL:
            consumed = g_instance->onRawMouseWheel(0, static_cast<SHORT>(HIWORD(data->mouseData)));
            break;
        default:
            break;
    }

    if (consumed)
        return 1; // nuốt sự kiện - không cho lan xuống hệ thống/ứng dụng cục bộ khác
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_instance)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    if (data->flags & LLKHF_INJECTED)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    const bool pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool extended = (data->flags & LLKHF_EXTENDED) != 0;
    const bool consumed = g_instance->onRawKey(static_cast<int>(data->vkCode), static_cast<int>(data->scanCode),
                                               pressed, extended);

    if (consumed)
        return 1;
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

InputHook::InputHook(QObject* parent)
    : QObject(parent)
{
}

InputHook::~InputHook()
{
    uninstall();
}

bool InputHook::install(QString* error)
{
    if (m_mouseHook && m_keyboardHook)
        return true;
    if (g_instance && g_instance != this)
    {
        if (error) *error = "Đã có một InputHook khác đang hoạt động trong tiến trình này.";
        return false;
    }

    g_instance = this;

    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, nullptr, 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, nullptr, 0);

    if (!mouse || !keyboard)
    {
        if (mouse) UnhookWindowsHookEx(mouse);
        if (keyboard) UnhookWindowsHookEx(keyboard);
        g_instance = nullptr;
        if (error) *error = QString("Không cài được hook bàn phím/chuột toàn cục (mã lỗi %1).").arg(GetLastError());
        return false;
    }

    m_mouseHook = mouse;
    m_keyboardHook = keyboard;
    m_hasLastPoint = false;
    return true;
}

void InputHook::uninstall()
{
    if (m_mouseHook)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_mouseHook));
        m_mouseHook = nullptr;
    }
    if (m_keyboardHook)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyboardHook));
        m_keyboardHook = nullptr;
    }
    if (g_instance == this)
        g_instance = nullptr;
    m_active = false;
}

bool InputHook::isInstalled() const
{
    return m_mouseHook != nullptr && m_keyboardHook != nullptr;
}

bool InputHook::onRawMouseMove(long x, long y)
{
    if (!m_active)
    {
        m_hasLastPoint = false; // không theo dõi khi không active - tránh bước nhảy delta lớn lúc kích hoạt lại
        return false;
    }

    if (!m_hasLastPoint)
    {
        m_lastX = x;
        m_lastY = y;
        m_hasLastPoint = true;
        return true; // vẫn nuốt sự kiện đầu tiên (không có delta hợp lệ để gửi)
    }

    const int dx = static_cast<int>(x - m_lastX);
    const int dy = static_cast<int>(y - m_lastY);
    m_lastX = x;
    m_lastY = y;

    if (dx != 0 || dy != 0)
        emit mouseMoveRelative(dx, dy);
    return true;
}

bool InputHook::onRawMouseButton(int button, bool pressed)
{
    if (!m_active)
        return false;
    emit mouseButtonChanged(button, pressed);
    return true;
}

bool InputHook::onRawMouseWheel(int deltaY, int deltaX)
{
    if (!m_active)
        return false;
    emit mouseWheelMoved(deltaY, deltaX);
    return true;
}

bool InputHook::onRawKey(int vkCode, int scanCode, bool pressed, bool extended)
{
    // Theo dõi Ctrl/Alt kể cả khi không active, để phím nóng Ctrl+Alt+Home hoạt động ngay cả khi
    // người dùng vừa mới trả quyền điều khiển (tránh lệch trạng thái modifier).
    if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL)
        m_ctrlDown = pressed;
    else if (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU)
        m_altDown = pressed;

    if (pressed && vkCode == VK_HOME && m_ctrlDown && m_altDown)
    {
        emit returnHotkeyPressed();
        return m_active; // nuốt tổ hợp phím nóng khi đang active (không gửi Home xuống máy đang điều khiển)
    }

    if (!m_active)
        return false;

    emit keyChanged(vkCode, scanCode, pressed, extended);
    return true;
}
