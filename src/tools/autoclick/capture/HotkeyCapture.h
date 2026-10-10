#pragma once

#include <QObject>
#include <QString>
#include <vector>

#include "../model/Action.h"

class QTimer;

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
    /// modOrder = các modifier ĐANG BẬT lúc phím chính được nhấn, xếp theo ĐÚNG THỨ TỰ LẦN ĐẦU chúng
    /// được bấm xuống (nhả ra rồi bấm lại KHÔNG dời vị trí trong danh sách) - trước đây chỉ trả 4 bool cố
    /// định thứ tự Ctrl-Alt-Shift-Win, không phản ánh thứ tự người dùng bấm thật (v1.19.7).
    /// vkCode = mã phím Win32 (VK_*) của phím "chính" (không phải phím bổ trợ) đã bắt được.
    void hotkeyCaptured(const std::vector<ModifierKey>& modOrder, int vkCode);
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
    /// Thứ tự LẦN ĐẦU mỗi modifier được bấm xuống trong lượt bắt này (reset trong startCapture()) - nhả
    /// ra không xóa khỏi danh sách, chỉ 4 bool ở trên đổi theo trạng thái đang giữ/đã nhả.
    std::vector<ModifierKey> m_pressOrder;

    // Tự hủy bắt sau một khoảng thời gian nếu người dùng không gõ gì - hook bàn phím toàn cục này NUỐT
    // MỌI phím thật (kể cả của ứng dụng khác) trong lúc đang bắt, và có thể chặn luôn hook khác đã cài
    // trước đó trong cùng tiến trình (vd InputHook của Connect Together - Windows gọi hook mới cài sau
    // TRƯỚC hook cũ) nếu người dùng bấm "Bắt tổ hợp phím" rồi bỏ đó không gõ gì - không có cơ chế này
    // trước đây, hook có thể treo VÔ THỜI HẠN.
    QTimer* m_timeoutTimer{nullptr};
};
