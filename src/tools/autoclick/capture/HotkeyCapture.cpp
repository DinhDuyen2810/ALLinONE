#include "HotkeyCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
// SetWindowsHookEx yêu cầu con trỏ hàm thường (không phải con trỏ hàm thành viên) - chỉ chuyển tiếp sự
// kiện tới MỘT instance đang bắt tại một thời điểm, đúng với cách dùng thực tế (chỉ một nút "Bắt tổ hợp
// phím" có thể đang chờ tại một thời điểm trong ActionEditorWidget).
HotkeyCapture* g_instance = nullptr;

bool isModifierVk(int vkCode)
{
    return vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL ||
           vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU ||
           vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT ||
           vkCode == VK_LWIN || vkCode == VK_RWIN;
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_instance)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    // Bỏ qua phím do chính ứng dụng này tiêm vào (SendInput) - không liên quan ở đây (không có luồng
    // nào tiêm phím trong lúc đang bắt tổ hợp), nhưng vẫn lọc cho nhất quán với InputHook.
    if (data->flags & LLKHF_INJECTED)
        return CallNextHookEx(nullptr, nCode, wParam, lParam);

    const bool pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    g_instance->onRawKey(static_cast<int>(data->vkCode), pressed);

    // Luôn nuốt MỌI phím trong lúc đang bắt - tránh tổ hợp đang gõ vô tình kích hoạt phím tắt thật của
    // ứng dụng khác đang có focus (vd Ctrl+Shift+S có thể là "Save As" ở nơi khác).
    return 1;
}
} // namespace

HotkeyCapture::HotkeyCapture(QObject* parent)
    : QObject(parent)
{
}

HotkeyCapture::~HotkeyCapture()
{
    uninstallHook();
}

void HotkeyCapture::startCapture()
{
    if (m_keyboardHook)
        return; // đã đang bắt

    m_ctrlDown = m_altDown = m_shiftDown = m_winDown = false;
    g_instance = this;
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!m_keyboardHook)
        g_instance = nullptr;
}

void HotkeyCapture::cancelCapture()
{
    if (!m_keyboardHook)
        return;
    uninstallHook();
    emit captureCancelled();
}

void HotkeyCapture::uninstallHook()
{
    if (m_keyboardHook)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_keyboardHook));
        m_keyboardHook = nullptr;
    }
    if (g_instance == this)
        g_instance = nullptr;
}

bool HotkeyCapture::onRawKey(int vkCode, bool pressed)
{
    if (vkCode == VK_ESCAPE && pressed)
    {
        cancelCapture();
        return true;
    }

    if (isModifierVk(vkCode))
    {
        const bool isCtrl = (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL);
        const bool isAlt = (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU);
        const bool isShift = (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT);
        const bool isWin = (vkCode == VK_LWIN || vkCode == VK_RWIN);

        if (isCtrl) m_ctrlDown = pressed;
        else if (isAlt) m_altDown = pressed;
        else if (isShift) m_shiftDown = pressed;
        else if (isWin) m_winDown = pressed;
        return true;
    }

    // Phím "chính" (không phải phím bổ trợ) - chỉ bắt lúc NHẤN XUỐNG, bỏ qua lúc nhả ra.
    if (pressed)
    {
        const bool ctrl = m_ctrlDown, alt = m_altDown, shift = m_shiftDown, win = m_winDown;
        uninstallHook();
        emit hotkeyCaptured(ctrl, alt, shift, win, vkCode);
    }
    return true;
}
