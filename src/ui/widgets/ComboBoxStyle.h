#pragma once

#include <QString>

/// CSS dùng chung để ẩn icon mũi tên xổ xuống của QComboBox trên toàn ứng dụng - `QComboBox::drop-down
/// { border: none; }` (đã dùng ở vài nơi trước đây) KHÔNG đủ, icon mũi tên là sub-control `down-arrow`
/// riêng và Qt vẫn tự vẽ nó. Nối `noArrow()` vào CUỐI stylesheet hiện có (toán tử +) - không đổi màu/
/// viền/bo góc đang có, chỉ ẩn icon. Bấm vào bất kỳ đâu trên thân combo vẫn mở được danh sách như cũ, vì
/// hành vi click mặc định của QComboBox không phụ thuộc icon mũi tên (yêu cầu người dùng, v1.19.8, áp
/// dụng cho toàn bộ tiện ích).
namespace ComboBoxStyle
{
inline QString noArrow()
{
    return "QComboBox::drop-down { border: none; width: 0px; }"
           "QComboBox::down-arrow { image: none; width: 0px; height: 0px; }";
}
} // namespace ComboBoxStyle
