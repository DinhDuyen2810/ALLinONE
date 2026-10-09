// Kiểm thử lõi WiFi (dựng/phân tích XML hồ sơ WLAN). Không cần phần cứng WiFi hay quyền Admin.
// Build: cmake --build build --target wifi_tests && build\wifi_tests.exe
#include <QCoreApplication>
#include <cstdio>

#include "tools/wifi/engine/WlanProfileXml.h"
#include "tools/wifi/model/WifiNetwork.h"

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

    // ---- 4. Namespace gốc LUÔN là v1, kể cả WPA3 ----
    // Hồi quy: hồ sơ WPA3 từng bị ghi gốc ".../profile/v3". Theo tài liệu "WLAN_profile schema" của
    // Microsoft, phần tử WLANProfile luôn ở namespace .../WLAN/profile/v1; WPA3SAE chỉ là một giá trị của
    // <authentication> trong chính schema đó.
    {
        const QString v1 = "xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\"";
        for (WifiSecurity s : {WifiSecurity::Open, WifiSecurity::Wep, WifiSecurity::WpaPsk, WifiSecurity::Wpa2Psk,
                               WifiSecurity::Wpa3Sae})
        {
            const QString xml = WlanProfileXml::build(QByteArray("X"), "X", s, "pw12345678", true);
            CHECK(xml.contains(v1));
            CHECK(!xml.contains("/profile/v2") && !xml.contains("/profile/v3") && !xml.contains("/profile/v4"));
        }
        const QString xmlWpa3 = WlanProfileXml::build(QByteArray("X"), "X", WifiSecurity::Wpa3Sae, "pw12345678", true);
        const QString xmlWpa2 = WlanProfileXml::build(QByteArray("X"), "X", WifiSecurity::Wpa2Psk, "pw12345678", true);
        CHECK(xmlWpa3.contains("<authentication>WPA3SAE</authentication>"));
        CHECK(xmlWpa2.contains("<authentication>WPA2PSK</authentication>"));
    }

    // ---- 4b. Mạng ẩn: <nonBroadcast>true</nonBroadcast> nằm trong SSIDConfig, SAU </SSID> ----
    {
        const QString hidden = WlanProfileXml::build(QByteArray("Hid"), "Hid", WifiSecurity::Wpa2Psk, "pw12345678", true, true);
        const QString normal = WlanProfileXml::build(QByteArray("Hid"), "Hid", WifiSecurity::Wpa2Psk, "pw12345678", true);
        CHECK(hidden.contains("<nonBroadcast>true</nonBroadcast>"));
        CHECK(!normal.contains("nonBroadcast"));
        const int ssidEnd = hidden.indexOf("</SSID>");
        const int nb = hidden.indexOf("<nonBroadcast>");
        const int cfgEnd = hidden.indexOf("</SSIDConfig>");
        CHECK(ssidEnd > 0 && nb > ssidEnd && cfgEnd > nb);
        WifiProfile p;
        CHECK(WlanProfileXml::parse(hidden, &p));
        CHECK(p.name == "Hid" && p.ssid == "Hid" && p.security == WifiSecurity::Wpa2Psk && p.password == "pw12345678");
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
        // Hồi quy: phải NHẬN BIẾT được trường hợp này (API trả thành công nhưng khóa mã hóa) để giao diện
        // hiện "(Cần quyền Admin)" thay vì "••••••••" không lời giải thích.
        CHECK(p.keyProtected);

        // Khóa dạng chữ / không có khóa thì keyProtected=false.
        WifiProfile plain;
        CHECK(WlanProfileXml::parse(WlanProfileXml::build(QByteArray("A"), "A", WifiSecurity::Wpa2Psk, "pw12345678", true), &plain));
        CHECK(plain.hasPassword && !plain.keyProtected);
        WifiProfile open;
        CHECK(WlanProfileXml::parse(WlanProfileXml::build(QByteArray("A"), "A", WifiSecurity::Open, "", true), &open));
        CHECK(!open.hasPassword && !open.keyProtected);
        // Tái dùng cùng một struct: kết quả lần parse trước không được rò sang lần sau.
        CHECK(WlanProfileXml::parse(xml, &plain));
        CHECK(!plain.hasPassword && plain.password.isEmpty() && plain.keyProtected);
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

    // ---- 11. Ánh xạ DOT11_AUTH_ALGORITHM -> WifiSecurity ----
    // Hồi quy: mọi giá trị > 7 (RSNA_PSK) từng bị coi là WPA3-Personal.
    {
        using WifiSecurityUtil::fromDot11;
        const unsigned kCipherNone = 0, kCipherWep = 0x101, kCipherCcmp = 4;
        CHECK(fromDot11(7, kCipherCcmp, false) == WifiSecurity::Open); // bSecurityEnabled=false thắng tất cả
        CHECK(fromDot11(1, kCipherNone, true) == WifiSecurity::Open);
        CHECK(fromDot11(1, kCipherWep, true) == WifiSecurity::Wep);
        CHECK(fromDot11(2, kCipherWep, true) == WifiSecurity::Wep);
        CHECK(fromDot11(3, kCipherCcmp, true) == WifiSecurity::Enterprise);  // WPA
        CHECK(fromDot11(4, kCipherCcmp, true) == WifiSecurity::WpaPsk);
        CHECK(fromDot11(5, kCipherCcmp, true) == WifiSecurity::Unknown);     // WPA_NONE (không hỗ trợ)
        CHECK(fromDot11(6, kCipherCcmp, true) == WifiSecurity::Enterprise);  // RSNA (WPA2-Enterprise)
        CHECK(fromDot11(7, kCipherCcmp, true) == WifiSecurity::Wpa2Psk);
        CHECK(fromDot11(8, kCipherCcmp, true) == WifiSecurity::Enterprise);  // WPA3-Enterprise 192-bit
        CHECK(fromDot11(9, kCipherCcmp, true) == WifiSecurity::Wpa3Sae);
        CHECK(fromDot11(10, kCipherCcmp, true) == WifiSecurity::Unknown);    // OWE - không dùng mật khẩu chung
        CHECK(fromDot11(11, kCipherCcmp, true) == WifiSecurity::Enterprise); // WPA3-Enterprise
        CHECK(fromDot11(12, kCipherCcmp, true) == WifiSecurity::Unknown);    // giá trị tương lai
        CHECK(fromDot11(0x80000001u, kCipherCcmp, true) == WifiSecurity::Unknown); // riêng của hãng (IHV)
        CHECK(!WifiSecurityUtil::requiresPassword(fromDot11(11, kCipherCcmp, true)));
    }

    // ---- 12. Gộp mục trùng của danh sách mạng ----
    {
        auto net = [](const QString& ssid, WifiSecurity sec, int signal, bool hasProfile, const QString& profile, bool connected = false) {
            WifiNetwork n;
            n.ssid = ssid;
            n.ssidBytes = ssid.toUtf8();
            n.security = sec;
            n.signalQuality = signal;
            n.hasProfile = hasProfile;
            n.profileName = profile;
            n.connected = connected;
            n.numberOfBssids = 1;
            n.hidden = ssid.isEmpty();
            return n;
        };

        // Hồi quy: mục KHÔNG gắn hồ sơ đứng trước mục có hồ sơ -> mục gộp từng mất profileName, nút
        // "Quên mạng này" sáng nhưng bấm không làm gì.
        QList<WifiNetwork> in = {net("Home", WifiSecurity::Wpa2Psk, 40, false, QString()),
                                 net("Home", WifiSecurity::Wpa2Psk, 80, true, "Home", true),
                                 net("Cafe", WifiSecurity::Open, 55, false, QString())};
        QList<WifiNetwork> out = WifiNetworkUtil::mergeDuplicates(in);
        CHECK(out.size() == 2);
        CHECK(out[0].ssid == "Home" && out[0].hasProfile && out[0].profileName == "Home");
        CHECK(out[0].signalQuality == 80 && out[0].connected && out[0].numberOfBssids == 2);
        CHECK(out[1].ssid == "Cafe" && !out[1].hasProfile && out[1].profileName.isEmpty());

        // Thứ tự ngược lại cho cùng kết quả; cùng SSID khác loại bảo mật thì KHÔNG gộp.
        in = {net("Home", WifiSecurity::Wpa2Psk, 80, true, "Home"), net("Home", WifiSecurity::Wpa2Psk, 40, false, QString()),
              net("Home", WifiSecurity::Open, 30, false, QString())};
        out = WifiNetworkUtil::mergeDuplicates(in);
        CHECK(out.size() == 2 && out[0].profileName == "Home" && out[1].security == WifiSecurity::Open);

        // Mạng ẩn: ssid RỖNG (không còn chuỗi giả "(Mạng ẩn)" trong ssid), tên trang trí chỉ ở displayName().
        in = {net(QString(), WifiSecurity::Wpa2Psk, 50, false, QString()), net(QString(), WifiSecurity::Wpa2Psk, 60, false, QString()),
              net(QString(), WifiSecurity::Wpa2Psk, 70, true, "Secret")};
        out = WifiNetworkUtil::mergeDuplicates(in);
        CHECK(out.size() == 2);
        CHECK(out[0].ssid.isEmpty() && out[0].hidden && out[0].signalQuality == 60);
        CHECK(out[0].displayName() == "(Mạng ẩn)");
        CHECK(out[1].ssid.isEmpty() && out[1].displayName().contains("Secret"));
        CHECK(net("Home", WifiSecurity::Open, 1, false, QString()).displayName() == "Home");
        CHECK(WifiNetworkUtil::mergeDuplicates({}).isEmpty());
    }

    // ---- SSID có ký tự điều khiển: XML KHÔNG thành lỗi mà thành hồ sơ mang tên KHÁC -> phải bị chặn từ trước ----
    {
        // Hồi quy: QXmlStreamWriter lặng lẽ bỏ ký tự không hợp lệ, bộ đọc XML đổi CR thành LF. SSID "Nha<0x01>"
        // từng được dựng thành hồ sơ tên "Nha" - đè lên hồ sơ "Nha" thật mà không sao lưu (WlanController sao
        // lưu/kết nối/hoàn tác theo tên "Nha<0x01>").
        WifiProfile p;
        const QString evil = QString("Nha") + QChar(0x01);
        CHECK(WlanProfileXml::parse(WlanProfileXml::build(evil.toUtf8(), evil, WifiSecurity::Wpa2Psk, "matkhau123", true), &p));
        CHECK(p.name == "Nha"); // đúng hiện tượng: tên đã bị rút gọn
        CHECK(!WlanProfileXml::isSafeProfileName(evil));
        CHECK(!WlanProfileXml::isSafeProfileName(QString("Nha") + QChar(u'\0') + "x"));
        CHECK(!WlanProfileXml::isSafeProfileName("Nha\rB"));
        CHECK(!WlanProfileXml::isSafeProfileName(QString("Nha") + QChar(0xD83D))); // surrogate lẻ
        CHECK(!WlanProfileXml::isSafeProfileName(QString()));
        CHECK(WlanProfileXml::isSafeProfileName("Wifi <Nhà> & \"Bạn\" 'ơi'"));
        CHECK(WlanProfileXml::isSafeProfileName("Tab\tAnd\nNewline"));
        CHECK(WlanProfileXml::isSafeProfileName(QString::fromUtf8("Quán \xF0\x9F\x98\x80")));
        CHECK(WlanProfileXml::isSafeProfileName(QString::fromLatin1("caf\xE9\x85"))); // SSID không phải UTF-8 (Latin-1)
    }

    // ---- Hồ sơ trùng TÊN nhưng của mạng khác: nhận ra bằng <hex>, không bằng tên ----
    {
        // Windows tự đặt tên "Foo 2" cho hồ sơ thứ hai của SSID "Foo". Một mạng có SSID thật là "Foo 2" khi đó
        // trùng TÊN với hồ sơ này - ghi đè sẽ xóa mất hồ sơ của mạng "Foo".
        const QString foo2 = WlanProfileXml::build("Foo", "Foo 2", WifiSecurity::Wpa2Psk, "matkhau123", true);
        CHECK(WlanProfileXml::describesSsid(foo2, "Foo"));
        CHECK(!WlanProfileXml::describesSsid(foo2, "Foo 2"));
        CHECK(!WlanProfileXml::describesSsid(foo2, QByteArray()));
        const QByteArray raw = QByteArray::fromHex("ff00fe41"); // SSID không phải UTF-8, có byte 0
        CHECK(WlanProfileXml::describesSsid(WlanProfileXml::build(raw, "x", WifiSecurity::Open, QString(), false), raw));
        CHECK(WlanProfileXml::describesSsid(QString(foo2).replace("466F6F", "466f6f"), "Foo")); // hex chữ thường
        // Không đủ căn cứ (XML hỏng, không có <hex>) thì KHÔNG kết luận "mạng khác".
        CHECK(WlanProfileXml::describesSsid("khong phai xml", "Foo"));
        CHECK(WlanProfileXml::describesSsid(QString(foo2).remove("<hex>466F6F</hex>"), "Bar"));
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
