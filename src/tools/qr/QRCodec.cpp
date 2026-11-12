#include "QRCodec.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QPainter>
#include <QPainterPath>
#include <QStringConverter>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <vector>
#include <memory>
#include <stdexcept>

#include "third_party/qrcodegen/qrcodegen.hpp"

extern "C" {
#include "third_party/quirc/quirc.h"
#include "third_party/quirc/quirc_internal.h" // chỉ để đọc tâm các ô định vị (QUIRC_MAX_REGIONS khai ở CMake cho qr_core)
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

QImage padFill(const QImage& gray, int pad, int fill)
{
    if (pad <= 0)
        return gray;
    QImage out(gray.width() + 2 * pad, gray.height() + 2 * pad, QImage::Format_Grayscale8);
    out.fill(fill);
    QPainter p(&out);
    p.drawImage(pad, pad, gray);
    p.end();
    return out;
}

QImage padWhite(const QImage& gray, int pad)
{
    return padFill(gray, pad, 255);
}

/// Giá trị xám trung vị của viền ngoài cùng ảnh (cỡ nền xung quanh). Dùng để đệm đúng màu nền khi mã nằm trên nền tối.
int edgeMedian(const QImage& gray)
{
    std::vector<uint8_t> v;
    const int w = gray.width(), h = gray.height();
    if (w < 4 || h < 4)
        return 255;
    for (int x = 0; x < w; ++x)
    {
        v.push_back(gray.constScanLine(0)[x]);
        v.push_back(gray.constScanLine(h - 1)[x]);
    }
    for (int y = 1; y < h - 1; ++y)
    {
        v.push_back(gray.constScanLine(y)[0]);
        v.push_back(gray.constScanLine(y)[w - 1]);
    }
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

// ---- Tinh chỉnh lưới cho mã QR "cách điệu" (ô bo tròn, chấm tròn, logo giữa, ảnh chụp lệch) ----
//
// quirc ước lượng 4 góc của lưới từ 3 ô định vị; góc thứ tư chỉ được ngoại suy (rồi sửa chút ít nhờ ô căn chỉnh). Mã có ô định vị BO TRÒN (Telegram, nhiều
// mã đẹp tạo bằng công cụ trực tuyến) làm góc bị lệch vào trong ~1-2 ô, lưới lấy mẫu trôi dần về phía góc kia và ECC hỏng dù mã hoàn toàn đọc được (đo thật
// trên mã Telegram 33x33: quirc báo "ECC failure"). Cách sửa: tối ưu 4 góc bằng leo đồi sao cho các mẫu CHỨC NĂNG đã biết trước của QR (3 ô định vị + khoảng
// trắng quanh chúng, 2 đường timing, các ô căn chỉnh) khớp nhất với ảnh, rồi lấy mẫu lại toàn bộ lưới và để quirc giải mã. Chỉ chạy khi giải mã thường thất bại.

struct Homography
{
    double a{1}, b{0}, c{0}, d{0}, e{1}, f{0}, g{0}, h{0};
    QPointF map(double u, double v) const
    {
        const double w = g * u + h * v + 1.0;
        return {(a * u + b * v + c) / w, (d * u + e * v + f) / w};
    }
};

/// Phép chiếu từ hình vuông đơn vị (TL, TR, BR, BL) lên tứ giác p[0..3] (Heckbert). false nếu suy biến.
bool unitSquareToQuad(const QPointF p[4], Homography* out)
{
    const double dx1 = p[1].x() - p[2].x(), dx2 = p[3].x() - p[2].x(), dx3 = p[0].x() - p[1].x() + p[2].x() - p[3].x();
    const double dy1 = p[1].y() - p[2].y(), dy2 = p[3].y() - p[2].y(), dy3 = p[0].y() - p[1].y() + p[2].y() - p[3].y();
    const double den = dx1 * dy2 - dx2 * dy1;
    Homography H;
    if (std::abs(dx3) < 1e-9 && std::abs(dy3) < 1e-9)
    {
        H.a = p[1].x() - p[0].x();
        H.b = p[3].x() - p[0].x();
        H.c = p[0].x();
        H.d = p[1].y() - p[0].y();
        H.e = p[3].y() - p[0].y();
        H.f = p[0].y();
    }
    else
    {
        if (std::abs(den) < 1e-12)
            return false;
        H.g = (dx3 * dy2 - dx2 * dy3) / den;
        H.h = (dx1 * dy3 - dx3 * dy1) / den;
        H.a = p[1].x() - p[0].x() + H.g * p[1].x();
        H.b = p[3].x() - p[0].x() + H.h * p[3].x();
        H.c = p[0].x();
        H.d = p[1].y() - p[0].y() + H.g * p[1].y();
        H.e = p[3].y() - p[0].y() + H.h * p[3].y();
        H.f = p[0].y();
    }
    *out = H;
    return true;
}

double sampleBilinear(const QImage& gray, double x, double y)
{
    if (x < 0 || y < 0 || x > gray.width() - 1.001 || y > gray.height() - 1.001)
        return 255.0; // ngoài ảnh: coi là nền sáng
    const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
    const double fx = x - x0, fy = y - y0;
    const uchar* r0 = gray.constScanLine(y0);
    const uchar* r1 = gray.constScanLine(y0 + 1);
    return (r0[x0] * (1 - fx) + r0[x0 + 1] * fx) * (1 - fy) + (r1[x0] * (1 - fx) + r1[x0 + 1] * fx) * fy;
}

/// Vị trí tâm các ô căn chỉnh (cùng công thức chuẩn ISO 18004 / qrcodegen).
std::vector<int> alignmentPositions(int version)
{
    std::vector<int> pos;
    if (version <= 1)
        return pos;
    const int n = version / 7 + 2;
    const int size = version * 4 + 17;
    const int step = version == 32 ? 26 : (version * 4 + n * 2 + 1) / (n * 2 - 2) * 2;
    for (int i = 0, p = size - 7; i < n - 1; ++i, p -= step)
        pos.insert(pos.begin(), p);
    pos.insert(pos.begin(), 6);
    return pos;
}

struct FunctionModule
{
    int x, y;
    bool dark;
};

/// Các ô có giá trị BIẾT TRƯỚC: ô định vị 7x7 + viền trắng 1 ô, đường timing, ô căn chỉnh 5x5.
std::vector<FunctionModule> functionModules(int size)
{
    const int version = (size - 17) / 4;
    std::vector<FunctionModule> out;
    auto finder = [&](int ox, int oy) {
        for (int j = -1; j <= 7; ++j)
            for (int i = -1; i <= 7; ++i)
            {
                const int x = ox + i, y = oy + j;
                if (x < 0 || y < 0 || x >= size || y >= size)
                    continue;
                const bool inside = i >= 0 && i <= 6 && j >= 0 && j <= 6;
                const bool dark = inside && (i == 0 || i == 6 || j == 0 || j == 6 || (i >= 2 && i <= 4 && j >= 2 && j <= 4));
                out.push_back({x, y, dark});
            }
    };
    finder(0, 0);
    finder(size - 7, 0);
    finder(0, size - 7);
    for (int i = 8; i < size - 8; ++i)
    {
        out.push_back({i, 6, i % 2 == 0});
        out.push_back({6, i, i % 2 == 0});
    }
    const std::vector<int> ap = alignmentPositions(version);
    for (size_t a = 0; a < ap.size(); ++a)
        for (size_t b = 0; b < ap.size(); ++b)
        {
            const int cx = ap[a], cy = ap[b];
            if ((a == 0 && b == 0) || (a == 0 && b == ap.size() - 1) || (a == ap.size() - 1 && b == 0))
                continue; // trùng ô định vị
            for (int j = -2; j <= 2; ++j)
                for (int i = -2; i <= 2; ++i)
                    out.push_back({cx + i, cy + j, std::max(std::abs(i), std::abs(j)) != 1});
        }
    return out;
}

/// Giá trị xám trung bình của một ô (tâm + 4 điểm lệch) theo phép chiếu H (hệ tọa độ: ô (x,y) chiếm [x,x+1) trên N ô).
double moduleValue(const QImage& gray, const Homography& H, int size, int x, int y)
{
    static const double off[5][2] = {{0.5, 0.5}, {0.3, 0.5}, {0.7, 0.5}, {0.5, 0.3}, {0.5, 0.7}};
    double sum = 0;
    for (const auto& o : off)
    {
        const QPointF p = H.map((x + o[0]) / size, (y + o[1]) / size);
        sum += sampleBilinear(gray, p.x(), p.y());
    }
    return sum / 5.0;
}

/// Điểm khớp liên tục của 4 góc với các ô chức năng: tổng "lề" (đúng phía ngưỡng, kẹp [-1,1]); ngưỡng = trung điểm trung bình các ô đáng sáng/đáng tối.
double scoreCorners(const QImage& gray, const QPointF c[4], int size, const std::vector<FunctionModule>& fm, double* thresholdOut = nullptr)
{
    Homography H;
    if (!unitSquareToQuad(c, &H))
        return -1e9;
    std::vector<double> v(fm.size());
    double darkSum = 0, lightSum = 0;
    int darkN = 0, lightN = 0;
    for (size_t i = 0; i < fm.size(); ++i)
    {
        v[i] = moduleValue(gray, H, size, fm[i].x, fm[i].y);
        (fm[i].dark ? darkSum : lightSum) += v[i];
        ++(fm[i].dark ? darkN : lightN);
    }
    if (darkN == 0 || lightN == 0)
        return -1e9;
    const double dm = darkSum / darkN, lm = lightSum / lightN;
    const double thr = (dm + lm) / 2.0;
    if (thresholdOut)
        *thresholdOut = thr;
    const double range = std::max(20.0, std::abs(lm - dm) / 2.0);
    double score = 0;
    for (size_t i = 0; i < fm.size(); ++i)
    {
        const double margin = (fm[i].dark ? thr - v[i] : v[i] - thr) / range;
        score += std::clamp(margin, -1.0, 1.0);
    }
    return score;
}

/// Leo đồi 4 góc (c vào/ra) theo điểm khớp các ô chức năng, bước đầu `firstStep` rồi thu dần; mỗi góc thử 4 hướng trục + 4 hướng chéo. Trả về điểm tốt nhất.
double climbCorners(const QImage& gray, QPointF c[4], int size, const std::vector<FunctionModule>& fm, double modulePx, double firstStep,
                    const QElapsedTimer& clock, qint64 budgetMs)
{
    double best = scoreCorners(gray, c, size, fm);
    static const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    static const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (double step = modulePx * firstStep; step > modulePx * 0.04; step *= 0.6)
    {
        bool improved = true;
        for (int round = 0; improved && round < 60; ++round)
        {
            improved = false;
            for (int k = 0; k < 4; ++k)
                for (int dir = 0; dir < 8; ++dir)
                {
                    if (clock.elapsed() > budgetMs)
                        return best; // hết ngân sách thời gian (ảnh nhiễu cho nhiều lưới giả rất lớn)
                    QPointF t[4] = {c[0], c[1], c[2], c[3]};
                    t[k] += QPointF(dx[dir] * step, dy[dir] * step);
                    const double s = scoreCorners(gray, t, size, fm);
                    if (s > best + 1e-9)
                    {
                        best = s;
                        for (int i = 0; i < 4; ++i)
                            c[i] = t[i];
                        improved = true;
                    }
                }
        }
    }
    return best;
}

/// Lấy mẫu lại toàn bộ lưới `size` x `size` theo 4 góc c vào `out`.
bool sampleGrid(const QImage& gray, const QPointF c[4], int size, const std::vector<FunctionModule>& fm, double thrOffset, quirc_code* out)
{
    Homography H;
    if (!unitSquareToQuad(c, &H))
        return false;
    double thr = 128;
    scoreCorners(gray, c, size, fm, &thr);
    thr += thrOffset;
    std::memset(out, 0, sizeof(*out));
    out->size = size;
    for (int i = 0; i < 4; ++i)
        out->corners[i] = {static_cast<int>(std::lround(c[i].x())), static_cast<int>(std::lround(c[i].y()))};
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            if (moduleValue(gray, H, size, x, y) < thr)
            {
                const int bit = y * size + x;
                out->cell_bitmap[bit >> 3] |= static_cast<uint8_t>(1 << (bit & 7));
            }
    return true;
}

/// Tinh chỉnh tâm một ô định vị: ô định vị đối xứng 180° nên trọng tâm của phần "mực" (điểm tối hơn trung bình) trong một đĩa bán kính R quanh tâm là chính tâm.
/// Lặp (mean-shift) vài lần từ điểm xuất phát. Tâm quirc báo cho ô định vị tròn/bo tròn có thể lệch cả nửa ô; còn trọng tâm thì không.
QPointF refineCentre(const QImage& gray, QPointF c, double R)
{
    const int w = gray.width(), h = gray.height();
    for (int it = 0; it < 8; ++it)
    {
        const int x0 = std::max(0, static_cast<int>(std::floor(c.x() - R))), x1 = std::min(w - 1, static_cast<int>(std::ceil(c.x() + R)));
        const int y0 = std::max(0, static_cast<int>(std::floor(c.y() - R))), y1 = std::min(h - 1, static_cast<int>(std::ceil(c.y() + R)));
        double sum = 0;
        int n = 0;
        for (int y = y0; y <= y1; ++y)
        {
            const uchar* row = gray.constScanLine(y);
            for (int x = x0; x <= x1; ++x)
                if ((x - c.x()) * (x - c.x()) + (y - c.y()) * (y - c.y()) <= R * R)
                {
                    sum += row[x];
                    ++n;
                }
        }
        if (n < 12)
            break;
        const double mean = sum / n;
        double sx = 0, sy = 0, sw = 0;
        for (int y = y0; y <= y1; ++y)
        {
            const uchar* row = gray.constScanLine(y);
            for (int x = x0; x <= x1; ++x)
                if ((x - c.x()) * (x - c.x()) + (y - c.y()) * (y - c.y()) <= R * R && row[x] < mean)
                {
                    sx += x;
                    sy += y;
                    sw += 1;
                }
        }
        if (sw < 6)
            break;
        const QPointF nc(sx / sw, sy / sw);
        const bool done = std::hypot(nc.x() - c.x(), nc.y() - c.y()) < 0.05;
        c = nc;
        if (done)
            break;
    }
    return c;
}

/// Tâm ba ô định vị (TL, TR, BL) của một mã - điểm xuất phát cho refineGrids().
struct SeedTriple
{
    QPointF tl, tr, bl;
};

/// Suy tâm ba ô định vị từ lưới quirc đã dựng (qua phép chiếu của chính lưới đó).
bool tripleFromGrid(const QImage& gray, const quirc_code& seed, SeedTriple* out)
{
    const int s0 = seed.size;
    if (s0 < 21)
        return false;
    QPointF sc[4];
    for (int i = 0; i < 4; ++i)
        sc[i] = QPointF(seed.corners[i].x, seed.corners[i].y);
    Homography H0;
    if (!unitSquareToQuad(sc, &H0))
        return false;
    out->tl = H0.map(3.5 / s0, 3.5 / s0);
    out->tr = H0.map((s0 - 3.5) / s0, 3.5 / s0);
    out->bl = H0.map(3.5 / s0, (s0 - 3.5) / s0);
    // quirc có thể đoán cỡ lưới sai => ước lượng bề rộng ô sai; lấy bán kính đĩa nhỏ (~2 ô theo ước lượng) để vẫn nằm trong lõi 3x3 của ô định vị (đĩa rộng hơn bị viền ngoài kéo lệch).
    const double R = std::max(2.5, 1.7 * std::hypot(out->tr.x() - out->tl.x(), out->tr.y() - out->tl.y()) / (s0 - 7));
    out->tl = refineCentre(gray, out->tl, R);
    out->tr = refineCentre(gray, out->tr, R);
    out->bl = refineCentre(gray, out->bl, R);
    return true;
}

/// quirc có thể THẤY ba ô định vị mà không ghép được thành lưới (kiểm tra hình học của nó dựa vào GÓC ô định vị - lệch với ô tròn/bo tròn): ghép tay từ TÂM
/// các ô định vị: ba tâm tạo góc vuông (cosin nhỏ) với hai cạnh gần bằng nhau. Trả tối đa maxN bộ ba, bộ "vuông" và "cân" nhất trước.
std::vector<SeedTriple> capstoneTriples(const QImage& gray, const quirc* q, size_t maxN)
{
    struct Scored
    {
        double badness;
        SeedTriple t;
    };
    std::vector<Scored> found;
    const int n = std::min(q->num_capstones, 12);
    std::vector<QPointF> centres(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const quirc_capstone& cap = q->capstones[i];
        double side = 0;
        for (int k = 0; k < 4; ++k)
            side += std::hypot(cap.corners[k].x - cap.corners[(k + 1) % 4].x, cap.corners[k].y - cap.corners[(k + 1) % 4].y) / 4.0;
        centres[static_cast<size_t>(i)] = refineCentre(gray, QPointF(cap.center.x, cap.center.y), std::max(2.5, 0.243 * side));
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            for (int k = j + 1; k < n; ++k)
            {
                const QPointF p[3] = {centres[static_cast<size_t>(i)], centres[static_cast<size_t>(j)], centres[static_cast<size_t>(k)]};
                for (int v = 0; v < 3; ++v)
                {
                    const QPointF a = p[(v + 1) % 3] - p[v], b = p[(v + 2) % 3] - p[v];
                    const double la = std::hypot(a.x(), a.y()), lb = std::hypot(b.x(), b.y());
                    if (la < 8 || lb < 8)
                        continue;
                    const double cosv = std::abs(a.x() * b.x() + a.y() * b.y()) / (la * lb);
                    const double ratio = la > lb ? la / lb : lb / la;
                    if (cosv > 0.3 || ratio > 1.7)
                        continue;
                    SeedTriple t;
                    t.tl = p[v];
                    const double cross = a.x() * b.y() - a.y() * b.x();
                    t.tr = cross > 0 ? p[v] + a : p[v] + b;
                    t.bl = cross > 0 ? p[v] + b : p[v] + a;
                    found.push_back({cosv + (ratio - 1.0), t});
                    break;
                }
            }
    std::stable_sort(found.begin(), found.end(), [](const Scored& a, const Scored& b) { return a.badness < b.badness; });
    std::vector<SeedTriple> out;
    for (const Scored& f : found)
    {
        if (out.size() >= maxN)
            break;
        out.push_back(f.t);
    }
    return out;
}

/// Dựng và thử các ứng viên lưới từ lưới quirc đã THẤY (seed) mà không giải mã được. quirc đoán cỡ lưới từ bề rộng ô định vị nên với ô định vị bo tròn/hình tròn
/// nó thường ước lượng ô QUÁ NHỎ => cỡ lưới QUÁ LỚN 8-16 ô (đo thật: 73 thay vì 61, 37 thay vì 29), và các góc ngoại suy theo đó cũng lệch. Tâm ba ô định vị thì
/// không bị lệch (độ lệch của hình bo tròn đối xứng quanh tâm) nên dựng lại từ đó: với MỌI cỡ lưới hợp lệ (21,25,...) đặt 4 góc theo tâm ô định vị, chấm điểm
/// khớp các ô chức năng (đường timing chỉ khớp ở cỡ đúng), rồi leo đồi chỉ ở vài cỡ điểm cao nhất. tryCode nhận từng lưới đã lấy mẫu, trả true khi giải mã xong.
void refineGrids(const QImage& gray, const SeedTriple& seed, const QElapsedTimer& clock, qint64 budgetMs, const std::function<bool(quirc_code&)>& tryCode)
{
    const QPointF cTL = seed.tl;
    const QPointF vx = seed.tr - seed.tl, vy = seed.bl - seed.tl;

    struct Cand
    {
        int size;
        double score;
        QPointF c[4];
        std::vector<FunctionModule> fm;
    };
    std::vector<Cand> cands;
    for (int size = 21; size <= 177; size += 4)
    {
        const double k = 1.0 / (size - 7);
        const QPointF ex = vx * k, ey = vy * k;
        if (std::hypot(ex.x(), ex.y()) < 1.8)
            break; // ô nhỏ hơn ~2 px: không thể đọc được
        Cand cd;
        cd.size = size;
        cd.c[0] = cTL - (ex + ey) * 3.5;
        cd.c[1] = cd.c[0] + ex * size;
        cd.c[3] = cd.c[0] + ey * size;
        cd.c[2] = cd.c[0] + (ex + ey) * size;
        cd.fm = functionModules(size);
        cd.score = scoreCorners(gray, cd.c, size, cd.fm) / static_cast<double>(cd.fm.size());
        cands.push_back(std::move(cd));
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });

    int tried = 0;
    for (Cand& cd : cands)
    {
        if (tried >= 3 || cd.score < 0.12 || clock.elapsed() > budgetMs)
            break;
        ++tried;
        const double modulePx = std::hypot(cd.c[1].x() - cd.c[0].x(), cd.c[1].y() - cd.c[0].y()) / cd.size;
        const double start = scoreCorners(gray, cd.c, cd.size, cd.fm);
        // Leo đồi từ hai bước đầu khác nhau (1 ô: sửa lệch nhỏ; 2,5 ô: góc dưới-phải chỉ được ô căn chỉnh nhỏ giữ nên ngoại suy có thể lệch tới vài ô).
        for (double firstStep : {1.0, 2.5})
        {
            QPointF c[4] = {cd.c[0], cd.c[1], cd.c[2], cd.c[3]};
            const double best = climbCorners(gray, c, cd.size, cd.fm, modulePx, firstStep, clock, budgetMs);
            if (best <= start + 1e-6 && firstStep > 1.0)
                continue;
            // Ngưỡng đen/trắng tính từ các ô chức năng; ảnh nén JPEG/mờ có thể cần ngưỡng lệch nhẹ nên thêm hai biến thể.
            for (double thrOffset : {0.0, -14.0, 14.0})
            {
                quirc_code code;
                if (sampleGrid(gray, c, cd.size, cd.fm, thrOffset, &code) && tryCode(code))
                    return;
            }
            if (clock.elapsed() > budgetMs)
                return;
        }
    }
}

/// Phép "đóng" hình thái học (nở rồi co theo cửa sổ (2r+1)^2) trên ảnh xám: lấp các khe sáng hẹp (<= ~2r px) giữa những ô tối. Mã vẽ từng ô TÁCH RỜI (có khe hở
/// giữa các ô, kể cả trong ô định vị) làm quirc không thấy ô định vị nào vì viền ô định vị bị cắt vụn - sau khi đóng, viền liền lại.
QImage closeGray(const QImage& src, int r)
{
    const int w = src.width(), h = src.height();
    QImage a = src.convertToFormat(QImage::Format_Grayscale8);
    QImage b(w, h, QImage::Format_Grayscale8);
    // takeMax=false: lấy min (nở vùng tối); true: lấy max (co lại). Ngang: qua bộ đệm có đệm biên; dọc: gộp từng HÀNG (thân thiện bộ nhớ đệm, trình biên dịch vector hóa được).
    std::vector<uchar> line(static_cast<size_t>(w) + 2 * r);
    auto filter = [&](const QImage& in, QImage& out, bool horizontal, bool takeMax) {
        for (int y = 0; y < h; ++y)
        {
            uchar* dst = out.scanLine(y);
            if (horizontal)
            {
                const uchar* row = in.constScanLine(y);
                std::memset(line.data(), row[0], static_cast<size_t>(r));
                std::memcpy(line.data() + r, row, static_cast<size_t>(w));
                std::memset(line.data() + r + w, row[w - 1], static_cast<size_t>(r));
                for (int x = 0; x < w; ++x)
                {
                    uchar v = line[static_cast<size_t>(x)];
                    for (int d = 1; d <= 2 * r; ++d)
                        v = takeMax ? std::max(v, line[static_cast<size_t>(x + d)]) : std::min(v, line[static_cast<size_t>(x + d)]);
                    dst[x] = v;
                }
            }
            else
            {
                std::memcpy(dst, in.constScanLine(std::clamp(y - r, 0, h - 1)), static_cast<size_t>(w));
                for (int d = -r + 1; d <= r; ++d)
                {
                    const uchar* row = in.constScanLine(std::clamp(y + d, 0, h - 1));
                    for (int x = 0; x < w; ++x)
                        dst[x] = takeMax ? std::max(dst[x], row[x]) : std::min(dst[x], row[x]);
                }
            }
        }
    };
    // Đóng vùng TỐI (lấp khe sáng) = nở vùng tối (MIN) rồi co lại (MAX).
    filter(a, b, true, false);
    filter(b, a, false, false);
    filter(a, b, true, true);
    filter(b, a, false, true);
    return a;
}

/// Các lưới mà quirc đã THẤY nhưng không giải mã được ở một lượt, kèm ảnh/tỉ lệ của lượt đó - giai đoạn 2 của QRCodec::decode() tinh chỉnh chúng
/// mà không phải dò lại cả ảnh (dò lại ảnh lớn rất tốn).
struct GridMiss
{
    QImage gray;
    double scale{1.0};
    int pad{0};
    std::vector<SeedTriple> seeds;
};

QRDecoded makeDecoded(const quirc_code& code, const quirc_data& data, double scale, int pad)
{
    QRDecoded d;
    d.text = payloadToString(data);
    d.format = formatName(data.data_type);
    d.version = data.version;
    d.ecc = fromQuircEcc(data.ecc_level);
    for (int c = 0; c < 4; ++c)
    {
        d.corners << QPoint(static_cast<int>((code.corners[c].x - pad) / scale),
                            static_cast<int>((code.corners[c].y - pad) / scale));
    }
    return d;
}

void addUnique(QList<QRDecoded>* results, const QRDecoded& d)
{
    const bool dup = std::any_of(results->begin(), results->end(), [&](const QRDecoded& r) { return r.text == d.text; });
    if (!dup)
        results->push_back(d);
}

/// undecoded (nếu khác null) nhận số lưới mã mà quirc ĐÃ NHÌN THẤY trong ảnh nhưng KHÔNG giải mã được ở
/// lượt này - nơi gọi dùng để biết kết quả mới chỉ là một phần (xem QRCodec::decode()). miss (nếu khác null) nhận chính các lưới đó.
QList<QRDecoded> decodeGray(const QImage& gray, double scale, int pad, int* undecoded, GridMiss* miss = nullptr)
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
        const quirc_code seed = *code; // bản chưa bị lật - dùng cho giai đoạn tinh chỉnh
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
            SeedTriple t;
            if (miss && miss->seeds.size() < 6 && tripleFromGrid(gray, seed, &t))
            {
                miss->gray = gray;
                miss->scale = scale;
                miss->pad = pad;
                miss->seeds.push_back(t);
            }
            continue;
        }
        addUnique(&results, makeDecoded(*code, *data, scale, pad));
    }
    if (miss && q->num_capstones >= 3 && (count == 0 || (undecoded && *undecoded > 0)))
    {
        // Thấy ô định vị nhưng không đọc được (quirc không ghép được lưới nào, hoặc ghép/đoán cỡ sai - ô định vị tròn/bo tròn): ghép tay từ TÂM ô định vị đã tinh chỉnh,
        // xếp TRƯỚC các hạt giống suy ra từ lưới quirc (lưới sai kéo theo tâm sai). Giai đoạn 2 sẽ thử đọc.
        const std::vector<SeedTriple> triples = capstoneTriples(gray, q.get(), 2);
        if (!triples.empty())
        {
            miss->gray = gray;
            miss->scale = scale;
            miss->pad = pad;
            miss->seeds.insert(miss->seeds.begin(), triples.begin(), triples.end());
            if (miss->seeds.size() > 6)
                miss->seeds.resize(6);
            if (undecoded && *undecoded == 0)
                *undecoded = 1;
        }
    }
    return results;
}

/// Giai đoạn 2: tinh chỉnh các lưới quirc đã thấy mà không giải mã được (xem refineGrids()). Trần thời gian ~900 ms cho cả lượt.
/// undecoded nhận số lưới vẫn chưa đọc được.
QList<QRDecoded> refineMissed(const GridMiss& miss, int* undecoded)
{
    QList<QRDecoded> results;
    if (undecoded)
        *undecoded = 0;
    auto data = std::make_unique<quirc_data>();
    QElapsedTimer clock;
    clock.start();
    for (const SeedTriple& seed : miss.seeds)
    {
        bool ok = false;
        if (clock.elapsed() < 900)
            refineGrids(miss.gray, seed, clock, std::min<qint64>(900, clock.elapsed() + 450), [&](quirc_code& cand) {
                quirc_decode_error_t err = quirc_decode(&cand, data.get());
                if (err == QUIRC_ERROR_DATA_ECC)
                {
                    quirc_flip(&cand);
                    err = quirc_decode(&cand, data.get());
                }
                if (err != QUIRC_SUCCESS)
                    return false;
                addUnique(&results, makeDecoded(cand, *data, miss.scale, miss.pad));
                ok = true;
                return true;
            });
        if (!ok && undecoded)
            ++*undecoded;
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

QList<QRDecoded> QRCodec::decode(const QImage& image, int* undecodedCount)
{
    if (undecodedCount)
        *undecodedCount = 0;
    if (image.isNull())
        return {};

    // Ảnh chụp màn hình/clipboard trên màn hình DPI cao (Windows scale 125%/150%...) mang devicePixelRatio > 1.
    // QPainter::drawImage() (padWhite() bên dưới) vẽ theo kích thước LOGIC = pixel / dpr nên nội dung bị CO lại
    // trước khi giải mã, trong khi tọa độ góc lại được quy đổi như chưa co -> khung nhận dạng chỉ còn 1/dpr
    // (đo thật: dpr 1,25 -> khung = 79,8% vùng mã; xem qr_tests). Chuẩn hóa về 1 - dữ liệu pixel không đổi,
    // chỉ bỏ siêu dữ liệu tỉ lệ, và mọi tọa độ trả về luôn là tọa độ PIXEL của ảnh đầu vào.
    QImage normalized = image;
    if (normalized.devicePixelRatio() != 1.0)
        normalized.setDevicePixelRatio(1.0);

    const QImage gray = toGray(normalized);

    // Hạn chế ảnh quá lớn để quét nhanh và tiết kiệm bộ nhớ
    const int maxDim = std::max(gray.width(), gray.height());
    const double baseScale = maxDim > 2400 ? 2400.0 / maxDim : 1.0;
    const QImage base = baseScale < 1.0
        ? gray.scaled(gray.size() * baseScale, Qt::KeepAspectRatio, Qt::SmoothTransformation)
        : gray;

    struct Pass { double scale; bool invert; int close{0}; };
    std::vector<Pass> passes = { {1.0, false}, {1.0, true} };
    if (std::max(base.width(), base.height()) > 800)
        passes.push_back({0.5, false});
    if (std::max(base.width(), base.height()) <= 1200)
        passes.push_back({2.0, false});
    if (std::max(base.width(), base.height()) <= 400)
        passes.push_back({3.0, false});
    if (std::max(base.width(), base.height()) > 800)
        passes.push_back({0.5, true});
    // ĐÃ THỬ thêm 2 tỉ lệ "ở giữa" (0.75x/1.5x) để nâng khả năng nhận diện ảnh chụp màn hình - đo thật
    // bằng qrwifi_stress_tests cho thấy chi phí RÕ RỆT (decode() nằm trên đường nóng: mỗi lần xem trước ở
    // tab Tạo đều tự giải mã lại để xác minh, scanImage() cũng gọi decode()) trong khi lợi ích chưa từng
    // được xác nhận THẬT trên một ảnh cụ thể bị quét hỏng - bỏ lại theo đúng nguyên tắc "xác nhận thật,
    // không đoán", chỉ giữ phần đã xác nhận rõ ràng (phân biệt thông báo lỗi, xem presentResults() ở
    // QRScanTab.cpp).

    // Trước đây trả về NGAY ở lượt đầu tiên có kết quả - nếu lượt đó nhìn thấy 2 mã nhưng chỉ đọc được 1
    // (mã kia quá nhỏ/mờ ở tỉ lệ này) thì mã còn lại bị bỏ sót hẳn dù một lượt phóng to/thu nhỏ đọc được.
    // Nay: lượt nào đọc được HẾT các lưới nó nhìn thấy thì vẫn dừng ngay như cũ (trường hợp thường gặp,
    // không tốn thêm gì); chỉ khi còn lưới chưa đọc được mới chạy tiếp các lượt CÙNG cực tính (bỏ qua các
    // lượt đảo màu ngược lại - gần như chắc chắn vô ích) rồi gộp kết quả, tối đa thêm 1-2 lượt.
    QList<QRDecoded> all;
    bool partial = false;
    bool partialInvert = false;
    int maxUndecoded = 0; // số lưới NHÌN THẤY nhưng không đọc được, lớn nhất qua các lượt đã thử

    // Lượt 0: đệm TRẮNG (mã sát mép ảnh vẫn có vùng yên tĩnh). Lượt 1 - chỉ khi lượt 0 không ra gì và viền ảnh KHÔNG gần trắng (nền tối của ứng dụng/trang web, thẻ màu
    // của ví điện tử/ngân hàng, nền xám): đệm bằng CHÍNH màu nền, vì dải trắng đệm sát nền khác màu làm quirc không còn thấy lưới nào (đo thật).
    const int edge = edgeMedian(base);
    auto runPass = [&](int attempt, const Pass& pass, int* undecoded, GridMiss* miss) {
        QImage work = base;
        if (pass.scale != 1.0)
        {
            work = base.scaled(QSize(std::max(21, static_cast<int>(base.width() * pass.scale)),
                                     std::max(21, static_cast<int>(base.height() * pass.scale))),
                               Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        }
        if (pass.invert)
            work.invertPixels();
        if (pass.close > 0)
            work = closeGray(work, pass.close);

        constexpr int pad = 32;
        const QImage padded = padFill(work, pad, attempt == 0 ? 255 : (pass.invert ? 255 - edge : edge));
        return decodeGray(padded, pass.scale * baseScale, pad, undecoded, miss);
    };
    auto merge = [&](const QList<QRDecoded>& found) {
        for (const QRDecoded& d : found)
        {
            const bool dup = std::any_of(all.begin(), all.end(), [&](const QRDecoded& r) { return r.text == d.text; });
            if (!dup)
                all.push_back(d);
        }
    };
    std::vector<GridMiss> sawGrids; // các lưới đã THẤY mà không đọc được (tối đa 4 lượt) - để tinh chỉnh ở giai đoạn 2

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        if (attempt == 1 && (!all.isEmpty() || maxUndecoded > 0 || edge >= 246))
            break;
        for (size_t pi = 0; pi < passes.size(); ++pi)
        {
            const Pass& pass = passes[pi];
            if (partial && pass.invert != partialInvert)
                continue;

            int undecoded = 0;
            GridMiss miss;
            const QList<QRDecoded> found = runPass(attempt, pass, &undecoded, &miss);
            maxUndecoded = std::max(maxUndecoded, undecoded);
            merge(found);
            if (undecoded > 0 && sawGrids.size() < 4)
                sawGrids.push_back(std::move(miss));

            if (!found.isEmpty() && undecoded == 0)
            {
                if (undecodedCount)
                    *undecodedCount = maxUndecoded;
                return all;
            }
            if (!all.isEmpty() && !partial)
            {
                partial = true;
                partialInvert = pass.invert;
            }
        }
    }

    // Giai đoạn 1b: chưa thấy GÌ cả (không ô định vị, không lưới) - có thể là mã vẽ các ô TÁCH RỜI (khe hở cắt vụn viền ô định vị). Thử lại sau phép đóng hình thái học
    // (r = 1 rồi 2); ảnh lớn thì làm ở nửa độ phân giải cho rẻ. Cực tính theo viền ảnh (nền tối -> mã sáng, đảo màu). Chỉ MỘT cực tính: thử cả hai làm quét ảnh
    // không có mã chậm gấp 4 (đo thật 164 -> 700 ms ở 1080p) - hạn chế đã biết: thẻ mã SÁNG kiểu ô tách rời đặt trên nền màn hình TỐI.
    if (all.isEmpty() && maxUndecoded == 0)
    {
        const bool big = std::max(base.width(), base.height()) > 1000;
        for (int r : {1, 2})
        {
            const Pass pass{big ? 0.5 : 1.0, edge < 128, r};
            int undecoded = 0;
            GridMiss miss;
            const QList<QRDecoded> found = runPass(edge < 246 ? 1 : 0, pass, &undecoded, &miss);
            maxUndecoded = std::max(maxUndecoded, undecoded);
            merge(found);
            if (undecoded > 0 && sawGrids.size() < 4)
                sawGrids.push_back(std::move(miss));
            if (!found.isEmpty() && undecoded == 0)
            {
                if (undecodedCount)
                    *undecodedCount = maxUndecoded;
                return all;
            }
            if (maxUndecoded > 0)
                break; // đã THẤY ô định vị: không phải lỗi khe hở - để giai đoạn 2 tinh chỉnh
        }
    }

    // Giai đoạn 2 (chỉ chạy khi quirc đã THẤY lưới mà không đọc được): tinh chỉnh lưới cho mã cách điệu/bo tròn/ngoại suy lệch (xem refineGrids()).
    // Tách riêng để ảnh thường/ảnh nhiễu không phải trả chi phí tinh chỉnh ở MỌI lượt; chỉ thử tối đa 2 lượt đã thấy lưới.
    if (maxUndecoded > 0)
    {
        // Lượt ảnh nhỏ (đã thu nhỏ) rẻ hơn nên thử trước.
        std::stable_sort(sawGrids.begin(), sawGrids.end(), [](const GridMiss& a, const GridMiss& b) { return a.scale < b.scale; });
        int tried = 0;
        for (const GridMiss& miss : sawGrids)
        {
            if (tried++ >= 2)
                break;
            int undecoded = 0;
            const QList<QRDecoded> found = refineMissed(miss, &undecoded);
            merge(found);
            if (!found.isEmpty() && undecoded == 0)
            {
                if (undecodedCount)
                    *undecodedCount = 0;
                return all;
            }
        }
    }
    if (undecodedCount)
        *undecodedCount = maxUndecoded;
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
