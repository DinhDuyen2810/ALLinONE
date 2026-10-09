#include "Action.h"
#include <QStringList>

QString Action::typeName() const
{
    switch (type)
    {
        case ActionType::MouseClick: return "Click chuột";
        case ActionType::MouseDrag:  return "Kéo chuột";
        case ActionType::MouseHold:  return "Giữ chuột";
        case ActionType::TypeText:   return "Gõ văn bản";
        case ActionType::Hotkey:     return "Phím tắt";
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
            QStringList mods;
            if (modCtrl)  mods << "Ctrl";
            if (modAlt)   mods << "Alt";
            if (modShift) mods << "Shift";
            if (modWin)   mods << "Win";
            mods << QString::fromStdString(keyName.empty() ? "Phím" : keyName);
            return QString("Phím tắt %1").arg(mods.join(" + "));
        }

        case ActionType::KeyPress:
            return QString("Nhấn %1").arg(QString::fromStdString(keyName.empty() ? "Phím" : keyName));

        case ActionType::Scroll:
        {
            QString dir;
            switch (scrollDirection) {
                case ScrollDirection::Down:  dir = "xuống"; break;
                case ScrollDirection::Up:    dir = "lên"; break;
                case ScrollDirection::Left:  dir = "trái"; break;
                case ScrollDirection::Right: dir = "phải"; break;
            }
            return QString("Cuộn %1, %2 bước").arg(dir).arg(scrollAmount);
        }
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
