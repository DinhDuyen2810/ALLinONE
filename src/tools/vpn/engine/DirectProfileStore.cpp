#include "DirectProfileStore.h"

#include "core/AppPaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
const QString kFile = QStringLiteral("vpn_direct_profiles.json");
constexpr int kMaxNameLen = 80;

QString protocolKey(DirectProtocol p)
{
    switch (p)
    {
        case DirectProtocol::GlobalProtect: return QStringLiteral("globalprotect");
        case DirectProtocol::FortinetSsl: return QStringLiteral("fortigate-ssl");
        case DirectProtocol::FortinetIpsec: return QStringLiteral("fortigate-ipsec");
        case DirectProtocol::None: default: return {};
    }
}

DirectProtocol protocolFromKey(const QString& key)
{
    if (key == QLatin1String("globalprotect"))
        return DirectProtocol::GlobalProtect;
    if (key == QLatin1String("fortigate-ssl"))
        return DirectProtocol::FortinetSsl;
    if (key == QLatin1String("fortigate-ipsec"))
        return DirectProtocol::FortinetIpsec;
    return DirectProtocol::None;
}

bool setError(QString* error, const QString& text)
{
    if (error)
        *error = text;
    return false;
}
} // namespace

QString DirectProfileStore::clientName(DirectProtocol p)
{
    switch (p)
    {
        case DirectProtocol::GlobalProtect: return QStringLiteral("GlobalProtect");
        case DirectProtocol::FortinetIpsec: return QStringLiteral("FortiClient (IPsec)");
        case DirectProtocol::FortinetSsl: return QStringLiteral("FortiClient");
        case DirectProtocol::None: default: return QString();
    }
}

QString DirectProfileStore::normalizeServer(const QString& raw)
{
    // Dùng chính bộ tách host[:cổng] của kết nối trực tiếp (bỏ https://, đường dẫn; chữ thường) - nếu sai định dạng thì trả
    // nguyên văn đã cắt khoảng trắng để validate() báo lỗi cụ thể.
    QString host;
    int port = 0;
    const QString trimmed = raw.trimmed();
    if (!DirectVpn::splitHostPort(trimmed, 0, &host, &port))
        return trimmed;
    return port > 0 ? QStringLiteral("%1:%2").arg(host).arg(port) : host;
}

bool DirectProfileStore::validate(const DirectVpnProfile& p, QString* error)
{
    const QString name = p.name.trimmed();
    if (name.isEmpty())
        return setError(error, QStringLiteral("Vui lòng nhập tên hồ sơ."));
    if (name.size() > kMaxNameLen)
        return setError(error, QStringLiteral("Tên hồ sơ quá dài (tối đa %1 ký tự).").arg(kMaxNameLen));
    if (p.protocol != DirectProtocol::GlobalProtect && p.protocol != DirectProtocol::FortinetSsl &&
        p.protocol != DirectProtocol::FortinetIpsec)
        return setError(error, QStringLiteral("Loại kết nối không được hỗ trợ."));
    if (p.server.trimmed().isEmpty())
        return setError(error, p.protocol == DirectProtocol::GlobalProtect ? QStringLiteral("Vui lòng nhập địa chỉ portal GlobalProtect.")
                                                                            : QStringLiteral("Vui lòng nhập địa chỉ máy chủ FortiGate."));
    QString host;
    int port = 0;
    if (!DirectVpn::splitHostPort(p.server, 0, &host, &port))
        return setError(error, QStringLiteral("Địa chỉ máy chủ không hợp lệ (chỉ chữ, số, dấu chấm, gạch ngang; có thể kèm :cổng)."));
    if (p.protocol == DirectProtocol::FortinetIpsec && port > 0)
        return setError(error, QStringLiteral("FortiClient IPsec chỉ cần địa chỉ máy chủ, không nhập cổng (dùng UDP 500/4500)."));
    return true;
}

QByteArray DirectProfileStore::internal::serialize(const QList<DirectVpnProfile>& profiles)
{
    QJsonArray arr;
    for (const DirectVpnProfile& p : profiles)
        arr.append(QJsonObject{{QStringLiteral("name"), p.name}, {QStringLiteral("server"), p.server}, {QStringLiteral("protocol"), protocolKey(p.protocol)}});
    return QJsonDocument(arr).toJson(QJsonDocument::Indented);
}

QList<DirectVpnProfile> DirectProfileStore::internal::parse(const QByteArray& json)
{
    QList<DirectVpnProfile> out;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return out;
    for (const QJsonValue& v : doc.array())
    {
        const QJsonObject o = v.toObject();
        DirectVpnProfile p;
        p.name = o.value(QStringLiteral("name")).toString().trimmed();
        p.server = o.value(QStringLiteral("server")).toString().trimmed();
        p.protocol = protocolFromKey(o.value(QStringLiteral("protocol")).toString());
        if (validate(p)) // mục sai/thiếu bị bỏ qua - tệp có thể do người dùng sửa tay
            out.append(p);
    }
    return out;
}

QList<DirectVpnProfile> DirectProfileStore::load()
{
    QFile f(AppPaths::profileFile(kFile));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return internal::parse(f.readAll());
}

namespace
{
bool save(const QList<DirectVpnProfile>& profiles, QString* error)
{
    QSaveFile f(AppPaths::profileFile(kFile));
    if (!f.open(QIODevice::WriteOnly))
        return setError(error, QStringLiteral("Không ghi được tệp hồ sơ: %1").arg(f.errorString()));
    f.write(DirectProfileStore::internal::serialize(profiles));
    if (!f.commit())
        return setError(error, QStringLiteral("Không lưu được tệp hồ sơ: %1").arg(f.errorString()));
    return true;
}
} // namespace

bool DirectProfileStore::add(const DirectVpnProfile& input, QString* error)
{
    DirectVpnProfile p = input;
    p.name = p.name.trimmed();
    p.server = normalizeServer(p.server);
    if (!validate(p, error))
        return false;
    QList<DirectVpnProfile> all = load();
    for (const DirectVpnProfile& e : all)
        if (e.protocol == p.protocol && e.name.compare(p.name, Qt::CaseInsensitive) == 0)
            return setError(error, QStringLiteral("Đã có hồ sơ \"%1\" (%2) - hãy đặt tên khác.").arg(p.name, clientName(p.protocol)));
    all.append(p);
    return save(all, error);
}

bool DirectProfileStore::remove(DirectProtocol protocol, const QString& name)
{
    QList<DirectVpnProfile> all = load();
    const qsizetype before = all.size();
    all.removeIf([&](const DirectVpnProfile& e) { return e.protocol == protocol && e.name == name; });
    if (all.size() == before)
        return false;
    return save(all, nullptr);
}

ThirdPartyVpnProfile DirectProfileStore::toThirdParty(const DirectVpnProfile& p)
{
    ThirdPartyVpnProfile t;
    t.clientName = clientName(p.protocol);
    t.name = p.name;
    t.server = p.server;
    t.source = QStringLiteral("Hồ sơ bạn tự thêm trong ứng dụng (%1)").arg(AppPaths::profileFile(kFile));
    t.direct = p.protocol;
    t.custom = true;
    return t;
}
