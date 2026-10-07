#include "DefenderController.h"

#include "core/PowerShellRunner.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace
{
QString escapePsString(const QString& s)
{
    QString out = s;
    out.replace('\'', "''");
    return out;
}

/// Đọc một trường DateTime đã được script dựng sẵn ép kiểu chuỗi ISO-8601 ("o") - rỗng nếu Defender
/// chưa từng quét (QuickScanEndTime/FullScanEndTime null) hoặc không đọc được.
QDateTime parseIsoDateTime(const QJsonValue& v)
{
    const QString s = v.toString();
    if (s.isEmpty())
        return {};
    return QDateTime::fromString(s, Qt::ISODate);
}

ThreatRecord threatFromJsonObject(const QJsonObject& o)
{
    ThreatRecord t;
    t.threatId = static_cast<qint64>(o.value("ThreatID").toDouble());
    t.threatName = o.value("ThreatName").toString();
    t.severity = o.value("SeverityID").toInt();
    t.isActive = o.value("IsActive").toBool();
    for (const QJsonValue& r : o.value("Resources").toArray())
        t.resources << r.toString();
    return t;
}
} // namespace

namespace DefenderController
{

DefenderStatus getStatus(QString* error)
{
    bool ok = false;
    QString runErr;
    const QByteArray json = PowerShellRunner::run(internal::buildGetComputerStatusScript(), &ok, &runErr);
    if (!ok)
    {
        if (error) *error = runErr;
        return {};
    }
    QString parseErr;
    auto status = internal::parseComputerStatusJson(json, &parseErr);
    if (!parseErr.isEmpty() && error)
        *error = parseErr;
    return status;
}

bool setRealtimeProtectionEnabled(bool enabled, bool* verifiedState, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildSetRealtimeMonitoringScript(enabled), &ok, error);
    // Tamper Protection có thể âm thầm chặn thay đổi mà KHÔNG báo lỗi - LUÔN đọc lại trạng thái thật
    // thay vì tin vào `ok` (xem ghi chú trong DefenderController.h).
    QString readErr;
    const auto status = getStatus(&readErr);
    if (verifiedState)
        *verifiedState = status.realTimeProtectionEnabled;
    if (!readErr.isEmpty() && error && error->isEmpty())
        *error = readErr;
    return ok && verifiedState && *verifiedState == enabled;
}

bool setNetworkProtectionEnabled(bool enabled, bool* verifiedEnabled, QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildSetNetworkProtectionScript(enabled), &ok, error);
    QString readErr;
    const auto status = getStatus(&readErr);
    if (verifiedEnabled)
        *verifiedEnabled = status.networkProtectionEnabled();
    if (!readErr.isEmpty() && error && error->isEmpty())
        *error = readErr;
    return ok && verifiedEnabled && *verifiedEnabled == enabled;
}

QList<ThreatRecord> listThreats(QString* error)
{
    bool ok = false;
    QString runErr;
    const QByteArray json = PowerShellRunner::run(internal::buildListThreatsScript(), &ok, &runErr);
    if (!ok)
    {
        if (error) *error = runErr;
        return {};
    }
    QString parseErr;
    auto list = internal::parseThreatsJson(json, &parseErr);
    if (!parseErr.isEmpty() && error)
        *error = parseErr;
    return list;
}

bool removeAllActiveThreats(QString* error)
{
    bool ok = false;
    PowerShellRunner::run(internal::buildRemoveAllThreatsScript(), &ok, error);
    return ok;
}

namespace internal
{

QString buildGetComputerStatusScript()
{
    // Gộp Get-MpComputerStatus (trạng thái bảo vệ/chữ ký/lần quét gần nhất) và Get-MpPreference (chế
    // độ Network Protection - KHÔNG nằm trong Get-MpComputerStatus, đã xác nhận qua tài liệu Microsoft
    // Learn) thành MỘT đối tượng JSON duy nhất. Mọi trường DateTime được ép kiểu chuỗi ISO-8601 ("o")
    // TRƯỚC khi ConvertTo-Json - tránh ConvertTo-Json serialize DateTime thành object lồng khó đọc.
    //
    // LƯU Ý QUAN TRỌNG (lỗi THẬT phát hiện khi kiểm tra tay trên máy thật): $p.EnableNetworkProtection
    // trên thực tế là kiểu System.Byte thô (0/1/2) chứ KHÔNG tự stringify ra "Disabled"/"Enabled"/
    // "AuditMode" khi ép [string] như tài liệu tham số của Set-MpPreference có thể khiến lầm tưởng (đó
    // là kiểu tham số ĐẦU VÀO của Set-MpPreference, không phải kiểu dữ liệu ĐỌC RA của Get-MpPreference)
    // - phải tự switch/map số sang chữ trong script, nếu không networkProtectionEnabled() sẽ luôn sai
    // (so "0" với "Enabled" không bao giờ khớp).
    return QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$ProgressPreference = 'SilentlyContinue'\n"
        "try {\n"
        "  $s = Get-MpComputerStatus -ErrorAction Stop\n"
        "  $p = Get-MpPreference -ErrorAction Stop\n"
        "  $result = [PSCustomObject]@{\n"
        "    AntivirusEnabled = [bool]$s.AntivirusEnabled\n"
        "    RealTimeProtectionEnabled = [bool]$s.RealTimeProtectionEnabled\n"
        "    IsTamperProtected = [bool]$s.IsTamperProtected\n"
        "    AMRunningMode = [string]$s.AMRunningMode\n"
        "    AntivirusSignatureVersion = [string]$s.AntivirusSignatureVersion\n"
        "    AntivirusSignatureLastUpdated = $(if ($s.AntivirusSignatureLastUpdated) { $s.AntivirusSignatureLastUpdated.ToString('o') } else { '' })\n"
        "    QuickScanEndTime = $(if ($s.QuickScanEndTime) { $s.QuickScanEndTime.ToString('o') } else { '' })\n"
        "    FullScanEndTime = $(if ($s.FullScanEndTime) { $s.FullScanEndTime.ToString('o') } else { '' })\n"
        "    NetworkProtectionMode = $(switch ([int]$p.EnableNetworkProtection) { 0 {'Disabled'} 1 {'Enabled'} 2 {'AuditMode'} default {'Unknown'} })\n"
        "  }\n"
        "  $result | ConvertTo-Json -Compress -Depth 4\n"
        "} catch {\n"
        "  [Console]::Error.WriteLine($_.Exception.Message)\n"
        "  exit 1\n"
        "}\n");
}

DefenderStatus parseComputerStatusJson(const QByteArray& json, QString* error)
{
    DefenderStatus st;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error) *error = "Không đọc được trạng thái Defender.";
        return st;
    }
    const QJsonObject o = doc.object();
    st.antivirusEnabled = o.value("AntivirusEnabled").toBool();
    st.realTimeProtectionEnabled = o.value("RealTimeProtectionEnabled").toBool();
    st.isTamperProtected = o.value("IsTamperProtected").toBool();
    st.runningMode = o.value("AMRunningMode").toString();
    st.signatureVersion = o.value("AntivirusSignatureVersion").toString();
    st.signatureLastUpdated = parseIsoDateTime(o.value("AntivirusSignatureLastUpdated"));
    st.lastQuickScan = parseIsoDateTime(o.value("QuickScanEndTime"));
    st.lastFullScan = parseIsoDateTime(o.value("FullScanEndTime"));
    st.networkProtectionMode = o.value("NetworkProtectionMode").toString();
    return st;
}

QString buildSetRealtimeMonitoringScript(bool enabled)
{
    // enabled=true (muốn BẬT bảo vệ thời gian thực) -> -DisableRealtimeMonitoring $false, và ngược lại.
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Set-MpPreference -DisableRealtimeMonitoring %1 -ErrorAction Stop\n"
               "  Write-Output 'OK'\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(enabled ? "$false" : "$true");
}

QString buildSetNetworkProtectionScript(bool enabled)
{
    // Giá trị hợp lệ đã xác nhận: Disabled/Enabled/AuditMode - chỉ dùng 2 trạng thái bật/tắt đơn giản
    // cho người dùng (giống công tắc bật/tắt của Kaspersky), không lộ AuditMode ra UI.
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Set-MpPreference -EnableNetworkProtection %1 -ErrorAction Stop\n"
               "  Write-Output 'OK'\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(enabled ? "Enabled" : "Disabled");
}

QString buildStartScanScript(ScanType type, const QString& customPath)
{
    QString typeArg;
    QString pathArg;
    switch (type)
    {
        case ScanType::Quick: typeArg = "QuickScan"; break;
        case ScanType::Full: typeArg = "FullScan"; break;
        case ScanType::Custom:
            typeArg = "CustomScan";
            pathArg = QStringLiteral(" -ScanPath '%1'").arg(escapePsString(customPath));
            break;
    }
    // Start-MpScan là lệnh ĐỒNG BỘ (chặn tới khi quét xong - đã xác nhận qua tài liệu API Win32
    // MpScanStart mà cmdlet này bọc lại) - nơi gọi (DefenderScanWorker) PHẢI chạy trên QThread riêng,
    // không được gọi trực tiếp từ luồng giao diện, nhất là với FullScan có thể mất rất lâu.
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Start-MpScan -ScanType %1%2 -ErrorAction Stop\n"
               "  Write-Output 'OK'\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(typeArg, pathArg);
}

QString buildListThreatsScript()
{
    // Get-MpThreat = "danh mục" mối đe dọa duy nhất (ThreatID/ThreatName/SeverityID/IsActive), khác
    // Get-MpThreatDetection là lịch sử TỪNG LẦN phát hiện - đã xác nhận qua tài liệu Microsoft Learn.
    // Bọc @() để tránh PowerShell 5.1 trả về 1 object đơn (không bọc mảng) khi chỉ có đúng 1 mối đe dọa.
    return QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$ProgressPreference = 'SilentlyContinue'\n"
        "try {\n"
        "  $result = @(Get-MpThreat -ErrorAction Stop | ForEach-Object {\n"
        "    [PSCustomObject]@{\n"
        "      ThreatID = [int64]$_.ThreatID\n"
        "      ThreatName = [string]$_.ThreatName\n"
        "      SeverityID = [int]$_.SeverityID\n"
        "      Resources = @($_.Resources | ForEach-Object { [string]$_ })\n"
        "      IsActive = [bool]$_.IsActive\n"
        "    }\n"
        "  })\n"
        "  $result | ConvertTo-Json -Compress -Depth 5\n"
        "} catch {\n"
        "  [Console]::Error.WriteLine($_.Exception.Message)\n"
        "  exit 1\n"
        "}\n");
}

QList<ThreatRecord> parseThreatsJson(const QByteArray& json, QString* error)
{
    QList<ThreatRecord> result;
    if (json.trimmed().isEmpty())
        return result; // không có mối đe dọa nào - không phải lỗi

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error) *error = "Không đọc được danh sách mối đe dọa (JSON lỗi): " + parseError.errorString();
        return result;
    }

    if (doc.isArray())
    {
        for (const QJsonValue& v : doc.array())
            if (v.isObject())
                result << threatFromJsonObject(v.toObject());
    }
    else if (doc.isObject())
    {
        // PowerShell 5.1: ConvertTo-Json không bọc mảng khi chỉ có đúng 1 phần tử, dù đã @() bọc trong
        // script - cùng hiện tượng đã gặp ở VpnController/PartitionManager, chủ động phòng vệ lại đây.
        result << threatFromJsonObject(doc.object());
    }
    else if (error)
    {
        *error = "Định dạng dữ liệu mối đe dọa không nhận ra được";
    }
    return result;
}

QString buildRemoveAllThreatsScript()
{
    // Remove-MpThreat KHÔNG có tham số chọn từng mục (đã xác nhận qua tài liệu Microsoft Learn) - luôn
    // xóa TẤT CẢ mối đe dọa đang hoạt động trên máy.
    return QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$ProgressPreference = 'SilentlyContinue'\n"
        "try {\n"
        "  Remove-MpThreat -ErrorAction Stop\n"
        "  Write-Output 'OK'\n"
        "} catch {\n"
        "  [Console]::Error.WriteLine($_.Exception.Message)\n"
        "  exit 1\n"
        "}\n");
}

} // namespace internal
} // namespace DefenderController
