// Công cụ kiểm tra tay CHỈ ĐỌC (không đụng hệ thống): dựng mã QR theo nhiều kiểu của các ứng dụng khác nhau (tests/QrAppStyles.h), áp hậu xử lý đời thật
// rồi giải mã bằng ĐÚNG QRCodec::decode(); in tỉ lệ đọc được theo từng kịch bản và liệt kê tổ hợp hỏng.
// Dùng: qr_style_matrix [lọc-tên-kịch-bản]   (biến QR_MATRIX_SAVE=<thư mục> lưu ảnh hỏng)
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QRegularExpression>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "QrAppStyles.h"

using namespace qrstyle;

namespace
{
struct Payload
{
    QString name;
    QString text;
};

const std::vector<Payload>& payloads()
{
    static const std::vector<Payload> p = {
        {"url", "https://t.me/example_user_42"},
        {"vietqr", "00020101021238540010A0000007270124000697041601101234567890208QRIBFTTA53037045405100005802VN62150811Thanh toan 6304ABCD"},
        {"totp", "otpauth://totp/Example:alice@example.com?secret=JBSWY3DPEHPK3PXP&issuer=Example&algorithm=SHA1&digits=6&period=30"},
        {"wifi", "WIFI:T:WPA;S:MyNetwork;P:supersecretpassword123;;"},
        {"migr", "otpauth-migration://offline?data=CjEKCkhlbGxvId6tvu8SGEV4YW1wbGU6YWxpY2VAZXhhbXBsZS5jb20aB0V4YW1wbGUgASgBMAIQARgBIAAojrzh2gQ%3D"},
        {"long", QString("https://example.com/path/to/a/rather/long/resource?token=") + QString(180, QChar('x')) + "abc123"},
    };
    return p;
}

struct Scenario
{
    QString name;
    std::function<Spec(double px)> spec;
    std::vector<double> pxs;
    std::function<QImage(const QImage&)> post; // hậu xử lý (có thể rỗng)
    QREcc ecc{QREcc::Medium};
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString filter = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString();
    const QString save = qEnvironmentVariable("QR_MATRIX_SAVE");

    const QColor dark(20, 24, 33);
    auto base = [](Dots d, Eyes e) {
        return [d, e](double px) {
            Spec s;
            s.dots = d;
            s.eyes = e;
            s.px = px;
            return s;
        };
    };
    auto withLogo = [](Dots d, Eyes e, Logo l, double ratio, bool carve) {
        return [=](double px) {
            Spec s;
            s.dots = d;
            s.eyes = e;
            s.logo = l;
            s.logoRatio = ratio;
            s.carveLogo = carve;
            s.px = px;
            return s;
        };
    };
    auto screenOf = [](const QColor& bg, int margin) {
        return [bg, margin](const QImage& src) { return camera(src, bg, 0, 0, margin); };
    };

    const std::vector<double> normalPx = {3.0, 4.0, 5.0, 7.0, 10.0};
    std::vector<Scenario> sc;
    // --- hình dạng ô / ô định vị (ảnh sạch) ---
    sc.push_back({"plain", base(Dots::Square, Eyes::Square), normalPx, nullptr});
    sc.push_back({"gap", base(Dots::Gap, Eyes::Square), normalPx, nullptr});
    sc.push_back({"round", base(Dots::Round, Eyes::Square), normalPx, nullptr});
    sc.push_back({"circle-dots", base(Dots::Circle, Eyes::Square), normalPx, nullptr});
    sc.push_back({"circle-dots+round-eyes", base(Dots::Circle, Eyes::Rounded), normalPx, nullptr});
    sc.push_back({"circle-dots+circle-eyes", base(Dots::Circle, Eyes::Circle), normalPx, nullptr});
    sc.push_back({"blob+round-eyes", base(Dots::Blob, Eyes::Rounded), normalPx, nullptr});
    sc.push_back({"bars+round-eyes", base(Dots::Bars, Eyes::Rounded), normalPx, nullptr});
    sc.push_back({"diamond", base(Dots::Diamond, Eyes::Square), normalPx, nullptr});
    sc.push_back({"square+dot-inner-eye", base(Dots::Square, Eyes::DotInner), normalPx, nullptr});
    // --- logo giữa (ECC cao) ---
    sc.push_back({"logo-box-25", withLogo(Dots::Square, Eyes::Square, Logo::SquareBox, 0.25, false), normalPx, nullptr, QREcc::High});
    sc.push_back({"logo-circle-carved", withLogo(Dots::Round, Eyes::Rounded, Logo::CircleBadge, 0.24, true), normalPx, nullptr, QREcc::High});
    sc.push_back({"logo-circle-carved+circle-eyes", withLogo(Dots::Circle, Eyes::Circle, Logo::CircleBadge, 0.22, true), normalPx, nullptr, QREcc::High});
    // --- màu ---
    sc.push_back({"inverted(white-on-dark)", [](double px) { Spec s; s.px = px; s.card = QColor(20, 24, 33); s.ink = Qt::white; s.eyeInk = Qt::white; return s; }, normalPx, nullptr});
    sc.push_back({"inverted+round", [](double px) { Spec s; s.px = px; s.dots = Dots::Round; s.eyes = Eyes::Rounded; s.card = QColor(20, 24, 33); s.ink = Qt::white; s.eyeInk = Qt::white; return s; }, normalPx, nullptr});
    sc.push_back({"colour-brand(purple)", [](double px) { Spec s; s.px = px; s.ink = QColor(115, 60, 190); s.eyeInk = s.ink; return s; }, normalPx, nullptr});
    sc.push_back({"colour-pink(momo)", [](double px) { Spec s; s.px = px; s.ink = QColor(174, 32, 112); s.eyeInk = s.ink; s.dots = Dots::Round; return s; }, normalPx, nullptr});
    sc.push_back({"gradient-blue-purple", [](double px) { Spec s; s.px = px; s.gradient = true; s.ink = QColor(20, 40, 160); s.ink2 = QColor(150, 30, 160); s.eyeInk = s.ink; s.dots = Dots::Round; s.eyes = Eyes::Rounded; return s; }, normalPx, nullptr});
    sc.push_back({"gradient-light-ink", [](double px) { Spec s; s.px = px; s.gradient = true; s.ink = QColor(255, 130, 40); s.ink2 = QColor(220, 40, 90); s.eyeInk = s.ink; s.card = QColor(255, 250, 240); return s; }, normalPx, nullptr});
    sc.push_back({"light-ink-on-dark-card(cyan)", [](double px) { Spec s; s.px = px; s.card = QColor(10, 20, 40); s.ink = QColor(80, 220, 255); s.eyeInk = s.ink; s.dots = Dots::Round; return s; }, normalPx, nullptr});
    sc.push_back({"low-contrast(40%)", base(Dots::Square, Eyes::Square), normalPx, [](const QImage& i) { return lowContrast(i, 0.4); }});
    // --- vùng yên tĩnh ---
    sc.push_back({"quiet-1", [](double px) { Spec s; s.px = px; s.quiet = 1; return s; }, normalPx, nullptr});
    sc.push_back({"quiet-0(cut-to-edge)", [](double px) { Spec s; s.px = px; s.quiet = 0; return s; }, normalPx, nullptr});
    // --- chụp màn hình: dán lên nền, lề khác nhau, ô co lẻ ---
    sc.push_back({"screen-dark+plain", base(Dots::Square, Eyes::Square), {2.5, 3.0, 4.0, 6.0}, screenOf(dark, 20)});
    sc.push_back({"screen-dark+circle-dots", base(Dots::Circle, Eyes::Rounded), {3.0, 4.0, 6.0}, screenOf(dark, 20)});
    sc.push_back({"screen-dark+inverted", [](double px) { Spec s; s.px = px; s.card = QColor(20, 24, 33); s.ink = Qt::white; s.eyeInk = Qt::white; return s; }, {3.0, 4.0, 6.0}, screenOf(QColor(0, 0, 0), 20)});
    // --- hậu xử lý ảnh kém ---
    sc.push_back({"jpeg-q60", base(Dots::Square, Eyes::Square), {4.0, 6.0, 10.0}, [](const QImage& i) { return jpeg(i, 60); }});
    sc.push_back({"jpeg-q40+round", base(Dots::Round, Eyes::Rounded), {5.0, 7.0, 10.0}, [](const QImage& i) { return jpeg(i, 40); }});
    sc.push_back({"blur-2px", base(Dots::Square, Eyes::Square), {6.0, 8.0, 12.0}, [](const QImage& i) { return lowRes(i, 2.0); }});
    sc.push_back({"blur-3px", base(Dots::Square, Eyes::Square), {8.0, 10.0, 14.0}, [](const QImage& i) { return lowRes(i, 3.0); }});
    sc.push_back({"noise-35", base(Dots::Square, Eyes::Square), {5.0, 7.0, 10.0}, [](const QImage& i) { return noise(i, 35); }});
    sc.push_back({"photo(blur+noise+jpeg)", base(Dots::Square, Eyes::Square), {8.0, 10.0, 14.0}, [](const QImage& i) { return jpeg(noise(lowRes(i, 2.0), 20), 70); }});
    // --- xoay / nghiêng (camera) ---
    sc.push_back({"rot-90", base(Dots::Square, Eyes::Square), {5.0, 8.0}, [](const QImage& i) { return camera(i, Qt::white, 90, 0, 10); }});
    sc.push_back({"rot-180", base(Dots::Square, Eyes::Square), {5.0, 8.0}, [](const QImage& i) { return camera(i, Qt::white, 180, 0, 10); }});
    sc.push_back({"rot-17", base(Dots::Square, Eyes::Square), {5.0, 8.0}, [](const QImage& i) { return camera(i, Qt::white, 17, 0, 10); }});
    sc.push_back({"rot-45", base(Dots::Square, Eyes::Square), {6.0, 9.0}, [](const QImage& i) { return camera(i, Qt::white, 45, 0, 10); }});
    sc.push_back({"tilt-persp-8%", base(Dots::Square, Eyes::Square), {6.0, 9.0}, [](const QImage& i) { return camera(i, Qt::white, 5, 0.08, 10); }});
    sc.push_back({"tilt-persp-8%+circle-dots", base(Dots::Circle, Eyes::Rounded), {7.0, 10.0}, [](const QImage& i) { return camera(i, Qt::white, 5, 0.08, 10); }});
    sc.push_back({"mirrored", base(Dots::Square, Eyes::Square), {5.0, 8.0}, [](const QImage& i) { return i.mirrored(true, false); }});

    // --- ảnh đời thật khó hơn ---
    sc.push_back({"camera-uneven-light", base(Dots::Square, Eyes::Square), {6.0, 9.0}, [](const QImage& i) { return shade(camera(i, Qt::white, 8, 0.04, 20), 0.55); }});
    sc.push_back({"camera-glare", base(Dots::Square, Eyes::Square), {6.0, 9.0}, [](const QImage& i) { return glare(i, 0.7, 0.3, 0.35); }});
    sc.push_back({"camera-strong-tilt-15%", base(Dots::Square, Eyes::Square), {7.0, 10.0}, [](const QImage& i) { return camera(i, Qt::white, 10, 0.15, 20); }});
    sc.push_back({"camera+blur+tilt+round", base(Dots::Round, Eyes::Rounded), {8.0, 11.0}, [](const QImage& i) { return jpeg(lowRes(camera(i, Qt::white, 6, 0.07, 20), 2.0), 75); }});
    sc.push_back({"red-eyes+black-dots", [](double px) { Spec s; s.px = px; s.eyes = Eyes::Rounded; s.eyeInk = QColor(200, 30, 40); s.dots = Dots::Round; return s; }, normalPx, nullptr});
    sc.push_back({"card-vietqr(banner+text)", base(Dots::Square, Eyes::Square), {5.0, 7.0}, [](const QImage& i) { return cardAround(i, QColor(0, 130, 200)); }});
    sc.push_back({"card+logo-box+jpeg", withLogo(Dots::Square, Eyes::Square, Logo::SquareBox, 0.2, false), {5.0, 7.0}, [](const QImage& i) { return jpeg(cardAround(i, QColor(220, 30, 120)), 70); }, QREcc::High});
    sc.push_back({"gap+round-eyes", [](double px) { Spec s; s.px = px; s.dots = Dots::Gap; s.eyes = Eyes::Rounded; return s; }, {6.0, 10.0}, nullptr});
    sc.push_back({"gap+jpeg", base(Dots::Gap, Eyes::Square), {7.0, 10.0}, [](const QImage& i) { return jpeg(i, 60); }});
    sc.push_back({"gap-inverted", [](double px) { Spec s; s.px = px; s.dots = Dots::Gap; s.card = QColor(20, 24, 33); s.ink = Qt::white; s.eyeInk = Qt::white; return s; }, {7.0, 10.0}, nullptr});
    sc.push_back({"circle-eyes+jpeg", base(Dots::Circle, Eyes::Circle), {5.0, 8.0}, [](const QImage& i) { return jpeg(i, 55); }});
    sc.push_back({"circle-eyes+tilt", base(Dots::Circle, Eyes::Circle), {8.0, 10.0}, [](const QImage& i) { return camera(i, Qt::white, 20, 0.08, 20); }});
    sc.push_back({"rot-30+blob-eyes", base(Dots::Blob, Eyes::Rounded), {8.0, 11.0}, [](const QImage& i) { return camera(i, Qt::white, 30, 0, 20); }});
    sc.push_back({"screen-dark+logo-circle", withLogo(Dots::Round, Eyes::Rounded, Logo::CircleBadge, 0.22, true), {4.0, 6.0}, screenOf(dark, 30), QREcc::High});
    sc.push_back({"ecc-L-dense(v11)", base(Dots::Square, Eyes::Square), {3.0, 4.0}, nullptr, QREcc::Low});

    if (qEnvironmentVariableIsSet("QR_MATRIX_RANDOM"))
    {
        // Chế độ ngẫu nhiên: trộn kiểu ô/ô định vị/logo/màu/kích thước + chuỗi hậu xử lý; hạt giống cố định để lặp lại được.
        const int trials = qEnvironmentVariableIntValue("QR_MATRIX_RANDOM");
        quint32 rs = qEnvironmentVariableIsSet("QR_MATRIX_SEED") ? qEnvironmentVariableIntValue("QR_MATRIX_SEED") : 4242;
        auto rnd = [&](int n) { rs = rs * 1664525u + 1013904223u; return static_cast<int>((rs >> 8) % static_cast<quint32>(n)); };
        auto rndf = [&](double lo, double hi) { return lo + (hi - lo) * rnd(10000) / 9999.0; };
        auto lum = [](const QColor& c) { return 0.2126 * c.redF() + 0.7152 * c.greenF() + 0.0722 * c.blueF(); };
        const char* dotNames[] = {"square", "gap", "round", "circle", "blob", "bars", "diamond"};
        const char* eyeNames[] = {"sq-eyes", "round-eyes", "circle-eyes", "dot-eyes"};
        int ok = 0;
        QStringList failures;
        for (int t = 0; t < trials; ++t)
        {
            Spec sp;
            sp.dots = static_cast<Dots>(rnd(7));
            sp.eyes = static_cast<Eyes>(rnd(4));
            sp.logo = rnd(3) == 0 ? static_cast<Logo>(1 + rnd(2)) : Logo::None;
            sp.logoRatio = rndf(0.16, 0.24);
            sp.carveLogo = sp.logo != Logo::None && rnd(2) == 0;
            sp.px = rndf(3.5, 11.0);
            const int quiets[] = {1, 2, 4, 4};
            sp.quiet = quiets[rnd(4)];
            // màu: mực tối trên nền sáng, hoặc đảo; bắt buộc chênh sáng đủ lớn
            QColor a(rnd(130), rnd(130), rnd(130)), b(150 + rnd(106), 150 + rnd(106), 150 + rnd(106));
            const bool inv = rnd(4) == 0;
            sp.ink = inv ? b : a;
            sp.card = inv ? a : b;
            sp.eyeInk = rnd(4) == 0 ? QColor(rnd(130), rnd(130), rnd(130)) : sp.ink;
            if (inv)
                sp.eyeInk = sp.ink;
            sp.gradient = !inv && rnd(4) == 0;
            sp.ink2 = QColor(rnd(130), rnd(130), rnd(130));
            const Payload& pl = payloads()[rnd(static_cast<int>(payloads().size()))];
            QString err;
            const QRMatrix m = QRCodec::encode(pl.text, sp.logo != Logo::None ? QREcc::High : (rnd(2) ? QREcc::Medium : QREcc::Quartile), &err);
            if (!m.isValid())
                continue;
            QImage img = render(m, sp);
            QStringList post;
            if (rnd(3) == 0)
            {
                const double blur = rndf(1.3, 2.2);
                img = lowRes(img, blur);
                post << QString("blur%1").arg(blur, 0, 'f', 1);
            }
            if (rnd(3) == 0)
            {
                const int amp = 8 + rnd(18);
                img = noise(img, amp, rs);
                post << QString("noise%1").arg(amp);
            }
            if (rnd(3) == 0)
            {
                const double deg = rnd(3) == 0 ? 90 * (1 + rnd(3)) : rndf(-30, 30), pers = rnd(3) == 0 ? rndf(0.0, 0.1) : 0.0;
                img = camera(img, sp.card, deg, pers, 10 + rnd(30));
                post << QString("rot%1/persp%2").arg(deg, 0, 'f', 0).arg(pers, 0, 'f', 2);
            }
            if (rnd(4) == 0)
            {
                img = shade(img, rndf(0.1, 0.5));
                post << "shade";
            }
            if (rnd(4) == 0)
            {
                const QColor bg = rnd(2) ? dark : QColor(236, 236, 238);
                img = camera(img, bg, 0, 0, 20 + rnd(60));
                post << (bg == dark ? "on-dark-screen" : "on-light-screen");
            }
            if (rnd(2) == 0)
            {
                const int q = 55 + rnd(40);
                img = jpeg(img, q);
                post << QString("jpeg%1").arg(q);
            }
            const auto r = QRCodec::decode(img);
            const bool good = std::any_of(r.begin(), r.end(), [&](const QRDecoded& d) { return d.text == pl.text; });
            ok += good ? 1 : 0;
            if (!good)
            {
                const QString desc = QString("#%1 %2 %3 %4 logo%5%6 px%7 quiet%8 v%9 %10 ink%11/%12 [%13]")
                                         .arg(t).arg(dotNames[static_cast<int>(sp.dots)], eyeNames[static_cast<int>(sp.eyes)], pl.name)
                                         .arg(static_cast<int>(sp.logo)).arg(sp.carveLogo ? "c" : "").arg(sp.px, 0, 'f', 1).arg(sp.quiet).arg(m.version)
                                         .arg(inv ? "INV" : "").arg(sp.ink.name(), sp.card.name()).arg(post.join(' '));
                failures << desc;
                if (!save.isEmpty())
                    img.save(QString("%1/rand_%2.png").arg(save).arg(t));
            }
        }
        std::printf("NGAU NHIEN: %d/%d doc duoc\n", ok, trials);
        for (const QString& f : failures)
            std::printf("  HONG %s\n", qPrintable(f));
        return 0;
    }

    int grandOk = 0, grandTotal = 0;
    QElapsedTimer clock;
    clock.start();
    for (const Scenario& s : sc)
    {
        if (!filter.isEmpty() && !s.name.contains(filter, Qt::CaseInsensitive))
            continue;
        int ok = 0, total = 0;
        QStringList fails;
        for (const Payload& pl : payloads())
        {
            QString err;
            const QRMatrix m = QRCodec::encode(pl.text, s.ecc == QREcc::High ? QREcc::High : s.ecc, &err);
            if (!m.isValid())
                continue;
            for (double px : s.pxs)
            {
                QImage img = render(m, s.spec(px));
                if (s.post)
                    img = s.post(img);
                ++total;
                const auto r = QRCodec::decode(img);
                const bool good = std::any_of(r.begin(), r.end(), [&](const QRDecoded& d) { return d.text == pl.text; });
                if (good)
                    ++ok;
                else
                {
                    fails << QString("%1@%2(v%3)").arg(pl.name).arg(px).arg(m.version);
                    if (!save.isEmpty())
                        img.save(QString("%1/%2_%3_%4.png").arg(save, QString(s.name).replace(QRegularExpression("[^A-Za-z0-9+-]"), "_"), pl.name).arg(px));
                }
            }
        }
        grandOk += ok;
        grandTotal += total;
        std::printf("%-34s %3d/%-3d %s%s\n", qPrintable(s.name), ok, total, ok == total ? "" : "  HONG: ", qPrintable(fails.join(' ')));
        std::fflush(stdout);
    }
    std::printf("TONG: %d/%d  (%.1f s)\n", grandOk, grandTotal, clock.elapsed() / 1000.0);
    return 0;
}
