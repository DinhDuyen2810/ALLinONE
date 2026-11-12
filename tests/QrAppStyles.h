// Trình dựng mã QR "kiểu các ứng dụng khác nhau" cho kiểm thử giải mã (chỉ dùng trong tests/).
// Mục đích: đo xem QRCodec::decode() đọc được những mã do Telegram/Zalo/WeChat/Momo/ngân hàng (VietQR)/Discord/Signal/...
// vẽ ra - ô tròn, ô tách rời, ô định vị bo tròn/tròn, logo giữa, màu/gradient, nền tối đảo màu - cùng các hậu xử lý của đời thật
// (ảnh chụp màn hình bị co/giãn, nén JPEG, mờ, nhiễu, xoay, nghiêng, sát mép). Không phải mã tạo QR cho người dùng.
#pragma once

#include <QBuffer>
#include <QColor>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTransform>

#include <cmath>
#include <vector>

#include "tools/qr/QRCodec.h"

namespace qrstyle
{

enum class Dots { Square, Gap, Round, Circle, Blob, Bars, Diamond };
enum class Eyes { Square, Rounded, Circle, DotInner };
enum class Logo { None, SquareBox, CircleBadge };

struct Spec
{
    Dots dots{Dots::Square};
    Eyes eyes{Eyes::Square};
    Logo logo{Logo::None};
    double logoRatio{0.22}; // cạnh logo / cạnh mã
    QColor card{Qt::white};
    QColor ink{Qt::black};
    QColor eyeInk{Qt::black};
    bool gradient{false}; // ô mã tô gradient (ink -> ink2)
    QColor ink2{QColor(0, 120, 255)};
    double px{8.0};
    int quiet{4};
    bool carveLogo{false}; // chừa trống vùng logo (không vẽ ô dưới logo) - ứng dụng thật hay làm vậy
};

inline QPainterPath roundedRectPath(const QRectF& r, double tl, double tr, double br, double bl)
{
    QPainterPath p;
    p.moveTo(r.left() + tl, r.top());
    p.lineTo(r.right() - tr, r.top());
    if (tr > 0) p.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
    p.lineTo(r.right(), r.bottom() - br);
    if (br > 0) p.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    p.lineTo(r.left() + bl, r.bottom());
    if (bl > 0) p.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), -90, -90);
    p.lineTo(r.left(), r.top() + tl);
    if (tl > 0) p.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
    p.closeSubpath();
    return p;
}

inline QImage render(const QRMatrix& m, const Spec& s)
{
    const int n = m.size;
    const int side = qRound((n + 2 * s.quiet) * s.px);
    QImage img(side, side, QImage::Format_RGB32);
    img.fill(s.card);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    const double px = s.px, o = s.quiet * px;
    auto inFinder = [&](int x, int y) {
        if (s.eyes == Eyes::Square)
            return false;
        return (x < 8 && y < 8) || (x >= n - 8 && y < 8) || (x < 8 && y >= n - 8);
    };
    const double cx = n / 2.0;
    const double logoSide = n * s.logoRatio;
    auto inLogo = [&](int x, int y) {
        if (s.logo == Logo::None || !s.carveLogo)
            return false;
        return std::abs(x + 0.5 - cx) < logoSide / 2 + 0.5 && std::abs(y + 0.5 - cx) < logoSide / 2 + 0.5;
    };

    QBrush ink(s.ink);
    if (s.gradient)
    {
        QLinearGradient g(0, 0, side, side);
        g.setColorAt(0, s.ink);
        g.setColorAt(1, s.ink2);
        ink = QBrush(g);
    }
    p.setBrush(ink);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
        {
            if (!m.at(x, y) || inFinder(x, y) || inLogo(x, y))
                continue;
            const QRectF r(o + x * px, o + y * px, px, px);
            switch (s.dots)
            {
            case Dots::Square:
                p.drawRect(r);
                break;
            case Dots::Gap:
                p.drawRect(r.adjusted(px * 0.1, px * 0.1, -px * 0.1, -px * 0.1));
                break;
            case Dots::Round:
                p.drawRoundedRect(r, px * 0.32, px * 0.32);
                break;
            case Dots::Circle:
                p.drawEllipse(r.adjusted(px * 0.04, px * 0.04, -px * 0.04, -px * 0.04));
                break;
            case Dots::Diamond:
            {
                QPolygonF d;
                d << QPointF(r.center().x(), r.top() - px * 0.1) << QPointF(r.right() + px * 0.1, r.center().y())
                  << QPointF(r.center().x(), r.bottom() + px * 0.1) << QPointF(r.left() - px * 0.1, r.center().y());
                p.drawPolygon(d);
                break;
            }
            case Dots::Blob:
            {
                const bool L = m.at(x - 1, y), R = m.at(x + 1, y), U = m.at(x, y - 1), D = m.at(x, y + 1);
                const double rr = px * 0.5;
                p.drawPath(roundedRectPath(r, (!L && !U) ? rr : 0, (!R && !U) ? rr : 0, (!R && !D) ? rr : 0, (!L && !D) ? rr : 0));
                break;
            }
            case Dots::Bars:
            {
                // dải dọc: mỗi cột liền mạch vẽ thành một thanh bo tròn hai đầu (kiểu "vertical" của vài trình tạo QR)
                if (m.at(x, y - 1))
                    break; // đã vẽ cùng thanh ở trên
                int len = 1;
                while (m.at(x, y + len) && !inFinder(x, y + len) && !inLogo(x, y + len))
                    ++len;
                p.drawRoundedRect(QRectF(o + x * px + px * 0.08, o + y * px, px * 0.84, len * px), px * 0.42, px * 0.42);
                break;
            }
            }
        }

    if (s.eyes != Eyes::Square)
    {
        auto eye = [&](int fx, int fy) {
            const double x0 = o + fx * px, y0 = o + fy * px;
            p.setBrush(s.eyeInk);
            switch (s.eyes)
            {
            case Eyes::Rounded:
                p.drawRoundedRect(QRectF(x0, y0, 7 * px, 7 * px), 2.3 * px, 2.3 * px);
                p.setBrush(s.card);
                p.drawRoundedRect(QRectF(x0 + px, y0 + px, 5 * px, 5 * px), 1.5 * px, 1.5 * px);
                p.setBrush(s.eyeInk);
                p.drawRoundedRect(QRectF(x0 + 2 * px, y0 + 2 * px, 3 * px, 3 * px), 1.0 * px, 1.0 * px);
                break;
            case Eyes::Circle:
                p.drawEllipse(QRectF(x0, y0, 7 * px, 7 * px));
                p.setBrush(s.card);
                p.drawEllipse(QRectF(x0 + px, y0 + px, 5 * px, 5 * px));
                p.setBrush(s.eyeInk);
                p.drawEllipse(QRectF(x0 + 2 * px, y0 + 2 * px, 3 * px, 3 * px));
                break;
            case Eyes::DotInner:
                p.drawRect(QRectF(x0, y0, 7 * px, 7 * px));
                p.setBrush(s.card);
                p.drawRect(QRectF(x0 + px, y0 + px, 5 * px, 5 * px));
                p.setBrush(s.eyeInk);
                p.drawEllipse(QRectF(x0 + 2 * px, y0 + 2 * px, 3 * px, 3 * px));
                break;
            default:
                break;
            }
        };
        eye(0, 0);
        eye(n - 7, 0);
        eye(0, n - 7);
    }

    if (s.logo != Logo::None)
    {
        const double c = o + cx * px, ls = logoSide * px;
        p.setBrush(s.card);
        if (s.logo == Logo::CircleBadge)
            p.drawEllipse(QPointF(c, c), ls / 2, ls / 2);
        else
            p.drawRoundedRect(QRectF(c - ls / 2, c - ls / 2, ls, ls), ls * 0.12, ls * 0.12);
        // "logo": đĩa màu + tam giác trắng (giống biểu tượng giấy bay của Telegram)
        p.setBrush(QColor(34, 158, 217));
        p.drawEllipse(QPointF(c, c), ls * 0.38, ls * 0.38);
        p.setBrush(Qt::white);
        p.drawPolygon(QPolygonF({QPointF(c - ls * 0.2, c + ls * 0.02), QPointF(c + ls * 0.22, c - ls * 0.18), QPointF(c + ls * 0.06, c + ls * 0.2)}));
    }
    p.end();
    return img;
}

// ---- hậu xử lý đời thật ----

inline QImage lowRes(const QImage& src, double factor) // thu nhỏ rồi phóng lại mịn = mờ như ảnh chụp
{
    const QSize s = src.size();
    return src.scaled(QSize(qMax(8, qRound(s.width() / factor)), qMax(8, qRound(s.height() / factor))), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
        .scaled(s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

inline QImage jpeg(const QImage& src, int quality)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    src.save(&buf, "JPG", quality);
    QImage out;
    out.loadFromData(ba, "JPG");
    return out.convertToFormat(QImage::Format_RGB32);
}

inline QImage noise(const QImage& src, int amplitude, quint32 seed = 777)
{
    QImage out = src.convertToFormat(QImage::Format_RGB32);
    quint32 s = seed;
    for (int y = 0; y < out.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            s = s * 1664525u + 1013904223u;
            const int d = static_cast<int>((s >> 24) % (2 * amplitude + 1)) - amplitude;
            line[x] = qRgb(qBound(0, qRed(line[x]) + d, 255), qBound(0, qGreen(line[x]) + d, 255), qBound(0, qBlue(line[x]) + d, 255));
        }
    }
    return out;
}

inline QImage lowContrast(const QImage& src, double keep) // keep=0.3: kéo mọi điểm về 30% biên độ quanh xám trung bình
{
    QImage out = src.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            auto f = [&](int v) { return qBound(0, qRound(150 + (v - 128) * keep), 255); };
            line[x] = qRgb(f(qRed(line[x])), f(qGreen(line[x])), f(qBlue(line[x])));
        }
    }
    return out;
}

/// Đặt `src` lên một "màn hình" nền `bg` rồi xoay `deg` độ và/hoặc nghiêng góc nhìn (persp = độ lệch tương đối của góc, 0 = không), cắt lại vùng có lề `margin`.
inline QImage camera(const QImage& src, const QColor& bg, double deg, double persp, int margin)
{
    const int w = src.width(), h = src.height();
    const int cw = w + 2 * margin + w / 2, ch = h + 2 * margin + h / 2;
    QImage out(cw, ch, QImage::Format_RGB32);
    out.fill(bg);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);
    QTransform t;
    t.translate(cw / 2.0, ch / 2.0);
    t.rotate(deg);
    if (persp != 0)
    {
        QPolygonF from({QPointF(-w / 2.0, -h / 2.0), QPointF(w / 2.0, -h / 2.0), QPointF(w / 2.0, h / 2.0), QPointF(-w / 2.0, h / 2.0)});
        QPolygonF to({QPointF(-w / 2.0 + w * persp, -h / 2.0 + h * persp * 0.4), QPointF(w / 2.0, -h / 2.0 - h * persp * 0.2),
                      QPointF(w / 2.0 - w * persp * 0.3, h / 2.0), QPointF(-w / 2.0, h / 2.0 - h * persp * 0.2)});
        QTransform q;
        if (QTransform::quadToQuad(from, to, q))
            t = q * t;
    }
    t.translate(0, 0);
    p.setTransform(t * QTransform().translate(0, 0));
    p.drawImage(QPointF(-w / 2.0, -h / 2.0), src);
    p.end();
    return out;
}


/// Ánh sáng không đều (chụp camera): nhân điểm ảnh với hệ số giảm dần theo đường chéo (strength=0,5: góc dưới-phải còn 50%).
inline QImage shade(const QImage& src, double strength)
{
    QImage out = src.convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < out.height(); ++y)
    {
        QRgb* line = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x)
        {
            const double f = 1.0 - strength * (0.5 * x / out.width() + 0.5 * y / out.height());
            line[x] = qRgb(static_cast<int>(qRed(line[x]) * f), static_cast<int>(qGreen(line[x]) * f), static_cast<int>(qBlue(line[x]) * f));
        }
    }
    return out;
}

/// Vệt chói (đèn/phản xạ màn hình): một đĩa sáng mờ dần phủ lên một phần mã.
inline QImage glare(const QImage& src, double cx, double cy, double radiusFrac)
{
    QImage out = src.convertToFormat(QImage::Format_RGB32);
    QPainter p(&out);
    QRadialGradient g(out.width() * cx, out.height() * cy, out.width() * radiusFrac);
    g.setColorAt(0, QColor(255, 255, 255, 235));
    g.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(out.rect(), g);
    p.end();
    return out;
}

/// Thẻ kiểu VietQR/ví điện tử: banner màu + dòng chữ giả phía trên, mã ở dưới, viền bo tròn, trên nền xám.
inline QImage cardAround(const QImage& code, const QColor& banner)
{
    const int w = code.width() + 120, h = code.height() + 260;
    QImage out(w, h, QImage::Format_RGB32);
    out.fill(QColor(225, 228, 232));
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(20, 20, w - 40, h - 40), 24, 24);
    p.setBrush(banner);
    p.drawRoundedRect(QRectF(20, 20, w - 40, 90), 24, 24);
    p.drawRect(QRectF(20, 70, w - 40, 40));
    p.setBrush(QColor(60, 60, 70));
    for (int i = 0; i < 3; ++i)
        p.drawRoundedRect(QRectF(50, 130 + i * 26, w * (0.7 - i * 0.15), 9), 4, 4); // dòng chữ giả
    p.drawImage(60, 220, code);
    p.end();
    return out;
}

} // namespace qrstyle
