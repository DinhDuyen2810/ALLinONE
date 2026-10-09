#include "PowerShellRunner.h"

#include "core/AppPaths.h"

#include <QProcess>
#include <QRegularExpression>

namespace
{
/// Mã hóa script thành dạng Base64(UTF-16LE) để truyền qua `-EncodedCommand` - cách Microsoft khuyến
/// nghị để chạy một script PowerShell phức tạp từ tiến trình khác. ĐÃ THỬ và loại bỏ 2 cách khác vì
/// phát hiện lỗi thật khi kiểm tra tay (khi xây PartitionManager - xem PROJECT_OVERVIEW.md mục 4f):
/// (1) truyền script nhiều dòng qua stdin với `-Command -` - powershell.exe thoát mã 0 nhưng KHÔNG in
/// gì cả (im lặng không chạy script) với script nhiều dòng/nhiều khối lệnh phức tạp, dù script y hệt
/// chạy đúng khi dán trực tiếp vào PowerShell; (2) ghi ra tệp .ps1 tạm rồi chạy bằng `-File` - gặp lỗi
/// "tệp đang được tiến trình khác sử dụng" (nhiều khả năng do phần mềm diệt virus quét tệp .ps1 vừa
/// tạo). `-EncodedCommand` không qua stdin, không tạo tệp tạm nào - tránh được cả hai vấn đề trên.
QString encodeCommand(const QString& script)
{
    QByteArray utf16le;
    utf16le.reserve(script.size() * 2);
    for (const QChar& c : script)
    {
        const ushort u = c.unicode();
        utf16le.append(static_cast<char>(u & 0xFF));
        utf16le.append(static_cast<char>((u >> 8) & 0xFF));
    }
    return QString::fromLatin1(utf16le.toBase64());
}

/// Chèn trước MỌI script:
///  - Ép stdout/stderr của powershell.exe sang UTF-8 KHÔNG BOM. Mặc định PowerShell 5.1 ghi theo
///    codepage OEM của máy, trong khi mọi nơi gọi đều phân tích kết quả bằng QJsonDocument (chỉ nhận
///    UTF-8) - tên kết nối VPN/nhãn ổ đĩa/đường dẫn mối đe dọa có dấu tiếng Việt làm hỏng JSON hoặc ra
///    sai tên (đã xác nhận thật: sau khi ép, "Hà Lan" ra đúng byte UTF-8, không kèm BOM).
///  - Tắt thanh tiến độ: khi stderr bị chuyển hướng, PowerShell tuần tự hóa cả bản ghi tiến độ ("Preparing
///    modules for first use") thành CLIXML lẫn vào thông báo lỗi.
const QString kPreamble = QStringLiteral(
    "$ProgressPreference='SilentlyContinue';"
    "try{$__u=New-Object System.Text.UTF8Encoding $false;[Console]::OutputEncoding=$__u;$OutputEncoding=$__u}catch{}; ");

QByteArray stripUtf8Bom(QByteArray data)
{
    static const QByteArray bom = QByteArray::fromHex("efbbbf");
    if (data.startsWith(bom))
        data.remove(0, 3);
    return data;
}

/// Bỏ khối CLIXML ("#< CLIXML" + một dòng <Objs ...>...</Objs>) nếu còn lọt vào stderr - phần chữ thường
/// do script tự ghi bằng [Console]::Error.WriteLine() được giữ nguyên.
QString cleanErrorText(const QByteArray& raw)
{
    QString text = QString::fromUtf8(stripUtf8Bom(raw));
    text.remove(QStringLiteral("#< CLIXML"));
    static const QRegularExpression objs(QStringLiteral("<Objs\\b.*?</Objs>"),
                                         QRegularExpression::DotMatchesEverythingOption);
    text.remove(objs);
    return text.trimmed();
}
} // namespace

namespace PowerShellRunner
{

QByteArray run(const QString& script, bool* ok, QString* error, int timeoutMs)
{
    return runCancelable(script, nullptr, ok, error, timeoutMs);
}

QByteArray runCancelable(const QString& script, const std::atomic_bool* cancelFlag, bool* ok, QString* error, int timeoutMs)
{
    *ok = false;
    QProcess proc;
    // Đường dẫn tuyệt đối trong System32 - không để Windows tự tìm "powershell.exe" theo thư mục ứng
    // dụng/CWD/PATH (xem AppPaths::systemExecutable).
    proc.setProgram(AppPaths::powershellExecutable());
    proc.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand",
                       encodeCommand(kPreamble + script)});
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        if (error) *error = "Không khởi chạy được powershell.exe";
        return {};
    }

    // Chờ theo từng bước nhỏ (200ms) thay vì một lần chờ dài duy nhất - cho phép kiểm tra cancelFlag
    // định kỳ để hủy SỚM thay vì phải đợi hết timeoutMs (quan trọng với các lệnh có thể chạy hàng giờ
    // như Start-MpScan quét toàn bộ máy).
    bool finished = false;
    bool canceled = false;
    for (int waited = 0; waited < timeoutMs; waited += 200)
    {
        finished = proc.waitForFinished(200);
        if (finished)
            break;
        if (cancelFlag && cancelFlag->load())
        {
            canceled = true;
            break;
        }
    }

    if (!finished)
    {
        proc.kill();
        proc.waitForFinished(2000);
        if (error) *error = canceled ? "Đã hủy." : "Hết thời gian chờ PowerShell";
        return stripUtf8Bom(proc.readAllStandardOutput());
    }

    const QByteArray out = stripUtf8Bom(proc.readAllStandardOutput());
    const QString err = cleanErrorText(proc.readAllStandardError());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (error) *error = !err.isEmpty() ? err : QString("PowerShell thoát với mã lỗi %1").arg(proc.exitCode());
        return out; // vẫn trả về output - một số lệnh in JSON rồi mới lỗi ở bước sau
    }

    *ok = true;
    return out;
}

QString encodedCommand(const QString& script)
{
    return encodeCommand(script);
}

QString quoteLiteral(const QString& value)
{
    QString out;
    out.reserve(value.size() + 8);
    for (const QChar c : value)
    {
        const ushort u = c.unicode();
        if (u == 0x0027 || u == 0x2018 || u == 0x2019 || u == 0x201A || u == 0x201B)
            out.append(c); // nhân đôi chính ký tự đó - cách PowerShell thoát dấu nháy trong chuỗi '...'
        out.append(c);
    }
    return out;
}

} // namespace PowerShellRunner
