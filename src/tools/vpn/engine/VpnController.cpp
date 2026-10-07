#include "VpnController.h"

#include "core/PowerShellRunner.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
QString profilesFilePath()
{
    QDir().mkpath("profiles");
    return "profiles/vpn_profiles.json";
}

/// Nhãn quốc gia do người dùng tự đặt cho từng tên kết nối - KHÔNG phải dữ liệu Windows biết, chỉ lưu
/// riêng trong ứng dụng để hiển thị. Đọc/ghi toàn bộ map mỗi lần (danh sách nhỏ, vài chục mục là nhiều).
QHash<QString, QString> loadCountryLabels()
{
    QHash<QString, QString> result;
    QFile f(profilesFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return result;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
        result[it.key()] = it.value().toString();
    return result;
}

void saveCountryLabels(const QHash<QString, QString>& labels)
{
    QJsonObject obj;
    for (auto it = labels.constBegin(); it != labels.constEnd(); ++it)
        obj[it.key()] = it.value();
    QFile f(profilesFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
}

QString tunnelTypeToPs(VpnTunnelType type)
{
    switch (type)
    {
        case VpnTunnelType::Ikev2: return "Ikev2";
        case VpnTunnelType::L2tp: return "L2tp";
        case VpnTunnelType::Sstp: return "Sstp";
        case VpnTunnelType::Pptp: return "Pptp";
        case VpnTunnelType::Automatic: default: return "Automatic";
    }
}

/// Xác nhận THẬT trên Windows (qua vpn_real_check.cpp) rằng Add-VpnConnection từ chối
/// -AuthenticationMethod MSChapv2 khi -TunnelType Ikev2, với lỗi: "IKEv2 tunnel type only supports Eap
/// and Machine certificate as authentication method." IKEv2 trên Windows xác thực user/pass qua
/// EAP-MSCHAPv2 (lựa chọn EAP mặc định của Windows cho VPN "Secured password"), nên rasdial với
/// username/password vẫn hoạt động bình thường với -AuthenticationMethod Eap. Các loại còn lại
/// (L2tp/Sstp/Pptp - giao thức nền PPP) và Automatic giữ nguyên MSChapv2 như trước - đây vẫn là cách
/// xác thực user/pass tiêu chuẩn cho các giao thức đó và chưa ghi nhận lỗi thật nào.
QString authMethodForTunnelType(VpnTunnelType type)
{
    switch (type)
    {
        case VpnTunnelType::Ikev2: return "Eap";
        default: return "MSChapv2";
    }
}
} // namespace

namespace VpnController
{

QList<VpnConnectionStatus> listConnections(QString* error)
{
    bool ok = false;
    QString runErr;
    const QByteArray json = PowerShellRunner::run(internal::buildListConnectionsScript(), &ok, &runErr);
    if (!ok)
    {
        if (error) *error = runErr;
        return {};
    }
    QString parseErr;
    auto result = internal::parseConnectionsJson(json, &parseErr);
    if (!parseErr.isEmpty() && error)
        *error = parseErr;

    const auto labels = loadCountryLabels();
    for (auto& c : result)
        c.countryLabel = labels.value(c.name);
    return result;
}

bool addConnection(const VpnProfile& profile, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildAddConnectionScript(profile), &ok, error);
    if (ok && !profile.countryLabel.isEmpty())
        setCountryLabel(profile.name, profile.countryLabel);
    return ok;
}

bool removeConnection(const QString& name, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildRemoveConnectionScript(name), &ok, error);
    if (ok)
    {
        auto labels = loadCountryLabels();
        labels.remove(name);
        saveCountryLabels(labels);
    }
    return ok;
}

void setCountryLabel(const QString& name, const QString& countryLabel)
{
    auto labels = loadCountryLabels();
    labels[name] = countryLabel;
    saveCountryLabels(labels);
}

namespace internal
{

QString escapePsString(const QString& s)
{
    QString out = s;
    out.replace('\'', "''");
    return out;
}

QString buildListConnectionsScript()
{
    return QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$ProgressPreference = 'SilentlyContinue'\n"
        "try {\n"
        "  $result = @(Get-VpnConnection -ErrorAction Stop | ForEach-Object {\n"
        "    [PSCustomObject]@{\n"
        "      Name = [string]$_.Name\n"
        "      ServerAddress = [string]$_.ServerAddress\n"
        "      TunnelType = [string]$_.TunnelType\n"
        "      ConnectionStatus = [string]$_.ConnectionStatus\n"
        "    }\n"
        "  })\n"
        "  $result | ConvertTo-Json -Compress -Depth 4\n"
        "} catch {\n"
        "  [Console]::Error.WriteLine($_.Exception.Message)\n"
        "  exit 1\n"
        "}\n");
}

QString buildAddConnectionScript(const VpnProfile& profile)
{
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Add-VpnConnection -Name '%1' -ServerAddress '%2' -TunnelType %3 "
               "-AuthenticationMethod %4 -EncryptionLevel Required -Force -ErrorAction Stop | Out-Null\n"
               "  Write-Output 'OK'\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(escapePsString(profile.name), escapePsString(profile.serverAddress), tunnelTypeToPs(profile.tunnelType),
             authMethodForTunnelType(profile.tunnelType));
}

QString buildRemoveConnectionScript(const QString& name)
{
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Remove-VpnConnection -Name '%1' -Force -ErrorAction Stop\n"
               "  Write-Output 'OK'\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(escapePsString(name));
}

namespace
{
VpnConnectionStatus connectionFromJsonObject(const QJsonObject& o)
{
    VpnConnectionStatus c;
    c.name = o.value("Name").toString();
    c.serverAddress = o.value("ServerAddress").toString();
    c.tunnelType = o.value("TunnelType").toString();
    c.connectionStatus = o.value("ConnectionStatus").toString();
    return c;
}
} // namespace

QList<VpnConnectionStatus> parseConnectionsJson(const QByteArray& json, QString* error)
{
    QList<VpnConnectionStatus> result;
    if (json.trimmed().isEmpty())
        return result; // không có kết nối VPN nào - không phải lỗi

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error) *error = "Không đọc được danh sách VPN (JSON lỗi): " + parseError.errorString();
        return result;
    }

    if (doc.isArray())
    {
        for (const QJsonValue& v : doc.array())
            if (v.isObject())
                result << connectionFromJsonObject(v.toObject());
    }
    else if (doc.isObject())
    {
        // PowerShell 5.1: ConvertTo-Json không bọc mảng khi chỉ có đúng 1 phần tử - xem PartitionManager.cpp.
        result << connectionFromJsonObject(doc.object());
    }
    else if (error)
    {
        *error = "Định dạng dữ liệu VPN không nhận ra được";
    }
    return result;
}

} // namespace internal
} // namespace VpnController
