#include "CommandAnalyzer.h"

#include <QRegularExpression>
#include <algorithm>

namespace
{
/// Phạm vi của phần "..." nằm giữa các mảnh của một Chain.
enum class Gap
{
    SameLine,      ///< [^\n]*   - không vượt qua dấu xuống dòng
    SameStatement, ///< [^\n|;]* - không vượt qua dấu xuống dòng, dấu | và dấu ;
    Anywhere,      ///< [\s\S]*  - tới hết văn bản
};

/// Mẫu dạng "ĐẦU ... MẢNH [... MẢNH]": các mảnh xuất hiện đúng thứ tự, phần "..." giữa hai mảnh liền nhau
/// chỉ gồm ký tự trong phạm vi `gap`. Về KẾT QUẢ thì đúng bằng biểu thức `head GAP* part1 GAP* part2`, nhưng
/// được so khớp bằng chainMatches() bên dưới thay vì để bộ máy regex tự quay lui.
///
/// Lý do (đo THẬT khi stress test): viết liền thành một biểu thức, vd `curl[^\n]*\|\s*iex`, thì với MỖI chữ
/// "curl" bộ máy regex quét lại tới hết dòng rồi mới chịu thua - thời gian tăng theo BÌNH PHƯƠNG độ dài.
/// 45 nghìn ký tự "curl curl curl ..." trên một dòng đã mất hơn 2 giây, 1 MB là hàng chục phút treo giao
/// diện; quá giới hạn so khớp của PCRE2 thì nó trả về "không khớp", tức âm thầm báo An toàn.
///
/// Điều kiện để hai cách cho cùng kết quả (mọi mẫu trong patterns() đều thỏa - giữ đúng khi thêm mẫu mới):
///  - một mảnh không thể bắt đầu ở bên trong phần co giãn cuối mảnh đứng trước nó (".exe", khoảng trắng);
///  - mảnh ĐỨNG GIỮA (khi có hai mảnh) có độ dài cố định và không chứa dấu ngắt của `gap`, trừ khi nó chỉ
///    có thể nằm ở đúng cuối đoạn (xem hai luật wmic).
struct Chain
{
    QRegularExpression head;
    Gap gap;
    QList<QRegularExpression> parts;
    bool headMaySpanBreak; ///< Phần đầu có thể chứa chính dấu ngắt của `gap` (có `\s`, tức khớp được '\n')
};

struct Pattern
{
    QList<QRegularExpression> plain; ///< Biểu thức thường - khớp MỘT cái là đủ
    QList<Chain> chains;             ///< ... hoặc một mẫu "đầu ... đuôi" - khớp MỘT cái là đủ
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
///
/// Trong các biểu thức, lượng từ chiếm hữu (`*+`, `?+`, `++`) đặt ở những chỗ hai `\s*` đứng cạnh nhau chỉ
/// cách bởi một mảnh tùy chọn (`\s*:?\s*`): không có nó, một dải khoảng trắng dài bị thử chia đôi theo mọi
/// cách (bậc hai). Chúng không đổi tập chuỗi khớp - phần "nhả lại" không bao giờ được mảnh nào khác nhận.
const QList<Pattern>& patterns()
{
    static const QList<Pattern> list = [] {
        auto rx = [](const QString& p) {
            return QRegularExpression(p, QRegularExpression::CaseInsensitiveOption);
        };
        auto chain = [&rx](const QString& head, Gap gap, const QStringList& parts) {
            Chain c;
            c.head = rx(head);
            c.gap = gap;
            for (const QString& part : parts)
                c.parts << rx(part);
            c.headMaySpanBreak = head.contains("\\s");
            return c;
        };
        using RL = CommandAnalyzer::RiskLevel;

        // Dấu mở tham số đứng đầu một "từ": '-' và các dấu gạch Unicode (en/em dash, horizontal bar) mà
        // powershell.exe cũng nhận. Riêng '/' (powershell.exe cũng nhận) chỉ dùng trong các mẫu đã có chữ
        // "powershell" đứng trước - đứng riêng thì trùng quá nhiều công cụ khác (xcopy /e, schtasks /create).
        const QString dash = R"((?:^|(?<=\s))[-\x{2013}\x{2014}\x{2015}])";
        // "powershell ... <dấu mở tham số>" trên cùng một câu lệnh: Chain(psHead, SameStatement, optStart + ...).
        const QString psHead = R"((?:powershell|pwsh)(?:\.exe)?)";
        const QString optStart = R"(\s[-/\x{2013}\x{2014}\x{2015}])";
        const QString enc = "(?:ec|" + anyPrefixOf("encodedcommand", 1) + ")";
        const QString execPolicy = "(?:ep|" + anyPrefixOf("executionpolicy", 2) + ")";
        const QString windowStyle = anyPrefixOf("windowstyle", 1);
        // Lệnh tải nội dung từ mạng: cmdlet đầy đủ, bí danh có sẵn của PowerShell (iwr/irm/curl/wget) và
        // curl.exe/wget.exe thật.
        const QString webCmd =
            R"((?:Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm|curl|wget)(?:\.exe)?(?![\w-])))";

        return QList<Pattern>{
            // ---- Tải về rồi thực thi ngay (download cradle) - kỹ thuật phổ biến nhất của mã độc ----
            {{rx(R"(DownloadString|DownloadFile|DownloadData)")}, {}, RL::Dangerous,
             "Tải nội dung từ mạng (Net.WebClient/DownloadString...) - mẫu \"download cradle\" thường gặp trong mã độc"},
            // IEX đứng riêng như một từ ở BẤT KỲ đâu (sau dấu |, trước biến, trong &('iex')...) - bản
            // trước chỉ bắt "IEX (" nên "irm https://... | iex" (cradle phổ biến nhất hiện nay) lọt.
            {{rx(R"((?<![\w-])IEX(?![\w-])|Invoke-Expression)")}, {}, RL::Dangerous,
             "Dùng Invoke-Expression/IEX để thực thi chuỗi lệnh động - kỹ thuật né tránh phát hiện phổ biến"},
            {{},
             {chain(webCmd, Gap::SameLine,
                    {R"(\|\s*+(?:&\s*+)?+\(?+\s*+['"]?+(?:iex|Invoke-Expression|powershell|pwsh|cmd|bash|sh)(?:\.exe)?(?![\w-]))"})},
             RL::Dangerous,
             "Tải nội dung từ mạng rồi chuyển thẳng (pipe) sang trình thực thi lệnh - chạy mã lấy từ Internet mà không qua bước xem lại nào"},
            {{}, {chain(webCmd, Gap::Anywhere, {R"(Start-Process|(?<![\w-])saps(?![\w-])|Invoke-Item)"})}, RL::Dangerous,
             "Tải tệp từ mạng rồi chạy ngay (Start-Process/Invoke-Item) trong cùng một đoạn lệnh"},
            {{rx(R"(Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm)(?![\w-]))")},
             {chain(R"((?<![\w-])(?:curl|wget)(?:\.exe)?\s)", Gap::SameLine, {R"(https?://)"})},
             RL::Suspicious,
             "Tải nội dung từ mạng (Invoke-WebRequest/Invoke-RestMethod/curl/wget) - thường hợp lệ, nhưng hãy chắc chắn bạn tin nguồn này"},
            {{rx(R"(Start-BitsTransfer)")},
             {chain(R"(bitsadmin(?:\.exe)?\s)", Gap::SameLine, {R"(/(?:transfer|addfile|create|resume|setnotifycmdline))"})},
             RL::Dangerous,
             "Tải tệp qua BITS (bitsadmin/Start-BitsTransfer) - kỹ thuật tải ngầm hay bị mã độc lợi dụng"},
            {{}, {chain(R"(certutil(?:\.exe)?\s)", Gap::SameLine, {R"([-/](?:urlcache|decode|decodehex|verifyctl))"})},
             RL::Dangerous,
             "Dùng certutil để tải/giải mã tệp - LOLBin hay bị lợi dụng thay vì công cụ tải thông thường"},

            // ---- Lệnh bị mã hóa/che giấu (obfuscation) ----
            // (1) mọi tiền tố của -EncodedCommand + chuỗi Base64 dài, ở bất kỳ đâu; (2) có chữ "powershell"
            // đứng trước thì chuỗi ngắn cũng tính ("powershell -e YwBhAGwAYwA=" chỉ 12 ký tự).
            {{rx(dash + enc + R"(\s+['"]?[A-Za-z0-9+/=]{40,})")},
             {chain(psHead, Gap::SameStatement, {optStart + enc + R"(\s+['"]?[A-Za-z0-9+/=]{8,})"})},
             RL::Dangerous,
             "Lệnh PowerShell dạng mã hóa Base64 (-EncodedCommand/-enc) - thường dùng để che giấu nội dung thật"},
            {{rx(R"(FromBase64String)")}, {}, RL::Dangerous,
             "Giải mã Base64 trong lệnh - thường kết hợp với kỹ thuật che giấu mã độc"},
            // Đúng 4 cụm liền nhau là đủ kết luận ("{4}" chứ không phải "{4,}": hàng trăm nghìn cụm liền nhau
            // làm bộ máy regex cạn ngăn xếp rồi trả về lỗi, tức không báo gì).
            {{rx(R"((?:\[char\]\s*+(?:0x[0-9a-f]++|\d++)\s*+[+,]?+\s*+){4})")}, {}, RL::Dangerous,
             "Ghép chuỗi từ nhiều mã ký tự [char] - kỹ thuật che giấu (obfuscation) nội dung lệnh thật"},
            // Dấu ` chen giữa tên lệnh (Down`loadString) - PowerShell bỏ qua nó, mắt người và bộ lọc thì
            // không. Loại trừ các chuỗi thoát thật (`n `t `r `0 `a `b `e `f `v `u).
            {{rx(R"([a-z]`(?![ntr0abefvu])[a-z])")}, {}, RL::Suspicious,
             "Chèn dấu ` vào giữa tên lệnh/tham số - cách viết PowerShell vẫn hiểu nhưng thường dùng để che mắt người đọc và bộ lọc"},
            {{rx(R"(\[(?:System\.)?Reflection\.Assembly\]::Load\()")}, {}, RL::Suspicious,
             "Nạp một assembly .NET trực tiếp từ bộ nhớ (Reflection.Assembly::Load) - có thể hợp lệ nhưng hay dùng để chạy mã không để lại tệp"},

            // ---- Bỏ qua chính sách thực thi / ẩn cửa sổ ----
            {{rx(dash + execPolicy + R"(\s+['"]?(?:bypass|unrestricted))")},
             {chain(psHead, Gap::SameStatement, {optStart + execPolicy + R"(\s+['"]?(?:bypass|unrestricted))"}),
              chain(R"(Set-ExecutionPolicy\s)", Gap::SameStatement, {R"(bypass|unrestricted)"})},
             RL::Suspicious, "Bỏ qua chính sách thực thi PowerShell (-ExecutionPolicy Bypass/Unrestricted)"},
            {{rx(dash + windowStyle + R"(\s+['"]?hidden)")},
             {chain(psHead, Gap::SameStatement,
                    {optStart + windowStyle + R"(\s+['"]?(?:hidden|hidde|hidd|hid|hi|h|1)(?!\w))"})},
             RL::Suspicious,
             "Chạy ẩn cửa sổ (-WindowStyle Hidden) - thường dùng để người dùng không nhận ra có tiến trình đang chạy"},

            // ---- Công cụ tấn công đã biết ----
            {{rx(R"(Invoke-Mimikatz|Invoke-ReflectivePEInjection|Invoke-Shellcode|Invoke-TokenManipulation|Invoke-DllInjection|Invoke-Kerberoast|Invoke-BloodHound|Invoke-PowerShellTcp|Invoke-PsExec|Invoke-WMIExec|Invoke-SMBExec|Get-GPPPassword|Out-Minidump|(?<![\w-])mimikatz|sekurlsa::|lsadump::)")},
             {}, RL::Dangerous, "Tên hàm trùng với công cụ tấn công đã biết (PowerSploit/Empire/Mimikatz...)"},
            {{rx(R"(AmsiUtils|amsiInitFailed)")},
             {chain(R"(\[Ref\]\.Assembly\.GetType\()", Gap::SameLine, {R"(Amsi)"})}, RL::Dangerous,
             "Cố gắng vô hiệu hóa AMSI (Antimalware Scan Interface) - kỹ thuật né tránh diệt virus phổ biến"},

            // ---- Né tránh phòng thủ ----
            // Mọi giá trị KHÁC $false/0 đều tính ($true, 1, :$true, biến, biểu thức...) và nhận cả tên tham
            // số viết tắt (-DisableRea...) - bản trước chỉ bắt đúng "-DisableRealtimeMonitoring $true". Các
            // lượng từ "*+"/"?+" (chiếm hữu, không quay lui) là BẮT BUỘC: nếu để regex quay lui nhả bớt
            // khoảng trắng/chữ cái thì phép nhìn trước phủ định bên dưới luôn qua được và "$false" cũng bị báo.
            {{rx(R"(DisableAntiSpyware|DisableAntiVirus)")},
             {chain(R"(Set-MpPreference\b)", Gap::SameStatement,
                    {R"(-Disable(?:Rea|Beh|IOAV|Scr|Blo|Int|Arc|Ema|Rem)[a-z]*+\s*+:?+\s*+(?!(?:\$false|false|0)(?!\w)))"})},
             RL::Dangerous,
             "Tự tắt một lớp bảo vệ của Windows Defender (bảo vệ thời gian thực/giám sát hành vi/quét tệp tải về...)"},
            {{},
             {chain(R"(Set-MpPreference\b)", Gap::SameStatement,
                    {R"(-(?:MAPSReporting|EnableNetworkProtection|EnableControlledFolderAccess|PUAProtection)\s*+:?+\s*+['"]?+(?:0|Disabled)(?!\w))"})},
             RL::Suspicious, "Hạ mức bảo vệ của Windows Defender (tắt bảo vệ đám mây/Network Protection/bảo vệ thư mục/chặn PUA)"},
            {{rx(R"(-AttackSurfaceReductionOnlyExclusions)")},
             {chain(R"((?:Add|Set)-MpPreference\b)", Gap::SameStatement, {R"(-Exclusion(?:Path|Process|Extension|IpAddress))"})},
             RL::Suspicious,
             "Thêm mục loại trừ quét virus (ExclusionPath/Process/Extension) - có thể hợp lệ nhưng cũng hay bị mã độc lợi dụng để tự ẩn"},
            {{rx(R"(netsh\s+advfirewall\s+set\s+\w+\s+state\s+off|netsh\s+firewall\s+set\s+opmode\s+(?:mode\s*=\s*)?disable)")},
             {chain(R"(Set-NetFirewallProfile\b)", Gap::SameStatement, {R"(-Enabled\s*+:?+\s*+['"]?+(?:\$?false|0)(?!\w))"})},
             RL::Dangerous, "Tự tắt Tường lửa Windows"},
            {{rx(R"((?<![\w-])(?:sc(?:\.exe)?\s+(?:stop|config|delete)|net1?(?:\.exe)?\s+stop)\s+['"]?(?:WinDefend|wscsvc|MpsSvc|Sense|WdNisSvc)(?!\w))")},
             {chain(R"(Stop-Service\s+)", Gap::SameLine, {R"(WinDefend|wscsvc|MpsSvc|Sense|WdNisSvc)"})},
             RL::Dangerous,
             "Cố dừng dịch vụ bảo vệ hệ thống (Windows Defender/Trung tâm bảo mật/Tường lửa)"},
            // wmic ... shadowcopy<khoảng trắng> ... delete: tách hai luật theo khoảng trắng sau "shadowcopy" -
            // là dấu xuống dòng thì phần "... delete" nằm ở dòng KẾ (đúng như biểu thức liền mạch trước đây).
            {{rx(R"(Win32_ShadowCopy[\s\S]{0,200}?(?:\.Delete\(|Remove-(?:Wmi|Cim)(?:Object|Instance)))")},
             {chain(R"(vssadmin(?:\.exe)?\s)", Gap::SameLine, {R"(delete\s+shadows|resize\s+shadowstorage)"}),
              chain(R"(wbadmin(?:\.exe)?\s)", Gap::SameLine, {R"(delete\s+(?:catalog|systemstatebackup|backup))"}),
              chain(R"(wmic(?:\.exe)?\s)", Gap::SameLine, {R"(shadowcopy[^\S\n])", R"(delete)"}),
              chain(R"(wmic(?:\.exe)?\s)", Gap::SameLine, {R"(shadowcopy\n)", R"(delete)"}),
              chain(R"(bcdedit(?:\.exe)?\s)", Gap::SameLine, {R"(recoveryenabled\s+no|bootstatuspolicy\s+ignoreallfailures)"})},
             RL::Dangerous,
             "Xóa bản sao lưu bóng (shadow copy)/bản sao lưu hệ thống hoặc tắt khôi phục khởi động - dấu hiệu điển hình của mã độc tống tiền"},
            {{rx(R"(wevtutil(?:\.exe)?\s+(?:cl|clear-log)\s|Clear-EventLog|Remove-EventLog)")}, {}, RL::Suspicious,
             "Xóa nhật ký sự kiện Windows - có thể hợp lệ nhưng cũng là cách xóa dấu vết sau khi xâm nhập"},

            // ---- Thiết lập duy trì (persistence) ----
            {{rx(R"(Register-ScheduledTask)")}, {chain(R"(schtasks(?:\.exe)?\s+)", Gap::SameLine, {R"(\/create)"})},
             RL::Suspicious,
             "Tạo tác vụ lên lịch (schtasks /create, Register-ScheduledTask) - có thể hợp lệ nhưng cũng là cách mã độc tự duy trì"},
            {{},
             {chain(R"(reg(?:\.exe)?\s+add\s+)", Gap::SameLine, {R"(\\Run\b)"}),
              chain(R"((?:New|Set)-ItemProperty\b)", Gap::SameLine, {R"(\\CurrentVersion\\Run)"})},
             RL::Suspicious,
             "Thêm khóa registry Run - cách phổ biến để chương trình tự khởi động cùng Windows"},
            {{rx(R"(Register-WmiEvent|__EventFilter|__EventConsumer)")}, {}, RL::Dangerous,
             "Thiết lập WMI Event Subscription - kỹ thuật duy trì (persistence) nâng cao ít khi dùng trong tác vụ thông thường"},

            // ---- Mã lệnh shell từ xa ----
            {{rx(R"(New-Object\s+System\.Net\.Sockets\.TCPClient|/dev/tcp/)")}, {}, RL::Dangerous,
             "Mẫu mã mở kết nối mạng thô (reverse shell) trực tiếp qua socket"},
            {{rx(R"(mshta(?:\.exe)?\s+)")},
             {chain(R"(regsvr32(?:\.exe)?\s)", Gap::SameLine, {R"([-/]i:\s*['"]?(?:https?:|ftp:|\\\\)|scrobj\.dll)"})},
             RL::Dangerous,
             "Dùng mshta/regsvr32 để chạy mã từ xa - LOLBin hay bị lợi dụng để né tránh phát hiện"},
            {{},
             {chain(R"(rundll32(?:\.exe)?\s)", Gap::SameLine,
                    {R"(javascript:|vbscript:|https?://|\\\\|mshtml\s*,\s*RunHTMLApplication|url(?:\.dll)?\s*,\s*(?:OpenURL|FileProtocolHandler)|shell32(?:\.dll)?\s*,\s*ShellExec_RunDLL|advpack(?:\.dll)?\s*,\s*(?:LaunchINFSection|RegisterOCX)|zipfldr(?:\.dll)?\s*,\s*RouteTheCall|pcwutl(?:\.dll)?\s*,\s*LaunchApplication|comsvcs(?:\.dll)?\s*+,?+\s*+(?:MiniDump|#24))"})},
             RL::Dangerous,
             "Dùng rundll32 để chạy mã từ xa/chạy chương trình gián tiếp/kết xuất bộ nhớ - LOLBin hay bị lợi dụng để né tránh phát hiện"},
        };
    }();
    return list;
}

/// Đầu vào đã qua withoutLoneSurrogates() nên bỏ được bước PCRE2 kiểm tra lại tính hợp lệ UTF-16 của TOÀN
/// BỘ chuỗi ở mỗi lần gọi (chainMatches gọi match() nhiều lần trên cùng một chuỗi).
constexpr auto kNoRecheck = QRegularExpression::DontCheckSubjectStringMatchOption;

/// Vị trí kết thúc dải ký tự thuộc phạm vi `gap` bắt đầu từ `from` (= vị trí dấu ngắt đầu tiên, hoặc hết chuỗi).
qsizetype gapEnd(const QString& text, qsizetype from, Gap gap)
{
    if (gap == Gap::Anywhere)
        return text.size();
    for (qsizetype i = from; i < text.size(); ++i)
    {
        const char16_t c = text[i].unicode();
        if (c == u'\n' || (gap == Gap::SameStatement && (c == u'|' || c == u';')))
            return i;
    }
    return text.size();
}

/// So khớp một Chain trong thời gian TUYẾN TÍNH theo độ dài văn bản. `*incomplete` = true nếu bộ máy regex
/// báo lỗi (vượt giới hạn...) - nơi gọi không được coi đó là "không khớp".
bool chainMatches(const Chain& chain, const QString& text, bool* incomplete)
{
    /// Lần khớp ĐẦU TIÊN của một mảnh kể từ một vị trí - nhớ lại để các lần hỏi sau (từ vị trí nằm giữa
    /// `from` và `start`) không phải quét lại đoạn đã quét.
    struct Next
    {
        qsizetype from{-1};
        qsizetype start{-1}; ///< -1 = không còn lần khớp nào kể từ `from`
        qsizetype end{-1};
    };
    QList<Next> cache(chain.parts.size());

    auto nextMatch = [&](int part, qsizetype from) -> const Next& {
        Next& n = cache[part];
        if (n.from >= 0 && from >= n.from && (n.start < 0 || from <= n.start))
            return n;
        const QRegularExpressionMatch m = chain.parts[part].match(text, from, QRegularExpression::NormalMatch, kNoRecheck);
        if (!m.isValid())
            *incomplete = true;
        n.from = from;
        n.start = m.hasMatch() ? m.capturedStart() : -1;
        n.end = m.hasMatch() ? m.capturedEnd() : -1;
        return n;
    };

    // Mọi phần đầu kết thúc trong [.., failedUpTo] đã biết chắc là không có đuôi: chúng nhìn thấy đúng cái
    // đoạn mà phần đầu đứng trước vừa tìm hết rồi.
    qsizetype failedUpTo = -1;
    qsizetype searchFrom = 0;
    while (searchFrom <= text.size())
    {
        const QRegularExpressionMatch head = chain.head.match(text, searchFrom, QRegularExpression::NormalMatch, kNoRecheck);
        if (!head.isValid())
            *incomplete = true;
        if (!head.hasMatch())
            return false;
        searchFrom = head.capturedStart() + 1;
        if (head.capturedEnd() <= failedUpTo)
            continue;

        const qsizetype firstGapEnd = gapEnd(text, head.capturedEnd(), chain.gap);
        qsizetype pos = head.capturedEnd();
        bool found = true;
        for (int i = 0; i < chain.parts.size(); ++i)
        {
            // Mảnh được phép bắt đầu ở ngay chính dấu ngắt (vd `\s` khớp dấu xuống dòng) - như bản regex liền.
            const Next& n = nextMatch(i, pos);
            if (n.start < 0 || n.start > (i == 0 ? firstGapEnd : gapEnd(text, pos, chain.gap)))
            {
                found = false;
                break;
            }
            pos = n.end;
        }
        if (found)
            return true;

        failedUpTo = firstGapEnd;
        // Phần đầu không chứa được dấu ngắt thì mọi phần đầu bắt đầu trước dấu ngắt cũng kết thúc trước nó -
        // nhảy thẳng qua cả đoạn thay vì tìm lại từng cái (hàng trăm nghìn chữ "curl" trên một dòng).
        if (!chain.headMaySpanBreak)
            searchFrom = qMax(searchFrom, failedUpTo);
    }
    return false;
}

bool patternMatches(const Pattern& pattern, const QString& text, bool* incomplete)
{
    for (const QRegularExpression& regex : pattern.plain)
    {
        const QRegularExpressionMatch m = regex.match(text, 0, QRegularExpression::NormalMatch, kNoRecheck);
        if (m.hasMatch())
            return true;
        if (!m.isValid())
            *incomplete = true;
    }
    for (const Chain& chain : pattern.chains)
    {
        if (chainMatches(chain, text, incomplete))
            return true;
    }
    return false;
}

/// Thay mọi nửa cặp thay thế (surrogate) MỒ CÔI bằng U+FFFD. Chuỗi UTF-16 không hợp lệ làm QRegularExpression
/// từ chối so khớp và trả về "không khớp" cho MỌI mẫu - dán kèm đúng một ký tự như vậy (Clipboard của
/// Windows chứa được) là cả đoạn lệnh, dù độc hại tới đâu, được báo An toàn (lỗi thật tìm thấy khi stress test).
QString withoutLoneSurrogates(const QString& text)
{
    QString out = text;
    for (qsizetype i = 0; i < out.size(); ++i)
    {
        const QChar c = out.at(i);
        if (c.isHighSurrogate() && i + 1 < out.size() && out.at(i + 1).isLowSurrogate())
            ++i; // cặp hợp lệ
        else if (c.isSurrogate())
            out[i] = QChar(QChar::ReplacementCharacter);
    }
    return out;
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
    const QString original = withoutLoneSurrogates(command);
    const QString normalized = internal::normalizeForMatching(original);
    const bool hasNormalizedForm = normalized != original;

    bool incomplete = false;
    for (const auto& p : patterns())
    {
        if (patternMatches(p, original, &incomplete) || (hasNormalizedForm && patternMatches(p, normalized, &incomplete)))
        {
            v.reasons << QString::fromUtf8(p.reason);
            if (p.level > v.level)
                v.level = p.level;
        }
    }

    // Bộ máy regex bỏ cuộc giữa chừng (vượt giới hạn so khớp/ngăn xếp) = CHƯA kiểm tra xong, không phải
    // "không có dấu hiệu" - không bao giờ để trường hợp này lặng lẽ ra kết quả An toàn.
    if (incomplete)
    {
        v.reasons << "Không phân tích hết được đoạn lệnh này (quá dài hoặc cấu trúc bất thường) - kết quả có thể "
                     "thiếu, hãy tự xem kỹ trước khi chạy";
        if (v.level < RiskLevel::Suspicious)
            v.level = RiskLevel::Suspicious;
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
