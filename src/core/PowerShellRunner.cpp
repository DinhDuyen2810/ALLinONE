#include "PowerShellRunner.h"

#include <QProcess>

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
    proc.setProgram("powershell.exe");
    proc.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-EncodedCommand", encodeCommand(script)});
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
        return proc.readAllStandardOutput();
    }

    const QByteArray out = proc.readAllStandardOutput();
    const QByteArray err = proc.readAllStandardError();
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        if (error) *error = !err.isEmpty() ? QString::fromLocal8Bit(err) : QString("PowerShell thoát với mã lỗi %1").arg(proc.exitCode());
        return out; // vẫn trả về output - một số lệnh in JSON rồi mới lỗi ở bước sau
    }

    *ok = true;
    return out;
}

} // namespace PowerShellRunner
