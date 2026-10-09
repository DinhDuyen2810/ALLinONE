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

struct Combo
{
    UINT modifiers;
    UINT key;
    const char* label;
};

// Thử LẦN LƯỢT, dùng tổ hợp đầu tiên Windows chấp nhận. Một tổ hợp duy nhất là không đủ: RegisterHotKey
// thất bại (lỗi 1409) khi một chương trình khác đã giữ đúng tổ hợp đó - đã gặp THẬT trên máy dev, Ctrl+Alt+F8
// bị chương trình khác chiếm nên phím dừng khẩn cấp lặng lẽ không tồn tại. Mọi phương án đều có ít nhất hai
// phím bổ trợ + một phím chức năng (xem StopHotkey.h vì sao không dùng phím đơn).
const Combo kCombos[] = {
    {MOD_CONTROL | MOD_ALT, VK_F8, "Ctrl+Alt+F8"},
    {MOD_CONTROL | MOD_ALT, VK_F9, "Ctrl+Alt+F9"},
    {MOD_CONTROL | MOD_ALT, VK_F10, "Ctrl+Alt+F10"},
    {MOD_CONTROL | MOD_SHIFT, VK_F8, "Ctrl+Shift+F8"},
};

// Chỉ đụng tới từ luồng GUI (registerFor/unregisterFor phải gọi từ luồng sở hữu cửa sổ).
int g_activeCombo = -1;
HWND g_activeWindow = nullptr;
} // namespace

namespace StopHotkey
{

QString label()
{
    return QString::fromLatin1(kCombos[g_activeCombo >= 0 ? g_activeCombo : 0].label);
}

bool registerFor(WId windowId)
{
    // Đang giữ một phím dừng rồi (ứng dụng chỉ có một chuỗi chạy tại một thời điểm): không đăng ký chồng.
    if (g_activeCombo >= 0)
        return false;

    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    for (int i = 0; i < static_cast<int>(sizeof(kCombos) / sizeof(kCombos[0])); ++i)
    {
        // MOD_NOREPEAT: giữ phím không sinh ra một chuỗi WM_HOTKEY lặp lại.
        if (RegisterHotKey(hwnd, kHotkeyId, kCombos[i].modifiers | MOD_NOREPEAT, kCombos[i].key) != 0)
        {
            g_activeCombo = i;
            g_activeWindow = hwnd;
            return true;
        }
    }
    return false;
}

void unregisterFor(WId windowId)
{
    const HWND hwnd = reinterpret_cast<HWND>(windowId);
    UnregisterHotKey(hwnd, kHotkeyId);
    if (hwnd == g_activeWindow)
    {
        g_activeCombo = -1;
        g_activeWindow = nullptr;
    }
}

bool isStopMessage(const void* message)
{
    const auto* msg = static_cast<const MSG*>(message);
    return msg && msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == kHotkeyId;
}

} // namespace StopHotkey
