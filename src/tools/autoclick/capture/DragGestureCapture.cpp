#include "DragGestureCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QTimer>

namespace
{
DragGestureCapture* g_instance = nullptr;

// 30 giây - đủ để người dùng chuyển sang cửa sổ đích rồi kéo, đủ ngắn để hook chuột toàn cục không treo
// mãi nếu người dùng bấm "Bắt thao tác kéo thật" rồi bỏ đó.
constexpr int kCaptureTimeoutMs = 30000;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_instance)
    {
        const auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        // CHỦ ĐÍCH không lọc LLMHF_INJECTED ở đây như InputHook: nếu người dùng dùng một công cụ khác
        // mô phỏng chuột (hiếm), vẫn muốn ghi lại tọa độ thật đã xảy ra. Quan trọng hơn: hàm này KHÔNG
        // BAO GIỜ trả về khác CallNextHookEx - không được phép chặn thao tác thật của người dùng.
        g_instance->onRawMouse(static_cast<int>(wParam), data->pt.x, data->pt.y);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_instance)
    {
        const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        // Chỉ nuốt đúng phím Esc (phím hủy) để nó không lọt xuống ứng dụng đang có focus phía dưới - mọi
        // phím khác đi qua nguyên vẹn, khác HotkeyCapture (nuốt tất cả).
        if (!(data->flags & LLKHF_INJECTED) && g_instance->onRawKey(static_cast<int>(data->vkCode), pressed))
            return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

DragGestureCapture::DragGestureCapture(QObject* parent)
    : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kCaptureTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, &DragGestureCapture::cancelCapture);
}

DragGestureCapture::~DragGestureCapture()
{
    uninstallHook();
}

bool DragGestureCapture::startCapture()
{
    if (m_mouseHook)
        return true; // đã đang bắt
    m_pressed = false;
    g_instance = this;
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (!m_mouseHook)
    {
        g_instance = nullptr;
        return false;
    }

    // Hook bàn phím chỉ để nhận Esc - không cài được thì vẫn bắt tiếp (còn lối thoát bằng hết giờ).
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);
    m_timeoutTimer->start();
    return true;
}

void DragGestureCapture::cancelCapture()
{
    if (!m_mouseHook)
        return;
    uninstallHook();
    // Phát QUA HÀNG ĐỢI sự kiện: hàm này có thể đang chạy BÊN TRONG callback hook bàn phím (Esc) - nơi
    // nhận sẽ hiện lại cửa sổ chính, không nên làm việc giao diện nặng khi Windows còn đang chờ callback
    // trả về (quá thời hạn LowLevelHooksTimeout thì chính sự kiện bàn phím đó bị trễ theo).
    QMetaObject::invokeMethod(this, &DragGestureCapture::captureCancelled, Qt::QueuedConnection);
}

void DragGestureCapture::uninstallHook()
{
    if (m_timeoutTimer)
        m_timeoutTimer->stop();
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
}

bool DragGestureCapture::onRawKey(int vkCode, bool pressed)
{
    if (vkCode != VK_ESCAPE)
        return false;
    if (pressed)
        cancelCapture();
    return true;
}

void DragGestureCapture::onRawMouse(int message, long x, long y)
{
    if (message == WM_LBUTTONDOWN)
    {
        m_pressed = true;
        m_startX = x;
        m_startY = y;
    }
    else if (message == WM_LBUTTONUP && m_pressed)
    {
        m_pressed = false;
        const int startX = static_cast<int>(m_startX);
        const int startY = static_cast<int>(m_startY);
        const int endX = static_cast<int>(x);
        const int endY = static_cast<int>(y);
        uninstallHook();
        // Phát QUA HÀNG ĐỢI sự kiện, không phát thẳng từ trong callback hook: nơi nhận hiện lại + kích
        // hoạt cửa sổ Auto Click, mà lúc này sự kiện NHẢ CHUỘT thật còn chưa tới ứng dụng đích (Windows
        // chỉ chuyển tiếp sau khi callback trả về) - phải để thao tác kéo thật kết thúc trọn vẹn ở ứng
        // dụng đích trước rồi mới đưa cửa sổ của ta lên trên.
        QMetaObject::invokeMethod(
            this, [this, startX, startY, endX, endY]() { emit dragCaptured(startX, startY, endX, endY); },
            Qt::QueuedConnection);
    }
}
