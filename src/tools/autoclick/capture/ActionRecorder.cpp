#include "ActionRecorder.h"
#include "../overlay/RuntimeOverlay.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace
{
ActionRecorder* g_instance = nullptr;

// Trần an toàn số sự kiện - KHÔNG phải timeout thời gian (ghi macro có thể kéo dài nhiều phút, không nên
// tự hủy như HotkeyCapture/DragGestureCapture). Chỉ để tránh phình bộ nhớ/JSON vô hạn nếu người dùng bấm
// "Bắt đầu ghi" rồi quên bấm Dừng.
constexpr size_t kEventCountSafetyLimit = 100000;

// Chỉ ghi lại MouseMove mới nếu cách lần ghi trước ≥ khoảng này (ms) - WM_MOUSEMOVE qua hook bắn rất dày,
// ghi mọi điểm sẽ phình dữ liệu vô ích mà không tăng độ mượt khi phát lại. MouseDown/Up/Wheel/KeyDown/Up
// LUÔN ghi đầy đủ, không giảm tải.
constexpr qint64 kMoveThrottleMs = 20;

LRESULT CALLBACK mouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_instance)
    {
        const auto* data = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        // CHỦ Ý không lọc theo thiết bị/nguồn khác ngoài LLMHF_INJECTED - không được phép chặn thao tác
        // thật của người dùng, hàm này KHÔNG BAO GIỜ trả về khác CallNextHookEx.
        if (!(data->flags & LLMHF_INJECTED))
        {
            short wheelDelta = 0;
            if (wParam == WM_MOUSEWHEEL || wParam == WM_MOUSEHWHEEL)
                wheelDelta = static_cast<short>(HIWORD(data->mouseData));
            g_instance->onRawMouse(static_cast<int>(wParam), data->pt.x, data->pt.y, wheelDelta);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK keyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && g_instance)
    {
        const auto* data = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        // Esc KHÔNG bị nuốt ở đây (khác HotkeyCapture/DragGestureCapture) - là phím hợp lệ người dùng có
        // thể cần ghi lại. Hàm này KHÔNG BAO GIỜ trả về khác CallNextHookEx.
        if (!(data->flags & LLKHF_INJECTED))
        {
            const bool pressed = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            g_instance->onRawKey(static_cast<int>(data->vkCode), pressed);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
} // namespace

ActionRecorder::ActionRecorder(QObject* parent)
    : QObject(parent)
{
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(200);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        emit progressUpdated(static_cast<int>(m_events.size()), m_elapsed.isValid() ? m_elapsed.elapsed() : 0);
    });
}

ActionRecorder::~ActionRecorder()
{
    uninstallHook();
}

bool ActionRecorder::start(RuntimeOverlay* excludedOverlay)
{
    if (m_mouseHook)
        return true; // đã đang ghi

    m_excludedOverlay = excludedOverlay;
    m_events.clear();
    m_keysDown.clear();
    m_buttonsDown.clear();
    m_lastEventMs = 0;
    m_lastMoveThrottleMs = -1000;
    m_lastKnownX = 0;
    m_lastKnownY = 0;
    m_elapsed.start();

    g_instance = this;
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, mouseProc, GetModuleHandleW(nullptr), 0);
    if (!m_mouseHook)
    {
        g_instance = nullptr;
        return false;
    }
    // Không cài được hook bàn phím (hiếm) vẫn tiếp tục ghi CHUỘT - còn hơn không ghi gì cả.
    m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, keyboardProc, GetModuleHandleW(nullptr), 0);

    m_progressTimer->start();
    return true;
}

void ActionRecorder::stop()
{
    if (m_mouseHook)
        finish();
}

void ActionRecorder::finish()
{
    // Không để kẹt phím/nút chuột: với mọi phím/nút còn đang "down" mà chưa có "up" tương ứng (vd người
    // dùng bấm Dừng trong lúc đang giữ phím), tự chèn thêm sự kiện Up (delay 0) trước khi kết thúc - đúng
    // triết lý "luôn nhả phím" đã áp dụng cho Hotkey trong ActionRunner.
    for (int vk : m_keysDown)
        appendEvent(RecordedEventType::KeyUp, 0, 0, MouseButtonType::Left, 0, false, vk);
    for (MouseButtonType btn : m_buttonsDown)
        appendEvent(RecordedEventType::MouseUp, static_cast<int>(m_lastKnownX), static_cast<int>(m_lastKnownY), btn, 0, false, 0);
    m_keysDown.clear();
    m_buttonsDown.clear();

    uninstallHook();
    std::vector<RecordedEvent> events = std::move(m_events);
    m_events.clear();
    // Luôn phát QUA HÀNG ĐỢI: finish() còn được gọi TRỰC TIẾP từ trong hook callback khi chạm trần an
    // toàn số sự kiện (xem onRawMouse/onRawKey) - không được làm việc UI đồng bộ ngay trong hook (xem
    // DragGestureCapture.h), dù gọi từ nút Dừng (luôn an toàn) cũng để qua hàng đợi cho nhất quán.
    QMetaObject::invokeMethod(
        this, [this, events]() mutable { emit recordingFinished(std::move(events)); }, Qt::QueuedConnection);
}

void ActionRecorder::uninstallHook()
{
    m_progressTimer->stop();
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

void ActionRecorder::appendEvent(RecordedEventType type, int x, int y, MouseButtonType button, int wheelDelta,
                                 bool wheelHorizontal, int keyCode)
{
    const qint64 now = m_elapsed.elapsed();
    RecordedEvent ev;
    ev.type = type;
    ev.delayMs = now - m_lastEventMs;
    ev.x = x;
    ev.y = y;
    ev.button = button;
    ev.wheelDelta = wheelDelta;
    ev.wheelHorizontal = wheelHorizontal;
    ev.keyCode = keyCode;
    m_events.push_back(ev);
    m_lastEventMs = now;
}

bool ActionRecorder::isInsideExcludedOverlay(int physicalX, int physicalY) const
{
    if (!m_excludedOverlay)
        return false;
    // Quy đổi tọa độ vật lý (Win32) sang logic (Qt) theo DPR màn hình chính - cùng cách đơn giản hóa
    // RuntimeOverlay::updateOverlayPosition() đã dùng (geometry() của overlay, cửa sổ top-level không
    // cha, đã là tọa độ màn hình LOGIC). Nới thêm biên nhỏ phòng sai số quy đổi DPI ở rìa overlay.
    QScreen* primary = QGuiApplication::primaryScreen();
    const qreal dpr = primary ? primary->devicePixelRatio() : 1.0;
    const int logicalX = static_cast<int>(physicalX / dpr);
    const int logicalY = static_cast<int>(physicalY / dpr);
    return m_excludedOverlay->geometry().adjusted(-8, -8, 8, 8).contains(logicalX, logicalY);
}

void ActionRecorder::onRawMouse(int message, long x, long y, short wheelDelta)
{
    if (isInsideExcludedOverlay(static_cast<int>(x), static_cast<int>(y)))
        return; // không ghi - sự kiện vẫn đi tiếp bình thường (callback luôn CallNextHookEx)

    m_lastKnownX = x;
    m_lastKnownY = y;

    switch (message)
    {
        case WM_MOUSEMOVE:
        {
            const qint64 now = m_elapsed.elapsed();
            if (now - m_lastMoveThrottleMs < kMoveThrottleMs)
                return;
            m_lastMoveThrottleMs = now;
            appendEvent(RecordedEventType::MouseMove, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Left, 0, false, 0);
            break;
        }
        case WM_LBUTTONDOWN:
            m_buttonsDown.insert(MouseButtonType::Left);
            appendEvent(RecordedEventType::MouseDown, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Left, 0, false, 0);
            break;
        case WM_LBUTTONUP:
            m_buttonsDown.erase(MouseButtonType::Left);
            appendEvent(RecordedEventType::MouseUp, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Left, 0, false, 0);
            break;
        case WM_RBUTTONDOWN:
            m_buttonsDown.insert(MouseButtonType::Right);
            appendEvent(RecordedEventType::MouseDown, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Right, 0, false, 0);
            break;
        case WM_RBUTTONUP:
            m_buttonsDown.erase(MouseButtonType::Right);
            appendEvent(RecordedEventType::MouseUp, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Right, 0, false, 0);
            break;
        case WM_MBUTTONDOWN:
            m_buttonsDown.insert(MouseButtonType::Middle);
            appendEvent(RecordedEventType::MouseDown, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Middle, 0, false, 0);
            break;
        case WM_MBUTTONUP:
            m_buttonsDown.erase(MouseButtonType::Middle);
            appendEvent(RecordedEventType::MouseUp, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Middle, 0, false, 0);
            break;
        case WM_MOUSEWHEEL:
            appendEvent(RecordedEventType::MouseWheel, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Left, wheelDelta, false, 0);
            break;
        case WM_MOUSEHWHEEL:
            appendEvent(RecordedEventType::MouseWheel, static_cast<int>(x), static_cast<int>(y), MouseButtonType::Left, wheelDelta, true, 0);
            break;
        default:
            return; // không phải sự kiện cần ghi - không chạm tới kiểm tra trần an toàn bên dưới
    }

    if (m_events.size() >= kEventCountSafetyLimit)
        finish();
}

void ActionRecorder::onRawKey(int vkCode, bool pressed)
{
    if (pressed)
        m_keysDown.insert(vkCode);
    else
        m_keysDown.erase(vkCode);
    appendEvent(pressed ? RecordedEventType::KeyDown : RecordedEventType::KeyUp, 0, 0, MouseButtonType::Left, 0, false, vkCode);

    if (m_events.size() >= kEventCountSafetyLimit)
        finish();
}
