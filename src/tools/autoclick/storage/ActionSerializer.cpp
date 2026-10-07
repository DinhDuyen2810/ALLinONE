#include "ActionSerializer.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>
#include <QStandardPaths>

void ActionSerializer::ensureProfilesDir()
{
    QDir dir("profiles");
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
}

QString ActionSerializer::getDefaultProfilePath()
{
    ensureProfilesDir();
    return "profiles/default.json";
}

static int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

bool ActionSerializer::fromJsonString(const QString& jsonStr, std::vector<ActionChain>& outChains)
{
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        return false;
    }

    QJsonObject root = doc.object();
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
            action.waitBefore = std::chrono::milliseconds(actObj["waitBefore"].toInteger(0));
            action.waitAfter = std::chrono::milliseconds(actObj["waitAfter"].toInteger(500));
            action.duration = std::chrono::milliseconds(actObj["duration"].toInteger(0));

            action.x = actObj["x"].toInt(500);
            action.y = actObj["y"].toInt(300);
            action.startX = actObj["startX"].toInt(100);
            action.startY = actObj["startY"].toInt(100);
            action.endX = actObj["endX"].toInt(500);
            action.endY = actObj["endY"].toInt(500);
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

bool ActionSerializer::saveToFile(const QString& filePath, const std::vector<ActionChain>& chains)
{
    ensureProfilesDir();
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        return false;
    }

    QString jsonStr = toJsonString(chains);
    file.write(jsonStr.toUtf8());
    file.close();
    return true;
}

bool ActionSerializer::loadFromFile(const QString& filePath, std::vector<ActionChain>& outChains)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    return fromJsonString(QString::fromUtf8(data), outChains);
}
