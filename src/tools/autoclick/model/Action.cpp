#include "Action.h"
#include <QStringList>
#include <algorithm>

namespace
{
QString modifierLabel(ModifierKey m)
{
    switch (m)
    {
        case ModifierKey::Ctrl:  return "Ctrl";
        case ModifierKey::Alt:   return "Alt";
        case ModifierKey::Shift: return "Shift";
        case ModifierKey::Win:   return "Win";
    }
    return QString();
}

QString scrollDirectionLabel(ScrollDirection dir)
{
    switch (dir)
    {
        case ScrollDirection::Down:  return "xuống";
        case ScrollDirection::Up:    return "lên";
        case ScrollDirection::Left:  return "trái";
        case ScrollDirection::Right: return "phải";
    }
    return QString();
}
} // namespace

void Action::setModifiers(const std::vector<ModifierKey>& order)
{
    modOrder = order;
    auto has = [&order](ModifierKey k) { return std::find(order.begin(), order.end(), k) != order.end(); };
    modCtrl = has(ModifierKey::Ctrl);
    modAlt = has(ModifierKey::Alt);
    modShift = has(ModifierKey::Shift);
    modWin = has(ModifierKey::Win);
}

std::vector<ModifierKey> Action::effectiveModOrder() const
{
    if (!modOrder.empty())
        return modOrder;
    // modOrder rỗng = hồ sơ cũ (trước v1.19.7) hoặc Action dựng tay chỉ gán 4 bool - suy luận lại ĐÚNG
    // thứ tự cố định Ctrl→Alt→Shift→Win đã dùng trước đây, giữ hành vi y hệt.
    std::vector<ModifierKey> order;
    if (modCtrl) order.push_back(ModifierKey::Ctrl);
    if (modAlt) order.push_back(ModifierKey::Alt);
    if (modShift) order.push_back(ModifierKey::Shift);
    if (modWin) order.push_back(ModifierKey::Win);
    return order;
}

QString Action::typeName() const
{
    switch (type)
    {
        case ActionType::MouseClick: return "Click chuột";
        case ActionType::MouseDrag:  return "Kéo chuột";
        case ActionType::MouseHold:  return "Giữ chuột";
        case ActionType::TypeText:   return "Gõ văn bản";
        case ActionType::Hotkey:     return "Tổ hợp phím";
        case ActionType::KeyPress:   return "Nhấn phím";
        case ActionType::Scroll:     return "Cuộn";
    }
    return "Không rõ";
}

QString Action::description() const
{
    // Chỉ dùng để HIỂN THỊ trong cột "Mô tả" - độc lập hoàn toàn với actionTypeToString() của
    // ActionSerializer (dùng khóa PascalCase riêng để lưu JSON), nên đổi chữ ở đây không ảnh hưởng gì
    // tới các hồ sơ đã lưu trước đó.
    auto btnStr = [](MouseButtonType btn) -> QString {
        switch (btn) {
            case MouseButtonType::Left:   return "Trái";
            case MouseButtonType::Right:  return "Phải";
            case MouseButtonType::Middle: return "Giữa";
        }
        return "Trái";
    };

    switch (type)
    {
        case ActionType::MouseClick:
            return QString("Click %1 tại (%2, %3)").arg(btnStr(mouseButton)).arg(x).arg(y);

        case ActionType::MouseDrag:
            return QString("Kéo (%1, %2) → (%3, %4) [%5ms]").arg(startX).arg(startY).arg(endX).arg(endY).arg(duration.count());

        case ActionType::MouseHold:
            return QString("Giữ %1 tại (%2, %3) trong %4ms").arg(btnStr(mouseButton)).arg(x).arg(y).arg(duration.count());

        case ActionType::TypeText:
            // arg() MỘT lần với cả hai đối số: gọi nối tiếp .arg(text).arg(...) thì "%1"/"%2" nằm trong chính
            // văn bản người dùng (vd chuỗi mã hóa URL "a%2Fb") bị lần arg() thứ hai thay nhầm bằng tên chế độ.
            return QString("Gõ \"%1\" (%2)")
                .arg(QString::fromStdString(text), QString(textMode == TextTypeMode::Instant ? "tức thì" : "từng ký tự"));

        case ActionType::Hotkey:
        {
            // Thứ tự THẬT người dùng đã bấm chọn (effectiveModOrder) - trước đây in cố định
            // Ctrl→Alt→Shift→Win bất kể thứ tự thật, không đúng với tổ hợp đã bắt/chọn (v1.19.7).
            QStringList mods;
            for (ModifierKey m : effectiveModOrder())
                mods << modifierLabel(m);

            QString tail;
            switch (hotkeyTrigger)
            {
                case HotkeyTrigger::KeyPress:
                    tail = QString::fromStdString(keyName.empty() ? "Phím" : keyName);
                    break;
                case HotkeyTrigger::Scroll:
                    tail = QString("Cuộn %1 (%2 bước)").arg(scrollDirectionLabel(scrollDirection)).arg(scrollAmount);
                    break;
                case HotkeyTrigger::Click:
                    tail = QString("Click %1 tại (%2, %3)").arg(btnStr(mouseButton)).arg(x).arg(y);
                    break;
            }
            mods << tail;
            return QString("Tổ hợp phím %1").arg(mods.join(" + "));
        }

        case ActionType::KeyPress:
            return QString("Nhấn %1").arg(QString::fromStdString(keyName.empty() ? "Phím" : keyName));

        case ActionType::Scroll:
            return QString("Cuộn %1, %2 bước").arg(scrollDirectionLabel(scrollDirection)).arg(scrollAmount);
    }
    return "Hành động";
}

QString Action::logDescription() const
{
    if (type != ActionType::TypeText)
        return description();

    return QString("Gõ văn bản (%1 ký tự, %2)")
        .arg(QString::fromStdString(text).size())
        .arg(textMode == TextTypeMode::Instant ? "tức thì" : "từng ký tự");
}
