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
    /// [^\n|;'"]* - như SameStatement và KHÔNG vượt qua dấu nháy: dùng cho tham số của powershell.exe. Phần nằm
    /// sau dấu nháy (vd `powershell -c "ping -w 1 ..."`) là nội dung lệnh chứ không còn là tham số của
    /// powershell.exe - trước v1.28.1 "-w 1" của ping bị hiểu là "-WindowStyle 1" (ẩn cửa sổ).
    SameStatementUnquoted,
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

        // Các dấu mở tham số mà PowerShell nhận: '-' và các dấu gạch Unicode (en/em dash, horizontal bar) - dán từ
        // Word/trang web hay bị đổi '-' thành '–', và kẻ tấn công cũng cố tình dùng (trước v1.28.1 các luật
        // Set-MpPreference/Add-MpPreference/Set-NetFirewallProfile chỉ nhận '-' ASCII nên "–DisableRealtime..." lọt).
        const QString dashChars = R"([-\x{2013}\x{2014}\x{2015}])";
        // Dấu mở tham số đứng đầu một "từ" (sau khoảng trắng hoặc dấu nháy - vd -ArgumentList '-enc ...'). Riêng '/'
        // (powershell.exe cũng nhận) chỉ dùng trong các mẫu đã có chữ "powershell" đứng trước - đứng riêng thì
        // trùng quá nhiều công cụ khác (xcopy /e, schtasks /create).
        const QString dash = R"((?:^|(?<=[\s'"]))[-\x{2013}\x{2014}\x{2015}])";
        // "powershell ... <dấu mở tham số>" trên cùng một câu lệnh, chưa qua dấu nháy nào:
        // Chain(psHead, SameStatementUnquoted, optStart + ...).
        const QString psHead = R"((?:powershell|pwsh)(?:\.exe)?)";
        const QString optStart = R"(\s[-/\x{2013}\x{2014}\x{2015}])";
        const QString enc = "(?:ec|" + anyPrefixOf("encodedcommand", 1) + ")";
        const QString execPolicy = "(?:ep|" + anyPrefixOf("executionpolicy", 2) + ")";
        const QString windowStyle = anyPrefixOf("windowstyle", 1);
        // -EncodedCommand BẮT BUỘC là Base64 của văn bản UTF-16LE: với lệnh chữ ASCII, nhóm 4 ký tự thứ hai bắt
        // đầu và kết thúc bằng 'A' (byte 0 của UTF-16LE) - "YwBhAGwA..." = "calc". Đòi đúng dạng này thì chuỗi hex
        // 40 ký tự (git grep -e <sha1>) hay "-Enc BigEndianUnicode" (tham số -Encoding của Get-Content) không còn
        // bị báo là lệnh mã hóa.
        const QString b64Utf16 = R"(['"]?[A-Za-z0-9+/]{4}A[A-Za-z0-9+/]{2}A)";
        // Lệnh tải nội dung từ mạng: cmdlet đầy đủ, bí danh có sẵn của PowerShell (iwr/irm/curl/wget) và
        // curl.exe/wget.exe thật.
        const QString webCmd =
            R"((?:Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm|curl|wget)(?:\.exe)?(?![\w-])))";
        // Tải về (kể cả xuống đĩa) - phần đầu của luật "tải rồi chạy".
        const QString webOrDownload = "(?:" + webCmd + R"(|(?<!\w)Download(?:String|Data|File)(?:Async|TaskAsync)?(?!\w)|Start-BitsTransfer))";
        // Chạy một thứ vừa tải: Start-Process/saps/start, Invoke-Item/ii, toán tử gọi "&", chạy tệp ".\x",
        // "powershell -File x" - phần đầu câu lệnh (đầu văn bản hoặc sau ; | xuống dòng hay dấu mở ngoặc).
        const QString runAfterDownload =
            R"(Start-Process|(?<![\w-])saps(?![\w-])|Invoke-Item|(?:^|(?<=[;|\n(]))\s*+(?:&\s*+[^\s&=]|\.{1,2}[\\/]|(?:start|ii)\s)|(?<![\w-])(?:powershell|pwsh)(?:\.exe)?\s+(?:[-/]\w+\s+){0,4}[-/]f(?:i(?:le?)?)?\s)";
        // Dựng khối lệnh từ chuỗi rồi chạy - tương đương IEX.
        const QString dynamicScript = R"(\[scriptblock\]::Create\(|\.InvokeScript\(|\.NewScriptBlock\()";

        // ---- Tham số -Disable* của Set-MpPreference (lấy THẬT từ Get-Command Set-MpPreference, Windows 10
        // 22H2). PowerShell nhận mọi tiền tố KHÔNG NHẬP NHẰNG của tên tham số và cả bí danh chính thức của nó:
        // tiền tố tối thiểu của mỗi tên được TÍNH từ danh sách đầy đủ (vd -DisableIO, -DisableBe, -DisableE đều
        // hợp lệ - bản trước liệt kê tay Rea|Beh|IOAV|... nên lọt). Tiền tố nhập nhằng thì PowerShell báo lỗi,
        // lệnh không chạy, nên không cần bắt.
        const QStringList mpDisableAll = {
            "DatagramProcessing", "CpuThrottleOnIdleScans", "AutoExclusions", "PrivacyMode", "BehaviorMonitoring",
            "IntrusionPreventionSystem", "IOAVProtection", "RealtimeMonitoring", "ScriptScanning", "ArchiveScanning",
            "CatchupFullScan", "CatchupQuickScan", "EmailScanning", "RemovableDriveScanning", "RestorePoint",
            "ScanningMappedNetworkDrivesForFullScan", "ScanningNetworkFiles", "BlockAtFirstSeen", "TlsParsing",
            "FtpParsing", "HttpParsing", "DnsParsing", "DnsOverTcpParsing", "SshParsing", "GradualRelease",
            "InboundConnectionFiltering", "RdpParsing", "NetworkProtectionPerfTelemetry", "TDTFeature",
            "TamperProtection", "SmtpParsing", "QuicParsing", "CacheMaintenance", "CoreServiceECSIntegration",
            "CoreServiceTelemetry"};
        auto unambiguousPrefixes = [&mpDisableAll](const QStringList& names) {
            QStringList alternatives;
            for (const QString& name : names)
            {
                int length = 1;
                for (; length < name.size(); ++length)
                {
                    const QString prefix = name.left(length).toLower();
                    bool unique = true;
                    for (const QString& other : mpDisableAll)
                        if (other != name && other.toLower().startsWith(prefix))
                            unique = false;
                    if (unique)
                        break;
                }
                alternatives << anyPrefixOf(name, length);
            }
            return alternatives.join('|');
        };
        // Lớp bảo vệ chính: tắt = Nguy hiểm.
        const QStringList mpCore = {"RealtimeMonitoring", "BehaviorMonitoring", "IOAVProtection", "ScriptScanning",
                                    "BlockAtFirstSeen", "IntrusionPreventionSystem", "ArchiveScanning", "EmailScanning",
                                    "RemovableDriveScanning", "TamperProtection"};
        const QString mpCoreAliases = "drtm|dbm|dioavp|dscrptsc|dbaf|dips|darchsc|demsc|drdsc|dtp";
        // Lớp kiểm tra phụ (quét tệp mạng, phân tích lưu lượng của Network Protection, Intel TDT...): tắt = Đáng chú ý.
        const QStringList mpMinor = {"ScanningNetworkFiles", "ScanningMappedNetworkDrivesForFullScan", "TlsParsing",
                                     "FtpParsing", "HttpParsing", "DnsParsing", "DnsOverTcpParsing", "SshParsing",
                                     "RdpParsing", "SmtpParsing", "QuicParsing", "InboundConnectionFiltering",
                                     "DatagramProcessing", "TDTFeature"};
        const QString mpMinorAliases = "dsnf|dsmndfsc|dtlsp|dftpp|dhttpp|ddnsp|ddnstcpp|dsshp|drdpp|dsmtpp|dquicp|dicf|ddtgp|dtdtf";
        // Mọi giá trị KHÁC $false/0 đều tính ($true, 1, :$true, biến, biểu thức...). Các lượng từ "*+"/"?+" (chiếm
        // hữu) là BẮT BUỘC: nếu để regex quay lui nhả bớt khoảng trắng thì phép nhìn trước phủ định luôn qua được
        // và "$false" cũng bị báo.
        const QString turnedOn = R"(\s*+:?+\s*+(?!(?:\$false|false|0)(?!\w)))";
        auto mpDisableParam = [&](const QStringList& names, const QString& aliases) {
            return dashChars + "(?:Disable(?:" + unambiguousPrefixes(names) + ")|" + aliases + ")(?![\\w-])" + turnedOn;
        };
        const QString setMpHead = R"(Set-MpPreference\b)";
        // Khóa registry chính sách của Defender mà đặt = 1 là TẮT một lớp bảo vệ.
        const QString defenderPolicyValues =
            R"((?:DisableRealtimeMonitoring|DisableBehaviorMonitoring|DisableOnAccessProtection|DisableScanOnRealtimeEnable|DisableIOAVProtection|DisableAntiSpyware|DisableAntiVirus|DisableRoutinelyTakingAction))";
        const QString regAdd = R"((?<![\w-])reg(?:\.exe)?\s+add\s)";
        const QString itemProperty = R"((?:New|Set)-ItemProperty\b)";
        const QString protectedServices = R"((?:WinDefend|wscsvc|MpsSvc|Sense|WdNisSvc)(?!\w))";

        return QList<Pattern>{
            // ---- Tải về rồi thực thi ngay (download cradle) - kỹ thuật phổ biến nhất của mã độc ----
            // Tải VÀO BỘ NHỚ (chuỗi/mảng byte) gần như chỉ để chạy ngay. "(?<!\w)...(?!\w)": "D:\DownloadFiles\" không
            // còn bị nhận nhầm là DownloadFile.
            {{rx(R"((?<!\w)Download(?:String|Data)(?:Async|TaskAsync)?(?!\w))")}, {}, RL::Dangerous,
             "Tải nội dung từ mạng thẳng vào bộ nhớ (Net.WebClient DownloadString/DownloadData) - mẫu \"download cradle\" thường gặp trong mã độc"},
            {{rx(R"((?<!\w)DownloadFile(?:Async|TaskAsync)?(?!\w))")}, {}, RL::Suspicious,
             "Tải tệp từ mạng xuống đĩa (Net.WebClient.DownloadFile) - thường hợp lệ, nhưng hãy chắc chắn bạn tin nguồn này"},
            // IEX đứng riêng như một từ ở BẤT KỲ đâu (sau dấu |, trước biến, trong &('iex')...) - bản
            // trước chỉ bắt "IEX (" nên "irm https://... | iex" (cradle phổ biến nhất hiện nay) lọt.
            {{rx(R"((?<![\w-])IEX(?![\w-])|Invoke-Expression)")}, {}, RL::Dangerous,
             "Dùng Invoke-Expression/IEX để thực thi chuỗi lệnh động - kỹ thuật né tránh phát hiện phổ biến"},
            {{},
             {chain(webCmd, Gap::SameLine,
                    {R"(\|\s*+(?:&\s*+)?+\(?+\s*+['"]?+(?:iex|Invoke-Expression|powershell|pwsh|cmd|bash|sh)(?:\.exe)?(?![\w-]))"})},
             RL::Dangerous,
             "Tải nội dung từ mạng rồi chuyển thẳng (pipe) sang trình thực thi lệnh - chạy mã lấy từ Internet mà không qua bước xem lại nào"},
            {{}, {chain(webOrDownload, Gap::Anywhere, {runAfterDownload})}, RL::Dangerous,
             "Tải tệp từ mạng rồi chạy ngay (Start-Process/start, Invoke-Item, toán tử &, .\\tệp, powershell -File) trong cùng một đoạn lệnh"},
            {{}, {chain(webOrDownload, Gap::Anywhere, {dynamicScript}), chain(dynamicScript, Gap::Anywhere, {webOrDownload})},
             RL::Dangerous,
             "Tải mã từ mạng rồi dựng thành khối lệnh để chạy ([scriptblock]::Create/InvokeScript) - tương đương IEX"},
            {{rx(dynamicScript)}, {}, RL::Suspicious,
             "Dựng và chạy khối lệnh từ một chuỗi ([scriptblock]::Create/InvokeScript) - cách thực thi giống IEX, hay dùng để né bộ lọc"},
            {{rx(R"(Invoke-WebRequest|Invoke-RestMethod|(?<![\w-])(?:iwr|irm)(?![\w-]))")},
             {chain(R"((?<![\w-])(?:curl|wget)(?:\.exe)?\s)", Gap::SameLine, {R"(https?://)"})},
             RL::Suspicious,
             "Tải nội dung từ mạng (Invoke-WebRequest/Invoke-RestMethod/curl/wget) - thường hợp lệ, nhưng hãy chắc chắn bạn tin nguồn này"},
            // BITS: bitsadmin (công cụ cũ, gần như chỉ còn mã độc dùng) = Nguy hiểm; Start-BitsTransfer tải về bình
            // thường = Đáng chú ý (tải rồi chạy thì đã có luật "tải rồi chạy" ở trên).
            {{}, {chain(R"(bitsadmin(?:\.exe)?\s)", Gap::SameLine, {R"(/(?:transfer|addfile|create|resume|setnotifycmdline))"})},
             RL::Dangerous,
             "Tải tệp/đặt lệnh chạy qua bitsadmin - công cụ tải ngầm hay bị mã độc lợi dụng"},
            {{rx(R"(Start-BitsTransfer)")}, {}, RL::Suspicious,
             "Tải tệp qua BITS (Start-BitsTransfer) - thường hợp lệ, nhưng BITS cũng hay bị lợi dụng để tải ngầm"},
            {{}, {chain(R"(certutil(?:\.exe)?\s)", Gap::SameLine, {R"([-/](?:urlcache|decode|decodehex|verifyctl))"})},
             RL::Dangerous,
             "Dùng certutil để tải/giải mã tệp - LOLBin hay bị lợi dụng thay vì công cụ tải thông thường"},
            {{}, {chain(R"((?<![\w-])msiexec(?:\.exe)?\s)", Gap::SameLine, {R"(https?://|\\\\)"})}, RL::Suspicious,
             "Cài gói MSI thẳng từ mạng (msiexec ... http://) - LOLBin hay bị lợi dụng; chỉ chạy nếu tin nguồn"},
            {{}, {chain(R"((?<![\w-])wmic(?:\.exe)?\s)", Gap::SameLine, {R"(/format:\s*+['"]?+(?:https?://|\\\\))"})},
             RL::Dangerous,
             "Dùng wmic /format: để tải và chạy tệp XSL từ xa - kỹ thuật thực thi né phát hiện"},

            // ---- Lệnh bị mã hóa/che giấu (obfuscation) ----
            // (1) mọi tiền tố của -EncodedCommand + chuỗi Base64 (UTF-16LE) dài, ở bất kỳ đâu; (2) có chữ
            // "powershell" đứng trước thì chuỗi ngắn cũng tính ("powershell -e YwBhAGwAYwA=" chỉ 12 ký tự).
            {{rx(dash + enc + R"(\s+)" + b64Utf16 + R"([A-Za-z0-9+/=]{32,})")},
             {chain(psHead, Gap::SameStatementUnquoted, {optStart + enc + R"(\s+)" + b64Utf16})},
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
            {{}, {chain(psHead, Gap::SameStatementUnquoted, {optStart + anyPrefixOf("version", 1) + R"(\s+['"]?2(?:\.0)?(?![\w.]))"})},
             RL::Suspicious,
             "Ép chạy PowerShell 2.0 (-Version 2) - bản cũ không có AMSI và không ghi log script, hay dùng để né phát hiện"},

            // ---- Bỏ qua chính sách thực thi / ẩn cửa sổ ----
            {{rx(dash + execPolicy + R"(\s+['"]?(?:bypass|unrestricted))")},
             {chain(psHead, Gap::SameStatementUnquoted, {optStart + execPolicy + R"(\s+['"]?(?:bypass|unrestricted))"}),
              chain(R"(Set-ExecutionPolicy\s)", Gap::SameStatement, {R"(bypass|unrestricted)"})},
             RL::Suspicious, "Bỏ qua chính sách thực thi PowerShell (-ExecutionPolicy Bypass/Unrestricted)"},
            {{rx(dash + windowStyle + R"(\s+['"]?hidden)")},
             {chain(psHead, Gap::SameStatementUnquoted,
                    {optStart + windowStyle + R"(\s+['"]?(?:hidden|hidde|hidd|hid|hi|h|1)(?!\w))"})},
             RL::Suspicious,
             "Chạy ẩn cửa sổ (-WindowStyle Hidden) - thường dùng để người dùng không nhận ra có tiến trình đang chạy"},

            // ---- Công cụ tấn công đã biết / đánh cắp thông tin đăng nhập ----
            {{rx(R"(Invoke-Mimikatz|Invoke-ReflectivePEInjection|Invoke-Shellcode|Invoke-TokenManipulation|Invoke-DllInjection|Invoke-Kerberoast|Invoke-BloodHound|Invoke-PowerShellTcp|Invoke-PsExec|Invoke-WMIExec|Invoke-SMBExec|Get-GPPPassword|Out-Minidump|(?<![\w-])mimikatz|sekurlsa::|lsadump::)")},
             {}, RL::Dangerous, "Tên hàm trùng với công cụ tấn công đã biết (PowerSploit/Empire/Mimikatz...)"},
            {{rx(R"((?<![\w-])reg(?:\.exe)?\s+save\s+['"]?HK(?:LM|EY_LOCAL_MACHINE)\\(?:SAM|SECURITY|SYSTEM)(?!\w))")},
             {chain(R"((?<![\w-])procdump(?:64)?(?:\.exe)?\s)", Gap::SameLine, {R"(lsass)"}),
              chain(R"((?<![\w-])ntdsutil(?:\.exe)?\s)", Gap::SameLine, {R"((?<!\w)ifm(?!\w))"})},
             RL::Dangerous,
             "Kết xuất cơ sở dữ liệu tài khoản hoặc bộ nhớ LSASS (reg save SAM/SECURITY, procdump lsass, ntdsutil ifm) - kỹ thuật đánh cắp mật khẩu"},
            {{rx(R"(AmsiUtils|amsiInitFailed|AmsiScanBuffer|AmsiOpenSession)")},
             {chain(R"(\[Ref\]\.Assembly\.GetType\()", Gap::SameLine, {R"(Amsi)"})}, RL::Dangerous,
             "Cố gắng vô hiệu hóa AMSI (Antimalware Scan Interface) - kỹ thuật né tránh diệt virus phổ biến"},

            // ---- Né tránh phòng thủ ----
            {{rx(R"((?<![\w-])Disable(?:RealtimeMonitoring|BehaviorMonitoring|IOAVProtection|ScriptScanning|BlockAtFirstSeen|IntrusionPreventionSystem|ArchiveScanning|EmailScanning|RemovableDriveScanning|TamperProtection)\s*+=\s*+['"]?+(?:\$true|1)(?!\w))")},
             {chain(setMpHead, Gap::SameStatement, {mpDisableParam(mpCore, mpCoreAliases)}),
              chain(regAdd, Gap::SameLine, {R"(/v\s+['"]?)" + defenderPolicyValues + R"(['"]?(?!\w))", R"(/d\s+['"]?(?:1|0x0*1)(?!\w))"}),
              chain(itemProperty, Gap::SameStatement, {R"(-Name\s+['"]?)" + defenderPolicyValues + R"(['"]?(?!\w))", R"(-Value\s+['"]?(?:1|\$true)(?!\w))"}),
              chain(regAdd, Gap::SameLine, {R"(/v\s+['"]?TamperProtection['"]?(?!\w))", R"(/d\s+['"]?[04](?!\w))"})},
             RL::Dangerous,
             "Tự tắt một lớp bảo vệ của Windows Defender (bảo vệ thời gian thực/giám sát hành vi/quét tệp tải về/Tamper Protection...) qua Set-MpPreference hoặc khóa registry chính sách"},
            {{}, {chain(setMpHead, Gap::SameStatement, {mpDisableParam(mpMinor, mpMinorAliases)})}, RL::Suspicious,
             "Tắt bớt một lớp kiểm tra của Windows Defender (quét tệp trên mạng, phân tích lưu lượng của Network Protection, Intel TDT...)"},
            {{rx(R"((?<![\w-])Disable(?:AntiSpyware|AntiVirus)(?!\w))")}, {}, RL::Suspicious,
             "Đụng tới khóa chính sách tắt Windows Defender (DisableAntiSpyware/DisableAntiVirus) - xem kỹ lệnh đang đặt giá trị gì"},
            {{},
             {chain(setMpHead, Gap::SameStatement,
                    {dashChars + R"((?:(?:Unknown|Low|Moderate|High|Severe)ThreatDefaultAction|unktdefac|ltdefac|mtdefac|htdefac|stdefac)\s*+:?+\s*+['"]?+(?:6|9|Allow|NoAction)(?!\w))"})},
             RL::Dangerous,
             "Đặt Windows Defender CHO PHÉP/bỏ qua mối đe dọa đã phát hiện (ThreatDefaultAction = Allow/NoAction)"},
            {{},
             {chain(setMpHead, Gap::SameStatement,
                    {dashChars + R"((?:MAPSReporting|EnableNetworkProtection|EnableControlledFolderAccess|PUAProtection)\s*+:?+\s*+['"]?+(?:0|Disabled)(?!\w))"})},
             RL::Suspicious, "Hạ mức bảo vệ của Windows Defender (tắt bảo vệ đám mây/Network Protection/bảo vệ thư mục/chặn PUA)"},
            {{rx(R"(-AttackSurfaceReductionOnlyExclusions)")},
             {chain(R"((?:Add|Set)-MpPreference\b)", Gap::SameStatement, {dashChars + R"(Exclusion(?:Path|Process|Extension|IpAddress))"})},
             RL::Suspicious,
             "Thêm mục loại trừ quét virus (ExclusionPath/Process/Extension) - có thể hợp lệ nhưng cũng hay bị mã độc lợi dụng để tự ẩn"},
            {{rx(R"(netsh\s+advfirewall\s+set\s+\w+\s+state\s+off|netsh\s+firewall\s+set\s+opmode\s+(?:mode\s*=\s*)?disable)")},
             {chain(R"(Set-NetFirewallProfile\b)", Gap::SameStatement, {dashChars + R"(Enabled\s*+:?+\s*+['"]?+(?:\$?false|0)(?!\w))"})},
             RL::Dangerous, "Tự tắt Tường lửa Windows"},
            // sc config chỉ nguy hiểm khi đặt start= disabled ("sc config WinDefend start= auto" là BẬT lại); Stop-Service
            // chỉ xét trong CÙNG câu lệnh ("Stop-Service Spooler; Get-Service WinDefend" không phải dừng WinDefend).
            {{rx(R"((?<![\w-])(?:sc(?:\.exe)?\s+(?:stop|delete)|net1?(?:\.exe)?\s+stop)\s+['"]?)" + protectedServices)},
             {chain(R"(Stop-Service\b)", Gap::SameStatement, {protectedServices}),
              chain(R"((?<![\w-])sc(?:\.exe)?\s+config\s+['"]?)" + protectedServices, Gap::SameLine, {R"(start=\s*+['"]?+disabled)"}),
              chain(R"(Set-Service\b)", Gap::SameStatement, {protectedServices, R"(-StartupType\s*+:?+\s*+['"]?+Disabled)"}),
              chain(R"(Get-Service\b)", Gap::SameLine, {protectedServices, R"(\|\s*+(?:Stop-Service|Set-Service\b[^\n|;]*?Disabled))"})},
             RL::Dangerous,
             "Cố dừng/vô hiệu hóa dịch vụ bảo vệ hệ thống (Windows Defender/Trung tâm bảo mật/Tường lửa)"},
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
            {{},
             {chain(regAdd, Gap::SameLine, {R"(/v\s+['"]?(?:EnableLUA|ConsentPromptBehaviorAdmin)['"]?(?!\w))", R"(/d\s+['"]?(?:0|0x0+)(?!\w))"}),
              chain(itemProperty, Gap::SameStatement, {R"(-Name\s+['"]?(?:EnableLUA|ConsentPromptBehaviorAdmin)['"]?(?!\w))", R"(-Value\s+['"]?0(?!\w))"})},
             RL::Dangerous,
             "Tắt UAC/hộp thoại xác nhận quyền quản trị (EnableLUA/ConsentPromptBehaviorAdmin = 0) - hạ bảo vệ của cả máy"},

            // ---- Thiết lập duy trì (persistence) ----
            {{rx(R"(Register-ScheduledTask)")}, {chain(R"(schtasks(?:\.exe)?\s+)", Gap::SameLine, {R"(\/create)"})},
             RL::Suspicious,
             "Tạo tác vụ lên lịch (schtasks /create, Register-ScheduledTask) - có thể hợp lệ nhưng cũng là cách mã độc tự duy trì"},
            {{},
             {chain(R"(reg(?:\.exe)?\s+add\s+)", Gap::SameLine, {R"(\\Run(?:Once|OnceEx|Services|ServicesOnce)?\b)"}),
              chain(R"((?:New|Set)-ItemProperty\b)", Gap::SameLine, {R"(\\CurrentVersion\\Run)"})},
             RL::Suspicious,
             "Thêm khóa registry Run/RunOnce - cách phổ biến để chương trình tự khởi động cùng Windows"},
            {{}, {chain(R"(Image File Execution Options)", Gap::SameLine, {R"((?<!\w)(?:Debugger|GlobalFlag)(?!\w))"})},
             RL::Dangerous,
             "Gắn \"Debugger\" cho một chương trình qua Image File Execution Options - kỹ thuật chiếm quyền chạy (vd thay sethc.exe/utilman.exe bằng cmd.exe để vào máy không cần mật khẩu)"},
            // Lớp WMI cụ thể mà kỹ thuật duy trì thật dùng (CommandLine/ActiveScript EventConsumer + binding), không
            // chỉ lớp cơ sở trừu tượng __EventConsumer.
            {{rx(R"(__EventFilter|__EventConsumer|__FilterToConsumerBinding|(?:CommandLine|ActiveScript)EventConsumer)")}, {},
             RL::Dangerous,
             "Thiết lập WMI Event Subscription - kỹ thuật duy trì (persistence) nâng cao ít khi dùng trong tác vụ thông thường"},
            {{rx(R"(Register-WmiEvent|Register-CimIndicationEvent)")}, {}, RL::Suspicious,
             "Đăng ký sự kiện WMI trong phiên hiện tại - thường dùng để theo dõi, nhưng cũng là một bước của kỹ thuật duy trì qua WMI"},

            // ---- Mã lệnh shell từ xa ----
            {{rx(R"(/dev/tcp/)")}, {chain(R"(Sockets\.TcpClient)", Gap::Anywhere, {R"(\.GetStream\()"})}, RL::Dangerous,
             "Mẫu mã mở kết nối mạng thô rồi đọc/ghi luồng dữ liệu qua socket (reverse shell)"},
            {{rx(R"(Sockets\.TcpClient)")}, {}, RL::Suspicious,
             "Mở kết nối TCP thô (System.Net.Sockets.TcpClient) - thường dùng để kiểm tra cổng, nhưng cũng là nền của reverse shell"},
            // mshta chỉ tính khi theo sau là thứ nó sẽ CHẠY (URL, javascript:/vbscript:, tệp .hta, đường dẫn) -
            // "Stop-Process -Name mshta"/"taskkill /im mshta.exe" là dừng nó, không phải chạy.
            {{rx(R"((?<![\w-])mshta(?:\.exe)?['"]?\s+['"]?(?:https?:|ftp:|javascript:|vbscript:|about:|\\\\|[A-Za-z]:\\|[^\s'"]*?\.hta(?!\w)))")},
             {chain(R"(regsvr32(?:\.exe)?\s)", Gap::SameLine, {R"([-/]i:\s*['"]?(?:https?:|ftp:|\\\\)|scrobj\.dll)"})},
             RL::Dangerous,
             "Dùng mshta/regsvr32 để chạy mã từ xa - LOLBin hay bị lợi dụng để né tránh phát hiện"},
            // rundll32 nạp DLL qua đường UNC (\\máy\chia-sẻ\x.dll) - chỉ khi UNC đứng NGAY sau rundll32 (là chính DLL
            // được nạp); "rundll32 printui.dll,PrintUIEntry /in /n \\máy-in\HP" là thêm máy in, không phải nạp DLL lạ.
            {{rx(R"((?<![\w-])rundll32(?:\.exe)?\s+['"]?\\\\)")},
             {chain(R"(rundll32(?:\.exe)?\s)", Gap::SameLine,
                    {R"(javascript:|vbscript:|https?://|mshtml\s*,\s*RunHTMLApplication|url(?:\.dll)?\s*,\s*(?:OpenURL|FileProtocolHandler)|shell32(?:\.dll)?\s*,\s*ShellExec_RunDLL|advpack(?:\.dll)?\s*,\s*(?:LaunchINFSection|RegisterOCX)|zipfldr(?:\.dll)?\s*,\s*RouteTheCall|pcwutl(?:\.dll)?\s*,\s*LaunchApplication|comsvcs(?:\.dll)?\s*+,?+\s*+(?:MiniDump|#24))"})},
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
        if (c == u'\n')
            return i;
        if (gap != Gap::SameLine && (c == u'|' || c == u';'))
            return i;
        if (gap == Gap::SameStatementUnquoted && (c == u'\'' || c == u'"'))
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
    // Dấu ^ là ký tự thoát của cmd.exe (p^owershell -e^nc ...); ^ ở CUỐI dòng là nối dòng của cmd.exe - dòng sau
    // được nối thẳng vào (giữ nguyên khoảng trắng đầu dòng sau), như "powershell -nop ^<xuống dòng>  -e <b64>".
    static const QRegularExpression caretContinuation("\\^\\r?\\n");
    s.remove(caretContinuation);
    s.remove('^');
    // Nháy cong: PowerShell coi ‘ ’ ‚ ‛ là nháy đơn và “ ” „ là nháy kép (cùng quy tắc PowerShellRunner::quoteLiteral
    // phải xử lý) - &(‘I’+‘EX’), i‘’ex chạy y như bản nháy thẳng. Đưa về nháy thẳng trước khi gỡ ghép chuỗi.
    for (const char16_t q : {u'‘', u'’', u'‚', u'‛'})
        s.replace(QChar(q), QChar('\''));
    for (const char16_t q : {u'“', u'”', u'„'})
        s.replace(QChar(q), QChar('"'));
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
