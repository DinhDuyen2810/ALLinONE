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
        {
            // Chỉ hỏi vị trí con trỏ thật khi đang chia sẻ điều khiển (không tốn thêm lời gọi hệ thống cho
            // mọi cử động chuột lúc bình thường). GetCursorPos thất bại (hiếm - vd đang ở desktop bảo mật)
            // thì coi như độ dịch 0: thà mất một cử động còn hơn gửi sang máy kia một bước nhảy sai.
            POINT cur = data->pt;
            if (g_instance->isActive())
                GetCursorPos(&cur);
            consumed = g_instance->onRawMouseMove(data->pt.x, data->pt.y, cur.x, cur.y);
            break;
        }
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
    return true;
}

void InputHook::uninstall()
{
    // Xóa g_instance TRƯỚC khi gỡ hook (không phải sau): Windows có thể đã xếp hàng một lệnh gọi
    // mouseProc/keyboardProc cho sự kiện chuột/phím thật xảy ra đúng lúc đang gỡ (race có thật - từng
    // gây crash ngẫu nhiên trong test khi có hoạt động chuột/phím thật trên máy lúc test chạy). Xóa
    // g_instance trước đảm bảo callback tĩnh thấy g_instance=nullptr ngay và chỉ gọi CallNextHookEx,
    // không bao giờ dereference một InputHook sắp/đang bị hủy.
    if (g_instance == this)
        g_instance = nullptr;

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
    m_active = false;

    // Không còn hook thì không còn thấy lần nhả nào nữa - xóa sạch sổ theo dõi để lần cài lại không bắt
    // đầu với trạng thái phím/nút "đang giữ" đã cũ.
    m_physKeys.reset();
    m_preHeldKeys.reset();
    m_swallowedKeys.reset();
    m_swallowedButtons = 0;
    m_ctrlDown = false;
    m_altDown = false;
    m_hotkeyHomeSwallowed = false;
}

bool InputHook::isInstalled() const
{
    return m_mouseHook != nullptr && m_keyboardHook != nullptr;
}

void InputHook::setActive(bool active)
{
    if (active && !m_active)
    {
        // Sổ m_physKeys dựng từ các sự kiện hook đã thấy - có thể còn sót phím mà lần NHẢ không bao giờ
        // tới hook (vd Win+L: nhả phím lúc đã ở màn hình khóa, nơi hook cấp thấp không chạy). Đối chiếu với
        // trạng thái thật của hệ điều hành để bỏ các mục đã cũ; nếu không, lần gõ kế tiếp của đúng phím đó
        // trong lúc điều khiển máy kia sẽ bị coi nhầm là "phím giữ sẵn" và rơi xuống máy này. Chỉ đối chiếu
        // khi hook thật đang cài (bộ kiểm thử gọi thẳng onRaw*() không có trạng thái hệ điều hành tương ứng).
        if (isInstalled())
        {
            for (int vk = 1; vk < 256; ++vk)
                if (m_physKeys.test(static_cast<size_t>(vk)) && !(GetAsyncKeyState(vk) & 0x8000))
                    m_physKeys.reset(static_cast<size_t>(vk));
        }
        m_preHeldKeys = m_physKeys;
    }
    else if (!active)
    {
        m_preHeldKeys.reset();
    }
    m_active = active;
}

bool InputHook::onRawMouseMove(long x, long y, long cursorX, long cursorY)
{
    if (!m_active)
        return false;

    const int dx = static_cast<int>(x - cursorX);
    const int dy = static_cast<int>(y - cursorY);
    if (dx != 0 || dy != 0)
        emit mouseMoveRelative(dx, dy);
    return true;
}

bool InputHook::onRawMouseButton(int button, bool pressed)
{
    if (button < 0 || button > 4)
        return false;
    const unsigned bit = 1u << button;

    if (pressed)
    {
        if (!m_active)
            return false;
        m_swallowedButtons |= bit;
        emit mouseButtonChanged(button, true);
        return true;
    }

    // Lần nhả: chỉ nuốt nếu lần NHẤN tương ứng đã bị nuốt. Nút được nhấn trước khi chia sẻ điều khiển thì
    // máy này đã thấy lần nhấn - phải cho nó thấy cả lần nhả, nếu không ứng dụng cục bộ kẹt ở trạng thái
    // "đang giữ nút" (vd đang kéo một cửa sổ).
    if (!(m_swallowedButtons & bit))
        return false;
    m_swallowedButtons &= ~bit;
    if (m_active) // đã trả quyền điều khiển thì ConnectSessionController đã tự gửi lệnh nhả sang máy kia
        emit mouseButtonChanged(button, false);
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

    if (vkCode == VK_HOME)
    {
        if (pressed && m_ctrlDown && m_altDown)
        {
            // Chốt trạng thái active TRƯỚC khi phát tín hiệu: nơi nhận (ConnectSessionController) tắt active
            // ngay trong lúc xử lý, nếu đọc lại m_active sau đó thì phím Home của tổ hợp lọt xuống ứng dụng
            // cục bộ.
            const bool wasActive = m_active;
            m_hotkeyHomeSwallowed = wasActive;
            emit returnHotkeyPressed();
            return wasActive; // nuốt tổ hợp phím nóng khi đang active (không gửi Home xuống máy đang điều khiển)
        }
        if (!pressed && m_hotkeyHomeSwallowed)
        {
            m_hotkeyHomeSwallowed = false;
            return true; // lần nhả của đúng phím Home vừa nuốt - không lọt xuống đâu cả
        }
    }

    const bool trackable = vkCode > 0 && vkCode < 256;
    const size_t idx = trackable ? static_cast<size_t>(vkCode) : 0;

    if (pressed)
    {
        if (!m_active)
        {
            if (trackable)
                m_physKeys.set(idx);
            return false;
        }
        // Phím đã giữ sẵn từ trước khi chia sẻ điều khiển (lần nhấn lặp do giữ phím): vẫn thuộc về máy này.
        if (trackable && m_preHeldKeys.test(idx))
            return false;
        if (trackable)
            m_swallowedKeys.set(idx);
        emit keyChanged(vkCode, scanCode, true, extended);
        return true;
    }

    if (trackable)
    {
        m_physKeys.reset(idx);
        if (m_preHeldKeys.test(idx))
        {
            m_preHeldKeys.reset(idx);
            return false; // nhả phím đã giữ từ trước - cho máy này thấy để không kẹt phím cục bộ
        }
        if (!m_swallowedKeys.test(idx))
            return false; // lần nhấn không bị nuốt (vd nhấn ngay trước khi kích hoạt) -> lần nhả cũng đi qua
        m_swallowedKeys.reset(idx);
        if (m_active) // đã trả quyền điều khiển thì ConnectSessionController đã tự gửi lệnh nhả sang máy kia
            emit keyChanged(vkCode, scanCode, false, extended);
        return true;
    }

    if (!m_active)
        return false;
    emit keyChanged(vkCode, scanCode, false, extended);
    return true;
}
