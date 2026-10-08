// Kiểm thử lõi tự cập nhật (UpdateChecker) - THUẦN, không gọi mạng thật: so sánh phiên bản + phân tích
// JSON mẫu dựng sẵn theo đúng cấu trúc GET /repos/{owner}/{repo}/releases/latest của GitHub (xem tài
// liệu GitHub REST API), tách riêng khỏi phần gọi QNetworkAccessManager thật - giống mẫu đã dùng cho
// AdbController::internal::parseDevicesOutput.
#include <QCoreApplication>
#include <cstdio>

#include "core/update/UpdateChecker.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
