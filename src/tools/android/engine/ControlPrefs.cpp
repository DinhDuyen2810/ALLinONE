#include "ControlPrefs.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace
{
const char* const kFile = "android_control_prefs.json";
}

QJsonObject ControlPrefs::toJson() const
{
    QJsonObject o;
    o["version"] = 1;
    o["stayAwake"] = stayAwake;
    o["turnScreenOff"] = turnScreenOff;
    return o;
}

ControlPrefs ControlPrefs::fromJson(const QJsonObject& o)
{
    ControlPrefs p;
    if (o.value("stayAwake").isBool())
        p.stayAwake = o.value("stayAwake").toBool();
    if (o.value("turnScreenOff").isBool())
        p.turnScreenOff = o.value("turnScreenOff").toBool();
    return p;
}

ControlPrefs ControlPrefs::load()
{
    QFile f(AppPaths::profileFile(kFile));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? fromJson(doc.object()) : ControlPrefs{};
}

bool ControlPrefs::save() const
{
    QSaveFile f(AppPaths::profileFile(kFile));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0 || !f.commit())
    {
        Logger::instance().warning("Android", "Không lưu được tùy chọn điều khiển: " + AppPaths::profileFile(kFile));
        return false;
    }
    return true;
}
