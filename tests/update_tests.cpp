// Kiểm thử lõi tự cập nhật (UpdateChecker) - THUẦN, không gọi mạng thật: so sánh phiên bản + phân tích
// JSON mẫu dựng sẵn theo đúng cấu trúc GET /repos/{owner}/{repo}/releases/latest của GitHub (xem tài
// liệu GitHub REST API), tách riêng khỏi phần gọi QNetworkAccessManager thật - giống mẫu đã dùng cho
// AdbController::internal::parseDevicesOutput.
#include <QCoreApplication>
#include <cstdio>

#include "core/update/UpdateChecker.h"
#include "core/update/UpdateInstaller.h"
#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"

#include <QFile>
#include <QTemporaryDir>
#include "TestDataDir.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // update_core dùng Logger - ép dữ liệu vào thư mục tạm để test không ghi log vào dữ liệu thật.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    using UpdateCheckerInternal::compareVersions;
    using UpdateCheckerInternal::parseLatestRelease;

    // ---- compareVersions ----
    {
        CHECK(compareVersions("1.2.3", "1.2.3") == 0);
        CHECK(compareVersions("1.2.4", "1.2.3") > 0);
        CHECK(compareVersions("1.2.3", "1.2.4") < 0);
        CHECK(compareVersions("1.3.0", "1.2.9") > 0);
        CHECK(compareVersions("2.0.0", "1.9.9") > 0);
        CHECK(compareVersions("v1.5.0", "1.5.0") == 0); // bỏ tiền tố "v"
        CHECK(compareVersions("V1.5.0", "1.5.0") == 0); // "V" hoa cũng bỏ
        CHECK(compareVersions("1.2", "1.2.0") == 0);     // thiếu patch coi là 0
        CHECK(compareVersions("1.15.0", "1.9.0") > 0);   // so số, không so chuỗi ("15" > "9")
        // Hậu tố kiểu semver sau số - lấy đúng dãy chữ số ở đầu, không rơi về 0 một cách sai lệch (lỗi
        // tìm thấy khi tự rà soát lại: trước đây "5-hotfix" không parse được bằng toInt() toàn chuỗi).
        CHECK(compareVersions("1.16.5-hotfix", "1.16.0") > 0);
        CHECK(compareVersions("1.16.0-rc1", "1.16.0") == 0);
        CHECK(compareVersions("1.16.0", "1.16.1-beta") < 0);
    }

    // ---- parseLatestRelease: JSON hợp lệ, đúng cấu trúc GitHub trả về ----
    {
        const QByteArray json = R"({
            "tag_name": "v1.16.0",
            "body": "- Sửa lỗi X\n- Thêm tính năng Y",
            "assets": [
                { "name": "OneForAll_Setup.exe", "browser_download_url": "https://github.com/x/y/releases/download/v1.16.0/OneForAll_Setup.exe", "size": 123456789 },
                { "name": "source.zip", "browser_download_url": "https://github.com/x/y/archive/v1.16.0.zip", "size": 999 }
            ]
        })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version == "1.16.0");
        CHECK(info.downloadUrl == "https://github.com/x/y/releases/download/v1.16.0/OneForAll_Setup.exe");
        CHECK(info.downloadSize == 123456789);
        CHECK(info.releaseNotes.contains("Sửa lỗi X"));
    }

    // ---- parseLatestRelease: asset tên GIỐNG nhưng KHÔNG ĐÚNG tuyệt đối "OneForAll_Setup.exe" - không
    // được khớp nhầm (lỗi tìm thấy khi tự rà soát lại: trước đây chỉ kiểm tra endsWith("Setup.exe"), có
    // thể khớp nhầm asset khác tên tương tự nếu một bản phát hành lỡ đính kèm nhiều file) ----
    {
        const QByteArray json = R"({
            "tag_name": "v1.16.0",
            "assets": [
                { "name": "SomeOtherApp_Setup.exe", "browser_download_url": "https://x/wrong.exe", "size": 10 },
                { "name": "OneForAll_Setup.exe.sha256", "browser_download_url": "https://x/checksum", "size": 1 }
            ]
        })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version.isEmpty()); // không có asset nào khớp ĐÚNG tên - coi như không hợp lệ
    }

    // ---- parseLatestRelease: thiếu tag_name - coi như không hợp lệ ----
    {
        const QByteArray json = R"({ "body": "khong co tag", "assets": [] })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version.isEmpty());
    }

    // ---- parseLatestRelease: có tag nhưng KHÔNG đính kèm installer (chỉ có source.zip) ----
    {
        const QByteArray json = R"({
            "tag_name": "v1.16.0",
            "assets": [ { "name": "source.zip", "browser_download_url": "https://x/source.zip", "size": 10 } ]
        })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version.isEmpty()); // không có gì để tự cập nhật -> coi như rỗng
    }

    // ---- parseLatestRelease: JSON hỏng/rỗng ----
    {
        CHECK(parseLatestRelease("").version.isEmpty());
        CHECK(parseLatestRelease("khong phai json").version.isEmpty());
        CHECK(parseLatestRelease("[1,2,3]").version.isEmpty()); // mảng, không phải object
    }

    // ---- parseLatestRelease: asset .msi + digest SHA-256 (trường "digest" của GitHub) ----
    {
        const QByteArray json = R"({
            "tag_name": "v1.19.0",
            "assets": [
                { "name": "OneForAll_Setup.msi", "browser_download_url": "https://github.com/x/y/releases/download/v1.19.0/OneForAll_Setup.msi", "size": 200,
                  "digest": "sha256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" },
                { "name": "OneForAll_Setup.exe", "browser_download_url": "https://github.com/x/y/releases/download/v1.19.0/OneForAll_Setup.exe", "size": 100,
                  "digest": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" },
                { "name": "OneForAll_Setup_exe.zip", "browser_download_url": "https://github.com/x/y/z.zip", "size": 1 }
            ]
        })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version == "1.19.0");
        CHECK(info.downloadUrl.endsWith("OneForAll_Setup.exe"));
        CHECK(info.sha256 == "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        CHECK(info.msiUrl.endsWith("OneForAll_Setup.msi"));
        CHECK(info.msiSize == 200);
        CHECK(info.msiSha256 == QString(64, QChar('a'))); // hex được chuẩn hóa về chữ thường
    }
    // digest thiếu / sai thuật toán / sai độ dài -> rỗng (bỏ qua bước đối chiếu, không coi là lỗi)
    {
        const QByteArray json = R"({
            "tag_name": "v1.19.0",
            "assets": [
                { "name": "OneForAll_Setup.exe", "browser_download_url": "https://github.com/x/y/a.exe", "size": 1, "digest": "md5:abcd" },
                { "name": "OneForAll_Setup.msi", "browser_download_url": "https://github.com/x/y/a.msi", "size": 1, "digest": "sha256:1234" }
            ]
        })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.sha256.isEmpty());
        CHECK(info.msiSha256.isEmpty());
        CHECK(!info.msiUrl.isEmpty());
    }
    // Bản phát hành cũ chỉ có .exe - msiUrl rỗng nhưng vẫn hợp lệ
    {
        const QByteArray json = R"({ "tag_name": "v1.17.0", "assets": [
            { "name": "OneForAll_Setup.exe", "browser_download_url": "https://github.com/x/y/a.exe", "size": 1 } ] })";
        const UpdateInfo info = parseLatestRelease(json);
        CHECK(info.version == "1.17.0");
        CHECK(info.msiUrl.isEmpty());
    }

    // ---- isTrustedDownloadUrl ----
    {
        using UpdateCheckerInternal::isTrustedDownloadUrl;
        CHECK(isTrustedDownloadUrl("https://github.com/DinhDuyen2810/ALLinONE/releases/download/v1.19.0/OneForAll_Setup.exe"));
        CHECK(!isTrustedDownloadUrl("http://github.com/x/y/a.exe"));          // không mã hóa
        CHECK(!isTrustedDownloadUrl("https://github.com.evil.example/a.exe")); // host lạ đội lốt
        CHECK(!isTrustedDownloadUrl("https://evil.example/github.com/a.exe"));
        CHECK(!isTrustedDownloadUrl("https://user@github.com/x/y/a.exe"));     // userinfo che host thật
        CHECK(!isTrustedDownloadUrl("file:///C:/a.exe"));
        CHECK(!isTrustedDownloadUrl(""));
        // Cổng: chỉ mặc định/443 (lỗi tìm thấy khi chạy core_stress_tests: cổng bất kỳ đều được nhận).
        CHECK(isTrustedDownloadUrl("https://github.com:443/x/y/a.exe"));
        CHECK(!isTrustedDownloadUrl("https://github.com:8443/x/y/a.exe"));
        CHECK(!isTrustedDownloadUrl("https://github.com:80/x/y/a.exe"));
        CHECK(!isTrustedDownloadUrl("https://github.com:0/x/y/a.exe"));
        CHECK(!isTrustedDownloadUrl("https://github.com./x/y/a.exe"));         // dấu chấm cuối tên máy
        CHECK(!isTrustedDownloadUrl("https://evil.example\\@github.com/a.exe")); // dấu gạch ngược trước '@'
        CHECK(!isTrustedDownloadUrl(" https://github.com/x/y/a.exe"));         // khoảng trắng đầu
    }

    // ---- parseLatestRelease: "size" ngoài miền (âm, phân số, 1e300, sai kiểu) -> 0, không tràn thành số âm ----
    {
        const auto sizeFor = [](const char* sizeJson) {
            const QByteArray json = QByteArray(R"({ "tag_name": "v9.0.0", "assets": [
                { "name": "OneForAll_Setup.exe", "browser_download_url": "https://github.com/x/y/a.exe", "size": )") +
                                    sizeJson + " } ] }";
            return parseLatestRelease(json).downloadSize;
        };
        CHECK(sizeFor("52428800") == 52428800);
        CHECK(sizeFor("5000000000") == 5000000000LL); // > 4 GB vẫn đúng
        CHECK(sizeFor("-5") == 0);
        CHECK(sizeFor("1e300") == 0);
        CHECK(sizeFor("-1e300") == 0);
        CHECK(sizeFor("1.5") == 0);
        CHECK(sizeFor("\"123\"") == 0);
        CHECK(sizeFor("null") == 0);
    }

    // ---- classifyInstall: chỉ coi là "đã cài" khi exe đang chạy nằm đúng trong thư mục cài tương ứng ----
    {
        using UpdateInstallerInternal::classifyInstall;
        using K = UpdateInstaller::InstallKind;
        const QString msiDir = "C:/Users/u/AppData/Local/One for ALL";
        const QString innoDir = R"(C:\Users\u\AppData\Local\Programs\One for ALL\)"; // dạng Inno ghi registry
        CHECK(classifyInstall(true, false, "c:/users/U/appdata/local/one for all", msiDir, "") == K::Msi);
        CHECK(classifyInstall(false, true, "C:/Users/u/AppData/Local/Programs/One for ALL", msiDir, innoDir) == K::InnoSetup);
        // Máy có cả hai bản cài: phân loại theo thư mục của exe ĐANG CHẠY
        CHECK(classifyInstall(true, true, "C:/Users/u/AppData/Local/One for ALL", msiDir, innoDir) == K::Msi);
        CHECK(classifyInstall(true, true, "C:/Users/u/AppData/Local/Programs/One for ALL", msiDir, innoDir) == K::InnoSetup);
        // Bản dev/portable chạy trên máy có cài sẵn bản khác - KHÔNG được coi là bản đã cài
        CHECK(classifyInstall(true, true, "D:/ALLinONE/OneForAll_Release", msiDir, innoDir) == K::Portable);
        CHECK(classifyInstall(false, false, "D:/x", msiDir, "") == K::Portable);
        CHECK(classifyInstall(true, false, "", msiDir, "") == K::Portable);
    }

    // ---- buildHelperScript: chờ đúng PID, chạy đúng trình cài đặt, mở lại ứng dụng, thoát dấu nháy ----
    {
        using UpdateInstallerInternal::buildHelperScript;
        using K = UpdateInstaller::InstallKind;
        const QString exe = buildHelperScript(K::InnoSetup, "C:/Temp/OneForAll_Update/OneForAll_Setup.exe",
                                              "C:/Apps/One for ALL/OneForAll.exe", 4321);
        CHECK(exe.contains("Wait-Process -Id 4321 -Timeout 60"));
        CHECK(exe.contains(R"('C:\Temp\OneForAll_Update\OneForAll_Setup.exe')"));
        CHECK(exe.contains("'/VERYSILENT'"));
        CHECK(!exe.contains("RESTARTAPPLICATIONS")); // không còn dựa vào Restart Manager để mở lại
        CHECK(exe.contains(R"(Start-Process -FilePath 'C:\Apps\One for ALL\OneForAll.exe' -WorkingDirectory 'C:\Apps\One for ALL')"));
        CHECK(exe.indexOf("Wait-Process") < exe.indexOf("VERYSILENT"));
        CHECK(exe.indexOf("VERYSILENT") < exe.lastIndexOf("Start-Process"));

        const QString msi = buildHelperScript(K::Msi, "C:/Temp/x/OneForAll_Setup.msi", "C:/A/OneForAll.exe", 7);
        CHECK(msi.contains("msiexec.exe'"));
        CHECK(msi.contains(R"('/i', '"C:\Temp\x\OneForAll_Setup.msi"', '/qn', '/norestart')"));
        CHECK(!msi.contains("VERYSILENT"));

        // Tên người dùng Windows có dấu nháy (O'Brien, hoặc dấu nháy cong U+2019) nằm trong đường dẫn -
        // không được thoát khỏi chuỗi nháy đơn của PowerShell.
        const QString curly(QChar(0x2019));
        const QString tricky = buildHelperScript(K::InnoSetup, "C:/Users/O'Bri" + curly + "en/s.exe", "C:/A/OneForAll.exe", 1);
        CHECK(tricky.contains(R"('C:\Users\O''Bri)" + curly + curly + R"(en\s.exe')"));
    }

    // ---- quoteLiteral: nhân đôi cả ' ASCII lẫn 4 biến thể Unicode mà PowerShell coi là dấu nháy đơn ----
    {
        CHECK(PowerShellRunner::quoteLiteral("abc") == "abc");
        CHECK(PowerShellRunner::quoteLiteral("a'b") == "a''b");
        const QString u = QString(QChar(0x2018)) + QChar(0x2019) + QChar(0x201A) + QChar(0x201B);
        QString doubled;
        for (const QChar c : u) { doubled += c; doubled += c; }
        CHECK(PowerShellRunner::quoteLiteral(u) == doubled);
    }

    // ---- sha256OfFile ----
    {
        const QString path = dataDir.filePath("hash.bin");
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly));
        f.write("abc");
        f.close();
        CHECK(UpdateInstallerInternal::sha256OfFile(path) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        CHECK(UpdateInstallerInternal::sha256OfFile(dataDir.filePath("khong-ton-tai")).isEmpty());
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
