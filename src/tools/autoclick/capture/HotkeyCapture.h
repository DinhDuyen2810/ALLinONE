#pragma once

#include <QObject>
#include <QString>

/**
 * @brief Bắt một tổ hợp phím THẬT (vd Ctrl+Shift+S) qua hook bàn phím toàn cục tạm thời
 * (WH_KEYBOARD_LL) - cùng kiểu cơ chế đã dùng cho InputHook (Connect Together), chỉ cài đặt trong lúc
 * đang "chờ bắt" rồi tự gỡ ngay sau khi bắt được hoặc bị hủy (Esc), không chạy nền thường trực.
 *
 * KHÔNG cần ẩn cửa sổ ứng dụng như MouseCapture: hook bàn phím hoạt động TOÀN HỆ THỐNG bất kể cửa sổ
 * nào đang có focus, không cần "nhìn thấy" màn hình thật phía sau như lúc bắt tọa độ chuột. Mọi phím
 * nhấn trong lúc đang bắt đều bị NUỐT (không cho lan sang ứng dụng khác) để tránh vô tình kích hoạt
 * phím tắt thật của một ứng dụng khác đang có focus trong lúc người dùng chỉ đang ĐỊNH NGHĨA tổ hợp.
 */
class HotkeyCapture : public QObject
{
    Q_OBJECT

public:
    explicit HotkeyCapture(QObject* parent = nullptr);
    ~HotkeyCapture() override;

    void startCapture();
    void cancelCapture();
    bool isCapturing() const { return m_keyboardHook != nullptr; }

signals:
    /// vkCode = mã phím Win32 (VK_*) của phím "chính" (không phải phím bổ trợ) đã bắt được.
    void hotkeyCaptured(bool ctrl, bool alt, bool shift, bool win, int vkCode);
    void captureCancelled();

public:
    // Chỉ dành cho hàm callback Win32 tĩnh trong .cpp gọi (xem InputHook.h cho quy ước tương tự) - trả
    // về true nếu đã "nuốt" sự kiện.
    bool onRawKey(int vkCode, bool pressed);

private:
    void uninstallHook();

    void* m_keyboardHook{nullptr};
    bool m_ctrlDown{false};
    bool m_altDown{false};
    bool m_shiftDown{false};
    bool m_winDown{false};
};
