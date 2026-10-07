#include "DragGestureCapture.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
DragGestureCapture* g_instance = nullptr;

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
} // namespace

DragGestureCapture::DragGestureCapture(QObject* parent)
    : QObject(parent)
{
}

DragGestureCapture::~DragGestureCapture()
{
    uninstallHook();
}

void DragGestureCapture::startCapture()
{
    if (m_mouseHook)
        return;
    m_pressed = false;
    g_instance = this;
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (!m_mouseHook)
        g_instance = nullptr;
}

void DragGestureCapture::cancelCapture()
{
    uninstallHook();
}

void DragGestureCapture::uninstallHook()
{
    if (m_mouseHook)
    {
        UnhookWindowsHookEx(static_cast<HHOOK>(m_mouseHook));
        m_mouseHook = nullptr;
    }
    if (g_instance == this)
        g_instance = nullptr;
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
        emit dragCaptured(startX, startY, endX, endY);
    }
}
