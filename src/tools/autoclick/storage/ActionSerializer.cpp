#include "ActionSerializer.h"
#include "core/AppPaths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QSaveFile>
#include <climits>

QString ActionSerializer::getDefaultProfilePath()
{
    return AppPaths::profileFile("default.json");
}

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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
        case ActionType::MouseClick: return "MouseClick";
        case ActionType::MouseDrag:  return "MouseDrag";
        case ActionType::MouseHold:  return "MouseHold";
        case ActionType::TypeText:   return "TypeText";
        case ActionType::Hotkey:     return "Hotkey";
        case ActionType::KeyPress:   return "KeyPress";
        case ActionType::Scroll:     return "Scroll";
    }
    return "MouseClick";
}

static ActionType stringToActionType(const QString& str)
{
    if (str == "MouseDrag") return ActionType::MouseDrag;
    if (str == "MouseHold") return ActionType::MouseHold;
    if (str == "TypeText")  return ActionType::TypeText;
    if (str == "Hotkey")    return ActionType::Hotkey;
    if (str == "KeyPress")  return ActionType::KeyPress;
    if (str == "Scroll")    return ActionType::Scroll;
    return ActionType::MouseClick;
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

            actObj["scrollDirection"] = static_cast<int>(action.scrollDirection);
            actObj["scrollAmount"] = action.scrollAmount;

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
            action.keyCode = actObj["keyCode"].toInt(13);
            action.keyName = actObj["keyName"].toString("ENTER").toStdString();
            action.modCtrl = actObj["modCtrl"].toBool(false);
            action.modAlt = actObj["modAlt"].toBool(false);
            action.modShift = actObj["modShift"].toBool(false);
            action.modWin = actObj["modWin"].toBool(false);

            action.scrollDirection = static_cast<ScrollDirection>(clampInt(actObj["scrollDirection"].toInt(0), 0, 3));
            action.scrollAmount = actObj["scrollAmount"].toInt(5);

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
