#pragma once

#include <QObject>
#include <atomic>

/**
 * @brief Lớp duy nhất cài đặt hook bàn phím/chuột toàn cục Windows (WH_MOUSE_LL/WH_KEYBOARD_LL).
 * Mọi kiểu Win32 (LRESULT/WPARAM/HOOKPROC...) chỉ nằm trong file .cpp - header này không lộ ra ngoài,
 * theo đúng nguyên tắc cô lập Win32 của dự án (xem WlanController dùng void* thay vì HANDLE).
 *
 * QUAN TRỌNG VỀ THIẾT KẾ AN TOÀN: hook luôn ở trạng thái "cài đặt nhưng không chặn gì" (setActive(false))
 * trừ khi người dùng VẬT LÝ di chuột chạm biên màn hình sang phía một máy đã ghép đôi (do
 * ConnectSessionController/EdgeDetector phát hiện, xem mục 4e trong PROJECT_OVERVIEW.md) - tức là một
 * hành động rõ ràng, hữu hình của chính người dùng, không có cách nào kích hoạt từ xa hay âm thầm.
 * Khi active=true: ConnectWindow luôn hiển thị "Đang điều khiển <tên máy>", không có chế độ ẩn.
 * Phím nóng Ctrl+Alt+Home luôn trả quyền điều khiển về máy này ngay lập tức.
 *
 * Chỉ nhận sự kiện bàn phím/chuột THẬT từ phần cứng (bỏ qua sự kiện do chính ứng dụng này tạo ra qua
 * InputInjector, nhận diện qua cờ LLMHF_INJECTED/LLKHF_INJECTED của Windows) để tránh vòng lặp vô hạn.
 */
class InputHook : public QObject
{
    Q_OBJECT

public:
    explicit InputHook(QObject* parent = nullptr);
    ~InputHook() override;

    bool install(QString* error = nullptr);
    void uninstall();
    bool isInstalled() const;

    /// true = đang chia sẻ điều khiển: chặn input thật khỏi máy này (không gửi tới ứng dụng cục bộ nào)
    /// và phát tín hiệu tương ứng để ConnectSessionController chuyển tiếp sang máy đang điều khiển.
    void setActive(bool active) { m_active = active; }
    bool isActive() const { return m_active; }

signals:
    void mouseMoveRelative(int dx, int dy);
    void mouseButtonChanged(int button, bool pressed); // 0=Left,1=Right,2=Middle,3=X1,4=X2
    void mouseWheelMoved(int deltaY, int deltaX);
    void keyChanged(int vkCode, int scanCode, bool pressed, bool extended);
    void returnHotkeyPressed(); // Ctrl+Alt+Home - luôn phát ra, bất kể active hay không

public:
    // Gọi từ hàm callback Win32 tĩnh trong .cpp khi có sự kiện thật (đã lọc injected); trả về true
    // nếu đã "nuốt" sự kiện (đang active, không cho lan tới hệ thống/ứng dụng khác). Không dành cho
    // code khác trong ứng dụng gọi - chỉ public vì callback Win32 phải là hàm tự do, không phải method.
    bool onRawMouseMove(long x, long y);
    bool onRawMouseButton(int button, bool pressed);
    bool onRawMouseWheel(int deltaY, int deltaX);
    bool onRawKey(int vkCode, int scanCode, bool pressed, bool extended);

private:
    void* m_mouseHook{nullptr};
    void* m_keyboardHook{nullptr};
    std::atomic_bool m_active{false};
    bool m_hasLastPoint{false};
    long m_lastX{0};
    long m_lastY{0};
    bool m_ctrlDown{false};
    bool m_altDown{false};
};
