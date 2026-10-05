#include "Action.h"
#include <QStringList>

QString Action::typeName() const
{
    switch (type)
    {
        case ActionType::MouseClick: return "Mouse Click";
        case ActionType::MouseDrag:  return "Mouse Drag";
        case ActionType::MouseHold:  return "Mouse Hold";
        case ActionType::TypeText:   return "Type Text";
        case ActionType::Hotkey:     return "Hotkey";
        case ActionType::KeyPress:   return "Key Press";
        case ActionType::Scroll:     return "Scroll";
    }
    return "Unknown";
}

QString Action::description() const
{
    auto btnStr = [](MouseButtonType btn) -> QString {
        switch (btn) {
            case MouseButtonType::Left:   return "Left";
            case MouseButtonType::Right:  return "Right";
            case MouseButtonType::Middle: return "Middle";
        }
        return "Left";
    };

    switch (type)
    {
        case ActionType::MouseClick:
            return QString("Click %1 at (%2, %3)").arg(btnStr(mouseButton)).arg(x).arg(y);

        case ActionType::MouseDrag:
            return QString("Drag (%1, %2) -> (%3, %4) [%5ms]").arg(startX).arg(startY).arg(endX).arg(endY).arg(duration.count());

        case ActionType::MouseHold:
            return QString("Hold %1 at (%2, %3) for %4ms").arg(btnStr(mouseButton)).arg(x).arg(y).arg(duration.count());

        case ActionType::TypeText:
            return QString("Type \"%1\" (%2)").arg(QString::fromStdString(text)).arg(textMode == TextTypeMode::Instant ? "Instant" : "Char-by-char");

        case ActionType::Hotkey:
        {
            QStringList mods;
            if (modCtrl)  mods << "Ctrl";
            if (modAlt)   mods << "Alt";
            if (modShift) mods << "Shift";
            if (modWin)   mods << "Win";
            mods << QString::fromStdString(keyName.empty() ? "Key" : keyName);
            return QString("Hotkey %1").arg(mods.join(" + "));
        }

        case ActionType::KeyPress:
            return QString("Press %1").arg(QString::fromStdString(keyName.empty() ? "Key" : keyName));

        case ActionType::Scroll:
        {
            QString dir;
            switch (scrollDirection) {
                case ScrollDirection::Down:  dir = "Down"; break;
                case ScrollDirection::Up:    dir = "Up"; break;
                case ScrollDirection::Left:  dir = "Left"; break;
                case ScrollDirection::Right: dir = "Right"; break;
            }
            return QString("Scroll %1 by %2").arg(dir).arg(scrollAmount);
        }
    }
    return "Action";
}
