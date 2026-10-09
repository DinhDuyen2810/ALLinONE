#include "AdbController.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <vector>
#endif

namespace
{
QString bundleDir()
{
    return QCoreApplication::applicationDirPath() + "/scrcpy";
}

/// Chạy adb.exe với các đối số cho trước, trả về (exitCode==0) qua *ok, toàn bộ STDOUT qua giá trị trả
/// về và STDERR riêng qua *stdErr (nếu cần). -NoProfile kiểu PowerShell không áp dụng ở đây - adb là một
/// exe thường, không có các vấn đề CLIXML/stdin phức tạp từng gặp với PowerShell (xem PartitionManager.cpp).
///
/// KHÔNG ghép stdout+stderr vào một chuỗi như trước: adb in các dòng chẩn đoán ra stderr ("adb.exe:
/// failed to check server version...", "* daemon not running; starting now...") - ghép chung rồi đưa
/// cho parseDevicesOutput() thì mỗi dòng đó thành một "thiết bị" giả trong danh sách. Nơi nào cần đọc
/// cả hai (pair/connect) thì tự ghép.
QString runAdb(const QStringList& args, bool* ok, QString* error, int timeoutMs = 15000, QString* stdErr = nullptr)
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
    if (stdErr) *stdErr = err;
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (error) *error = !err.trimmed().isEmpty() ? err.trimmed() : QString("adb thoát với mã lỗi %1").arg(proc.exitCode());
        return out;
    }

    *ok = true;
    return out;
}

QString normalizedExecutablePath(const QString& path)
{
    QString p = QDir::fromNativeSeparators(path.trimmed());
    if (p.startsWith("//?/"))
        p = p.mid(4); // tiền tố đường dẫn dài của Win32 ("\\?\C:\...")

    // Phân giải liên kết tượng trưng khi tệp tồn tại. KHÔNG đủ để coi hai cách viết của cùng một tệp là
    // bằng nhau: canonicalFilePath() giữ nguyên tên ngắn 8.3 ("C:\PROGRA~1\...") - xem isSameFile().
    const QString canonical = QFileInfo(p).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? p : canonical);
}

/// Hai đường dẫn (đều phải mở được) có là CÙNG một tệp vật lý không - so danh tính tệp do hệ thống cấp (số
/// sê-ri ổ đĩa + chỉ số tệp). Bắt được các cách viết mà so chuỗi bỏ sót: tên ngắn 8.3, junction, ổ `subst`.
/// Đã xác nhận thật bằng core_stress_tests: so chuỗi coi "...\THUMUC~1\adb.exe" và đường dẫn dài của chính
/// tệp đó là hai tệp khác nhau - ứng dụng mở qua một đường dẫn ngắn thì daemon adb của nó không bao giờ được
/// dừng.
bool isSameFile(const QString& a, const QString& b)
{
#ifdef Q_OS_WIN
    const auto identity = [](const QString& path, BY_HANDLE_FILE_INFORMATION* info) {
        // Quyền truy cập 0 = chỉ hỏi thông tin: mở được cả tệp exe đang chạy.
        HANDLE h = CreateFileW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(path).utf16()), 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        const bool ok = GetFileInformationByHandle(h, info) != 0;
        CloseHandle(h);
        return ok;
    };
    BY_HANDLE_FILE_INFORMATION infoA{}, infoB{};
    return identity(a, &infoA) && identity(b, &infoB) && infoA.dwVolumeSerialNumber == infoB.dwVolumeSerialNumber &&
           infoA.nFileIndexHigh == infoB.nFileIndexHigh && infoA.nFileIndexLow == infoB.nFileIndexLow;
#else
    Q_UNUSED(a);
    Q_UNUSED(b);
    return false;
#endif
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

bool isValidIpAndPort(const QString& text)
{
    // máy = IPv6 trong ngoặc vuông, hoặc IPv4/tên máy (chữ, số, '.', '-'; bắt đầu và kết thúc bằng chữ/số
    // - tức KHÔNG bao giờ bắt đầu bằng '-'). Cổng bắt buộc, 1-65535. Neo bằng \A...\z chứ không phải
    // ^...$: '$' của biểu thức chính quy còn khớp NGAY TRƯỚC một ký tự xuống dòng ở cuối chuỗi.
    static const QRegularExpression re(
        QStringLiteral("\\A(?:\\[[0-9A-Fa-f:.]+\\]|[A-Za-z0-9](?:[A-Za-z0-9.-]{0,251}[A-Za-z0-9])?):([0-9]{1,5})\\z"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return false;
    const int port = m.captured(1).toInt();
    return port >= 1 && port <= 65535;
}

bool isValidPairingCode(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("\\A[0-9]{6}\\z"));
    return re.match(text).hasMatch();
}

bool pairWireless(const QString& ipAndPairPort, const QString& pairingCode, QString* error)
{
    if (!isValidIpAndPort(ipAndPairPort))
    {
        if (error) *error = "Địa chỉ ghép đôi không hợp lệ - cần dạng IP:Cổng, vd 192.168.1.23:41234.";
        return false;
    }
    if (!isValidPairingCode(pairingCode))
    {
        if (error) *error = "Mã ghép đôi không hợp lệ - cần đúng 6 chữ số hiển thị trên điện thoại.";
        return false;
    }

    bool ok = false;
    QString err;
    const QString out = runAdb({"pair", ipAndPairPort, pairingCode}, &ok, error, 20000, &err);
    // adb in kết quả pair/connect ra stdout nhưng lý do thất bại có thể nằm ở stderr - đọc cả hai.
    const QString all = out + err;
    if (ok && !all.contains("Successfully paired", Qt::CaseInsensitive))
    {
        // adb trả về exitCode 0 ngay cả khi ghép đôi thất bại (vd sai mã) - phải tự đọc nội dung.
        ok = false;
        if (error) *error = all.trimmed().isEmpty() ? "Ghép đôi thất bại - kiểm tra lại địa chỉ và mã." : all.trimmed();
    }
    return ok;
}

bool connectWireless(const QString& ipAndPort, QString* error)
{
    if (!isValidIpAndPort(ipAndPort))
    {
        if (error) *error = "Địa chỉ kết nối không hợp lệ - cần dạng IP:Cổng, vd 192.168.1.23:5555.";
        return false;
    }

    bool ok = false;
    QString err;
    const QString out = runAdb({"connect", ipAndPort}, &ok, error, 15000, &err);
    const QString all = out + err;
    if (ok && !all.contains("connected to", Qt::CaseInsensitive))
    {
        ok = false;
        if (error) *error = all.trimmed().isEmpty() ? "Kết nối thất bại." : all.trimmed();
    }
    return ok;
}

bool disconnectWireless(const QString& ipAndPort, QString* error)
{
    if (!isValidIpAndPort(ipAndPort))
    {
        if (error) *error = "Địa chỉ không hợp lệ - cần dạng IP:Cổng.";
        return false;
    }

    bool ok = false;
    runAdb({"disconnect", ipAndPort}, &ok, error);
    return ok;
}

int stopBundledAdbServer()
{
    const QString adbPath = adbExecutablePath();
    if (adbPath.isEmpty())
        return 0;
    return internal::terminateProcessesByImagePath(adbPath);
}

namespace internal
{

bool isKnownDeviceState(const QString& state)
{
    // Các tên trạng thái kết nối adb thật in ra ở cột thứ hai của `adb devices` (adb.cpp,
    // connection_state_name) + "no permissions" (hai từ, xử lý riêng trong parseDevicesOutput).
    static const QStringList states = {
        "device",   "unauthorized", "offline", "authorizing", "connecting", "recovery",
        "sideload", "bootloader",   "host",    "rescue",      "detached",   "unknown",
    };
    return states.contains(state);
}

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
        int firstExtra = 2;
        if (dev.state == "no" && tokens.size() >= 3 && tokens[2] == "permissions")
        {
            dev.state = "no permissions";
            firstExtra = 3;
        }
        else if (!isKnownDeviceState(dev.state))
        {
            // KHÔNG phải dòng thiết bị: một dòng thông báo/lỗi bất kỳ ("adb.exe: failed to check server
            // version", "error: cannot connect to daemon"...) cũng có >= 2 từ - trước đây mỗi dòng như
            // vậy thành một thiết bị giả với "serial" là từ đầu tiên của câu.
            continue;
        }

        for (int i = firstExtra; i < tokens.size(); ++i)
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

bool isSameExecutablePath(const QString& a, const QString& b)
{
    if (a.trimmed().isEmpty() || b.trimmed().isEmpty())
        return false;
    const QString normalizedA = normalizedExecutablePath(a);
    const QString normalizedB = normalizedExecutablePath(b);
    return normalizedA.compare(normalizedB, Qt::CaseInsensitive) == 0 || isSameFile(normalizedA, normalizedB);
}

int terminateProcessesByImagePath(const QString& exePath)
{
    int terminated = 0;
#ifdef Q_OS_WIN
    const QString wantedName = QFileInfo(QDir::fromNativeSeparators(exePath)).fileName();
    if (wantedName.isEmpty())
        return 0;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    std::vector<DWORD> candidates; // lọc sơ bộ theo TÊN tệp - rẻ, không cần mở tiến trình
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID != GetCurrentProcessId() &&
                QString::fromWCharArray(entry.szExeFile).compare(wantedName, Qt::CaseInsensitive) == 0)
            {
                candidates.push_back(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::vector<wchar_t> buffer(32768);
    for (const DWORD pid : candidates)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!process)
            continue; // đã thoát, hoặc thuộc người dùng/mức quyền khác - không phải của ta

        // So ĐƯỜNG DẪN ĐẦY ĐỦ đọc từ chính handle vừa mở (không phải từ snapshot): handle giữ đúng tiến
        // trình đó dù PID có bị tái sử dụng sau lúc chụp snapshot, và adb.exe của công cụ khác (Android
        // Studio, platform-tools trong PATH...) có cùng TÊN nhưng khác đường dẫn nên không bao giờ khớp.
        DWORD length = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length) &&
            isSameExecutablePath(QString::fromWCharArray(buffer.data(), static_cast<int>(length)), exePath))
        {
            if (TerminateProcess(process, 1))
            {
                WaitForSingleObject(process, 2000); // chờ thoát hẳn để khóa tệp adb.exe được nhả
                ++terminated;
            }
        }
        CloseHandle(process);
    }
#else
    Q_UNUSED(exePath);
#endif
    return terminated;
}

} // namespace internal
} // namespace AdbController
