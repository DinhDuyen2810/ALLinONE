#include "ActionSerializer.h"
#include "core/AppPaths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <algorithm>
#include <climits>

QString ActionSerializer::getDefaultProfilePath()
{
    return AppPaths::profileFile("default.json");
}

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

/// Số thực từ JSON -> int trong [lo, hi]. static_cast<int>() một double ngoài miền int (vd 1e300 - JSON hợp
/// lệ) là hành vi KHÔNG XÁC ĐỊNH theo chuẩn C++; so sánh trên double TRƯỚC khi ép kiểu. Không phải số -> lo.
static int clampDoubleToInt(double v, int lo, int hi)
{
    if (!(v >= lo)) // gồm cả NaN
        return lo;
    if (v >= hi)
        return hi;
    return static_cast<int>(v);
}

static QString modifierKeyToString(ModifierKey m)
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

static bool stringToModifierKey(const QString& s, ModifierKey& out)
{
    if (s == "Ctrl") { out = ModifierKey::Ctrl; return true; }
    if (s == "Alt") { out = ModifierKey::Alt; return true; }
    if (s == "Shift") { out = ModifierKey::Shift; return true; }
    if (s == "Win") { out = ModifierKey::Win; return true; }
    return false;
}

// Màn hình ảo Windows: tọa độ 16-bit có dấu (cùng miền với các ô X/Y của ActionEditorWidget).
static constexpr int kMinScreenCoord = -32768;
static constexpr int kMaxScreenCoord = 32767;

/// Một khoảng thời gian (ms) đọc từ JSON, ép về miền [0, INT_MAX] (~24 ngày). Tệp hồ sơ có thể do người
/// dùng sửa tay hoặc nhập từ nơi khác: số âm trước đây được nhận nguyên (hiện "-5 ms" trong danh sách),
/// còn số khổng lồ (vd 1e18) làm TRÀN phép cộng mốc thời gian steady_clock trong ActionRunner/
/// InputController (ms -> ns) và bị cắt cụt thành số vô nghĩa ở mọi chỗ ép về int (ô nhập của editor,
/// thời lượng kéo/gõ/cuộn).
static std::chrono::milliseconds readDurationMs(const QJsonValue& value, int defaultMs)
{
    if (!value.isDouble())
        return std::chrono::milliseconds(defaultMs);
    const double ms = value.toDouble();
    if (!(ms > 0.0)) // âm, 0, NaN
        return std::chrono::milliseconds(0);
    if (ms >= static_cast<double>(INT_MAX))
        return std::chrono::milliseconds(INT_MAX);
    return std::chrono::milliseconds(static_cast<qint64>(ms));
}

static QString actionTypeToString(ActionType type)
{
    switch (type)
    {
        case ActionType::MouseClick:   return "MouseClick";
        case ActionType::MouseDrag:    return "MouseDrag";
        case ActionType::MouseHold:    return "MouseHold";
        case ActionType::TypeText:     return "TypeText";
        case ActionType::Hotkey:       return "Hotkey";
        case ActionType::KeyPress:     return "KeyPress";
        case ActionType::Scroll:       return "Scroll";
        case ActionType::RecordedMacro: return "RecordedMacro";
    }
    return "MouseClick";
}

static ActionType stringToActionType(const QString& str)
{
    if (str == "MouseDrag")     return ActionType::MouseDrag;
    if (str == "MouseHold")     return ActionType::MouseHold;
    if (str == "TypeText")      return ActionType::TypeText;
    if (str == "Hotkey")        return ActionType::Hotkey;
    if (str == "KeyPress")      return ActionType::KeyPress;
    if (str == "Scroll")        return ActionType::Scroll;
    if (str == "RecordedMacro") return ActionType::RecordedMacro;
    return ActionType::MouseClick;
}

static QString recordedEventTypeToString(RecordedEventType type)
{
    switch (type)
    {
        case RecordedEventType::MouseMove:  return "MouseMove";
        case RecordedEventType::MouseDown:  return "MouseDown";
        case RecordedEventType::MouseUp:    return "MouseUp";
        case RecordedEventType::MouseWheel: return "MouseWheel";
        case RecordedEventType::KeyDown:    return "KeyDown";
        case RecordedEventType::KeyUp:      return "KeyUp";
    }
    return "MouseMove";
}

static bool stringToRecordedEventType(const QString& s, RecordedEventType& out)
{
    if (s == "MouseMove")  { out = RecordedEventType::MouseMove;  return true; }
    if (s == "MouseDown")  { out = RecordedEventType::MouseDown;  return true; }
    if (s == "MouseUp")    { out = RecordedEventType::MouseUp;    return true; }
    if (s == "MouseWheel") { out = RecordedEventType::MouseWheel; return true; }
    if (s == "KeyDown")    { out = RecordedEventType::KeyDown;    return true; }
    if (s == "KeyUp")      { out = RecordedEventType::KeyUp;      return true; }
    return false;
}

QString ActionSerializer::toJsonString(const std::vector<ActionChain>& chains)
{
    QJsonObject root;
    root["version"] = "1.0";

    QJsonArray chainsArr;
    for (const auto& chain : chains)
    {
        QJsonObject chainObj;
        chainObj["id"] = QString::fromStdString(chain.id);
        chainObj["name"] = QString::fromStdString(chain.name);
        chainObj["description"] = QString::fromStdString(chain.description);
        chainObj["enabled"] = chain.enabled;
        chainObj["repeatCount"] = chain.repeatCount;
        chainObj["pinned"] = chain.pinned;

        QJsonArray actionsArr;
        for (const auto& action : chain.actions)
        {
            QJsonObject actObj;
            actObj["type"] = actionTypeToString(action.type);
            actObj["enabled"] = action.enabled;
            actObj["waitBefore"] = static_cast<qint64>(action.waitBefore.count());
            actObj["waitAfter"] = static_cast<qint64>(action.waitAfter.count());
            actObj["duration"] = static_cast<qint64>(action.duration.count());

            actObj["x"] = action.x;
            actObj["y"] = action.y;
            actObj["startX"] = action.startX;
            actObj["startY"] = action.startY;
            actObj["endX"] = action.endX;
            actObj["endY"] = action.endY;
            actObj["mouseButton"] = static_cast<int>(action.mouseButton);

            actObj["text"] = QString::fromStdString(action.text);
            actObj["textMode"] = static_cast<int>(action.textMode);
            actObj["keyCode"] = action.keyCode;
            actObj["keyName"] = QString::fromStdString(action.keyName);
            actObj["modCtrl"] = action.modCtrl;
            actObj["modAlt"] = action.modAlt;
            actObj["modShift"] = action.modShift;
            actObj["modWin"] = action.modWin;
            // modOrder: THỨ TỰ thật đã bấm chọn (v1.19.7) - ghi THÊM, không thay thế 4 bool ở trên (hồ sơ
            // vẫn đọc được bởi code CŨ, chỉ mất thông tin thứ tự). Luôn ghi từ effectiveModOrder() (tường
            // minh dù modOrder gốc rỗng) để hồ sơ lưu ra từ Action dựng tay (demo chain...) vẫn có thứ tự
            // xác định khi đọc lại.
            QJsonArray modOrderArr;
            for (ModifierKey m : action.effectiveModOrder())
                modOrderArr.append(modifierKeyToString(m));
            actObj["modOrder"] = modOrderArr;
            actObj["hotkeyTrigger"] = static_cast<int>(action.hotkeyTrigger);

            actObj["scrollDirection"] = static_cast<int>(action.scrollDirection);
            actObj["scrollAmount"] = action.scrollAmount;

            // Mảng có thể rất dài (hàng nghìn sự kiện) - chỉ ghi field LIÊN QUAN tới từng type cho gọn,
            // khác cách ghi phẳng hết mọi field ở cấp Action phía trên.
            QJsonArray recordedEventsArr;
            for (const auto& ev : action.recordedEvents)
            {
                QJsonObject evObj;
                evObj["type"] = recordedEventTypeToString(ev.type);
                evObj["delayMs"] = static_cast<qint64>(ev.delayMs);
                switch (ev.type)
                {
                    case RecordedEventType::MouseMove:
                    case RecordedEventType::MouseDown:
                    case RecordedEventType::MouseUp:
                        evObj["x"] = ev.x;
                        evObj["y"] = ev.y;
                        if (ev.type != RecordedEventType::MouseMove)
                            evObj["button"] = static_cast<int>(ev.button);
                        break;
                    case RecordedEventType::MouseWheel:
                        evObj["x"] = ev.x;
                        evObj["y"] = ev.y;
                        evObj["wheelDelta"] = ev.wheelDelta;
                        evObj["wheelHorizontal"] = ev.wheelHorizontal;
                        break;
                    case RecordedEventType::KeyDown:
                    case RecordedEventType::KeyUp:
                        evObj["keyCode"] = ev.keyCode;
                        break;
                }
                recordedEventsArr.append(evObj);
            }
            actObj["recordedEvents"] = recordedEventsArr;

            actionsArr.append(actObj);
        }
        chainObj["actions"] = actionsArr;
        chainsArr.append(chainObj);
    }

    root["chains"] = chainsArr;

    QJsonDocument doc(root);
    return doc.toJson(QJsonDocument::Indented);
}

bool ActionSerializer::fromJsonString(const QString& jsonStr, std::vector<ActionChain>& outChains, QString* error)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error)
            *error = QString("JSON không hợp lệ (%1, vị trí %2).").arg(parseError.errorString()).arg(parseError.offset);
        return false;
    }
    if (!doc.isObject())
    {
        if (error) *error = "Tệp không phải hồ sơ Auto Click (phần gốc không phải đối tượng JSON).";
        return false;
    }

    QJsonObject root = doc.object();
    // Thiếu "chains" (hoặc không phải mảng) = đây không phải tệp hồ sơ Auto Click. Trước đây trường hợp
    // này vẫn trả về true với danh sách rỗng, rồi nơi gọi coi là "chưa có chuỗi nào" và ghi đè tệp mẫu
    // lên chính tệp đó.
    if (!root["chains"].isArray())
    {
        if (error) *error = "Tệp không phải hồ sơ Auto Click (thiếu danh sách \"chains\").";
        return false;
    }
    QJsonArray chainsArr = root["chains"].toArray();

    outChains.clear();
    for (const auto& chainVal : chainsArr)
    {
        QJsonObject chainObj = chainVal.toObject();
        ActionChain chain;
        chain.id = chainObj["id"].toString().toStdString();
        chain.name = chainObj["name"].toString("Chain Mới").toStdString();
        chain.description = chainObj["description"].toString().toStdString();
        chain.enabled = chainObj["enabled"].toBool(true);
        chain.repeatCount = chainObj["repeatCount"].toInt(1);
        if (chain.repeatCount == 0 || chain.repeatCount < -1)
            chain.repeatCount = 1;
        chain.pinned = chainObj["pinned"].toBool(false); // thiếu field = hồ sơ cũ -> chưa ghim

        QJsonArray actionsArr = chainObj["actions"].toArray();
        for (const auto& actVal : actionsArr)
        {
            QJsonObject actObj = actVal.toObject();
            Action action;
            action.type = stringToActionType(actObj["type"].toString());
            action.enabled = actObj["enabled"].toBool(true);
            action.waitBefore = readDurationMs(actObj["waitBefore"], 0);
            action.waitAfter = readDurationMs(actObj["waitAfter"], 500);
            action.duration = readDurationMs(actObj["duration"], 0);

            // Tọa độ ép về miền màn hình ảo Windows (cùng miền với ô nhập của editor): giá trị cỡ ±2 tỉ trong
            // tệp sửa tay làm tràn phép nội suy (end - start) lúc kéo chuột.
            action.x = clampInt(actObj["x"].toInt(500), kMinScreenCoord, kMaxScreenCoord);
            action.y = clampInt(actObj["y"].toInt(300), kMinScreenCoord, kMaxScreenCoord);
            action.startX = clampInt(actObj["startX"].toInt(100), kMinScreenCoord, kMaxScreenCoord);
            action.startY = clampInt(actObj["startY"].toInt(100), kMinScreenCoord, kMaxScreenCoord);
            action.endX = clampInt(actObj["endX"].toInt(500), kMinScreenCoord, kMaxScreenCoord);
            action.endY = clampInt(actObj["endY"].toInt(500), kMinScreenCoord, kMaxScreenCoord);
            action.mouseButton = static_cast<MouseButtonType>(clampInt(actObj["mouseButton"].toInt(0), 0, 2));

            action.text = actObj["text"].toString().toStdString();
            action.textMode = static_cast<TextTypeMode>(clampInt(actObj["textMode"].toInt(0), 0, 1));
            // Mã VK hợp lệ nằm trong 1..254; giá trị ngoài miền (tệp sửa tay/hỏng) trước đây được gửi thẳng vào
            // SendInput (WORD bị cắt bớt -> một phím bất kỳ khác).
            const int keyCode = actObj["keyCode"].toInt(13);
            action.keyCode = (keyCode >= 1 && keyCode <= 254) ? keyCode : 13;
            action.keyName = actObj["keyName"].toString("ENTER").toStdString();
            action.modCtrl = actObj["modCtrl"].toBool(false);
            action.modAlt = actObj["modAlt"].toBool(false);
            action.modShift = actObj["modShift"].toBool(false);
            action.modWin = actObj["modWin"].toBool(false);
            // "modOrder" chỉ có trong hồ sơ lưu từ v1.19.7 trở đi - THẮNG TUYỆT ĐỐI nếu mâu thuẫn 4 bool ở
            // trên (tệp sửa tay). THIẾU key này (hồ sơ cũ) thì giữ modOrder RỖNG - effectiveModOrder() tự
            // suy luận lại đúng thứ tự cố định Ctrl→Alt→Shift→Win từ 4 bool vừa đọc, hành vi y hệt trước đây.
            if (actObj["modOrder"].isArray())
            {
                std::vector<ModifierKey> order;
                for (const auto& v : actObj["modOrder"].toArray())
                {
                    ModifierKey mk;
                    if (stringToModifierKey(v.toString(), mk) &&
                        std::find(order.begin(), order.end(), mk) == order.end()) // khử trùng (tệp sửa tay)
                        order.push_back(mk);
                }
                action.setModifiers(order);
            }
            action.hotkeyTrigger = static_cast<HotkeyTrigger>(clampInt(actObj["hotkeyTrigger"].toInt(0), 0, 2));

            action.scrollDirection = static_cast<ScrollDirection>(clampInt(actObj["scrollDirection"].toInt(0), 0, 3));
            // Cùng miền với ô nhập trên giao diện (1-100 nấc). Không giới hạn thì một tệp hồ sơ ghi
            // "scrollAmount": 2000000000 sinh ra hai tỉ lần SendInput liên tiếp.
            action.scrollAmount = clampInt(actObj["scrollAmount"].toInt(5), 1, 100);

            // Thiếu key (hồ sơ cũ trước khi có "Ghi nhớ chuỗi") -> rỗng, không lỗi. Mỗi phần tử trong mảng
            // validate RIÊNG - type lạ/field thiếu thì bỏ qua đúng phần tử đó, không làm hỏng cả hồ sơ.
            if (actObj["recordedEvents"].isArray())
            {
                for (const auto& evVal : actObj["recordedEvents"].toArray())
                {
                    if (!evVal.isObject())
                        continue;
                    QJsonObject evObj = evVal.toObject();
                    RecordedEvent ev;
                    if (!stringToRecordedEventType(evObj["type"].toString(), ev.type))
                        continue;
                    // Một giá trị delayMs hỏng/ác ý không được tạo ra một bước chờ cả ngày lúc phát lại.
                    ev.delayMs = clampDoubleToInt(evObj["delayMs"].toDouble(0), 0, 60000);
                    ev.x = clampInt(evObj["x"].toInt(0), kMinScreenCoord, kMaxScreenCoord);
                    ev.y = clampInt(evObj["y"].toInt(0), kMinScreenCoord, kMaxScreenCoord);
                    ev.button = static_cast<MouseButtonType>(clampInt(evObj["button"].toInt(0), 0, 2));
                    // Một lần cuộn thật là bội nhỏ của WHEEL_DELTA (120); mouseData của SendInput là DWORD nhưng
                    // Windows hiểu theo short có dấu - giới hạn trong miền short.
                    ev.wheelDelta = clampInt(evObj["wheelDelta"].toInt(0), -32768, 32767);
                    ev.wheelHorizontal = evObj["wheelHorizontal"].toBool(false);
                    ev.keyCode = evObj["keyCode"].toInt(0);
                    if ((ev.type == RecordedEventType::KeyDown || ev.type == RecordedEventType::KeyUp) &&
                        (ev.keyCode < 1 || ev.keyCode > 254))
                        continue; // sự kiện phím không có mã phím hợp lệ - bỏ đúng sự kiện này
                    action.recordedEvents.push_back(ev);
                }
            }

            chain.actions.push_back(action);
        }

        outChains.push_back(chain);
    }

    return true;
}

bool ActionSerializer::saveToFile(const QString& filePath, const std::vector<ActionChain>& chains, QString* error)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (error) *error = file.errorString();
        return false;
    }

    const QByteArray data = toJsonString(chains).toUtf8();
    // Kiểm tra CẢ số byte đã ghi lẫn commit(): write() có thể ghi thiếu (đầy đĩa) mà không báo lỗi lúc
    // mở, và chỉ commit() mới thật sự đổi tên tệp tạm đè lên tệp đích - thất bại ở bước nào thì tệp
    // đích cũ vẫn còn nguyên.
    if (file.write(data) != data.size())
    {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
    {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool ActionSerializer::loadFromFile(const QString& filePath, std::vector<ActionChain>& outChains, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error) *error = file.errorString();
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    return fromJsonString(QString::fromUtf8(data), outChains, error);
}
