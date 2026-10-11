#include "ModernMenu.h"

#include <QMenu>

namespace ModernMenu
{
void style(QMenu* menu)
{
    if (!menu)
        return;
    menu->setStyleSheet(
        "QMenu { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px; padding: 6px; }"
        "QMenu::item { padding: 6px 14px 6px 10px; border-radius: 6px; color: #1f2328; font-size: 12px; }"
        "QMenu::item:selected { background-color: #0969da; color: #ffffff; }"
        "QMenu::item:disabled { color: #8c959f; }"
        "QMenu::separator { height: 1px; background: #eaeef2; margin: 6px 8px; }"
    );
    // Theo yêu cầu người dùng (v1.19.8): phân biệt mục bấm được bằng cách đổi CHÍNH CON TRỎ CHUỘT thành
    // ngón tay trỏ khi di qua menu, không phải icon vẽ trong từng dòng (thử ở v1.19.7, người dùng phản
    // hồi không đúng ý). QMenu tự vẽ các dòng bên trong (không phải widget con riêng) nên set cursor cho
    // cả menu là đủ để áp dụng với mọi mục.
    menu->setCursor(Qt::PointingHandCursor);
}

QAction* addAction(QMenu& menu, const QString& text)
{
    return menu.addAction(text);
}
} // namespace ModernMenu
