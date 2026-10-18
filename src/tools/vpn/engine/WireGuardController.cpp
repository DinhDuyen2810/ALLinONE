#include "WireGuardController.h"

#include "core/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

namespace
{
QString metaFilePath()
{
    return AppPaths::profileFile("wireguard_tunnels.json");
}

/// Trường hiển thị (KHÔNG bí mật) ứng dụng tự lưu cho một tunnel - nhãn quốc gia + các trường đọc được
/// từ .conf lúc nhập. KHÔNG BAO GIỜ chứa khóa riêng.
QHash<QString, WireGuardTunnel> loadMetas()
{
    QHash<QString, WireGuardTunnel> result;
    QFile f(metaFilePath());
    if (!f.open(QIODevice::ReadOnly))
        return result;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return result;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
    {
        if (!it.value().isObject())
            continue;
        const QJsonObject o = it.value().toObject();
        WireGuardTunnel t;
        t.name = it.key();
        t.countryLabel = o.value("country").toString();
        t.address = o.value("address").toString();
        t.dns = o.value("dns").toString();
        t.endpoint = o.value("endpoint").toString();
        t.allowedIps = o.value("allowedIps").toString();
        result[it.key()] = t;
    }
    return result;
}

/// Ghi NGUYÊN TỬ qua QSaveFile - cùng mẫu VpnController::saveProfileMetas().
bool saveMetas(const QHash<QString, WireGuardTunnel>& metas, QString* error)
{
    QJsonObject obj;
    for (auto it = metas.constBegin(); it != metas.constEnd(); ++it)
    {
        QJsonObject o;
        o["country"] = it.value().countryLabel;
        o["address"] = it.value().address;
        o["dns"] = it.value().dns;
        o["endpoint"] = it.value().endpoint;
        o["allowedIps"] = it.value().allowedIps;
        obj[it.key()] = o;
    }

    QSaveFile f(metaFilePath());
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
    {
        if (error)
            *error = "Không lưu được thông tin tunnel WireGuard vào " + f.fileName() + ": " + f.errorString();
        return false;
    }
    return true;
}

/// Chạy một tiến trình, trả về (exitCode==0) qua *ok, STDOUT qua giá trị trả về - cùng mẫu
/// AdbController::runAdb(), chỉ khác là dùng cho wireguard.exe/sc.exe thay vì adb.exe.
QString runProcess(const QString& exe, const QStringList& args, bool* ok, QString* error, int timeoutMs = 15000)
{
    *ok = false;
    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        if (error)
            *error = "Không khởi chạy được " + exe + ".";
        return {};
    }
    if (!proc.waitForFinished(timeoutMs))
    {
        proc.kill();
        proc.waitForFinished(2000);
        if (error)
            *error = "Hết thời gian chờ " + exe + ".";
        return {};
    }

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const QString err = QString::fromLocal8Bit(proc.readAllStandardError());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (error)
            *error = !err.trimmed().isEmpty() ? err.trimmed()
                                              : (!out.trimmed().isEmpty() ? out.trimmed()
                                                                          : QString("%1 thoát với mã lỗi %2").arg(exe).arg(proc.exitCode()));
        return out;
    }

    *ok = true;
    return out;
}

QString scExecutablePath()
{
    return AppPaths::systemExecutable("sc.exe");
}
} // namespace

namespace WireGuardController
{
QString executablePath()
{
    // WireGuard for Windows chỉ phát hành bản 64-bit, cài vào %ProgramFiles%\WireGuard - đường dẫn cố
    // định duy nhất của trình cài đặt chính thức (không cho chọn thư mục khác). Dùng biến môi trường
    // thay vì ghi cứng "C:/..." để vẫn đúng trên máy có %ProgramFiles% ở ổ khác.
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    if (!programFiles.isEmpty())
    {
        const QString candidate = programFiles + "/WireGuard/wireguard.exe";
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    const QString fallback = "C:/Program Files/WireGuard/wireguard.exe";
    if (QFileInfo::exists(fallback))
        return fallback;
    return {};
}

bool isInstalled()
{
    return !executablePath().isEmpty();
}

bool importTunnel(const QString& confFilePath, const QString& desiredName, const QString& countryLabel,
                  QString* resultName, QString* error)
{
    const QString exe = executablePath();
    if (exe.isEmpty())
    {
        if (error)
            *error = "Chưa cài WireGuard for Windows. Tải tại https://www.wireguard.com/install/ rồi thử lại.";
        return false;
    }

    QFile src(confFilePath);
    if (!src.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = "Không đọc được tệp .conf: " + src.errorString();
        return false;
    }
    const QByteArray confBytes = src.readAll();
    src.close();

    const QString name = internal::sanitizeTunnelName(desiredName);

    // wireguard.exe lấy TÊN TUNNEL từ chính tên tệp (không có đuôi .conf) - phải chép sang một tệp tạm
    // mang ĐÚNG tên đã chuẩn hóa, dù tệp người dùng chọn tên khác. Dùng QTemporaryDir để chắc chắn tệp
    // (có chứa khóa riêng dạng chữ) được xóa ngay khi hàm này kết thúc, dù thành công hay thất bại -
    // wireguard.exe đã đọc và tự mã hóa (DPAPI) nội dung vào kho cấu hình riêng của nó trước khi tiến
    // trình /installtunnelservice thoát, nên không cần giữ lại bản chữ thường nào sau bước này.
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        if (error)
            *error = "Không tạo được thư mục tạm.";
        return false;
    }
    const QString tempConfPath = tmp.filePath(name + ".conf");
    QFile dst(tempConfPath);
    if (!dst.open(QIODevice::WriteOnly) || dst.write(confBytes) != confBytes.size())
    {
        if (error)
            *error = "Không ghi được tệp cấu hình tạm: " + dst.errorString();
        return false;
    }
    dst.close();

    bool ok = false;
    QString psError;
    runProcess(exe, {"/installtunnelservice", QDir::toNativeSeparators(tempConfPath)}, &ok, &psError);
    if (!ok)
    {
        if (error)
            *error = "Cài tunnel thất bại (cần quyền Administrator): " + psError;
        return false;
    }

    // Chỉ lưu các trường KHÔNG bí mật để hiển thị - đọc lại từ chính nội dung vừa nhập.
    const internal::ParsedConf parsed = internal::parseConfForDisplay(QString::fromUtf8(confBytes));
    auto metas = loadMetas();
    WireGuardTunnel t;
    t.name = name;
    t.countryLabel = countryLabel;
    t.address = parsed.address;
    t.dns = parsed.dns;
    t.endpoint = parsed.endpoint;
    t.allowedIps = parsed.allowedIps;
    metas[name] = t;
    QString saveError;
    if (!saveMetas(metas, &saveError) && error)
        *error = "Đã cài tunnel nhưng không lưu được thông tin hiển thị: " + saveError;

    if (resultName)
        *resultName = name;
    return true;
}

bool removeTunnel(const QString& name, QString* error)
{
    const QString exe = executablePath();
    if (exe.isEmpty())
    {
        if (error)
            *error = "Chưa cài WireGuard for Windows.";
        return false;
    }

    bool ok = false;
    QString psError;
    runProcess(exe, {"/uninstalltunnelservice", name}, &ok, &psError);
    if (!ok)
    {
        if (error)
            *error = "Gỡ tunnel thất bại: " + psError;
        return false;
    }

    auto metas = loadMetas();
    QString saveError;
    if (metas.remove(name) > 0 && !saveMetas(metas, &saveError) && error)
        *error = "Đã gỡ tunnel nhưng không cập nhật được tệp thông tin hiển thị: " + saveError;
    return true;
}

bool startTunnel(const QString& name, QString* error)
{
    bool ok = false;
    QString scError;
    runProcess(scExecutablePath(), {"start", internal::serviceName(name)}, &ok, &scError);
    if (!ok && error)
        *error = "Không bật được tunnel: " + scError;
    return ok;
}

bool stopTunnel(const QString& name, QString* error)
{
    bool ok = false;
    QString scError;
    runProcess(scExecutablePath(), {"stop", internal::serviceName(name)}, &ok, &scError);
    if (!ok && error)
        *error = "Không tắt được tunnel: " + scError;
    return ok;
}

QList<WireGuardTunnel> listTunnels(QString* error)
{
    QList<WireGuardTunnel> result;
    const QString exe = executablePath();
    if (exe.isEmpty())
    {
        if (error)
            *error = "Chưa cài WireGuard for Windows.";
        return result;
    }

    const QString configDir = QFileInfo(exe).absolutePath() + "/Data/Configurations";
    QDir dir(configDir);
    if (!dir.exists())
    {
        // Chưa có tunnel nào từng được cài - KHÔNG phải lỗi.
        return result;
    }

    const QStringList files = dir.entryList(QStringList() << "*.conf.dpapi", QDir::Files);
    if (files.isEmpty() && error)
    {
        // entryList() trả rỗng cả khi KHÔNG đọc được thư mục (thiếu quyền) lẫn khi thư mục thật sự trống -
        // Qt không phân biệt hai trường hợp qua entryList(). Thử đọc thử MỘT lần để báo đúng nguyên nhân.
        QFileInfo probe(configDir);
        if (!probe.isReadable())
            *error = "Không đọc được thư mục cấu hình WireGuard (cần quyền Administrator): " + configDir;
    }

    const auto metas = loadMetas();
    for (const QString& fileName : files)
    {
        const QString name = internal::tunnelNameFromConfigFileName(fileName);
        if (name.isEmpty())
            continue;

        WireGuardTunnel t = metas.value(name);
        t.name = name;

        bool ok = false;
        const QString scOut = runProcess(scExecutablePath(), {"query", internal::serviceName(name)}, &ok, nullptr);
        t.isRunning = internal::isServiceRunningOutput(scOut);
        result.append(t);
    }
    return result;
}

void setCountryLabel(const QString& name, const QString& countryLabel)
{
    auto metas = loadMetas();
    WireGuardTunnel t = metas.value(name);
    t.name = name;
    t.countryLabel = countryLabel;
    metas[name] = t;
    saveMetas(metas, nullptr);
}

namespace internal
{
QString sanitizeTunnelName(const QString& rawName)
{
    QString out;
    out.reserve(rawName.size());
    for (const QChar c : rawName)
    {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) ||
                        (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) ||
                        (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                        c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.');
        out += ok ? c : QLatin1Char('_');
    }
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('.')))
        out.chop(1);
    if (out.size() > 32)
        out = out.left(32);
    if (out.isEmpty())
        out = QStringLiteral("tunnel");
    return out;
}

QString serviceName(const QString& tunnelName)
{
    return QStringLiteral("WireGuardTunnel$") + tunnelName;
}

ParsedConf parseConfForDisplay(const QString& confText)
{
    ParsedConf r;
    QString section;
    const QStringList lines = confText.split(QLatin1Char('\n'));
    for (QString line : lines)
    {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
        {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();
        if (section == QLatin1String("interface"))
        {
            if (key == QLatin1String("address"))
                r.address = value;
            else if (key == QLatin1String("dns"))
                r.dns = value;
        }
        else if (section == QLatin1String("peer"))
        {
            if (key == QLatin1String("endpoint"))
                r.endpoint = value;
            else if (key == QLatin1String("allowedips"))
                r.allowedIps = value;
        }
    }
    r.valid = !r.address.isEmpty() || !r.endpoint.isEmpty();
    if (!r.valid)
        r.error = "Không đọc được trường nào từ tệp .conf (có thể không đúng định dạng WireGuard).";
    return r;
}

bool isServiceRunningOutput(const QString& scQueryOutput)
{
    const QStringList lines = scQueryOutput.split(QLatin1Char('\n'));
    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith(QLatin1String("STATE"), Qt::CaseInsensitive))
            return trimmed.contains(QLatin1String("RUNNING"), Qt::CaseInsensitive) &&
                   !trimmed.contains(QLatin1String("STOP_PENDING"), Qt::CaseInsensitive);
    }
    return false;
}

QString tunnelNameFromConfigFileName(const QString& fileName)
{
    static const QString kSuffix = QStringLiteral(".conf.dpapi");
    if (fileName.endsWith(kSuffix, Qt::CaseInsensitive))
        return fileName.left(fileName.size() - kSuffix.size());
    return {};
}
} // namespace internal
} // namespace WireGuardController
