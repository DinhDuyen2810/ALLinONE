#include "VpnController.h"

#include "core/PowerShellRunner.h"

#include "core/AppPaths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace
{
/// %LOCALAPPDATA%\OneForAll\profiles\vpn_profiles.json (AppPaths) - trước đây là đường dẫn TƯƠNG ĐỐI
/// "profiles/vpn_profiles.json", tức phụ thuộc thư mục làm việc lúc khởi động: cài vào Program Files hay
/// chạy lại với quyền Administrator là ghi thất bại im lặng hoặc đọc nhầm một tệp khác.
QString profilesFilePath()
{
    return AppPaths::profileFile("vpn_profiles.json");
}

/// Thông tin PHỤ do người dùng tự đặt cho từng tên kết nối (nhãn quốc gia, tên đăng nhập) - KHÔNG phải
/// dữ liệu Windows biết, chỉ lưu riêng trong ứng dụng. KHÔNG BAO GIỜ chứa mật khẩu. Đọc/ghi toàn bộ map
/// mỗi lần (danh sách nhỏ, vài chục mục là nhiều). Định dạng: { "<tên>": {"country": "...", "username":
/// "..."} }; bản cũ lưu { "<tên>": "<nhãn quốc gia>" } vẫn đọc được.
QHash<QString, VpnController::VpnProfileMeta> loadProfileMetas()
{
    QHash<QString, VpnController::VpnProfileMeta> result;
    QFile f(profilesFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return result;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        VpnController::VpnProfileMeta meta;
        if (it.value().isObject())
        {
            const QJsonObject o = it.value().toObject();
            meta.countryLabel = o.value("country").toString();
            meta.username = o.value("username").toString();
        }
        else
        {
            meta.countryLabel = it.value().toString();
        }
        result[it.key()] = meta;
    }
    return result;
}

/// Ghi NGUYÊN TỬ qua QSaveFile (ghi ra tệp tạm rồi đổi tên): mất điện/crash giữa chừng không để lại tệp
/// rỗng hay tệp JSON cụt làm mất toàn bộ nhãn đã đặt. Trả về false + lý do nếu không ghi được.
bool saveProfileMetas(const QHash<QString, VpnController::VpnProfileMeta>& metas, QString* error)
{
    QJsonObject obj;
    for (auto it = metas.constBegin(); it != metas.constEnd(); ++it)
    {
        if (it.value().countryLabel.isEmpty() && it.value().username.isEmpty())
            continue;
        QJsonObject o;
        o["country"] = it.value().countryLabel;
        o["username"] = it.value().username;
        obj[it.key()] = o;
    }

    QSaveFile f(profilesFilePath());
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
    {
        if (error)
            *error = "Không lưu được thông tin hồ sơ VPN vào " + f.fileName() + ": " + f.errorString();
        return false;
    }
    return true;
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

    const auto metas = loadProfileMetas();
    for (auto& c : result)
    {
        const VpnProfileMeta meta = metas.value(c.name);
        c.countryLabel = meta.countryLabel;
        c.username = meta.username;
    }
    return result;
}

bool addConnection(const VpnProfile& profile, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildAddConnectionScript(profile), &ok, error);
    if (ok && (!profile.countryLabel.isEmpty() || !profile.username.isEmpty()))
    {
        // Kết nối đã được tạo thật trong Windows - lưu thông tin phụ thất bại KHÔNG biến việc thêm thành
        // thất bại, chỉ trả kèm cảnh báo qua *error (nơi gọi hiển thị như một ghi chú).
        QString saveError;
        if (!setProfileMeta(profile.name, {profile.countryLabel, profile.username}, &saveError) && error)
            *error = saveError;
    }
    return ok;
}

bool removeConnection(const QString& name, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildRemoveConnectionScript(name), &ok, error);
    if (ok)
    {
        auto metas = loadProfileMetas();
        if (metas.remove(name) > 0)
            saveProfileMetas(metas, nullptr);
    }
    return ok;
}

VpnProfileMeta profileMeta(const QString& name)
{
    return loadProfileMetas().value(name);
}

bool setProfileMeta(const QString& name, const VpnProfileMeta& meta, QString* error)
{
    auto metas = loadProfileMetas();
    metas[name] = meta;
    return saveProfileMetas(metas, error);
}

void setCountryLabel(const QString& name, const QString& countryLabel)
{
    VpnProfileMeta meta = profileMeta(name);
    meta.countryLabel = countryLabel;
    setProfileMeta(name, meta, nullptr);
}

namespace internal
{

QString escapePsString(const QString& s)
{
    // Lớp bọc giữ tên cũ: PowerShellRunner::quoteLiteral nhân đôi CẢ dấu ' ASCII lẫn 4 dấu nháy "cong"
    // U+2018/2019/201A/201B mà PowerShell cũng coi là dấu nháy đơn - bản riêng trước đây chỉ xử lý dấu
    // ASCII, nên một tên kết nối chứa ’ đóng chuỗi sớm và phần còn lại chạy như LỆNH PowerShell (tên lấy
    // từ Get-VpnConnection, tức không phải lúc nào cũng do chính ứng dụng này đặt).
    return PowerShellRunner::quoteLiteral(s);
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
