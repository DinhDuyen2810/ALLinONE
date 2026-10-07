#include "AdbController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace
{
QString bundleDir()
{
    return QCoreApplication::applicationDirPath() + "/scrcpy";
}

/// Chạy adb.exe với các đối số cho trước, trả về (exitCode==0) qua *ok, toàn bộ stdout qua giá trị
/// trả về. -NoProfile kiểu PowerShell không áp dụng ở đây - adb là một exe thường, không có các vấn đề
/// CLIXML/stdin phức tạp từng gặp với PowerShell (xem PartitionManager.cpp).
QString runAdb(const QStringList& args, bool* ok, QString* error, int timeoutMs = 15000)
{
    *ok = false;
    const QString adbPath = AdbController::adbExecutablePath();
    if (adbPath.isEmpty())
    {
        if (error) *error = "Không tìm thấy adb.exe (thư mục scrcpy/ cạnh file chạy bị thiếu).";
        return {};
    }

    QProcess proc;
    proc.setProgram(adbPath);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        if (error) *error = "Không khởi chạy được adb.exe.";
        return {};
    }
    if (!proc.waitForFinished(timeoutMs))
    {
        proc.kill();
        proc.waitForFinished(2000);
        if (error) *error = "Hết thời gian chờ adb.";
        return {};
    }

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString err = QString::fromLocal8Bit(proc.readAllStandardError());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (error) *error = !err.trimmed().isEmpty() ? err.trimmed() : QString("adb thoát với mã lỗi %1").arg(proc.exitCode());
        return out;
    }

    *ok = true;
    // Một số lệnh adb (pair/connect) in kết quả ra stdout, một số in CẢNH BÁO vô hại ra stderr
    // ("* daemon not running; starting now..."), nên ghép cả hai để không mất thông tin khi caller cần
    // đọc nội dung (vd thông báo "Paired with ...").
    return out + err;
}
} // namespace

namespace AdbController
{

QString adbExecutablePath()
{
    const QString p = bundleDir() + "/adb.exe";
    return QFileInfo::exists(p) ? p : QString();
}

QString scrcpyExecutablePath()
{
    const QString p = bundleDir() + "/scrcpy.exe";
    return QFileInfo::exists(p) ? p : QString();
}

bool isBundleAvailable(QString* missingWhat)
{
    const bool hasAdb = !adbExecutablePath().isEmpty();
    const bool hasScrcpy = !scrcpyExecutablePath().isEmpty();
    if (missingWhat)
    {
        if (!hasAdb && !hasScrcpy) *missingWhat = "adb.exe và scrcpy.exe";
        else if (!hasAdb) *missingWhat = "adb.exe";
        else if (!hasScrcpy) *missingWhat = "scrcpy.exe";
    }
    return hasAdb && hasScrcpy;
}

QList<AndroidDeviceInfo> listDevices(QString* error)
{
    bool ok = false;
    QString runErr;
    const QString out = runAdb({"devices", "-l"}, &ok, &runErr);
    if (!ok)
    {
        if (error) *error = runErr;
        return {};
    }
    return internal::parseDevicesOutput(out);
}

bool pairWireless(const QString& ipAndPairPort, const QString& pairingCode, QString* error)
{
    bool ok = false;
    const QString out = runAdb({"pair", ipAndPairPort, pairingCode}, &ok, error, 20000);
    if (ok && !out.contains("Successfully paired", Qt::CaseInsensitive))
    {
        // adb trả về exitCode 0 ngay cả khi ghép đôi thất bại (vd sai mã) - phải tự đọc nội dung.
        ok = false;
        if (error) *error = out.trimmed().isEmpty() ? "Ghép đôi thất bại - kiểm tra lại địa chỉ và mã." : out.trimmed();
    }
    return ok;
}

bool connectWireless(const QString& ipAndPort, QString* error)
{
    bool ok = false;
    const QString out = runAdb({"connect", ipAndPort}, &ok, error, 15000);
    if (ok && !out.contains("connected to", Qt::CaseInsensitive))
    {
        ok = false;
        if (error) *error = out.trimmed().isEmpty() ? "Kết nối thất bại." : out.trimmed();
    }
    return ok;
}

bool disconnectWireless(const QString& ipAndPort, QString* error)
{
    bool ok = false;
    runAdb({"disconnect", ipAndPort}, &ok, error);
    return ok;
}

namespace internal
{

QList<AndroidDeviceInfo> parseDevicesOutput(const QString& output)
{
    QList<AndroidDeviceInfo> result;
    const QStringList lines = output.split('\n');
    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith("List of devices attached") || line.startsWith('*'))
            continue; // bỏ dòng tiêu đề + cảnh báo "* daemon not running..."

        const QStringList tokens = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (tokens.size() < 2)
            continue; // dòng không đúng định dạng "<serial> <state> [key:value...]"

        AndroidDeviceInfo dev;
        dev.serial = tokens[0];
        dev.state = tokens[1];

        for (int i = 2; i < tokens.size(); ++i)
        {
            const int colon = tokens[i].indexOf(':');
            if (colon < 0)
                continue;
            const QString key = tokens[i].left(colon);
            const QString value = tokens[i].mid(colon + 1);
            if (key == "model") dev.model = value;
            else if (key == "product") dev.product = value;
            else if (key == "transport_id") dev.transportId = value;
        }

        result << dev;
    }
    return result;
}

} // namespace internal
} // namespace AdbController
