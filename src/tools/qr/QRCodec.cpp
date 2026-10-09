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

/// undecoded (nếu khác null) nhận số lưới mã mà quirc ĐÃ NHÌN THẤY trong ảnh nhưng KHÔNG giải mã được ở
/// lượt này - nơi gọi dùng để biết kết quả mới chỉ là một phần (xem QRCodec::decode()).
QList<QRDecoded> decodeGray(const QImage& gray, double scale, int pad, int* undecoded)
{
    QList<QRDecoded> results;
    if (undecoded)
        *undecoded = 0;
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
        quirc_decode_error_t err = quirc_decode(code.get(), data.get());
        if (err == QUIRC_ERROR_DATA_ECC)
        {
            // Mã bị lật gương (ảnh chụp qua gương/camera trước, in ngược...): lưới đọc được nhưng dữ liệu
            // sai ECC. Lật lưới rồi thử lại đúng một lần - cách dùng quirc_flip() theo tài liệu quirc.
            quirc_flip(code.get());
            err = quirc_decode(code.get(), data.get());
        }
        if (err != QUIRC_SUCCESS)
        {
            if (undecoded)
                ++*undecoded;
            continue;
        }

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
    const auto tooLong = [&] {
        return QString("Nội dung quá dài (%1 byte). Mức sửa lỗi %2 chỉ chứa tối đa khoảng %3 byte văn bản "
                       "thường (chuỗi chỉ gồm chữ số hoặc chữ HOA/số chứa được nhiều hơn).")
            .arg(utf8.size()).arg(eccName(ecc)).arg(maxBytes(ecc));
    };

    // Sức chứa THẬT phụ thuộc chế độ mã hóa mà thư viện tự chọn (numeric/alphanumeric/byte): một chuỗi
    // toàn chữ số dài tới 7089 ký tự vẫn vừa mã v40-L, trong khi maxBytes() chỉ là giới hạn của chế độ
    // byte. Trước đây chặn cứng theo maxBytes() nên từ chối oan các chuỗi số/chữ HOA dài. Nay chỉ chặn
    // sớm đầu vào chắc chắn không mã QR nào chứa nổi (tránh phân tích vô ích một văn bản khổng lồ), phần
    // còn lại để thư viện tự báo data_too_long.
    constexpr int kAbsoluteMaxChars = 7089; // v40, mức L, toàn chữ số
    if (utf8.size() > kAbsoluteMaxChars)
    {
        if (error) *error = tooLong();
        return m;
    }

    try
    {
        // makeSegments() nhận chuỗi C (kết thúc bằng NUL) - nội dung có ký tự U+0000 ở giữa (vd "Tạo lại
        // mã" từ một mã nhị phân vừa quét) sẽ bị cắt cụt âm thầm tại đó. Gặp NUL thì mã hóa nguyên khối
        // byte bằng makeBytes() (không phụ thuộc ký tự kết thúc).
        std::vector<qrcodegen::QrSegment> segs;
        if (utf8.contains('\0'))
        {
            const std::vector<std::uint8_t> bytes(utf8.cbegin(), utf8.cend());
            segs.push_back(qrcodegen::QrSegment::makeBytes(bytes));
        }
        else
        {
            segs = qrcodegen::QrSegment::makeSegments(utf8.constData());
        }
        const auto qr = qrcodegen::QrCode::encodeSegments(segs, toLibEcc(ecc), 1, 40, -1, true);

        m.size = qr.getSize();
        m.version = qr.getVersion();
        m.ecc = fromLibEcc(qr.getErrorCorrectionLevel());
        m.modules.resize(static_cast<size_t>(m.size) * m.size);
        for (int y = 0; y < m.size; ++y)
            for (int x = 0; x < m.size; ++x)
                m.modules[static_cast<size_t>(y) * m.size + x] = qr.getModule(x, y);
    }
    catch (const qrcodegen::data_too_long&)
    {
        m = QRMatrix();
        if (error) *error = tooLong();
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
    // crispEdges chỉ đúng cho ô vuông (tắt khử răng cưa để các ô liền nhau không hở khe); ô bo tròn cần
    // khử răng cưa nên dùng geometricPrecision.
    svg += QString("<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
                   "version=\"1.1\" viewBox=\"0 0 %1 %1\" width=\"%2\" height=\"%2\" shape-rendering=\"%3\">\n")
               .arg(cells).arg(style.targetSize).arg(style.roundedModules ? "geometricPrecision" : "crispEdges");
    svg += QString("<rect width=\"%1\" height=\"%1\" fill=\"%2\"/>\n").arg(cells).arg(colorToSvg(style.background));

    if (style.roundedModules)
    {
        // Khớp với render(): MỖI ô là một hình vuông bo góc riêng, bán kính 0.32 ô. Trước đây toSvg() bỏ
        // qua roundedModules nên tệp SVG xuất ra (ô vuông) khác hẳn bản xem trước/PNG (ô bo tròn).
        svg += QString("<g fill=\"%1\">\n").arg(colorToSvg(style.foreground));
        for (int y = 0; y < matrix.size; ++y)
            for (int x = 0; x < matrix.size; ++x)
                if (matrix.at(x, y))
                    svg += QString("<rect x=\"%1\" y=\"%2\" width=\"1\" height=\"1\" rx=\"0.32\" ry=\"0.32\"/>\n")
                               .arg(x + quiet).arg(y + quiet);
        svg += "</g>\n";
    }
    else
    {
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
    }

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

    // Trước đây trả về NGAY ở lượt đầu tiên có kết quả - nếu lượt đó nhìn thấy 2 mã nhưng chỉ đọc được 1
    // (mã kia quá nhỏ/mờ ở tỉ lệ này) thì mã còn lại bị bỏ sót hẳn dù một lượt phóng to/thu nhỏ đọc được.
    // Nay: lượt nào đọc được HẾT các lưới nó nhìn thấy thì vẫn dừng ngay như cũ (trường hợp thường gặp,
    // không tốn thêm gì); chỉ khi còn lưới chưa đọc được mới chạy tiếp các lượt CÙNG cực tính (bỏ qua các
    // lượt đảo màu ngược lại - gần như chắc chắn vô ích) rồi gộp kết quả, tối đa thêm 1-2 lượt.
    QList<QRDecoded> all;
    bool partial = false;
    bool partialInvert = false;

    for (const Pass& pass : passes)
    {
        if (partial && pass.invert != partialInvert)
            continue;

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
        int undecoded = 0;
        const QList<QRDecoded> found = decodeGray(padded, pass.scale * baseScale, pad, &undecoded);
        for (const QRDecoded& d : found)
        {
            const bool dup = std::any_of(all.begin(), all.end(),
                                         [&](const QRDecoded& r) { return r.text == d.text; });
            if (!dup)
                all.push_back(d);
        }

        if (!found.isEmpty() && undecoded == 0)
            return all;
        if (!all.isEmpty() && !partial)
        {
            partial = true;
            partialInvert = pass.invert;
        }
    }
    return all;
}

double QRCodec::contrastRatio(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    const double hi = std::max(la, lb);
    const double lo = std::min(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}
