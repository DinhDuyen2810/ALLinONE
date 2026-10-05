#include "QRCodec.h"

#include <QBuffer>
#include <QPainter>
#include <QPainterPath>
#include <QStringConverter>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>

#include "third_party/qrcodegen/qrcodegen.hpp"

extern "C" {
#include "third_party/quirc/quirc.h"
}

namespace
{
qrcodegen::QrCode::Ecc toLibEcc(QREcc ecc)
{
    switch (ecc)
    {
        case QREcc::Low:      return qrcodegen::QrCode::Ecc::LOW;
        case QREcc::Medium:   return qrcodegen::QrCode::Ecc::MEDIUM;
        case QREcc::Quartile: return qrcodegen::QrCode::Ecc::QUARTILE;
        case QREcc::High:     return qrcodegen::QrCode::Ecc::HIGH;
    }
    return qrcodegen::QrCode::Ecc::MEDIUM;
}

QREcc fromLibEcc(qrcodegen::QrCode::Ecc ecc)
{
    switch (ecc)
    {
        case qrcodegen::QrCode::Ecc::LOW:      return QREcc::Low;
        case qrcodegen::QrCode::Ecc::MEDIUM:   return QREcc::Medium;
        case qrcodegen::QrCode::Ecc::QUARTILE: return QREcc::Quartile;
        case qrcodegen::QrCode::Ecc::HIGH:     return QREcc::High;
    }
    return QREcc::Medium;
}

QREcc fromQuircEcc(int level)
{
    switch (level)
    {
        case QUIRC_ECC_LEVEL_L: return QREcc::Low;
        case QUIRC_ECC_LEVEL_M: return QREcc::Medium;
        case QUIRC_ECC_LEVEL_Q: return QREcc::Quartile;
        case QUIRC_ECC_LEVEL_H: return QREcc::High;
    }
    return QREcc::Medium;
}

QString formatName(int dataType)
{
    switch (dataType)
    {
        case QUIRC_DATA_TYPE_NUMERIC: return "Numeric";
        case QUIRC_DATA_TYPE_ALPHA:   return "Alphanumeric";
        case QUIRC_DATA_TYPE_BYTE:    return "Byte";
        case QUIRC_DATA_TYPE_KANJI:   return "Kanji";
    }
    return "Unknown";
}

QString payloadToString(const quirc_data& data)
{
    const QByteArray raw(reinterpret_cast<const char*>(data.payload), data.payload_len);

    if (data.data_type == QUIRC_DATA_TYPE_KANJI || data.eci == QUIRC_ECI_SHIFT_JIS)
    {
        QStringDecoder sjis("Shift-JIS");
        if (sjis.isValid())
        {
            QString s = sjis(raw);
            if (!sjis.hasError())
                return s;
        }
    }

    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    QString s = utf8(raw);
    if (!utf8.hasError())
        return s;

    return QString::fromLatin1(raw);
}

/// Đưa ảnh về thang xám 8-bit, nền trong suốt được trộn lên màu trắng.
QImage toGray(const QImage& src)
{
    if (src.hasAlphaChannel())
    {
        QImage flat(src.size(), QImage::Format_RGB32);
        flat.fill(Qt::white);
        QPainter p(&flat);
        p.drawImage(0, 0, src);
        p.end();
        return flat.convertToFormat(QImage::Format_Grayscale8);
    }
    return src.convertToFormat(QImage::Format_Grayscale8);
}

QImage padWhite(const QImage& gray, int pad)
{
    if (pad <= 0)
        return gray;
    QImage out(gray.width() + 2 * pad, gray.height() + 2 * pad, QImage::Format_Grayscale8);
    out.fill(255);
    QPainter p(&out);
    p.drawImage(pad, pad, gray);
    p.end();
    return out;
}

QList<QRDecoded> decodeGray(const QImage& gray, double scale, int pad, const QPoint& /*unused*/)
{
    QList<QRDecoded> results;
    if (gray.isNull() || gray.width() < 21 || gray.height() < 21)
        return results;

    std::unique_ptr<quirc, decltype(&quirc_destroy)> q(quirc_new(), quirc_destroy);
    if (!q || quirc_resize(q.get(), gray.width(), gray.height()) < 0)
        return results;

    int w = 0, h = 0;
    uint8_t* buf = quirc_begin(q.get(), &w, &h);
    for (int y = 0; y < h; ++y)
        std::copy(gray.constScanLine(y), gray.constScanLine(y) + w, buf + static_cast<size_t>(y) * w);
    quirc_end(q.get());

    const int count = quirc_count(q.get());
    auto code = std::make_unique<quirc_code>();
    auto data = std::make_unique<quirc_data>();

    for (int i = 0; i < count; ++i)
    {
        quirc_extract(q.get(), i, code.get());
        if (quirc_decode(code.get(), data.get()) != QUIRC_SUCCESS)
            continue;

        QRDecoded d;
        d.text = payloadToString(*data);
        d.format = formatName(data->data_type);
        d.version = data->version;
        d.ecc = fromQuircEcc(data->ecc_level);
        for (int c = 0; c < 4; ++c)
        {
            d.corners << QPoint(static_cast<int>((code->corners[c].x - pad) / scale),
                                static_cast<int>((code->corners[c].y - pad) / scale));
        }

        const bool dup = std::any_of(results.begin(), results.end(),
                                     [&](const QRDecoded& r) { return r.text == d.text; });
        if (!dup)
            results.push_back(d);
    }
    return results;
}

double channelLuminance(double c)
{
    c /= 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& c)
{
    return 0.2126 * channelLuminance(c.red()) + 0.7152 * channelLuminance(c.green()) +
           0.0722 * channelLuminance(c.blue());
}

QString colorToSvg(const QColor& c)
{
    return c.name(QColor::HexRgb);
}
} // namespace

QString QRCodec::eccName(QREcc ecc)
{
    switch (ecc)
    {
        case QREcc::Low:      return "L (~7%)";
        case QREcc::Medium:   return "M (~15%)";
        case QREcc::Quartile: return "Q (~25%)";
        case QREcc::High:     return "H (~30%)";
    }
    return "M";
}

int QRCodec::maxBytes(QREcc ecc)
{
    switch (ecc)
    {
        case QREcc::Low:      return 2953;
        case QREcc::Medium:   return 2331;
        case QREcc::Quartile: return 1663;
        case QREcc::High:     return 1273;
    }
    return 2331;
}

QRMatrix QRCodec::encode(const QString& text, QREcc ecc, QString* error)
{
    QRMatrix m;
    if (text.isEmpty())
    {
        if (error) *error = "Nội dung trống.";
        return m;
    }

    const QByteArray utf8 = text.toUtf8();
    if (utf8.size() > maxBytes(ecc))
    {
        if (error)
            *error = QString("Nội dung quá dài (%1 byte). Mức sửa lỗi %2 chỉ chứa tối đa khoảng %3 byte.")
                         .arg(utf8.size()).arg(eccName(ecc)).arg(maxBytes(ecc));
        return m;
    }

    try
    {
        const auto segs = qrcodegen::QrSegment::makeSegments(utf8.constData());
        const auto qr = qrcodegen::QrCode::encodeSegments(segs, toLibEcc(ecc), 1, 40, -1, true);

        m.size = qr.getSize();
        m.version = qr.getVersion();
        m.ecc = fromLibEcc(qr.getErrorCorrectionLevel());
        m.modules.resize(static_cast<size_t>(m.size) * m.size);
        for (int y = 0; y < m.size; ++y)
            for (int x = 0; x < m.size; ++x)
                m.modules[static_cast<size_t>(y) * m.size + x] = qr.getModule(x, y);
    }
    catch (const std::exception& e)
    {
        m = QRMatrix();
        if (error) *error = QString("Không thể mã hóa: %1").arg(QString::fromUtf8(e.what()));
    }
    return m;
}

QImage QRCodec::render(const QRMatrix& matrix, const QRStyle& style)
{
    if (!matrix.isValid())
        return QImage();

    const int quiet = std::clamp(style.quietZone, 0, 32);
    const int cells = matrix.size + 2 * quiet;
    const int scale = std::max(1, style.targetSize / cells);
    const int side = cells * scale;

    QImage img(side, side, QImage::Format_ARGB32);
    img.fill(style.background);

    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.setBrush(style.foreground);
    if (style.roundedModules)
        p.setRenderHint(QPainter::Antialiasing, true);

    const double radius = scale * 0.32;
    for (int y = 0; y < matrix.size; ++y)
    {
        for (int x = 0; x < matrix.size; ++x)
        {
            if (!matrix.at(x, y))
                continue;
            const QRectF r((quiet + x) * scale, (quiet + y) * scale, scale, scale);
            if (style.roundedModules)
                p.drawRoundedRect(r, radius, radius);
            else
                p.drawRect(r);
        }
    }

    if (!style.logo.isNull())
    {
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const double ratio = std::clamp(style.logoRatio, 0.05, 0.30);
        const double codeSide = static_cast<double>(matrix.size) * scale;
        const double boxSide = std::round(codeSide * ratio);
        const double center = side / 2.0;

        const QRectF box(center - boxSide / 2, center - boxSide / 2, boxSide, boxSide);
        p.setBrush(style.background);
        p.drawRoundedRect(box, boxSide * 0.12, boxSide * 0.12);

        const QRectF inner = box.adjusted(boxSide * 0.08, boxSide * 0.08, -boxSide * 0.08, -boxSide * 0.08);
        const QSizeF fit = QSizeF(style.logo.size()).scaled(inner.size(), Qt::KeepAspectRatio);
        const QRectF target(inner.center().x() - fit.width() / 2, inner.center().y() - fit.height() / 2,
                            fit.width(), fit.height());
        p.drawImage(target, style.logo);
    }
    p.end();
    return img;
}

QString QRCodec::toSvg(const QRMatrix& matrix, const QRStyle& style)
{
    if (!matrix.isValid())
        return QString();

    const int quiet = std::clamp(style.quietZone, 0, 32);
    const int cells = matrix.size + 2 * quiet;

    QString svg;
    svg += QString("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    svg += QString("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                   "version=\"1.1\" viewBox=\"0 0 %1 %1\" width=\"%2\" height=\"%2\" shape-rendering=\"crispEdges\">\n")
               .arg(cells).arg(style.targetSize);
    svg += QString("<rect width=\"%1\" height=\"%1\" fill=\"%2\"/>\n").arg(cells).arg(colorToSvg(style.background));

    QString path;
    for (int y = 0; y < matrix.size; ++y)
    {
        int x = 0;
        while (x < matrix.size)
        {
            if (!matrix.at(x, y))
            {
                ++x;
                continue;
            }
            int run = 0;
            while (x + run < matrix.size && matrix.at(x + run, y))
                ++run;
            path += QString("M%1,%2h%3v1h-%3z").arg(x + quiet).arg(y + quiet).arg(run);
            x += run;
        }
    }
    svg += QString("<path d=\"%1\" fill=\"%2\"/>\n").arg(path).arg(colorToSvg(style.foreground));

    if (!style.logo.isNull())
    {
        const double ratio = std::clamp(style.logoRatio, 0.05, 0.30);
        const double box = matrix.size * ratio;
        const double c = cells / 2.0;
        const double inner = box * 0.84;

        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        style.logo.save(&buf, "PNG");

        svg += QString("<rect x=\"%1\" y=\"%1\" width=\"%2\" height=\"%2\" rx=\"%3\" fill=\"%4\"/>\n")
                   .arg(c - box / 2).arg(box).arg(box * 0.12).arg(colorToSvg(style.background));
        svg += QString("<image x=\"%1\" y=\"%1\" width=\"%2\" height=\"%2\" preserveAspectRatio=\"xMidYMid meet\" "
                       "xlink:href=\"data:image/png;base64,%3\"/>\n")
                   .arg(c - inner / 2).arg(inner).arg(QString::fromLatin1(png.toBase64()));
    }

    svg += "</svg>\n";
    return svg;
}

QList<QRDecoded> QRCodec::decode(const QImage& image)
{
    if (image.isNull())
        return {};

    const QImage gray = toGray(image);

    // Hạn chế ảnh quá lớn để quét nhanh và tiết kiệm bộ nhớ
    const int maxDim = std::max(gray.width(), gray.height());
    const double baseScale = maxDim > 2400 ? 2400.0 / maxDim : 1.0;
    const QImage base = baseScale < 1.0
        ? gray.scaled(gray.size() * baseScale, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : gray;

    struct Pass { double scale; bool invert; };
    std::vector<Pass> passes = { {1.0, false}, {1.0, true} };
    if (std::max(base.width(), base.height()) > 800)
        passes.push_back({0.5, false});
    if (std::max(base.width(), base.height()) <= 1200)
        passes.push_back({2.0, false});
    if (std::max(base.width(), base.height()) <= 400)
        passes.push_back({3.0, false});
    if (std::max(base.width(), base.height()) > 800)
        passes.push_back({0.5, true});

    for (const Pass& pass : passes)
    {
        QImage work = base;
        if (pass.scale != 1.0)
        {
            work = base.scaled(QSize(std::max(21, static_cast<int>(base.width() * pass.scale)),
                                     std::max(21, static_cast<int>(base.height() * pass.scale))),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (pass.invert)
            work.invertPixels();

        constexpr int pad = 32;
        const QImage padded = padWhite(work, pad);
        QList<QRDecoded> found = decodeGray(padded, pass.scale * baseScale, pad, QPoint());
        if (!found.isEmpty())
            return found;
    }
    return {};
}

double QRCodec::contrastRatio(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}
