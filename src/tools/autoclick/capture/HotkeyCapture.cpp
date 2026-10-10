#include "HotkeyCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QTimer>
#include <algorithm>

namespace
{
// SetWindowsHookEx yêu cầu con trỏ hàm thường (không phải con trỏ hàm thành viên) - chỉ chuyển tiếp sự
// kiện tới MỘT instance đang bắt tại một thời điểm, đúng với cách dùng thực tế (chỉ một nút "Bắt tổ hợp
// phím" có thể đang chờ tại một thời điểm trong ActionEditorWidget).
HotkeyCapture* g_instance = nullptr;

// 20 giây - đủ thời gian để người dùng bình tĩnh gõ tổ hợp mong muốn, đủ ngắn để không chặn hook bàn
// phím khác (vd InputHook của Connect Together) quá lâu nếu người dùng bấm "Bắt tổ hợp phím" rồi quên.
constexpr int kCaptureTimeoutMs = 20000;

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
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kCaptureTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, &HotkeyCapture::cancelCapture);
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
    m_pressOrder.clear();
    g_instance = this;
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!m_keyboardHook)
    {
        g_instance = nullptr;
        return;
    }
    m_timeoutTimer->start();
}

void HotkeyCapture::cancelCapture()
{
    if (!m_keyboardHook)
        return;
    uninstallHook();
    // Phát QUA HÀNG ĐỢI sự kiện (cả hotkeyCaptured bên dưới): hàm này có thể đang chạy BÊN TRONG callback
    // hook bàn phím (Esc) - nơi nhận cập nhật giao diện, không nên làm việc đó khi Windows còn đang chờ
    // callback trả về.
    QMetaObject::invokeMethod(this, &HotkeyCapture::captureCancelled, Qt::QueuedConnection);
}

void HotkeyCapture::uninstallHook()
{
    m_timeoutTimer->stop();
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
        const ModifierKey key = isCtrl ? ModifierKey::Ctrl : isAlt ? ModifierKey::Alt
                                                             : isShift ? ModifierKey::Shift : ModifierKey::Win;

        if (isCtrl) m_ctrlDown = pressed;
        else if (isAlt) m_altDown = pressed;
        else if (isShift) m_shiftDown = pressed;
        else if (isWin) m_winDown = pressed;

        // Ghi LẦN ĐẦU TIÊN modifier này được bấm xuống - nhả ra không xóa khỏi danh sách, bấm lại không
        // dời vị trí (giữ đúng ngữ nghĩa "thứ tự đã bấm", khác "đang giữ" của 4 bool ở trên).
        if (pressed && std::find(m_pressOrder.begin(), m_pressOrder.end(), key) == m_pressOrder.end())
            m_pressOrder.push_back(key);
        return true;
    }

    // Phím "chính" (không phải phím bổ trợ) - chỉ bắt lúc NHẤN XUỐNG, bỏ qua lúc nhả ra.
    if (pressed)
    {
        // Lọc m_pressOrder chỉ giữ modifier ĐANG BẬT lúc phím chính được nhấn (đúng rule cũ), nhưng xếp
        // theo đúng thứ tự LẦN ĐẦU chúng được bấm (khác thứ tự cố định Ctrl-Alt-Shift-Win trước đây).
        std::vector<ModifierKey> order;
        for (ModifierKey k : m_pressOrder)
        {
            const bool down = (k == ModifierKey::Ctrl && m_ctrlDown) || (k == ModifierKey::Alt && m_altDown) ||
                              (k == ModifierKey::Shift && m_shiftDown) || (k == ModifierKey::Win && m_winDown);
            if (down)
                order.push_back(k);
        }
        uninstallHook();
        QMetaObject::invokeMethod(
            this, [this, order, vkCode]() { emit hotkeyCaptured(order, vkCode); }, Qt::QueuedConnection);
    }
    return true;
}
