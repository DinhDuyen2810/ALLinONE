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
}

QAction* addAction(QMenu& menu, const QString& text)
{
    return menu.addAction(QString::fromUtf8("👆 ") + text);
}
} // namespace ModernMenu
