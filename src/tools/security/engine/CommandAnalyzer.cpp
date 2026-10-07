#include "CommandAnalyzer.h"

#include <QRegularExpression>
#include <algorithm>

namespace
{
struct Pattern
{
    QRegularExpression regex;
    CommandAnalyzer::RiskLevel level;
    const char* reason; // tiếng Việt, giải thích vì sao khớp
};

/// Danh sách mẫu dấu hiệu - tham khảo các kỹ thuật MITRE ATT&CK/Living-off-the-Land phổ biến trong mã
/// độc dùng PowerShell làm bệ phóng. Biên dịch 1 lần (static, thread-safe vì QRegularExpression không
/// đổi sau khi khởi tạo và hàm chỉ đọc).
const QList<Pattern>& patterns()
{
    static const QList<Pattern> list = [] {
        auto rx = [](const char* p) {
            return QRegularExpression(QString::fromUtf8(p), QRegularExpression::CaseInsensitiveOption);
        };
        using RL = CommandAnalyzer::RiskLevel;
        return QList<Pattern>{
            // ---- Tải về rồi thực thi ngay (download cradle) - kỹ thuật phổ biến nhất của mã độc ----
            {rx(R"(DownloadString|DownloadFile|DownloadData)"), RL::Dangerous,
             "Tải nội dung từ mạng (Net.WebClient/DownloadString...) - mẫu \"download cradle\" thường gặp trong mã độc"},
            {rx(R"(IEX\s*\(|Invoke-Expression)"), RL::Dangerous,
             "Dùng Invoke-Expression/IEX để thực thi chuỗi lệnh động - kỹ thuật né tránh phát hiện phổ biến"},
            {rx(R"(Start-BitsTransfer|bitsadmin\s+/transfer)"), RL::Dangerous,
             "Tải tệp qua BITS (bitsadmin/Start-BitsTransfer) - kỹ thuật tải ngầm hay bị mã độc lợi dụng"},
            {rx(R"(certutil(\.exe)?\s+.*-urlcache|certutil(\.exe)?\s+.*-decode)"), RL::Dangerous,
             "Dùng certutil để tải/giải mã tệp - LOLBin hay bị lợi dụng thay vì công cụ tải thông thường"},

            // ---- Lệnh bị mã hóa/che giấu (obfuscation) ----
            {rx(R"(-e(nc(odedcommand)?)?\s+[A-Za-z0-9+/=]{40,})"), RL::Dangerous,
             "Lệnh PowerShell dạng mã hóa Base64 (-EncodedCommand/-enc) - thường dùng để che giấu nội dung thật"},
            {rx(R"(FromBase64String)"), RL::Dangerous,
             "Giải mã Base64 trong lệnh - thường kết hợp với kỹ thuật che giấu mã độc"},
            {rx(R"((\[char\]\s*\d+\s*){5,})"), RL::Dangerous,
             "Ghép chuỗi từ nhiều mã ký tự [char] - kỹ thuật che giấu (obfuscation) nội dung lệnh thật"},

            // ---- Bỏ qua chính sách thực thi / ẩn cửa sổ ----
            {rx(R"(-ExecutionPolicy\s+Bypass|-ep\s+bypass)"), RL::Suspicious,
             "Bỏ qua chính sách thực thi PowerShell (-ExecutionPolicy Bypass)"},
            {rx(R"(-WindowStyle\s+Hidden|-w(indowstyle)?\s+hidden|-W\s+Hidden)"), RL::Suspicious,
             "Chạy ẩn cửa sổ (-WindowStyle Hidden) - thường dùng để người dùng không nhận ra có tiến trình đang chạy"},

            // ---- Công cụ tấn công đã biết ----
            {rx(R"(Invoke-Mimikatz|Invoke-ReflectivePEInjection|Invoke-Shellcode|Invoke-TokenManipulation|Invoke-DllInjection)"),
             RL::Dangerous, "Tên hàm trùng với công cụ tấn công đã biết (PowerSploit/Empire...)"},
            {rx(R"(AmsiUtils|amsiInitFailed|\[Ref\]\.Assembly\.GetType\(.*Amsi)"), RL::Dangerous,
             "Cố gắng vô hiệu hóa AMSI (Antimalware Scan Interface) - kỹ thuật né tránh diệt virus phổ biến"},

            // ---- Né tránh phòng thủ ----
            {rx(R"(Set-MpPreference\s+.*-DisableRealtimeMonitoring\s+\$true)"), RL::Dangerous,
             "Tự tắt bảo vệ thời gian thực của Windows Defender"},
            {rx(R"(Add-MpPreference\s+.*-ExclusionPath)"), RL::Suspicious,
             "Thêm thư mục loại trừ quét virus (ExclusionPath) - có thể hợp lệ nhưng cũng hay bị mã độc lợi dụng để tự ẩn"},
            {rx(R"(netsh\s+advfirewall\s+set\s+allprofiles\s+state\s+off)"), RL::Dangerous,
             "Tự tắt Tường lửa Windows trên mọi profile"},
            {rx(R"(Stop-Service\s+.*(WinDefend|wscsvc|MpsSvc))"), RL::Dangerous,
             "Cố dừng dịch vụ bảo vệ hệ thống (Windows Defender/Trung tâm bảo mật/Tường lửa)"},

            // ---- Thiết lập duy trì (persistence) ----
            {rx(R"(schtasks(\.exe)?\s+.*\/create)"), RL::Suspicious,
             "Tạo tác vụ lên lịch (schtasks /create) - có thể hợp lệ nhưng cũng là cách mã độc tự duy trì"},
            {rx(R"(reg(\.exe)?\s+add\s+.*\\Run\b)"), RL::Suspicious,
             "Thêm khóa registry Run - cách phổ biến để chương trình tự khởi động cùng Windows"},
            {rx(R"(Register-WmiEvent|__EventFilter|__EventConsumer)"), RL::Dangerous,
             "Thiết lập WMI Event Subscription - kỹ thuật duy trì (persistence) nâng cao ít khi dùng trong tác vụ thông thường"},

            // ---- Mã lệnh shell từ xa ----
            {rx(R"(New-Object\s+System\.Net\.Sockets\.TCPClient|/dev/tcp/)"), RL::Dangerous,
             "Mẫu mã mở kết nối mạng thô (reverse shell) trực tiếp qua socket"},
            {rx(R"(mshta(\.exe)?\s+|regsvr32(\.exe)?\s+.*\/i:https?:)"), RL::Dangerous,
             "Dùng mshta/regsvr32 để chạy mã từ xa - LOLBin hay bị lợi dụng để né tránh phát hiện"},
        };
    }();
    return list;
}
} // namespace

namespace CommandAnalyzer
{

Verdict analyze(const QString& command)
{
    Verdict v;
    if (command.trimmed().isEmpty())
        return v;

    for (const auto& p : patterns())
    {
        if (p.regex.match(command).hasMatch())
        {
            v.reasons << QString::fromUtf8(p.reason);
            if (p.level > v.level)
                v.level = p.level;
        }
    }
    return v;
}

QString riskLevelLabel(RiskLevel level)
{
    switch (level)
    {
        case RiskLevel::Safe: return "An toàn";
        case RiskLevel::Suspicious: return "Đáng chú ý";
        case RiskLevel::Dangerous: return "Nguy hiểm";
    }
    return {};
}

} // namespace CommandAnalyzer
