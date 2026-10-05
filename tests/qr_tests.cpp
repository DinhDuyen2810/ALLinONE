// Kiểm thử lõi QR (mã hóa, render, giải mã, payload). Không cần GUI.
// Build: cmake --build build --target qr_tests && build\qr_tests.exe
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QSvgRenderer>
#include <QTransform>
#include <cstdio>

#include "tools/qr/QRCodec.h"
#include "tools/qr/QRPayload.h"

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (cond) { ++g_pass; }                                          \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static bool roundTrips(const QString& text, QREcc ecc, const QRStyle& style, const char* what,
                       const QImage* transformed = nullptr)
{
    QString err;
    const QRMatrix m = QRCodec::encode(text, ecc, &err);
    if (!m.isValid())
    {
        std::printf("  [%s] encode failed: %s\n", what, qPrintable(err));
        return false;
    }
    const QImage img = transformed ? *transformed : QRCodec::render(m, style);
    const QList<QRDecoded> res = QRCodec::decode(img);
    const bool ok = res.size() >= 1 && res.first().text == text;
    if (!ok)
        std::printf("  [%s] decode mismatch (found %lld)\n", what, static_cast<long long>(res.size()));
    return ok;
}

static QImage makeLogo()
{
    QImage logo(64, 64, QImage::Format_ARGB32);
    logo.fill(Qt::transparent);
    QPainter p(&logo);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(220, 30, 30));
    p.setPen(Qt::NoPen);
    p.drawEllipse(4, 4, 56, 56);
    p.end();
    return logo;
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv); // cần cho QPainter/QImage một số nền tảng

    const QStringList samples = {
        "1",
        "HELLO WORLD",
        "https://example.com/path?query=1&x=y",
        "Xin chào Việt Nam! Tiếng Việt có dấu: ắằẳẵặ ềểễệ ơớờở ưứừử đ",
        "日本語のテキスト 😀",
        "0123456789012345678901234567890123456789",
        QString(500, 'a'),
        "line1\nline2\r\nline3",
    };

    // 1. Round-trip mọi mẫu x mọi mức ECC
    for (const QString& s : samples)
        for (QREcc ecc : {QREcc::Low, QREcc::Medium, QREcc::Quartile, QREcc::High})
        {
            QRStyle style;
            CHECK(roundTrips(s, ecc, style, qPrintable(QString("basic ecc=%1 len=%2").arg(int(ecc)).arg(s.size()))));
        }

    // 2. Nội dung dài gần giới hạn
    {
        QRStyle style;
        style.targetSize = 1400;
        CHECK(roundTrips(QString(2900, 'x'), QREcc::Low, style, "long 2900"));
        QString err;
        const QRMatrix tooLong = QRCodec::encode(QString(3000, 'x'), QREcc::Low, &err);
        CHECK(!tooLong.isValid() && !err.isEmpty());
        const QRMatrix empty = QRCodec::encode(QString(), QREcc::Low, &err);
        CHECK(!empty.isValid());
    }

    // 3. Màu sắc, kiểu ô bo tròn, kích thước nhỏ, viền hẹp
    {
        QRStyle style;
        style.foreground = QColor(20, 40, 120);
        style.background = QColor(255, 250, 220);
        CHECK(roundTrips("color test", QREcc::Medium, style, "colors"));

        style = QRStyle();
        style.roundedModules = true;
        style.targetSize = 600;
        CHECK(roundTrips("rounded modules https://example.com", QREcc::Quartile, style, "rounded"));

        style = QRStyle();
        style.targetSize = 120;
        CHECK(roundTrips("small", QREcc::Medium, style, "small 120px"));

        style = QRStyle();
        style.quietZone = 1;
        CHECK(roundTrips("narrow quiet zone", QREcc::Medium, style, "quiet 1"));

        style = QRStyle();
        style.quietZone = 0;
        CHECK(roundTrips("no quiet zone", QREcc::Medium, style, "quiet 0"));
    }

    // 4. Đảo màu (chữ trắng nền đen)
    {
        QRStyle style;
        style.foreground = Qt::white;
        style.background = Qt::black;
        CHECK(roundTrips("inverted code", QREcc::Medium, style, "inverted"));
    }

    // 5. Logo ở giữa với mức H
    {
        QRStyle style;
        style.logo = makeLogo();
        style.logoRatio = 0.22;
        style.targetSize = 600;
        CHECK(roundTrips("https://example.com/with-logo", QREcc::High, style, "logo H 0.22"));
        style.logoRatio = 0.30;
        CHECK(roundTrips("logo max", QREcc::High, style, "logo H 0.30"));
    }

    // 6. Biến đổi ảnh: thu nhỏ, xoay, nền thêm
    {
        QString err;
        const QRMatrix m = QRCodec::encode("transform test 123", QREcc::Medium, &err);
        QRStyle style;
        style.targetSize = 600;
        const QImage big = QRCodec::render(m, style);

        const QImage shrunk = big.scaled(150, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "downscale 150", &shrunk));

        const QImage rot90 = big.transformed(QTransform().rotate(90));
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "rotate 90", &rot90));

        QImage rot20(900, 900, QImage::Format_RGB32);
        rot20.fill(QColor(200, 200, 200));
        {
            QPainter p(&rot20);
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.translate(450, 450);
            p.rotate(20);
            p.translate(-300, -300);
            p.drawImage(0, 0, big);
        }
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "rotate 20 on gray bg", &rot20));

        // Ảnh lớn có mã nhỏ ở góc
        QImage canvas(3000, 2000, QImage::Format_RGB32);
        canvas.fill(QColor(240, 240, 240));
        {
            QPainter p(&canvas);
            p.drawImage(2200, 1300, big.scaled(500, 500, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "big canvas", &canvas));
    }

    // 7. Nhiều mã trong một ảnh
    {
        QRStyle style;
        style.targetSize = 300;
        const QImage a = QRCodec::render(QRCodec::encode("first code", QREcc::Medium), style);
        const QImage b = QRCodec::render(QRCodec::encode("second code", QREcc::Medium), style);
        QImage canvas(700, 340, QImage::Format_RGB32);
        canvas.fill(Qt::white);
        QPainter p(&canvas);
        p.drawImage(10, 20, a);
        p.drawImage(380, 20, b);
        p.end();
        const QList<QRDecoded> res = QRCodec::decode(canvas);
        CHECK(res.size() == 2);
        QStringList texts;
        for (const auto& r : res) texts << r.text;
        CHECK(texts.contains("first code") && texts.contains("second code"));
        CHECK(!res.isEmpty() && res.first().corners.size() == 4);
    }

    // 8. SVG render -> decode
    {
        QRStyle style;
        style.targetSize = 400;
        style.foreground = QColor(0, 0, 0);
        const QRMatrix m = QRCodec::encode("svg round trip", QREcc::Medium);
        const QString svg = QRCodec::toSvg(m, style);
        CHECK(svg.contains("<svg") && svg.contains("<path"));
        QSvgRenderer r(svg.toUtf8());
        CHECK(r.isValid());
        QImage img(400, 400, QImage::Format_ARGB32);
        img.fill(Qt::white);
        QPainter p(&img);
        r.render(&p);
        p.end();
        const auto res = QRCodec::decode(img);
        CHECK(res.size() == 1 && res.first().text == "svg round trip");

        // SVG có logo vẫn hợp lệ
        style.logo = makeLogo();
        const QString svg2 = QRCodec::toSvg(QRCodec::encode("svg logo", QREcc::High), style);
        CHECK(QSvgRenderer(svg2.toUtf8()).isValid() && svg2.contains("<image"));
    }

    // 9. Độ tương phản
    CHECK(QRCodec::contrastRatio(Qt::black, Qt::white) > 20.0);
    CHECK(QRCodec::contrastRatio(Qt::white, Qt::white) < 1.01);

    // 10. Payload: tạo -> quét -> phân tích
    {
        WifiInfo w;
        w.ssid = "My;Wi:Fi,\"Net\"";
        w.password = "p\\ss;w:ord,";
        w.security = "WPA";
        w.hidden = true;
        const QString s = QRPayload::makeWifi(w);
        QRStyle style;
        CHECK(roundTrips(s, QREcc::Medium, style, "wifi payload"));
        const ParsedPayload p = QRPayload::parse(s);
        CHECK(p.type == QRContentType::Wifi);
        CHECK(p.fields.value(0).second == w.ssid);
        CHECK(p.fields.value(2).second == w.password);
        CHECK(p.fields.value(3).second == "Có");

        WifiInfo open;
        open.ssid = "Open";
        open.security = "nopass";
        const QString so = QRPayload::makeWifi(open);
        CHECK(!so.contains("P:"));
        CHECK(QRPayload::parse(so).fields.value(1).second == "nopass");

        EmailInfo e{"a@b.com", "Chào bạn & bạn", "Nội dung\nthứ hai"};
        const QString es = QRPayload::makeEmail(e);
        const ParsedPayload ep = QRPayload::parse(es);
        CHECK(ep.type == QRContentType::Email);
        CHECK(ep.fields.value(0).second == "a@b.com");
        CHECK(ep.fields.value(1).second == e.subject);
        CHECK(ep.fields.value(2).second == e.body);
        CHECK(ep.actionUrl.startsWith("mailto:"));

        const QString ps = QRPayload::makePhone("+84 (912) 345-678");
        CHECK(ps == "tel:+84912345678");
        CHECK(QRPayload::parse(ps).type == QRContentType::Phone);

        const QString ss = QRPayload::makeSms({"0912 345 678", "Xin chào: bạn"});
        const ParsedPayload sp = QRPayload::parse(ss);
        CHECK(sp.type == QRContentType::Sms);
        CHECK(sp.fields.value(0).second == "0912345678");
        CHECK(sp.fields.value(1).second == "Xin chào: bạn");

        const QString gs = QRPayload::makeGeo({21.028511, 105.804817});
        const ParsedPayload gp = QRPayload::parse(gs);
        CHECK(gs == "geo:21.028511,105.804817");
        CHECK(gp.type == QRContentType::Geo && gp.actionUrl.contains("openstreetmap"));

        VCardInfo v;
        v.firstName = "Văn";
        v.lastName = "Nguyễn";
        v.organization = "Công ty; A, B";
        v.title = "Kỹ sư";
        v.phone = "+84912345678";
        v.email = "van@example.com";
        v.url = "https://example.com";
        v.address = "1 Phố Huế, Hà Nội";
        const QString vs = QRPayload::makeVCard(v);
        CHECK(roundTrips(vs, QREcc::Medium, style, "vcard payload"));
        const ParsedPayload vp = QRPayload::parse(vs);
        CHECK(vp.type == QRContentType::VCard);
        CHECK(vp.fields.value(0).second == "Văn Nguyễn");
        bool hasOrg = false;
        for (const auto& f : vp.fields)
            if (f.first == "Công ty" && f.second.contains("Công ty")) hasOrg = true;
        CHECK(hasOrg);

        CHECK(QRPayload::makeUrl("example.com") == "https://example.com");
        CHECK(QRPayload::makeUrl("http://x.y") == "http://x.y");
        CHECK(QRPayload::parse("https://example.com").type == QRContentType::Url);
        CHECK(QRPayload::parse("https://example.com").actionUrl == "https://example.com");
        CHECK(QRPayload::parse("hello world").type == QRContentType::Text);
        CHECK(QRPayload::parse("user@example.com").type == QRContentType::Email);
        CHECK(QRPayload::parse("+84 912 345 678").type == QRContentType::Phone);
        CHECK(QRPayload::parse("MECARD:N:Nguyen,Van;TEL:0912345678;;").type == QRContentType::VCard);
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
