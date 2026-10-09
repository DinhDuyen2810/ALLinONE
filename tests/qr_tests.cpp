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

        // Hồi quy: chuỗi TOÀN CHỮ SỐ dài hơn maxBytes() (giới hạn chế độ byte) vẫn vừa mã QR ở chế độ
        // numeric - trước đây bị chặn oan bởi phép so với maxBytes().
        const QString digits = QString(4000, QChar('7'));
        CHECK(digits.size() > QRCodec::maxBytes(QREcc::Low));
        err.clear();
        const QRMatrix numeric = QRCodec::encode(digits, QREcc::Low, &err);
        CHECK(numeric.isValid() && err.isEmpty());
        CHECK(roundTrips(digits, QREcc::Low, style, "numeric 4000 digits"));
        // ...nhưng vượt sức chứa thật thì vẫn phải báo lỗi rõ ràng (không ném ngoại lệ ra ngoài).
        err.clear();
        CHECK(!QRCodec::encode(QString(7090, QChar('7')), QREcc::Low, &err).isValid() && !err.isEmpty());
        err.clear();
        CHECK(!QRCodec::encode(QString(5000, QChar('7')), QREcc::High, &err).isValid() && !err.isEmpty());

        // Hồi quy: nội dung có ký tự NUL (U+0000) ở giữa từng bị cắt cụt tại NUL khi mã hóa.
        const QString withNul = QString("ab") + QChar(0) + QString("cd") + QChar(0) + QString("ef");
        CHECK(withNul.size() == 8);
        QRStyle plain;
        CHECK(roundTrips(withNul, QREcc::Medium, plain, "embedded NUL"));
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

        // Hồi quy: mã bị lật gương (ảnh chụp qua gương/camera trước) - quirc_flip() rồi thử lại.
        const QImage mirroredH = big.transformed(QTransform().scale(-1, 1));
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "mirrored horizontally", &mirroredH));
        const QImage mirroredV = big.transformed(QTransform().scale(1, -1));
        CHECK(roundTrips("transform test 123", QREcc::Medium, style, "mirrored vertically", &mirroredV));
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

        // Hồi quy: mã thứ hai RẤT NHỎ (~1,5 px/ô) - ở tỉ lệ gốc quirc nhìn thấy lưới nhưng không giải mã
        // được, phải phóng to mới đọc ra. decode() cũ trả về ngay sau lượt đầu (chỉ 1 mã); nay thấy còn
        // lưới chưa đọc được thì chạy tiếp lượt phóng to và gộp kết quả. (Kích thước 63/48 px lấy từ thực
        // nghiệm với bản decode() cũ: cả hai đều chỉ ra 1 mã.)
        const QRMatrix tinyMatrix = QRCodec::encode("second code - smaller one", QREcc::Medium);
        QRStyle tinyStyle;
        tinyStyle.targetSize = (tinyMatrix.size + 8) * 6;
        const QImage tinySource = QRCodec::render(tinyMatrix, tinyStyle);
        for (int tinySide : {63, 48})
        {
            QImage mixed(700, 340, QImage::Format_RGB32);
            mixed.fill(Qt::white);
            QPainter pm(&mixed);
            pm.drawImage(10, 20, a);
            pm.drawImage(380, 20, tinySource.scaled(tinySide, tinySide, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            pm.end();
            const QList<QRDecoded> both = QRCodec::decode(mixed);
            QStringList bothTexts;
            for (const auto& r : both) bothTexts << r.text;
            CHECK(both.size() == 2);
            CHECK(bothTexts.contains("first code") && bothTexts.contains("second code - smaller one"));
            // Tọa độ góc của mã đọc được ở lượt phóng to vẫn phải quy về ảnh GỐC (nằm trong vùng mã nhỏ).
            for (const auto& r : both)
                if (r.text.startsWith("second"))
                    for (const QPoint& c : r.corners)
                        CHECK(c.x() >= 370 && c.x() <= 390 + tinySide && c.y() >= 10 && c.y() <= 30 + tinySide);
        }
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

        // Hồi quy: toSvg() từng bỏ qua roundedModules (SVG ra ô vuông dù bản xem trước bo tròn).
        QRStyle round;
        round.targetSize = 400;
        round.roundedModules = true;
        const QRMatrix mr = QRCodec::encode("svg rounded modules", QREcc::Quartile);
        const QString svgRound = QRCodec::toSvg(mr, round);
        QRStyle square = round;
        square.roundedModules = false;
        const QString svgSquare = QRCodec::toSvg(mr, square);
        CHECK(svgRound != svgSquare);
        CHECK(svgRound.contains("rx=\"0.32\"") && !svgSquare.contains("rx=\"0.32\""));
        CHECK(svgSquare.contains("crispEdges") && !svgRound.contains("crispEdges"));
        QSvgRenderer rr(svgRound.toUtf8());
        CHECK(rr.isValid());
        QImage imgRound(400, 400, QImage::Format_ARGB32);
        imgRound.fill(Qt::white);
        {
            QPainter pr(&imgRound);
            pr.setRenderHint(QPainter::Antialiasing);
            rr.render(&pr);
        }
        const auto resRound = QRCodec::decode(imgRound);
        CHECK(resRound.size() == 1 && resRound.first().text == "svg rounded modules");
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
        CHECK(p.secretField == 2 && p.fields.value(p.secretField).first == "Mật khẩu");

        // Bản hiển thị: che đúng giá trị P:, mọi phần khác (kể cả ký tự escape trong SSID) giữ nguyên.
        const QString masked = QRPayload::maskSecrets(s);
        CHECK(masked != s);
        CHECK(!masked.contains("ss\\;w") && !masked.contains("ord"));
        CHECK(masked.contains(QRPayload::secretMask()));
        CHECK(masked.startsWith("WIFI:T:WPA;S:" + QRPayload::escapeWifi(w.ssid) + ";P:" + QRPayload::secretMask() + ";"));
        const ParsedPayload pm = QRPayload::parse(masked);
        CHECK(pm.fields.value(0).second == w.ssid && pm.fields.value(2).second == QRPayload::secretMask());
        CHECK(QRPayload::maskSecrets("https://example.com/?P:secret;") == "https://example.com/?P:secret;");
        CHECK(QRPayload::maskSecrets("hello; P:world") == "hello; P:world");
        CHECK(QRPayload::maskSecrets("wifi:S:x;t:WPA;p:abc12345;;") == "wifi:S:x;t:WPA;p:" + QRPayload::secretMask() + ";;");

        WifiInfo open;
        open.ssid = "Open";
        open.security = "nopass";
        const QString so = QRPayload::makeWifi(open);
        CHECK(!so.contains("P:"));
        CHECK(QRPayload::parse(so).fields.value(1).second == "nopass");
        CHECK(QRPayload::parse(so).secretField == -1); // không có mật khẩu thì không có gì phải che
        CHECK(QRPayload::maskSecrets(so) == so);

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
        // Hồi quy: makeVCard() xuất N trước FN - parse() từng thêm HAI dòng "Họ tên".
        auto countName = [](const ParsedPayload& pp) {
            int n = 0;
            for (const auto& f : pp.fields)
                if (f.first == "Họ tên") ++n;
            return n;
        };
        CHECK(countName(vp) == 1);
        const ParsedPayload fnFirst = QRPayload::parse("BEGIN:VCARD\nVERSION:3.0\nFN:Tên Đầy Đủ\nN:Ho;Ten;;;\nEND:VCARD");
        CHECK(countName(fnFirst) == 1 && fnFirst.fields.value(0).second == "Tên Đầy Đủ");
        const ParsedPayload nFirst = QRPayload::parse("BEGIN:VCARD\nVERSION:3.0\nN:Ho;Ten;;;\nTEL:123\nFN:Tên Đầy Đủ\nEND:VCARD");
        CHECK(countName(nFirst) == 1 && nFirst.fields.value(0).second == "Tên Đầy Đủ"); // FN được ưu tiên
        const ParsedPayload nOnly = QRPayload::parse("BEGIN:VCARD\nVERSION:3.0\nN:Ho;Ten;;;\nEND:VCARD");
        CHECK(countName(nOnly) == 1 && nOnly.fields.value(0).second == "Ten Ho");
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

        // Hồi quy: MATMSG có SUB/BODY đứng trước TO từng lấy tiêu đề làm địa chỉ mailto:.
        const ParsedPayload mm = QRPayload::parse("MATMSG:SUB:Xin chao;BODY:noi dung;TO:nguoi@example.com;;");
        CHECK(mm.type == QRContentType::Email);
        CHECK(mm.actionUrl == "mailto:nguoi@example.com");
        const ParsedPayload mmStd = QRPayload::parse("MATMSG:TO:a@b.com;SUB:s;BODY:b;;");
        CHECK(mmStd.actionUrl == "mailto:a@b.com");
        const ParsedPayload mmNoTo = QRPayload::parse("MATMSG:SUB:chi co tieu de;;");
        CHECK(mmNoTo.actionUrl.isEmpty()); // không có người nhận thì không dựng mailto: từ trường khác
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
