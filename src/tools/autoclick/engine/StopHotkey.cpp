#include "StopHotkey.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
// Mã định danh phím tắt trong phạm vi MỘT cửa sổ (0x0000-0xBFFF cho ứng dụng) - giá trị tùy ý, chỉ cần
// không trùng phím tắt khác đăng ký cho cùng cửa sổ (hiện không có cái nào khác).
constexpr int kHotkeyId = 0x4F41; // "OA"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif
} // namespace

namespace StopHotkey
{

QString label()
{
    return QStringLiteral("Ctrl+Alt+F8");
}

bool registerFor(WId windowId)
{
    // MOD_NOREPEAT: giữ phím không sinh ra một chuỗi WM_HOTKEY lặp lại.
    return RegisterHotKey(reinterpret_cast<HWND>(windowId), kHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F8) != 0;
}

void unregisterFor(WId windowId)
{
    UnregisterHotKey(reinterpret_cast<HWND>(windowId), kHotkeyId);
}

bool isStopMessage(const void* message)
{
    const auto* msg = static_cast<const MSG*>(message);
    return msg && msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotkeyId;
}

} // namespace StopHotkey
