#include "PartitionManager.h"

#include "core/PowerShellRunner.h"
#include "core/WinElevation.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cmath>
#include <limits>

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
    // KHÔNG đặt giới hạn thời gian thực tế cho thao tác này: PowerShellRunner sẽ kill() powershell.exe
    // khi hết thời gian chờ, mà thu nhỏ một ổ lớn/chậm (Windows phải di chuyển dữ liệu) có thể mất rất
    // lâu - giới hạn 3 phút trước đây vừa báo "thất bại" sai, vừa cắt ngang một thao tác đĩa đang chạy.
    // 7 ngày: đủ dài để coi như vô hạn, vẫn nằm trong kiểu int của tham số (đơn vị ms).
    constexpr int kNoPracticalTimeoutMs = 7 * 24 * 60 * 60 * 1000;
    runPowerShell(internal::buildResizeScript(diskNumber, partitionNumber, newSizeBytes), &ok, &runErr, kNoPracticalTimeoutMs);
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
/// double -> qint64 CÓ BÃO HÒA. Ép kiểu thẳng một double ngoài khoảng qint64 (JSON hỏng/bị cắt cụt cho ra
/// 1e300, hay NaN/vô cực từ phép tính) là hành vi KHÔNG XÁC ĐỊNH trong C++ - trên x86-64 thực tế cho ra
/// INT64_MIN, tức một "kích thước" âm khổng lồ đi tiếp vào các phép so khoảng.
qint64 saturatedInt64(double value)
{
    if (std::isnan(value))
        return 0;
    if (value >= 9223372036854775807.0)
        return std::numeric_limits<qint64>::max();
    if (value <= -9223372036854775808.0)
        return std::numeric_limits<qint64>::min();
    return static_cast<qint64>(value);
}

PartitionInfo partitionFromJsonObject(const QJsonObject& o)
{
    PartitionInfo p;
    p.diskNumber = o.value("DiskNumber").toInt(-1);
    p.partitionNumber = o.value("PartitionNumber").toInt(-1);
    p.driveLetter = o.value("DriveLetter").toString();
    p.type = o.value("Type").toString();
    p.sizeBytes = saturatedInt64(o.value("Size").toDouble(0));
    p.isBoot = o.value("IsBoot").toBool(false);
    p.isSystem = o.value("IsSystem").toBool(false);
    p.isActive = o.value("IsActive").toBool(false);
    p.fileSystem = o.value("FileSystem").toString();
    p.label = o.value("Label").toString();
    const qint64 remaining = saturatedInt64(o.value("SizeRemaining").toDouble(-1));
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
    range.minBytes = saturatedInt64(o.value("SizeMin").toDouble(0));
    range.maxBytes = saturatedInt64(o.value("SizeMax").toDouble(0));
    range.ok = range.minBytes >= 0 && range.maxBytes > 0 && range.maxBytes >= range.minBytes;
    if (!range.ok && error)
        *error = "Kích thước cho phép không hợp lệ";
    return range;
}

qint64 clampResizeBytes(double sizeGb, const SupportedSizeRange& range)
{
    // Khoảng không hợp lệ hoặc giá trị không phải số: trả về 0 - resizeBlockReason() từ chối mọi kích
    // thước <= 0, nên không bao giờ thành một lệnh đổi kích thước thật.
    if (!range.ok || range.minBytes < 0 || range.maxBytes < range.minBytes || std::isnan(sizeGb))
        return 0;
    const qint64 bytes = saturatedInt64(sizeGb * 1024.0 * 1024.0 * 1024.0);
    return qBound(range.minBytes, bytes, range.maxBytes);
}

QString resizeBlockReason(int diskNumber, int partitionNumber, qint64 sizeAtQueryBytes,
                          const QList<PartitionInfo>& currentPartitions, const SupportedSizeRange& currentRange,
                          qint64 newSizeBytes)
{
    if (diskNumber < 0 || partitionNumber < 0)
        return "Chưa xác định được phân vùng cần đổi kích thước.";
    // Kích thước lúc tra <= 0 nghĩa là lúc đó KHÔNG đọc được kích thước (JSON thiếu trường Size) - phép
    // đối chiếu "vẫn là phân vùng đó" bên dưới sẽ thành 0 == 0, tức không đối chiếu được gì.
    if (sizeAtQueryBytes <= 0)
        return "Không xác định được kích thước của phân vùng lúc tra - hãy bấm Làm mới và tra lại.";

    const PartitionInfo* current = nullptr;
    for (const PartitionInfo& p : currentPartitions)
    {
        if (p.diskNumber == diskNumber && p.partitionNumber == partitionNumber)
        {
            current = &p;
            break;
        }
    }
    if (!current)
        return "Không còn thấy phân vùng này trên máy - danh sách đĩa đã thay đổi. Hãy bấm Làm mới và thử lại.";
    if (current->sizeBytes != sizeAtQueryBytes)
        return "Kích thước hiện tại của phân vùng đã khác lúc tra (danh sách đĩa/phân vùng đã thay đổi). Hãy "
               "bấm Làm mới, chọn lại phân vùng và tra lại kích thước.";
    if (!currentRange.ok || currentRange.minBytes < 0 || currentRange.maxBytes < currentRange.minBytes)
        return "Không xác nhận lại được khoảng kích thước Windows cho phép.";
    if (newSizeBytes <= 0 || newSizeBytes < currentRange.minBytes || newSizeBytes > currentRange.maxBytes)
        return QString("Kích thước mới (%1 byte) nằm ngoài khoảng Windows cho phép (%2 - %3 byte).")
            .arg(newSizeBytes)
            .arg(currentRange.minBytes)
            .arg(currentRange.maxBytes);
    if (newSizeBytes == current->sizeBytes)
        return "Kích thước mới bằng kích thước hiện tại - không có gì để đổi.";
    return {};
}

bool isSameSizeAtInputPrecision(double sizeGb, qint64 currentBytes)
{
    if (!std::isfinite(sizeGb) || std::fabs(sizeGb) > 1e12) // ngoài khoảng này llround() không còn xác định
        return false;
    const double currentGb = static_cast<double>(currentBytes) / (1024.0 * 1024.0 * 1024.0);
    return std::llround(sizeGb * 100.0) == std::llround(currentGb * 100.0);
}

} // namespace internal
} // namespace PartitionManager
