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

/// Biểu thức khớp MỌI tiền tố hợp lệ của một tên tham số, từ `minLength` ký tự trở lên - PowerShell nhận
/// bất kỳ cách viết tắt không nhập nhằng nào (-EncodedCommand = -e/-en/-enc/-enco/...). Bản trước chỉ
/// liệt kê tay vài dạng (-e, -enc, -encodedcommand), nên "-en"/"-enco"/"-exec bypass"/"-win hidden" lọt.
/// Vd anyPrefixOf("enc", 1) -> "e(?:n(?:c)?)?".
QString anyPrefixOf(const QString& word, int minLength)
{
    QString tail;
    for (int i = word.size() - 1; i >= minLength; --i)
        tail = "(?:" + QString(word[i]) + tail + ")?";
    return word.left(minLength) + tail;
}

/// Danh sách mẫu dấu hiệu - tham khảo các kỹ thuật MITRE ATT&CK/Living-off-the-Land phổ biến trong mã
/// độc dùng PowerShell làm bệ phóng. Biên dịch 1 lần (static, thread-safe vì QRegularExpression không
/// đổi sau khi khởi tạo và hàm chỉ đọc).
const QList<Pattern>& patterns()
{
    static const QList<Pattern> list = [] {
        auto rx = [](const QString& p) {
            return QRegularExpression(p, QRegularExpression::CaseInsensitiveOption);
        };
        using RL = CommandAnalyzer::RiskLevel;

        // Dấu mở tham số đứng đầu một "từ": '-' và các dấu gạch Unicode (en/em dash, horizontal bar) mà
        // powershell.exe cũng nhận. Riêng '/' (powershell.exe cũng nhận) chỉ dùng trong các mẫu đã có chữ
        // "powershell" đứng trước - đứng riêng thì trùng quá nhiều công cụ khác (xcopy /e, schtasks /create).
        const QString dash = R"((?:^|(?<=\s))[-\x{2013}\x{2014}\x{2015}])";
        // "powershell ... <dấu mở tham số>" trên cùng một câu lệnh.
        const QString psThenOpt = R"((?:powershell|pwsh)(?:\.exe)?[^\n|;]*?\s[-/\x{2013}\x{2014}\x{2015}])";
        const QString enc = "(?:ec|" + anyPrefixOf("encodedcommand", 1) + ")";
        const QString execPolicy = "(?:ep|" + anyPrefixOf("executionpolicy", 2) + ")";
        const QString windowStyle = anyPrefixOf("windowstyle", 1);
        // Lệnh tải nội dung từ mạng: cmdlet đầy đủ, bí danh có sẵn của PowerShell (iwr/irm/curl/wget) và
        // curl.exe/wget.exe thật.
        const QString webCmd =
            R"((?:Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm|curl|wget)(?:\.exe)?(?![\w-])))";

        return QList<Pattern>{
            // ---- Tải về rồi thực thi ngay (download cradle) - kỹ thuật phổ biến nhất của mã độc ----
            {rx(R"(DownloadString|DownloadFile|DownloadData)"), RL::Dangerous,
             "Tải nội dung từ mạng (Net.WebClient/DownloadString...) - mẫu \"download cradle\" thường gặp trong mã độc"},
            // IEX đứng riêng như một từ ở BẤT KỲ đâu (sau dấu |, trước biến, trong &('iex')...) - bản
            // trước chỉ bắt "IEX (" nên "irm https://... | iex" (cradle phổ biến nhất hiện nay) lọt.
            {rx(R"((?<![\w-])IEX(?![\w-])|Invoke-Expression)"), RL::Dangerous,
             "Dùng Invoke-Expression/IEX để thực thi chuỗi lệnh động - kỹ thuật né tránh phát hiện phổ biến"},
            {rx(webCmd + R"([^\n]*\|\s*(?:&\s*)?\(?\s*['"]?(?:iex|Invoke-Expression|powershell|pwsh|cmd|bash|sh)(?:\.exe)?(?![\w-]))"),
             RL::Dangerous,
             "Tải nội dung từ mạng rồi chuyển thẳng (pipe) sang trình thực thi lệnh - chạy mã lấy từ Internet mà không qua bước xem lại nào"},
            {rx(webCmd + R"([\s\S]*?(?:Start-Process|(?<![\w-])saps(?![\w-])|Invoke-Item))"), RL::Dangerous,
             "Tải tệp từ mạng rồi chạy ngay (Start-Process/Invoke-Item) trong cùng một đoạn lệnh"},
            {rx(R"(Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm)(?![\w-])|(?<![\w-])(?:curl|wget)(?:\.exe)?\s[^\n]*https?://)"),
             RL::Suspicious,
             "Tải nội dung từ mạng (Invoke-WebRequest/Invoke-RestMethod/curl/wget) - thường hợp lệ, nhưng hãy chắc chắn bạn tin nguồn này"},
            {rx(R"(Start-BitsTransfer|bitsadmin(?:\.exe)?\s[^\n]*/(?:transfer|addfile|create|resume|setnotifycmdline))"),
             RL::Dangerous,
             "Tải tệp qua BITS (bitsadmin/Start-BitsTransfer) - kỹ thuật tải ngầm hay bị mã độc lợi dụng"},
            {rx(R"(certutil(?:\.exe)?\s[^\n]*[-/](?:urlcache|decode|decodehex|verifyctl))"), RL::Dangerous,
             "Dùng certutil để tải/giải mã tệp - LOLBin hay bị lợi dụng thay vì công cụ tải thông thường"},

            // ---- Lệnh bị mã hóa/che giấu (obfuscation) ----
            // (1) mọi tiền tố của -EncodedCommand + chuỗi Base64 dài, ở bất kỳ đâu; (2) có chữ "powershell"
            // đứng trước thì chuỗi ngắn cũng tính ("powershell -e YwBhAGwAYwA=" chỉ 12 ký tự).
            {rx(dash + enc + R"(\s+['"]?[A-Za-z0-9+/=]{40,}|)" + psThenOpt + enc + R"(\s+['"]?[A-Za-z0-9+/=]{8,})"),
             RL::Dangerous,
             "Lệnh PowerShell dạng mã hóa Base64 (-EncodedCommand/-enc) - thường dùng để che giấu nội dung thật"},
            {rx(R"(FromBase64String)"), RL::Dangerous,
             "Giải mã Base64 trong lệnh - thường kết hợp với kỹ thuật che giấu mã độc"},
            {rx(R"((?:\[char\]\s*(?:0x[0-9a-f]+|\d+)\s*[+,]?\s*){4,})"), RL::Dangerous,
             "Ghép chuỗi từ nhiều mã ký tự [char] - kỹ thuật che giấu (obfuscation) nội dung lệnh thật"},
            // Dấu ` chen giữa tên lệnh (Down`loadString) - PowerShell bỏ qua nó, mắt người và bộ lọc thì
            // không. Loại trừ các chuỗi thoát thật (`n `t `r `0 `a `b `e `f `v `u).
            {rx(R"([a-z]`(?![ntr0abefvu])[a-z])"), RL::Suspicious,
             "Chèn dấu ` vào giữa tên lệnh/tham số - cách viết PowerShell vẫn hiểu nhưng thường dùng để che mắt người đọc và bộ lọc"},
            {rx(R"(\[(?:System\.)?Reflection\.Assembly\]::Load\()"), RL::Suspicious,
             "Nạp một assembly .NET trực tiếp từ bộ nhớ (Reflection.Assembly::Load) - có thể hợp lệ nhưng hay dùng để chạy mã không để lại tệp"},

            // ---- Bỏ qua chính sách thực thi / ẩn cửa sổ ----
            {rx(dash + execPolicy + R"(\s+['"]?(?:bypass|unrestricted)|)" + psThenOpt + execPolicy +
                R"(\s+['"]?(?:bypass|unrestricted)|Set-ExecutionPolicy\s[^\n;|]*(?:bypass|unrestricted))"),
             RL::Suspicious, "Bỏ qua chính sách thực thi PowerShell (-ExecutionPolicy Bypass/Unrestricted)"},
            {rx(dash + windowStyle + R"(\s+['"]?hidden|)" + psThenOpt + windowStyle +
                R"(\s+['"]?(?:hidden|hidde|hidd|hid|hi|h|1)(?!\w))"),
             RL::Suspicious,
             "Chạy ẩn cửa sổ (-WindowStyle Hidden) - thường dùng để người dùng không nhận ra có tiến trình đang chạy"},

            // ---- Công cụ tấn công đã biết ----
            {rx(R"(Invoke-Mimikatz|Invoke-ReflectivePEInjection|Invoke-Shellcode|Invoke-TokenManipulation|Invoke-DllInjection|Invoke-Kerberoast|Invoke-BloodHound|Invoke-PowerShellTcp|Invoke-PsExec|Invoke-WMIExec|Invoke-SMBExec|Get-GPPPassword|Out-Minidump|(?<![\w-])mimikatz|sekurlsa::|lsadump::)"),
             RL::Dangerous, "Tên hàm trùng với công cụ tấn công đã biết (PowerSploit/Empire/Mimikatz...)"},
            {rx(R"(AmsiUtils|amsiInitFailed|\[Ref\]\.Assembly\.GetType\(.*Amsi)"), RL::Dangerous,
             "Cố gắng vô hiệu hóa AMSI (Antimalware Scan Interface) - kỹ thuật né tránh diệt virus phổ biến"},

            // ---- Né tránh phòng thủ ----
            // Mọi giá trị KHÁC $false/0 đều tính ($true, 1, :$true, biến, biểu thức...) và nhận cả tên tham
            // số viết tắt (-DisableRea...) - bản trước chỉ bắt đúng "-DisableRealtimeMonitoring $true". Các
            // lượng từ "*+"/"?+" (chiếm hữu, không quay lui) là BẮT BUỘC: nếu để regex quay lui nhả bớt
            // khoảng trắng/chữ cái thì phép nhìn trước phủ định bên dưới luôn qua được và "$false" cũng bị báo.
            {rx(R"(Set-MpPreference\b[^\n;|]*-Disable(?:Rea|Beh|IOAV|Scr|Blo|Int|Arc|Ema|Rem)[a-z]*+\s*+:?+\s*+(?!(?:\$false|false|0)(?!\w))|DisableAntiSpyware|DisableAntiVirus)"),
             RL::Dangerous,
             "Tự tắt một lớp bảo vệ của Windows Defender (bảo vệ thời gian thực/giám sát hành vi/quét tệp tải về...)"},
            {rx(R"(Set-MpPreference\b[^\n;|]*-(?:MAPSReporting|EnableNetworkProtection|EnableControlledFolderAccess|PUAProtection)\s*:?\s*['"]?(?:0|Disabled)(?!\w))"),
             RL::Suspicious, "Hạ mức bảo vệ của Windows Defender (tắt bảo vệ đám mây/Network Protection/bảo vệ thư mục/chặn PUA)"},
            {rx(R"((?:Add|Set)-MpPreference\b[^\n;|]*-Exclusion(?:Path|Process|Extension|IpAddress)|-AttackSurfaceReductionOnlyExclusions)"),
             RL::Suspicious,
             "Thêm mục loại trừ quét virus (ExclusionPath/Process/Extension) - có thể hợp lệ nhưng cũng hay bị mã độc lợi dụng để tự ẩn"},
            {rx(R"(netsh\s+advfirewall\s+set\s+\w+\s+state\s+off|netsh\s+firewall\s+set\s+opmode\s+(?:mode\s*=\s*)?disable|Set-NetFirewallProfile\b[^\n;|]*-Enabled\s*:?\s*['"]?(?:\$?false|0)(?!\w))"),
             RL::Dangerous, "Tự tắt Tường lửa Windows"},
            {rx(R"(Stop-Service\s+.*(?:WinDefend|wscsvc|MpsSvc|Sense|WdNisSvc)|(?<![\w-])(?:sc(?:\.exe)?\s+(?:stop|config|delete)|net1?(?:\.exe)?\s+stop)\s+['"]?(?:WinDefend|wscsvc|MpsSvc|Sense|WdNisSvc)(?!\w))"),
             RL::Dangerous,
             "Cố dừng dịch vụ bảo vệ hệ thống (Windows Defender/Trung tâm bảo mật/Tường lửa)"},
            {rx(R"(vssadmin(?:\.exe)?\s[^\n]*(?:delete\s+shadows|resize\s+shadowstorage)|wbadmin(?:\.exe)?\s[^\n]*delete\s+(?:catalog|systemstatebackup|backup)|wmic(?:\.exe)?\s[^\n]*shadowcopy\s[^\n]*delete|Win32_ShadowCopy[\s\S]{0,200}?(?:\.Delete\(|Remove-(?:Wmi|Cim)(?:Object|Instance))|bcdedit(?:\.exe)?\s[^\n]*(?:recoveryenabled\s+no|bootstatuspolicy\s+ignoreallfailures))"),
             RL::Dangerous,
             "Xóa bản sao lưu bóng (shadow copy)/bản sao lưu hệ thống hoặc tắt khôi phục khởi động - dấu hiệu điển hình của mã độc tống tiền"},
            {rx(R"(wevtutil(?:\.exe)?\s+(?:cl|clear-log)\s|Clear-EventLog|Remove-EventLog)"), RL::Suspicious,
             "Xóa nhật ký sự kiện Windows - có thể hợp lệ nhưng cũng là cách xóa dấu vết sau khi xâm nhập"},

            // ---- Thiết lập duy trì (persistence) ----
            {rx(R"(schtasks(\.exe)?\s+.*\/create|Register-ScheduledTask)"), RL::Suspicious,
             "Tạo tác vụ lên lịch (schtasks /create, Register-ScheduledTask) - có thể hợp lệ nhưng cũng là cách mã độc tự duy trì"},
            {rx(R"(reg(\.exe)?\s+add\s+.*\\Run\b|(?:New|Set)-ItemProperty\b[^\n]*\\CurrentVersion\\Run)"), RL::Suspicious,
             "Thêm khóa registry Run - cách phổ biến để chương trình tự khởi động cùng Windows"},
            {rx(R"(Register-WmiEvent|__EventFilter|__EventConsumer)"), RL::Dangerous,
             "Thiết lập WMI Event Subscription - kỹ thuật duy trì (persistence) nâng cao ít khi dùng trong tác vụ thông thường"},

            // ---- Mã lệnh shell từ xa ----
            {rx(R"(New-Object\s+System\.Net\.Sockets\.TCPClient|/dev/tcp/)"), RL::Dangerous,
             "Mẫu mã mở kết nối mạng thô (reverse shell) trực tiếp qua socket"},
            {rx(R"(mshta(\.exe)?\s+|regsvr32(?:\.exe)?\s[^\n]*(?:[-/]i:\s*['"]?(?:https?:|ftp:|\\\\)|scrobj\.dll))"), RL::Dangerous,
             "Dùng mshta/regsvr32 để chạy mã từ xa - LOLBin hay bị lợi dụng để né tránh phát hiện"},
            {rx(R"(rundll32(?:\.exe)?\s[^\n]*(?:javascript:|vbscript:|https?://|\\\\|mshtml\s*,\s*RunHTMLApplication|url(?:\.dll)?\s*,\s*(?:OpenURL|FileProtocolHandler)|shell32(?:\.dll)?\s*,\s*ShellExec_RunDLL|advpack(?:\.dll)?\s*,\s*(?:LaunchINFSection|RegisterOCX)|zipfldr(?:\.dll)?\s*,\s*RouteTheCall|pcwutl(?:\.dll)?\s*,\s*LaunchApplication|comsvcs(?:\.dll)?\s*,?\s*(?:MiniDump|#24)))"),
             RL::Dangerous,
             "Dùng rundll32 để chạy mã từ xa/chạy chương trình gián tiếp/kết xuất bộ nhớ - LOLBin hay bị lợi dụng để né tránh phát hiện"},
        };
    }();
    return list;
}
} // namespace

namespace CommandAnalyzer
{

namespace internal
{
QString normalizeForMatching(const QString& command)
{
    QString s = command;

    // Dấu ` cuối dòng = nối dòng; ở chỗ khác nó là ký tự thoát mà PowerShell bỏ qua trước chữ thường
    // (Down`loadString chạy y như DownloadString).
    static const QRegularExpression lineContinuation("`\\r?\\n[ \\t]*");
    s.replace(lineContinuation, " ");
    s.remove('`');
    // Dấu ^ là ký tự thoát của cmd.exe (p^owershell -e^nc ...).
    s.remove('^');
    // Ghép chuỗi đơn giản: 'Down'+'loadString', ("IE"+"X").
    static const QRegularExpression concatenation(R"(['"]\s*\+\s*['"])");
    s.remove(concatenation);
    // Cặp nháy rỗng chen giữa tên lệnh: i''ex, I""EX.
    s.remove("''");
    s.remove("\"\"");
    return s;
}
} // namespace internal

Verdict analyze(const QString& command)
{
    Verdict v;
    if (command.trimmed().isEmpty())
        return v;

    // So khớp trên CẢ nguyên văn lẫn bản đã gỡ các lớp che giấu đơn giản - một mẫu khớp ở bản nào cũng
    // tính. Giữ nguyên văn để không mẫu nào đang khớp bị mất đi vì bước chuẩn hóa.
    const QString normalized = internal::normalizeForMatching(command);
    const bool hasNormalizedForm = normalized != command;

    for (const auto& p : patterns())
    {
        if (p.regex.match(command).hasMatch() || (hasNormalizedForm && p.regex.match(normalized).hasMatch()))
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
