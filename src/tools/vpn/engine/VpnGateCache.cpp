#include "VpnGateCache.h"

#include "VpnGateClient.h" // VpnGateClientInternal::isValidIpv4
#include "core/AppPaths.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace VpnGateCache
{

QList<VpnGateServer> merge(QList<VpnGateServer> existing, const QList<VpnGateServer>& fresh, qint64 nowEpoch,
                           qint64 maxAgeSecs, int maxCount)
{
    // Chỉ số theo IP để dedup (bản mới ghi đè bản cũ cùng IP).
    QHash<QString, int> byIp;
    for (int i = 0; i < existing.size(); ++i)
        byIp.insert(existing[i].ip, i);

    for (const VpnGateServer& f : fresh)
    {
        VpnGateServer s = f;
        s.lastSeenEpoch = nowEpoch;
        const auto it = byIp.constFind(s.ip);
        if (it != byIp.constEnd())
            existing[it.value()] = s; // cập nhật máy chủ đã biết (ping/điểm/tốc độ mới + lastSeen mới)
        else
        {
            byIp.insert(s.ip, existing.size());
            existing.push_back(s);
        }
    }

    // Loại máy chủ quá cũ (không còn xuất hiện - IP có thể đã chết). lastSeenEpoch==0 (dữ liệu cũ không có
    // mốc) được coi là vừa thấy để không xóa oan ngay lần nâng cấp đầu.
    QList<VpnGateServer> result;
    result.reserve(existing.size());
    for (const VpnGateServer& s : existing)
    {
        const qint64 seen = s.lastSeenEpoch > 0 ? s.lastSeenEpoch : nowEpoch;
        if (nowEpoch - seen <= maxAgeSecs)
            result.push_back(s);
    }

    std::sort(result.begin(), result.end(),
              [](const VpnGateServer& a, const VpnGateServer& b) { return a.score > b.score; });
    if (maxCount > 0 && result.size() > maxCount)
        result = result.mid(0, maxCount);
    return result;
}

namespace internal
{

QByteArray toJson(const QList<VpnGateServer>& servers)
{
    QJsonArray arr;
    for (const VpnGateServer& s : servers)
    {
        QJsonObject o;
        o["hostName"] = s.hostName;
        o["ip"] = s.ip;
        o["countryLong"] = s.countryLong;
        o["countryShort"] = s.countryShort;
        o["ping"] = s.ping;
        o["speedBps"] = static_cast<double>(s.speedBps);
        o["score"] = static_cast<double>(s.score);
        o["sessions"] = s.sessions;
        o["lastSeenEpoch"] = static_cast<double>(s.lastSeenEpoch);
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["servers"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QList<VpnGateServer> fromJson(const QByteArray& json)
{
    QList<VpnGateServer> servers;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return servers;
    for (const QJsonValue& v : doc.object().value("servers").toArray())
    {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        VpnGateServer s;
        s.hostName = o.value("hostName").toString();
        s.ip = o.value("ip").toString();
        s.countryLong = o.value("countryLong").toString();
        s.countryShort = o.value("countryShort").toString().toUpper();
        s.ping = qMax(0, o.value("ping").toInt());
        s.speedBps = qMax<qint64>(0, static_cast<qint64>(o.value("speedBps").toDouble()));
        s.score = qMax<qint64>(0, static_cast<qint64>(o.value("score").toDouble()));
        s.sessions = qMax(0, o.value("sessions").toInt());
        s.lastSeenEpoch = static_cast<qint64>(o.value("lastSeenEpoch").toDouble());
        // Bỏ bản ghi hỏng (IP không hợp lệ) - cùng lý do an toàn như parseCsv (IP sẽ thành đích kết nối VPN).
        if (!s.ip.isEmpty() && VpnGateClientInternal::isValidIpv4(s.ip) && s.countryShort.size() == 2)
            servers.push_back(s);
    }
    return servers;
}

} // namespace internal

QList<VpnGateServer> load()
{
    const QString path = AppPaths::profileFile("vpngate_servers.json");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = f.readAll();
    f.close();
    return internal::fromJson(data);
}

void save(const QList<VpnGateServer>& servers)
{
    const QString path = AppPaths::profileFile("vpngate_servers.json");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(internal::toJson(servers));
    f.commit();
}

} // namespace VpnGateCache
