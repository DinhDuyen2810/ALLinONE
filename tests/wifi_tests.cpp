// Kiểm thử lõi WiFi (dựng/phân tích XML hồ sơ WLAN). Không cần phần cứng WiFi hay quyền Admin.
// Build: cmake --build build --target wifi_tests && build\wifi_tests.exe
#include <QCoreApplication>
#include <cstdio>

#include "tools/wifi/engine/WlanProfileXml.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static bool roundTrips(const QString& ssid, WifiSecurity sec, const QString& password, bool autoConnect, const char* what)
{
    const QByteArray bytes = ssid.toUtf8();
    const QString xml = WlanProfileXml::build(bytes, ssid, sec, password, autoConnect);

    WifiProfile p;
    if (!WlanProfileXml::parse(xml, &p))
    {
        std::printf("  [%s] parse failed\n  xml=%s\n", what, qPrintable(xml));
        return false;
    }

    bool ok = true;
    ok &= (p.name == ssid);
    ok &= (p.ssid == ssid);
    ok &= (p.security == sec);
    ok &= (p.autoConnect == autoConnect);
    if (WifiSecurityUtil::requiresPassword(sec))
        ok &= (p.hasPassword && p.password == password);
    else
        ok &= !p.hasPassword;

    if (!ok)
        std::printf("  [%s] mismatch (name=%s ssid=%s sec=%d auto=%d hasPw=%d pw=%s)\n  xml=%s\n",
                    what, qPrintable(p.name), qPrintable(p.ssid), int(p.security), p.autoConnect, p.hasPassword,
                    qPrintable(p.password), qPrintable(xml));
    return ok;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- 1. Round-trip mọi loại bảo mật, có/không tự kết nối ----
    CHECK(roundTrips("MyHomeWifi", WifiSecurity::Wpa2Psk, "password123", true, "wpa2 auto"));
    CHECK(roundTrips("MyHomeWifi", WifiSecurity::Wpa2Psk, "password123", false, "wpa2 manual"));
    CHECK(roundTrips("Office5G", WifiSecurity::Wpa3Sae, "S3cur3P@ss!", true, "wpa3"));
    CHECK(roundTrips("OldRouter", WifiSecurity::WpaPsk, "legacykey", true, "wpa"));
    CHECK(roundTrips("AncientAP", WifiSecurity::Wep, "ABCDE12345", true, "wep"));
    CHECK(roundTrips("CafeOpen", WifiSecurity::Open, QString(), true, "open"));

    // ---- 2. Ký tự đặc biệt trong SSID và mật khẩu (XML phải tự escape đúng) ----
    CHECK(roundTrips("Wifi <Nhà> & \"Bạn\" 'ơi'", WifiSecurity::Wpa2Psk, "p@ss<>&\"'word", true, "xml special chars"));
    CHECK(roundTrips(QString::fromUtf8("Quán Cà Phê Việt"), WifiSecurity::Wpa2Psk, QString::fromUtf8("mậtkhẩu123"), true,
                     "vietnamese"));
    CHECK(roundTrips("日本語のWiFi", WifiSecurity::Wpa2Psk, "パスワード123", true, "japanese"));
    CHECK(roundTrips(QString(32, QChar('A')), WifiSecurity::Wpa2Psk, "longssid", true, "max length ssid"));
    CHECK(roundTrips("Tab\tAnd\nNewline", WifiSecurity::Wpa2Psk, "pw", true, "whitespace in ssid"));

    // ---- 3. Mật khẩu rỗng cho mạng cần mật khẩu: build vẫn ra XML hợp lệ dù rỗng (validate ở tầng gọi) ----
    {
        const QString xml = WlanProfileXml::build(QByteArray("Test"), "Test", WifiSecurity::Wpa2Psk, "", true);
        WifiProfile p;
        CHECK(WlanProfileXml::parse(xml, &p));
        CHECK(p.security == WifiSecurity::Wpa2Psk);
        CHECK(!p.hasPassword); // keyMaterial rỗng -> readElementText trả "" -> hasPassword=false theo thiết kế parse()
    }

    // ---- 4. Namespace đúng cho WPA3 (v3) khác với các loại khác (v1) ----
    {
        const QString xmlWpa3 = WlanProfileXml::build(QByteArray("X"), "X", WifiSecurity::Wpa3Sae, "pw12345678", true);
        const QString xmlWpa2 = WlanProfileXml::build(QByteArray("X"), "X", WifiSecurity::Wpa2Psk, "pw12345678", true);
        CHECK(xmlWpa3.contains("/profile/v3"));
        CHECK(xmlWpa2.contains("/profile/v1"));
        CHECK(xmlWpa3.contains("WPA3SAE"));
        CHECK(xmlWpa2.contains("WPA2PSK"));
    }

    // ---- 5. Open network: không có khối sharedKey ----
    {
        const QString xml = WlanProfileXml::build(QByteArray("Open"), "Open", WifiSecurity::Open, "", true);
        CHECK(!xml.contains("sharedKey"));
        CHECK(xml.contains("<authentication>open</authentication>"));
    }

    // ---- 6. WEP dùng keyType networkKey, các loại PSK dùng passPhrase ----
    {
        const QString xmlWep = WlanProfileXml::build(QByteArray("W"), "W", WifiSecurity::Wep, "ABCDE", true);
        const QString xmlWpa2 = WlanProfileXml::build(QByteArray("W"), "W", WifiSecurity::Wpa2Psk, "ABCDEFGH", true);
        CHECK(xmlWep.contains("networkKey"));
        CHECK(xmlWpa2.contains("passPhrase"));
    }

    // ---- 7. Hex encode/decode SSID ----
    {
        const QByteArray bytes = QByteArray::fromHex("48656C6C6F"); // "Hello"
        const QString hex = WlanProfileXml::toHex(bytes);
        CHECK(hex == "48656C6C6F");
        CHECK(WlanProfileXml::fromHex(hex) == bytes);
        CHECK(WlanProfileXml::fromHex("") .isEmpty());

        const QByteArray nonUtf8("\xC0\xC1\xF5", 3); // byte không hợp lệ UTF-8
        const QString hex2 = WlanProfileXml::toHex(nonUtf8);
        CHECK(WlanProfileXml::fromHex(hex2) == nonUtf8);
    }

    // ---- 8. parse() với XML hỏng/thiếu trường trả về false, không crash ----
    {
        WifiProfile p;
        CHECK(!WlanProfileXml::parse("", &p));
        CHECK(!WlanProfileXml::parse("<not valid xml", &p));
        CHECK(!WlanProfileXml::parse("<WLANProfile></WLANProfile>", &p)); // thiếu <name>
        CHECK(!WlanProfileXml::parse("<root><foo>bar</foo></root>", &p));
        CHECK(!WlanProfileXml::parse(QString(), nullptr)); // out=nullptr phải an toàn
    }

    // ---- 9b. keyMaterial bị mã hóa (protected=true, như khi KHÔNG xin plaintext key) không được
    //          coi là mật khẩu thật - đây là lỗi thật đã phát hiện khi test trên WiFi adapter thật.
    {
        const QString xml =
            "<?xml version=\"1.0\"?>"
            "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
            "<name>HomeNet</name>"
            "<SSIDConfig><SSID><hex>484F4D45</hex><name>HomeNet</name></SSID></SSIDConfig>"
            "<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
            "<MSM><security><authEncryption><authentication>WPA2PSK</authentication><encryption>AES</encryption>"
            "<useOneX>false</useOneX></authEncryption>"
            "<sharedKey><keyType>passPhrase</keyType><protected>true</protected>"
            "<keyMaterial>01000000D08C9DDF0115D1118C7A00C04FC297EB...(ciphertext)</keyMaterial></sharedKey>"
            "</security></MSM></WLANProfile>";
        WifiProfile p;
        CHECK(WlanProfileXml::parse(xml, &p));
        CHECK(p.security == WifiSecurity::Wpa2Psk);
        CHECK(!p.hasPassword); // protected=true => không phải mật khẩu dạng chữ, không được hiển thị
        CHECK(p.password.isEmpty());
    }

    // ---- 9. parse() hồ sơ Enterprise (useOneX=true) nhận diện đúng ----
    {
        const QString xml =
            "<?xml version=\"1.0\"?>"
            "<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">"
            "<name>CorpWifi</name>"
            "<SSIDConfig><SSID><hex>434F5250</hex><name>CorpWifi</name></SSID></SSIDConfig>"
            "<connectionType>ESS</connectionType><connectionMode>auto</connectionMode>"
            "<MSM><security><authEncryption><authentication>WPA2</authentication><encryption>AES</encryption>"
            "<useOneX>true</useOneX></authEncryption></security></MSM></WLANProfile>";
        WifiProfile p;
        CHECK(WlanProfileXml::parse(xml, &p));
        CHECK(p.security == WifiSecurity::Enterprise);
        CHECK(!WifiSecurityUtil::isSupportedForQuickConnect(p.security));
    }

    // ---- 10. WifiSecurityUtil: yêu cầu mật khẩu đúng theo từng loại ----
    CHECK(!WifiSecurityUtil::requiresPassword(WifiSecurity::Open));
    CHECK(WifiSecurityUtil::requiresPassword(WifiSecurity::Wep));
    CHECK(WifiSecurityUtil::requiresPassword(WifiSecurity::WpaPsk));
    CHECK(WifiSecurityUtil::requiresPassword(WifiSecurity::Wpa2Psk));
    CHECK(WifiSecurityUtil::requiresPassword(WifiSecurity::Wpa3Sae));
    CHECK(!WifiSecurityUtil::requiresPassword(WifiSecurity::Enterprise));
    CHECK(!WifiSecurityUtil::isSupportedForQuickConnect(WifiSecurity::Enterprise));
    CHECK(!WifiSecurityUtil::isSupportedForQuickConnect(WifiSecurity::Unknown));
    CHECK(WifiSecurityUtil::isSupportedForQuickConnect(WifiSecurity::Open));
    for (WifiSecurity s : {WifiSecurity::Open, WifiSecurity::Wep, WifiSecurity::WpaPsk, WifiSecurity::Wpa2Psk,
                           WifiSecurity::Wpa3Sae, WifiSecurity::Enterprise, WifiSecurity::Unknown})
        CHECK(!WifiSecurityUtil::displayName(s).isEmpty());

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
