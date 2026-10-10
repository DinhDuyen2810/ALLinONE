#pragma once

#include <QString>

class QMenu;
class QAction;

/// Style + icon dùng chung cho context menu (chuột phải) trong toàn ứng dụng - trước đây mọi QMenu dùng
/// style mặc định của hệ điều hành (trắng/xám Windows, không bo góc), khác hẳn phong cách bo góc/màu
/// #0969da của phần còn lại ứng dụng, và không có icon nào để phân biệt mục nào bấm được (xác nhận qua
/// ảnh người dùng gửi, v1.19.7).
namespace ModernMenu
{
/// Gọi NGAY SAU khi tạo `QMenu` (trước khi addAction) - bo góc, viền, màu hover đồng bộ UI ứng dụng.
void style(QMenu* menu);

/// Thay cho `menu.addAction(text)` - tự thêm tiền tố 👆 (ngón tay trỏ) để phân biệt mục menu bấm được,
/// DÙNG CHUNG một icon duy nhất cho mọi mục thay vì mỗi hành động một icon ngữ nghĩa riêng (quyết định rõ
/// ràng của người dùng khi được hỏi, v1.19.7) - đồng bộ cho toàn bộ context menu trong ứng dụng.
QAction* addAction(QMenu& menu, const QString& text);
} // namespace ModernMenu
