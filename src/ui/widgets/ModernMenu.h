#pragma once

#include <QString>

class QMenu;
class QAction;

/// Style + con trỏ dùng chung cho context menu (chuột phải) trong toàn ứng dụng - trước đây mọi QMenu
/// dùng style mặc định của hệ điều hành (trắng/xám Windows, không bo góc), khác hẳn phong cách bo góc/màu
/// #0969da của phần còn lại ứng dụng (xác nhận qua ảnh người dùng gửi, v1.19.7). Phân biệt mục bấm được
/// bằng cách đổi con trỏ chuột thành ngón tay trỏ khi di qua menu (v1.19.8) - thử icon 👆 vẽ trong từng
/// dòng ở v1.19.7 trước, người dùng phản hồi không đúng ý (muốn đổi chính con trỏ, không phải icon).
namespace ModernMenu
{
/// Gọi NGAY SAU khi tạo `QMenu` (trước khi addAction) - bo góc, viền, màu hover, con trỏ ngón tay.
void style(QMenu* menu);

/// Thay cho `menu.addAction(text)` - dùng cùng lúc với `style()` để mọi context menu trong ứng dụng nhất
/// quán nhau; bản thân hàm này hiện không thêm gì khác ngoài addAction thường, giữ lại để nơi gọi không
/// cần đổi nếu sau này cần thêm xử lý chung.
QAction* addAction(QMenu& menu, const QString& text);
} // namespace ModernMenu
