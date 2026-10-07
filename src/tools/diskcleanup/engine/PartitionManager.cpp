#include "PartitionManager.h"

#include "core/PowerShellRunner.h"
#include "core/WinElevation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace
{
/// Dùng chung PowerShellRunner (src/core/) - xem ở đó lý do dùng -EncodedCommand thay vì stdin/-File.
QByteArray runPowerShell(const QString& script, bool* ok, QString* error, int timeoutMs = 20000)
{
    return PowerShellRunner::run(script, ok, error, timeoutMs);
}
} // namespace

namespace PartitionManager
{

bool isElevated()
{
    return WinElevation::isElevated();
}

bool relaunchElevated(QString* error)
{
    return WinElevation::relaunchElevated(error);
}

QList<PartitionInfo> listPartitions(QString* error)
{
    bool ok = false;
    QString runErr;
    const QByteArray json = runPowerShell(internal::buildListPartitionsScript(), &ok, &runErr);
    if (!ok)
    {
        if (error) *error = runErr;
        return {};
    }
    QString parseErr;
    const auto result = internal::parsePartitionsJson(json, &parseErr);
    if (!parseErr.isEmpty() && error)
        *error = parseErr;
    return result;
}

SupportedSizeRange querySupportedSize(int diskNumber, int partitionNumber, QString* error)
{
    bool ok = false;
    QString runErr;
    const QByteArray json = runPowerShell(internal::buildSupportedSizeScript(diskNumber, partitionNumber), &ok, &runErr);
    if (!ok)
    {
        SupportedSizeRange r;
        r.error = runErr;
        if (error) *error = runErr;
        return r;
    }
    QString parseErr;
    auto result = internal::parseSupportedSizeJson(json, &parseErr);
    if (!parseErr.isEmpty() && error)
        *error = parseErr;
    return result;
}

bool resizePartition(int diskNumber, int partitionNumber, qint64 newSizeBytes, QString* error)
{
    bool ok = false;
    QString runErr;
    runPowerShell(internal::buildResizeScript(diskNumber, partitionNumber, newSizeBytes), &ok, &runErr, 180000 /* 3 phút - co giãn ổ lớn có thể lâu */);
    if (!ok && error)
        *error = runErr;
    return ok;
}

namespace internal
{

QString buildListPartitionsScript()
{
    // $p.DriveLetter là System.Char: phân vùng không có ổ đĩa logic trả về ký tự NUL ([char]0), không
    // phải chuỗi rỗng - .Trim([char]0) chuẩn hóa về chuỗi rỗng để JSON phía C++ dễ xử lý.
    return QStringLiteral(
        "$ErrorActionPreference = 'Stop'\n"
        "$ProgressPreference = 'SilentlyContinue'\n" // tắt luồng Write-Progress - nếu không sẽ lẫn CLIXML vào stderr
        "try {\n"
        "  $result = @(Get-Partition | ForEach-Object {\n"
        "    $p = $_\n"
        "    $vol = $null\n"
        "    try { $vol = $p | Get-Volume -ErrorAction Stop } catch {}\n"
        "    [PSCustomObject]@{\n"
        "      DiskNumber = $p.DiskNumber\n"
        "      PartitionNumber = $p.PartitionNumber\n"
        "      DriveLetter = ([string]$p.DriveLetter).Trim([char]0)\n"
        "      Type = [string]$p.Type\n"
        "      Size = [int64]$p.Size\n"
        "      IsBoot = [bool]$p.IsBoot\n"
        "      IsSystem = [bool]$p.IsSystem\n"
        "      IsActive = [bool]$p.IsActive\n"
        "      FileSystem = if ($vol) { [string]$vol.FileSystem } else { '' }\n"
        "      Label = if ($vol) { [string]$vol.FileSystemLabel } else { '' }\n"
        "      SizeRemaining = if ($vol) { [int64]$vol.SizeRemaining } else { -1 }\n"
        "    }\n"
        "  })\n"
        "  $result | ConvertTo-Json -Compress -Depth 4\n"
        "} catch {\n"
        // [Console]::Error ghi thẳng ra luồng lỗi .NET, KHÔNG qua pipeline đối tượng của PowerShell -
        // Write-Error thì có, và khi stderr bị redirect (luôn đúng khi QProcess gọi), PowerShell tự
        // serialize nó thành CLIXML (XML thô kèm _x000D__x000A_...) thay vì văn bản đọc được.
        "  [Console]::Error.WriteLine($_.Exception.Message)\n"
        "  exit 1\n"
        "}\n");
}

QString buildSupportedSizeScript(int diskNumber, int partitionNumber)
{
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  $r = Get-PartitionSupportedSize -DiskNumber %1 -PartitionNumber %2\n"
               "  [PSCustomObject]@{ SizeMin = [int64]$r.SizeMin; SizeMax = [int64]$r.SizeMax } | ConvertTo-Json -Compress\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(diskNumber)
        .arg(partitionNumber);
}

QString buildResizeScript(int diskNumber, int partitionNumber, qint64 newSizeBytes)
{
    return QStringLiteral(
               "$ErrorActionPreference = 'Stop'\n"
               "$ProgressPreference = 'SilentlyContinue'\n"
               "try {\n"
               "  Resize-Partition -DiskNumber %1 -PartitionNumber %2 -Size %3\n"
               "} catch {\n"
               "  [Console]::Error.WriteLine($_.Exception.Message)\n"
               "  exit 1\n"
               "}\n")
        .arg(diskNumber)
        .arg(partitionNumber)
        .arg(newSizeBytes);
}

namespace
{
PartitionInfo partitionFromJsonObject(const QJsonObject& o)
{
    PartitionInfo p;
    p.diskNumber = o.value("DiskNumber").toInt(-1);
    p.partitionNumber = o.value("PartitionNumber").toInt(-1);
    p.driveLetter = o.value("DriveLetter").toString();
    p.type = o.value("Type").toString();
    p.sizeBytes = static_cast<qint64>(o.value("Size").toDouble(0));
    p.isBoot = o.value("IsBoot").toBool(false);
    p.isSystem = o.value("IsSystem").toBool(false);
    p.isActive = o.value("IsActive").toBool(false);
    p.fileSystem = o.value("FileSystem").toString();
    p.label = o.value("Label").toString();
    const qint64 remaining = static_cast<qint64>(o.value("SizeRemaining").toDouble(-1));
    p.freeBytes = remaining;
    return p;
}
} // namespace

QList<PartitionInfo> parsePartitionsJson(const QByteArray& json, QString* error)
{
    QList<PartitionInfo> result;
    if (json.trimmed().isEmpty())
        return result; // không có phân vùng nào (hiếm, nhưng không phải lỗi)

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error) *error = "Không đọc được dữ liệu phân vùng (JSON lỗi): " + parseError.errorString();
        return result;
    }

    if (doc.isArray())
    {
        for (const QJsonValue& v : doc.array())
            if (v.isObject())
                result << partitionFromJsonObject(v.toObject());
    }
    else if (doc.isObject())
    {
        // PowerShell 5.1: ConvertTo-Json không bọc mảng khi chỉ có đúng 1 phần tử - 1 object đơn.
        result << partitionFromJsonObject(doc.object());
    }
    else if (error)
    {
        *error = "Định dạng dữ liệu phân vùng không nhận ra được";
    }
    return result;
}

SupportedSizeRange parseSupportedSizeJson(const QByteArray& json, QString* error)
{
    SupportedSizeRange range;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        range.error = "Không đọc được kích thước cho phép (JSON lỗi)";
        if (error) *error = range.error;
        return range;
    }
    const QJsonObject o = doc.object();
    range.minBytes = static_cast<qint64>(o.value("SizeMin").toDouble(0));
    range.maxBytes = static_cast<qint64>(o.value("SizeMax").toDouble(0));
    range.ok = range.maxBytes > 0 && range.maxBytes >= range.minBytes;
    if (!range.ok && error)
        *error = "Kích thước cho phép không hợp lệ";
    return range;
}

} // namespace internal
} // namespace PartitionManager
