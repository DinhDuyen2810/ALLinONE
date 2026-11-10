// Công cụ kiểm tra tay CHỈ ĐỌC: giải mã mã QR từ một hoặc nhiều tệp ảnh bằng ĐÚNG QRCodec::decode() của ứng dụng.
// Dùng: qr_decode_probe <ảnh1> [ảnh2 ...]   - in nội dung đọc được hoặc "KHONG DOC DUOC (thay N luoi)".
#include <QBuffer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QPainter>

#include <cstdio>

#include "tools/qr/QRCodec.h"
#include "third_party/quirc/quirc.h"

#include <memory>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int failed = 0;
    if (qEnvironmentVariableIsSet("QR_PROBE_NOISE"))
    {
        // Đo thời gian giải mã ảnh nhiễu ngẫu nhiên cỡ NxN (đường "không có mã" - không được chậm đi do bước tinh chỉnh lưới).
        const int n = qEnvironmentVariableIntValue("QR_PROBE_NOISE");
        QImage noise(n, n, QImage::Format_RGB32);
        quint32 s = 12345;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                s = s * 1664525u + 1013904223u;
                noise.setPixel(x, y, (s >> 24) & 1 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
            }
        QElapsedTimer nt;
        nt.start();
        int und = 0;
        const auto r = QRCodec::decode(noise, &und);
        std::printf("nhieu %dx%d: %lld ms, doc duoc %lld, thay %d luoi\n", n, n, nt.elapsed(), static_cast<long long>(r.size()), und);
        return 0;
    }
    for (int i = 1; i < argc; ++i)
    {
        const QImage img(QString::fromLocal8Bit(argv[i]));
        if (img.isNull())
        {
            std::printf("%s: KHONG MO DUOC ANH\n", argv[i]);
            ++failed;
            continue;
        }
        if (qEnvironmentVariableIsSet("QR_PROBE_RAW"))
        {
            // Gỡ lỗi mức thấp: một lượt quirc trên ảnh xám gốc (không tiền xử lý) - in kích thước lưới + mã lỗi.
            QImage g = img.convertToFormat(QImage::Format_Grayscale8);
            if (qEnvironmentVariableIsSet("QR_PROBE_OTSU"))
            {
                // Thử nhị phân hóa toàn cục kiểu Otsu trước khi đưa cho quirc.
                long hist[256] = {};
                for (int y = 0; y < g.height(); ++y)
                    for (int x = 0; x < g.width(); ++x)
                        ++hist[g.constScanLine(y)[x]];
                const long total = static_cast<long>(g.width()) * g.height();
                double sumAll = 0;
                for (int i = 0; i < 256; ++i)
                    sumAll += i * static_cast<double>(hist[i]);
                double sumB = 0, best = -1;
                long wB = 0;
                int thr = 128;
                for (int i = 0; i < 256; ++i)
                {
                    wB += hist[i];
                    if (wB == 0 || wB == total)
                        continue;
                    sumB += i * static_cast<double>(hist[i]);
                    const double mB = sumB / wB, mF = (sumAll - sumB) / (total - wB);
                    const double between = static_cast<double>(wB) * (total - wB) * (mB - mF) * (mB - mF);
                    if (between > best)
                    {
                        best = between;
                        thr = i;
                    }
                }
                for (int y = 0; y < g.height(); ++y)
                    for (int x = 0; x < g.width(); ++x)
                        g.scanLine(y)[x] = g.constScanLine(y)[x] > thr ? 255 : 0;
            }
            if (qEnvironmentVariableIsSet("QR_PROBE_PAD"))
            {
                const int pd = qEnvironmentVariableIntValue("QR_PROBE_PAD");
                QImage padded(g.width() + 2 * pd, g.height() + 2 * pd, QImage::Format_Grayscale8);
                padded.fill(255);
                QPainter pp(&padded);
                pp.drawImage(pd, pd, g);
                pp.end();
                g = padded;
            }
            std::unique_ptr<quirc, decltype(&quirc_destroy)> q(quirc_new(), quirc_destroy);
            quirc_resize(q.get(), g.width(), g.height());
            int w = 0, h = 0;
            uint8_t* buf = quirc_begin(q.get(), &w, &h);
            for (int y = 0; y < h; ++y)
                std::copy(g.constScanLine(y), g.constScanLine(y) + w, buf + static_cast<size_t>(y) * w);
            quirc_end(q.get());
            for (int k = 0; k < quirc_count(q.get()); ++k)
            {
                quirc_code code;
                quirc_data data;
                quirc_extract(q.get(), k, &code);
                if (qEnvironmentVariableIsSet("QR_PROBE_DUMP"))
                {
                    QImage m((code.size + 8) * 8, (code.size + 8) * 8, QImage::Format_Grayscale8);
                    m.fill(255);
                    for (int yy = 0; yy < code.size; ++yy)
                        for (int xx = 0; xx < code.size; ++xx)
                        {
                            const int bit = yy * code.size + xx;
                            if (code.cell_bitmap[bit >> 3] & (1 << (bit & 7)))
                                for (int a = 0; a < 8; ++a)
                                    for (int b = 0; b < 8; ++b)
                                        m.setPixel((xx + 4) * 8 + a, (yy + 4) * 8 + b, 0);
                        }
                    m.save(qEnvironmentVariable("QR_PROBE_DUMP"));
                    std::printf("  corners: (%d,%d) (%d,%d) (%d,%d) (%d,%d)\n", code.corners[0].x, code.corners[0].y, code.corners[1].x, code.corners[1].y,
                                code.corners[2].x, code.corners[2].y, code.corners[3].x, code.corners[3].y);
                }
                std::printf("  luoi %d: size=%d  err=%s\n", k, code.size, quirc_strerror(quirc_decode(&code, &data)));
            }
        }
        if (qEnvironmentVariableIsSet("QR_PROBE_SWEEP"))
        {
            // Mô phỏng quét màn hình: dán mã (đã đổi tỉ lệ) lên một "màn hình" 1920x1080 nền tối/gradient rồi giải mã; in tỉ lệ đọc được.
            int ok = 0, total = 0;
            const double scales[] = {0.35, 0.5, 0.6, 0.75, 0.9, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0};
            for (int bgKind = 0; bgKind < 3; ++bgKind)
                for (double sc : scales)
                {
                    const QImage src = img.convertToFormat(QImage::Format_RGB32);
                    const QImage scaled = src.scaled(qMax(8, qRound(src.width() * sc)), qMax(8, qRound(src.height() * sc)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                    QImage screen(1920, 1080, QImage::Format_RGB32);
                    for (int y = 0; y < screen.height(); ++y)
                        for (int x = 0; x < screen.width(); ++x)
                            screen.setPixel(x, y, bgKind == 0 ? qRgb(20, 24, 33) : bgKind == 1 ? qRgb(x * 255 / 1919, 90, y * 255 / 1079) : qRgb(240, 240, 240));
                    QPainter pt(&screen);
                    pt.drawImage(317, 211, scaled);
                    pt.end();
                    ++total;
                    const bool good = !QRCodec::decode(screen).isEmpty();
                    ok += good ? 1 : 0;
                    if (!good && qEnvironmentVariableIsSet("QR_PROBE_SAVE"))
                        screen.save(qEnvironmentVariable("QR_PROBE_SAVE") + "/fail_" + QString::number(bgKind) + "_" + QString::number(sc) + ".png");
                    if (!good)
                        std::printf("    that bai: nen %d, ti le %.2f\n", bgKind, sc);
                }
            std::printf("  QUET MAN HINH GIA LAP: %d/%d\n", ok, total);
        }
        if (qEnvironmentVariableIsSet("QR_PROBE_SNIP"))
        {
            // Mô phỏng "Chụp vùng màn hình": người dùng kéo khung quanh mã - khung ôm sát/rộng/hơi cắt vào vùng yên tĩnh, trên nền sáng/tối/gradient.
            int ok = 0, total = 0;
            const double scales[] = {0.6, 0.75, 1.0, 1.25, 1.5, 2.0};
            const int margins[] = {-6, 0, 12, 60, 200};
            for (int bgKind = 0; bgKind < 3; ++bgKind)
                for (double sc : scales)
                    for (int m : margins)
                    {
                        const QImage src = img.convertToFormat(QImage::Format_RGB32);
                        const QImage scaled = src.scaled(qMax(8, qRound(src.width() * sc)), qMax(8, qRound(src.height() * sc)), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                        QImage screen(1000, 800, QImage::Format_RGB32);
                        for (int y = 0; y < screen.height(); ++y)
                            for (int x = 0; x < screen.width(); ++x)
                                screen.setPixel(x, y, bgKind == 0 ? qRgb(20, 24, 33) : bgKind == 1 ? qRgb(x * 255 / 999, 90, y * 255 / 799) : qRgb(240, 240, 240));
                        QPainter pt(&screen);
                        pt.drawImage(100, 80, scaled);
                        pt.end();
                        const QRect r = QRect(100, 80, scaled.width(), scaled.height()).adjusted(-m, -m, m, m).intersected(screen.rect());
                        ++total;
                        QImage snip = screen.copy(r);
                        if (qEnvironmentVariableIsSet("QR_PROBE_JPEG"))
                        {
                            QByteArray ba;
                            QBuffer buf(&ba);
                            buf.open(QIODevice::WriteOnly);
                            snip.save(&buf, "JPG", qEnvironmentVariableIntValue("QR_PROBE_JPEG"));
                            snip.loadFromData(ba, "JPG");
                        }
                        const bool good = !QRCodec::decode(snip).isEmpty();
                        ok += good ? 1 : 0;
                        if (!good && qEnvironmentVariableIsSet("QR_PROBE_SAVE"))
                            snip.save(qEnvironmentVariable("QR_PROBE_SAVE") + "/snip_" + QString::number(bgKind) + "_" + QString::number(sc) + "_" + QString::number(m) + ".png");
                        if (!good)
                            std::printf("    snip that bai: nen %d, ti le %.2f, le %d\n", bgKind, sc, m);
                    }
            std::printf("  SNIP GIA LAP: %d/%d\n", ok, total);
        }
        QElapsedTimer t;
        t.start();
        int undecoded = 0;
        const auto res = QRCodec::decode(img, &undecoded);
        if (res.isEmpty())
        {
            ++failed;
            std::printf("%s (%dx%d): KHONG DOC DUOC (thay %d luoi) %lld ms\n", argv[i], img.width(), img.height(), undecoded, t.elapsed());
        }
        else
            for (const auto& d : res)
                std::printf("%s (%dx%d): OK v%d %s -> %s  %lld ms\n", argv[i], img.width(), img.height(), d.version, qPrintable(d.format), qPrintable(d.text), t.elapsed());
    }
    return failed == 0 ? 0 : 1;
}
