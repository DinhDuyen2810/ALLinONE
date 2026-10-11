#pragma once

#include <QComboBox>
#include <QString>
#include <QWidget>

/// CSS + con trỏ dùng chung cho QComboBox trên toàn ứng dụng.
namespace ComboBoxStyle
{
/// Ẩn icon mũi tên xổ xuống - `QComboBox::drop-down { border: none; }` (đã dùng ở vài nơi trước đây)
/// KHÔNG đủ, icon mũi tên là sub-control `down-arrow` riêng và Qt vẫn tự vẽ nó. Nối vào CUỐI stylesheet
/// hiện có (toán tử +) - không đổi màu/viền/bo góc đang có, chỉ ẩn icon. Bấm vào bất kỳ đâu trên thân
/// combo vẫn mở được danh sách như cũ, vì hành vi click mặc định của QComboBox không phụ thuộc icon mũi
/// tên (yêu cầu người dùng, v1.19.8, áp dụng cho toàn bộ tiện ích).
inline QString noArrow()
{
    return "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; border: none; width: 0px; }"
           "QComboBox::down-arrow { image: none; width: 0px; height: 0px; }";
}

/// Gọi SAU KHI toàn bộ widget con đã dựng xong (vd cuối constructor của cửa sổ/tab/hộp thoại) - tự tìm
/// MỌI QComboBox hậu duệ (qua QObject parent-child, không cần widget cha trực tiếp) và đổi con trỏ thành
/// ngón tay trỏ khi di chuột qua, vì combo là nơi "bấm ra danh sách" (yêu cầu người dùng, v1.19.10). Chỉ
/// cần gọi MỘT LẦN ở cấp cao nhất đã dựng xong mọi combo con tại thời điểm gọi - combo tạo SAU đó (vd
/// trong một hộp thoại mở theo yêu cầu) phải tự gọi lại ở hộp thoại của chính nó.
inline void applyCursor(QWidget* root)
{
    if (!root)
        return;
    const auto combos = root->findChildren<QComboBox*>();
    for (QComboBox* combo : combos)
        combo->setCursor(Qt::PointingHandCursor);
}
} // namespace ComboBoxStyle
