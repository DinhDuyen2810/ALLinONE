// Stress test cho QR Tools + WiFi Connection: dội hàng nghìn/chục nghìn đầu vào ngẫu nhiên, bệnh lý và thao
// tác dồn dập vào lõi + giao diện, kiểm tra các BẤT BIẾN (không crash/ngoại lệ, không lộ mật khẩu, không
// mất dữ liệu âm thầm, máy trạng thái không kẹt) thay vì từng ca cụ thể như qr_tests/wifi_tests.
// Build: cmake --build build --target qrwifi_stress_tests && build\qrwifi_stress_tests.exe
//
// Tùy chọn:
//   qrwifi_stress_tests.exe [nhóm,...]   chỉ chạy vài nhóm: qr,decode,payload,history,xml,speed,ui
//   QRWIFI_STRESS_SEED=<số>               chạy lại đúng chuỗi ngẫu nhiên của một lần lỗi (seed in ở đầu)
//
// LƯU Ý AN TOÀN (CLAUDE.md mục 5):
//  - WiFi: CHỈ gọi API WLAN đọc (qua việc dựng WifiWindow). ConnectDialog được thử với một WlanController
//    CHƯA open() - mọi lời gọi của nó dừng ở ensureOpen() nên không có lệnh WlanSetProfile/WlanConnect/
//    WlanDisconnect/WlanDeleteProfile nào tới được hệ thống.
//  - Đo tốc độ: KHÔNG ra Internet. QNetworkAccessManager của SpeedTestRunner được ép đi qua một "proxy"
//    giả nghe ở 127.0.0.1 ngay trong test (URL của runner là hằng số https nên không trỏ thẳng về máy chủ
//    cục bộ được) - đủ để thử máy trạng thái start/stop/lỗi/hết giờ, KHÔNG thử được pha tải xuống/tải lên.
//  - Không bật camera, không chụp màn hình, không đụng clipboard.
#include "OffscreenPlatform.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSet>
#include <QSpinBox>
#include <QSvgRenderer>
#include <QTabWidget>
#include <QTableWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QTimer>
#include <QTransform>
#include <cstdio>
#include <exception>

#include <windows.h>
#include <psapi.h>

#include "core/AppPaths.h"
#include "tools/qr/QRCodec.h"
#include "tools/qr/QRGenerateTab.h"
#include "tools/qr/QRHistoryStore.h"
#include "tools/qr/QRHistoryTab.h"
#include "tools/qr/QRPayload.h"
#include "tools/qr/QRScanTab.h"
#include "tools/qr/QRWindow.h"
#include "tools/wifi/ConnectDialog.h"
#include "tools/wifi/WifiWindow.h"
#include "tools/wifi/engine/ConnectionWatcher.h"
#include "tools/wifi/engine/SpeedTestRunner.h"
#include "tools/wifi/engine/WlanController.h"
#include "tools/wifi/engine/WlanProfileXml.h"
#include "tools/wifi/model/WifiNetwork.h"

static int g_fail = 0, g_pass = 0;
// Vòng lặp chục nghìn lần mà cùng hỏng một chỗ thì chỉ in vài chục dòng đầu (vẫn đếm đủ vào failed=).
#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (cond) { ++g_pass; }                                                        \
        else { if (++g_fail <= 60) std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static QRandomGenerator g_rng;
static int g_notes = 0;

static int rnd(int lo, int hi) // cả hai đầu
{
    return lo + static_cast<int>(g_rng.bounded(static_cast<quint32>(hi - lo + 1)));
}
static bool chance(int percent)
{
    return rnd(0, 99) < percent;
}
/// Ghi chú ngữ cảnh cho một lần FAIL (giới hạn số dòng).
static void note(const QString& text)
{
    if (++g_notes <= 60)
        std::printf("    ^ %s\n", qPrintable(text));
}
static QString preview(const QString& s)
{
    return QString::fromLatin1(s.left(48).toUtf8().toPercentEncoding(" :;,\\\"=<>/"));
}
static void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    while (t.elapsed() < ms);
}
/// In thời gian từng chặng của các nhóm chạy lâu (và đẩy stdout ngay) để thấy tiến độ / chặng nào chậm.
static QElapsedTimer g_lap;
static void lap(const char* what)
{
    std::printf("    . %s: %lld ms\n", what, static_cast<long long>(g_lap.restart()));
    std::fflush(stdout);
}

static double privateMb()
{
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return 0.0;
    return static_cast<double>(pmc.PrivateUsage) / (1024.0 * 1024.0);
}

// ------------------------------------------------------------------ Sinh chuỗi ngẫu nhiên

static void appendCodePoint(QString& s, char32_t cp)
{
    if (QChar::requiresSurrogates(cp))
    {
        s += QChar(QChar::highSurrogate(cp));
        s += QChar(QChar::lowSurrogate(cp));
    }
    else
    {
        s += QChar(static_cast<char16_t>(cp));
    }
}

/// Chuỗi Unicode HỢP LỆ (không có surrogate lẻ): ASCII, tiếng Việt, kana, CJK, emoji, ký tự điều khiển (kể
/// cả U+0000) nếu allowControl.
static QString randomUnicode(int len, bool allowControl = true)
{
    QString s;
    while (s.size() < len)
    {
        switch (rnd(0, 9))
        {
            case 0: case 1: case 2: case 3: appendCodePoint(s, rnd(0x20, 0x7E)); break;
            case 4: appendCodePoint(s, rnd(0xC0, 0x1EF9)); break;
            case 5: appendCodePoint(s, rnd(0x3040, 0x30FF)); break;
            case 6: appendCodePoint(s, rnd(0x4E00, 0x9FFF)); break;
            case 7: appendCodePoint(s, rnd(0x1F300, 0x1F64F)); break;
            case 8: appendCodePoint(s, allowControl ? rnd(0x00, 0x1F) : rnd(0x20, 0x7E)); break;
            default:
            {
                int c = rnd(0x80, 0xFFFD);
                if ((c >= 0xD800 && c <= 0xDFFF) || c == 0xFEFF)
                    c = 0xFFFD;
                appendCodePoint(s, c);
                break;
            }
        }
    }
    return s;
}

static QString randomFrom(const char* alphabet, int len)
{
    const int n = static_cast<int>(qstrlen(alphabet));
    QString s;
    s.reserve(len);
    for (int i = 0; i < len; ++i)
        s += QLatin1Char(alphabet[rnd(0, n - 1)]);
    return s;
}

// ================================================================== 1. QR: mã hóa -> render -> giải mã

static QString randomQrPayload(int* kindOut)
{
    const int kind = rnd(0, 6);
    *kindOut = kind;
    int len;
    const int bucket = rnd(0, 99);
    if (bucket < 55)      len = rnd(0, 60);
    else if (bucket < 80) len = rnd(61, 600);
    else if (bucket < 93) len = rnd(601, 3000);
    else                  len = rnd(3001, 8000); // phần lớn vượt sức chứa -> phải báo lỗi sạch

    switch (kind)
    {
        case 0: return randomFrom("0123456789", len);
        case 1: return randomFrom("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:", len);
        case 2: return randomFrom("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", len);
        case 3: // "nhị phân": mọi điểm mã 0..255, kể cả NUL
        {
            QString s;
            for (int i = 0; i < len; ++i)
                s += QChar(static_cast<char16_t>(rnd(0, 255)));
            return s;
        }
        case 4: return randomUnicode(len, false);
        case 5: return randomUnicode(len, true);
        default: // chuỗi số/chữ HOA có NUL chen giữa: nhánh makeBytes() của encode()
        {
            QString s = randomFrom("0123456789ABCDEF", len);
            for (int i = 0, n = rnd(1, 3); i < n && !s.isEmpty(); ++i)
                s[rnd(0, s.size() - 1)] = QChar(u'\0');
            return s;
        }
    }
}

static void stressQrRoundTrip(int count)
{
    QElapsedTimer timer;
    timer.start();
    int encoded = 0, rejected = 0, mirrored = 0, viaSvg = 0, maxVersion = 0;

    for (int i = 0; i < count; ++i)
    {
        int kind = 0;
        const QString text = randomQrPayload(&kind);
        const QREcc ecc = static_cast<QREcc>(rnd(0, 3));
        const QByteArray utf8 = text.toUtf8();
        const QString expected = QString::fromUtf8(utf8);

        QString err;
        QRMatrix m;
        try
        {
            m = QRCodec::encode(text, ecc, &err);
        }
        catch (...)
        {
            CHECK(!"QRCodec::encode de lot ngoai le");
            note(QString("kind=%1 len=%2 ecc=%3").arg(kind).arg(text.size()).arg(int(ecc)));
            continue;
        }

        if (!m.isValid())
        {
            ++rejected;
            CHECK(!err.isEmpty());
            // Mức H ở chế độ byte chứa 1273 byte: nội dung ngắn hơn thế mà bị từ chối là lỗi.
            const bool plausible = text.isEmpty() || utf8.size() > 1273;
            CHECK(plausible);
            if (!plausible)
                note(QString("tu choi oan: kind=%1 bytes=%2 ecc=%3 err=%4").arg(kind).arg(utf8.size()).arg(int(ecc)).arg(err));
            continue;
        }

        ++encoded;
        maxVersion = std::max(maxVersion, m.version);
        CHECK(err.isEmpty());
        CHECK(m.version >= 1 && m.version <= 40 && m.size == 17 + 4 * m.version);
        CHECK(m.modules.size() == static_cast<size_t>(m.size) * m.size);
        CHECK(static_cast<int>(m.ecc) >= static_cast<int>(ecc)); // thư viện chỉ được NÂNG mức sửa lỗi

        QRStyle style;
        style.quietZone = rnd(0, 8);
        const int px = rnd(3, 6);
        style.targetSize = (m.size + 2 * style.quietZone) * px;
        const QImage img = QRCodec::render(m, style);
        CHECK(!img.isNull() && img.width() == style.targetSize && img.height() == img.width());

        const QList<QRDecoded> res = QRCodec::decode(img);
        const bool ok = res.size() == 1 && res.first().text == expected;
        CHECK(ok);
        if (!ok)
            note(QString("round-trip: kind=%1 chars=%2 bytes=%3 ecc=%4 v%5 px=%6 quiet=%7 found=%8 text=%9")
                     .arg(kind).arg(text.size()).arg(utf8.size()).arg(int(ecc)).arg(m.version).arg(px)
                     .arg(style.quietZone).arg(res.size()).arg(preview(text)));
        else
        {
            CHECK(res.first().version == m.version && res.first().ecc == m.ecc);
            CHECK(res.first().corners.size() == 4);
        }

        // Ảnh bị lật gương (camera trước/in ngược) vẫn phải ra đúng nội dung.
        if (ok && chance(10))
        {
            ++mirrored;
            const QImage flipped = img.transformed(QTransform().scale(-1, 1));
            const QList<QRDecoded> r2 = QRCodec::decode(flipped);
            const bool ok2 = r2.size() == 1 && r2.first().text == expected;
            CHECK(ok2);
            if (!ok2)
                note(QString("mirror: v%1 px=%2 bytes=%3").arg(m.version).arg(px).arg(utf8.size()));
        }

        // SVG xuất ra phải là cùng một mã (dựng lại bằng QSvgRenderer rồi giải mã).
        if (ok && m.version <= 20 && chance(6))
        {
            ++viaSvg;
            style.roundedModules = false;
            QSvgRenderer renderer(QRCodec::toSvg(m, style).toUtf8());
            CHECK(renderer.isValid());
            QImage canvas(img.size(), QImage::Format_ARGB32);
            canvas.fill(Qt::white);
            QPainter p(&canvas);
            renderer.render(&p);
            p.end();
            const QList<QRDecoded> r3 = QRCodec::decode(canvas);
            const bool ok3 = r3.size() == 1 && r3.first().text == expected;
            CHECK(ok3);
            if (!ok3)
                note(QString("svg: v%1 px=%2 bytes=%3").arg(m.version).arg(px).arg(utf8.size()));
        }
    }

    // Biên sức chứa của mã v40: đúng giới hạn thì mã hóa + đọc lại được, quá 1 ký tự thì báo lỗi sạch.
    struct Edge { QString text; QREcc ecc; bool fits; };
    QList<Edge> edges = {
        {QString(7089, QChar('7')), QREcc::Low, true},
        {QString(7090, QChar('7')), QREcc::Low, false},
        {QString(4296, QChar('A')), QREcc::Low, true},
        {QString(4297, QChar('A')), QREcc::Low, false},
        {QString(3057, QChar('3')), QREcc::High, true},
        {QString(3058, QChar('3')), QREcc::High, false},
        {QString(100000, QChar('1')), QREcc::Low, false},
        {QString(1, QChar(u'\0')), QREcc::High, true},
        {QString(2953, QChar(u'\0')), QREcc::Low, true},
        {QString(1 << 20, QChar(0x1EF9)), QREcc::Medium, false},
    };
    for (QREcc e : {QREcc::Low, QREcc::Medium, QREcc::Quartile, QREcc::High})
    {
        edges.push_back({QString(QRCodec::maxBytes(e), QChar('z')), e, true});
        edges.push_back({QString(QRCodec::maxBytes(e) + 1, QChar('z')), e, false});
    }
    for (const Edge& e : edges)
    {
        QString err;
        const QRMatrix m = QRCodec::encode(e.text, e.ecc, &err);
        CHECK(m.isValid() == e.fits);
        CHECK(m.isValid() == err.isEmpty());
        if (!m.isValid())
            continue;
        QRStyle style;
        style.targetSize = (m.size + 8) * 4;
        const QList<QRDecoded> res = QRCodec::decode(QRCodec::render(m, style));
        const bool ok = res.size() == 1 && res.first().text == e.text;
        CHECK(ok);
        if (!ok)
            note(QString("bien suc chua: chars=%1 ecc=%2 v%3 found=%4").arg(e.text.size()).arg(int(e.ecc)).arg(m.version).arg(res.size()));
    }

    // Kiểu vẽ bệnh lý: không crash, ảnh hợp lệ hoặc null (không bao giờ ảnh "nửa vời").
    {
        const QRMatrix m = QRCodec::encode("render edge", QREcc::High);
        QImage hugeLogo(3000, 7, QImage::Format_ARGB32);
        hugeLogo.fill(Qt::red);
        QImage dotLogo(1, 1, QImage::Format_Mono);
        dotLogo.fill(1);
        for (int size : {-100000, -1, 0, 1, 20, 21, 512, 4096})
            for (int quiet : {-50, -1, 0, 4, 32, 1000})
                for (int logo = 0; logo < 3; ++logo)
                {
                    QRStyle style;
                    style.targetSize = size;
                    style.quietZone = quiet;
                    style.roundedModules = (logo == 1);
                    style.logo = logo == 1 ? hugeLogo : (logo == 2 ? dotLogo : QImage());
                    style.logoRatio = logo == 1 ? 99.0 : -3.0;
                    const QImage img = QRCodec::render(m, style);
                    const int cells = m.size + 2 * std::clamp(quiet, 0, 32);
                    CHECK(!img.isNull() && img.width() == img.height() && img.width() >= cells && img.width() % cells == 0);
                    const QString svg = QRCodec::toSvg(m, style);
                    CHECK(svg.startsWith("<?xml") && svg.trimmed().endsWith("</svg>"));
                    if (logo == 0)
                    {
                        const QList<QRDecoded> res = QRCodec::decode(img);
                        CHECK(res.size() == 1 && res.first().text == "render edge");
                    }
                }
        // Ma trận rỗng/không hợp lệ.
        CHECK(QRCodec::render(QRMatrix(), QRStyle()).isNull());
        CHECK(QRCodec::toSvg(QRMatrix(), QRStyle()).isEmpty());
    }

    std::printf("[qr] %d payload: ma hoa %d (lon nhat v%d), tu choi %d, lat guong %d, qua SVG %d - %lld ms\n", count,
                encoded, maxVersion, rejected, mirrored, viaSvg, static_cast<long long>(timer.elapsed()));
}

// ================================================================== 2. QR: giải mã ảnh rác / nhiễu / cỡ biên

static void fillRandom(QImage& img)
{
    if (img.isNull())
        return;
    if (img.format() == QImage::Format_Mono || img.format() == QImage::Format_MonoLSB)
        img.setColorTable({qRgb(rnd(0, 255), rnd(0, 255), rnd(0, 255)), qRgb(rnd(0, 255), rnd(0, 255), rnd(0, 255))});
    else if (img.format() == QImage::Format_Indexed8)
    {
        QList<QRgb> table;
        for (int i = 0; i < 256; ++i)
            table << qRgba(rnd(0, 255), rnd(0, 255), rnd(0, 255), rnd(0, 255));
        img.setColorTable(table);
    }
    g_rng.fillRange(reinterpret_cast<quint32*>(img.bits()), img.sizeInBytes() / 4);
}

static void stampFinder(QImage& img, int x0, int y0, int px)
{
    QPainter p(&img);
    p.setPen(Qt::NoPen);
    p.fillRect(x0 - px, y0 - px, 9 * px, 9 * px, Qt::white);
    p.fillRect(x0, y0, 7 * px, 7 * px, Qt::black);
    p.fillRect(x0 + px, y0 + px, 5 * px, 5 * px, Qt::white);
    p.fillRect(x0 + 2 * px, y0 + 2 * px, 3 * px, 3 * px, Qt::black);
}

/// Ảnh nhiễu nhị phân có 3 ô định vị thật: quirc "nhìn thấy" một lưới nhưng không giải mã được - đi qua
/// nhánh lật gương + các lượt phóng to/thu nhỏ bổ sung của decode().
static QImage fakeCode(int modules, int px)
{
    const int side = (modules + 8) * px;
    QImage img(side, side, QImage::Format_RGB32);
    img.fill(Qt::white);
    {
        QPainter p(&img);
        p.setPen(Qt::NoPen);
        for (int y = 0; y < modules; ++y)
            for (int x = 0; x < modules; ++x)
                if (chance(50))
                    p.fillRect((x + 4) * px, (y + 4) * px, px, px, Qt::black);
    }
    stampFinder(img, 4 * px, 4 * px, px);
    stampFinder(img, (4 + modules - 7) * px, 4 * px, px);
    stampFinder(img, 4 * px, (4 + modules - 7) * px, px);
    return img;
}

static QList<QRDecoded> timedDecode(const QImage& img, qint64* worstMs, QString* worstWhat, const QString& what)
{
    QElapsedTimer t;
    t.start();
    QList<QRDecoded> res;
    try
    {
        res = QRCodec::decode(img);
    }
    catch (...)
    {
        CHECK(!"QRCodec::decode de lot ngoai le");
        note(what);
    }
    const qint64 ms = t.elapsed();
    if (ms > *worstMs)
    {
        *worstMs = ms;
        *worstWhat = what;
    }
    for (const QRDecoded& d : res)
        CHECK(d.corners.size() == 4 && d.version >= 1 && d.version <= 40 && !d.format.isEmpty());
    return res;
}

static void stressDecode(int noiseCount, int damagedCount)
{
    QElapsedTimer timer;
    timer.start();
    qint64 worstMs = 0;
    QString worstWhat;
    int images = 0, falsePositives = 0;

    // (a) Mọi QImage::Format x các cỡ biên.
    const QList<QSize> edgeSizes = {{1, 1}, {1, 21}, {21, 1}, {20, 20}, {21, 21}, {22, 500}, {64, 64}, {333, 77}};
    for (int f = QImage::Format_Mono; f < QImage::NImageFormats; ++f)
        for (const QSize& sz : edgeSizes)
        {
            QImage img(sz, static_cast<QImage::Format>(f));
            fillRandom(img);
            ++images;
            falsePositives += timedDecode(img, &worstMs, &worstWhat, QString("format %1 %2x%3").arg(f).arg(sz.width()).arg(sz.height())).size();
        }
    CHECK(QRCodec::decode(QImage()).isEmpty());
    CHECK(QRCodec::decode(QImage(0, 0, QImage::Format_RGB32)).isEmpty());
    CHECK(QRCodec::decode(QImage(0, 50, QImage::Format_RGB32)).isEmpty());

    // (b) Ảnh rất lớn / rất dẹt.
    struct Big { int w, h; QImage::Format f; bool noise; };
    const QList<Big> bigs = {
        {1, 5000, QImage::Format_RGB32, true},        {5000, 1, QImage::Format_Grayscale8, true},
        {3, 5000, QImage::Format_ARGB32, true},       {5000, 21, QImage::Format_RGB888, true},
        {21, 20000, QImage::Format_Mono, true},       {2401, 2401, QImage::Format_RGB32, false},
        {4000, 4000, QImage::Format_RGB32, true},     {4000, 4000, QImage::Format_ARGB32, true},
        {6000, 6000, QImage::Format_Grayscale8, true}, {30000, 30, QImage::Format_Grayscale8, true},
    };
    for (const Big& b : bigs)
    {
        QImage img(b.w, b.h, b.f);
        if (img.isNull())
            continue;
        if (b.noise)
            fillRandom(img);
        else
            img.fill(Qt::white);
        ++images;
        qint64 one = 0;
        QString ignored;
        falsePositives += timedDecode(img, &one, &ignored, QString()).size();
        std::printf("    anh lon %dx%d format %d: %lld ms\n", b.w, b.h, int(b.f), static_cast<long long>(one));
        CHECK(one < 60000);
        if (one > worstMs)
        {
            worstMs = one;
            worstWhat = QString("%1x%2").arg(b.w).arg(b.h);
        }
    }

    // Một mã thật nằm trong ảnh rất lớn vẫn phải được tìm thấy (đường thu nhỏ về 2400 px).
    {
        QImage canvas(4000, 3000, QImage::Format_RGB32);
        canvas.fill(Qt::white);
        QRStyle style;
        style.targetSize = 900;
        QPainter p(&canvas);
        p.drawImage(2500, 1700, QRCodec::render(QRCodec::encode("ma trong anh lon", QREcc::Medium), style));
        p.end();
        const QList<QRDecoded> res = timedDecode(canvas, &worstMs, &worstWhat, "ma that trong anh 4000x3000");
        CHECK(res.size() == 1 && res.first().text == "ma trong anh lon");
        if (res.size() == 1)
            CHECK(QRect(2480, 1680, 940, 940).contains(res.first().corners.boundingRect())); // tọa độ ảnh GỐC
    }

    // (c) Hàng nghìn ảnh nhiễu nhỏ: nhiễu trắng, nhiễu khối, lưới giả có ô định vị.
    for (int i = 0; i < noiseCount; ++i)
    {
        QImage img;
        const int mode = rnd(0, 3);
        if (mode == 0)
        {
            img = QImage(rnd(1, 180), rnd(1, 180), chance(50) ? QImage::Format_RGB32 : QImage::Format_Grayscale8);
            fillRandom(img);
        }
        else if (mode == 1) // nhiễu khối đen/trắng cỡ "ô QR"
        {
            const int block = rnd(1, 8), w = rnd(21, 60), h = rnd(21, 60);
            img = QImage(w * block, h * block, QImage::Format_RGB32);
            QPainter p(&img);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    p.fillRect(x * block, y * block, block, block, chance(50) ? Qt::black : Qt::white);
        }
        else
        {
            img = fakeCode(17 + 4 * rnd(1, 10), rnd(2, 5));
            if (chance(30))
                img.invertPixels();
        }
        ++images;
        falsePositives += timedDecode(img, &worstMs, &worstWhat, QString("nhieu mode %1 %2x%3").arg(mode).arg(img.width()).arg(img.height())).size();
    }

    // (d) Mã thật bị làm hỏng: xoay, đảo màu (phải đọc ĐÚNG), che khối, muối tiêu, cắt, thu nhỏ, xoay lệch
    // (đọc được thì phải ra đúng nội dung gốc - không bao giờ ra nội dung KHÁC).
    int lossless = 0, lossy = 0, lossyRead = 0, wrong = 0;
    for (int i = 0; i < damagedCount; ++i)
    {
        const QString text = randomUnicode(rnd(1, 120), false);
        const QString expected = QString::fromUtf8(text.toUtf8());
        const QRMatrix m = QRCodec::encode(text, static_cast<QREcc>(rnd(0, 3)));
        if (!m.isValid())
            continue;
        QRStyle style;
        style.targetSize = (m.size + 8) * rnd(3, 6);
        const QImage clean = QRCodec::render(m, style).convertToFormat(QImage::Format_RGB32);

        const int mode = rnd(0, 7);
        QImage img = clean;
        bool mustRead = false;
        switch (mode)
        {
            case 0:
                img = clean.transformed(QTransform().rotate(90 * rnd(1, 3)));
                mustRead = true;
                break;
            case 1:
                img.invertPixels();
                mustRead = true;
                break;
            case 2: // che khối
            {
                QPainter p(&img);
                for (int k = 0, n = rnd(1, 12); k < n; ++k)
                {
                    const int s = rnd(1, std::max(2, img.width() / 5));
                    p.fillRect(rnd(0, img.width() - 1), rnd(0, img.height() - 1), s, s, QColor::fromRgb(g_rng.generate()));
                }
                break;
            }
            case 3: // muối tiêu
            {
                const int percent = rnd(1, 30);
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (chance(percent))
                            img.setPixel(x, y, chance(50) ? qRgb(0, 0, 0) : qRgb(255, 255, 255));
                break;
            }
            case 4: // cắt
                img = clean.copy(rnd(0, img.width() / 2), rnd(0, img.height() / 2), rnd(1, img.width()), rnd(1, img.height()));
                break;
            case 5: // thu nhỏ mạnh rồi phóng lại
            {
                const int s = std::max(8, img.width() * rnd(15, 90) / 100);
                img = clean.scaled(s, s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                if (chance(50))
                    img = img.scaled(clean.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
                break;
            }
            case 6: // xoay góc bất kỳ (góc ảnh trong suốt)
                img = clean.convertToFormat(QImage::Format_ARGB32).transformed(QTransform().rotate(rnd(1, 359)), Qt::SmoothTransformation);
                break;
            default: // méo xiên
                img = clean.transformed(QTransform().shear(rnd(-40, 40) / 100.0, rnd(-40, 40) / 100.0), Qt::SmoothTransformation);
                break;
        }

        ++images;
        const QList<QRDecoded> res = timedDecode(img, &worstMs, &worstWhat, QString("ma hong mode %1 v%2").arg(mode).arg(m.version));
        const bool read = res.size() == 1 && res.first().text == expected;
        if (mustRead)
        {
            ++lossless;
            CHECK(read);
            if (!read)
                note(QString("mode %1 v%2 found=%3").arg(mode).arg(m.version).arg(res.size()));
        }
        else
        {
            ++lossy;
            lossyRead += read ? 1 : 0;
            for (const QRDecoded& d : res)
                if (d.text != expected)
                {
                    ++wrong;
                    note(QString("doc SAI noi dung: mode %1 v%2 got=%3 want=%4").arg(mode).arg(m.version).arg(preview(d.text), preview(expected)));
                }
        }
    }
    CHECK(wrong == 0);
    CHECK(worstMs < 60000);

    std::printf("[decode] %d anh (nhieu/cỡ bien/ma hong): bao dong gia tren nhieu %d, khong mat du lieu %d, co mat %d (doc lai duoc %d), "
                "cham nhat %lld ms (%s) - %lld ms\n",
                images, falsePositives, lossless, lossy, lossyRead, static_cast<long long>(worstMs), qPrintable(worstWhat),
                static_cast<long long>(timer.elapsed()));
}

// ================================================================== 3. QRPayload::parse + maskSecrets

/// Bộ tách ĐỘC LẬP với QRPayload (không dùng lại mã đang được kiểm): tách theo ';' không bị escape.
static QStringList refSplit(const QString& body)
{
    QStringList parts;
    QString cur;
    bool escaped = false;
    for (const QChar c : body)
    {
        if (escaped)
        {
            cur += c;
            escaped = false;
        }
        else if (c == u'\\')
        {
            cur += c;
            escaped = true;
        }
        else if (c == u';')
        {
            parts << cur;
            cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    parts << cur;
    return parts;
}

/// Trong một chuỗi WIFI: (đã che), còn trường P nào mang giá trị khác rỗng và khác mặt nạ không?
static bool hasVisibleWifiPassword(const QString& shown)
{
    const QString t = shown.trimmed();
    if (!t.startsWith("WIFI:", Qt::CaseInsensitive))
        return false;
    for (const QString& part : refSplit(t.mid(5)))
        if (part.size() > 2 && part[1] == u':' && (part[0] == u'P' || part[0] == u'p') && part.mid(2) != QRPayload::secretMask())
            return true;
    return false;
}

static QString randomPayloadSoup()
{
    static const QStringList tokens = {
        "WIFI:", "wifi:", "WiFi:", " WIFI:", "S:", "P:", "p:", "T:", "H:", "T:WPA", "T:nopass", "H:true", ";", ";;", "\\", "\\;",
        "\\\\", ":", ",", "\"", "\n", "\r\n", "\t", " ", "BEGIN:VCARD", "END:VCARD", "VERSION:3.0", "N:", "FN:", "ORG:",
        "TEL;TYPE=CELL:", "ADR:;;", "EMAIL:", "URL:", "MECARD:", "MATMSG:", "TO:", "SUB:", "BODY:", "mailto:", "MAILTO:",
        "?subject=", "&body=", "%", "%zz", "%00", "tel:", "smsto:", "SMSTO:", "sms:", "geo:", "1e999", "nan", "-0", "inf",
        "21.02,105.8", "http://", "https://", "ftp://", "a@b.co", "+84 912 345 678", "?", "#", "<b>", "&amp;", "••••••••",
    };
    QString s;
    for (int i = 0, n = rnd(0, 14); i < n; ++i)
    {
        const int pick = rnd(0, 9);
        if (pick < 6)
            s += tokens[rnd(0, tokens.size() - 1)];
        else if (pick < 9)
            s += randomUnicode(rnd(0, 12));
        else
            s += QString(rnd(1, 40), QChar(static_cast<char16_t>(rnd(0x20, 0x7E))));
    }
    return s;
}

/// Chuỗi WIFI: dựng từ các trường ngẫu nhiên (lặp khóa, khóa chữ thường, giá trị rỗng, escape dở dang...) -
/// súp token chung hiếm khi ra được một trường P có giá trị.
static QString randomWifiSoup()
{
    static const QStringList prefixes = {"WIFI:", "wifi:", "WiFi:", "  WIFI:"};
    static const QStringList keys = {"S:", "P:", "p:", "T:", "H:", "X:", "PASS:", ":", "", "P", "P:\\", "P::"};
    QString s = prefixes[rnd(0, prefixes.size() - 1)];
    for (int i = 0, n = rnd(0, 6); i < n; ++i)
    {
        s += keys[rnd(0, keys.size() - 1)];
        switch (rnd(0, 5))
        {
            case 0: break; // giá trị rỗng
            case 1: s += randomFrom(";:\\,\"ab", rnd(0, 6)); break;
            case 2: s += QRPayload::escapeWifi(randomUnicode(rnd(1, 8))); break;
            default: s += randomFrom("abcXYZ019", rnd(1, 8)); break;
        }
        if (chance(85))
            s += ";";
    }
    if (chance(50))
        s += ";";
    return s;
}

static void stressPayload(int soupCount, int wifiCount)
{
    QElapsedTimer timer;
    timer.start();
    int wifiSeen = 0, secretsSeen = 0;

    // (a) Súp token ngẫu nhiên: không crash; che mật khẩu nhất quán với parse().
    for (int i = 0; i < soupCount; ++i)
    {
        const QString s = chance(35) ? randomWifiSoup() : randomPayloadSoup();
        ParsedPayload p;
        QString masked;
        try
        {
            p = QRPayload::parse(s);
            masked = QRPayload::maskSecrets(s);
        }
        catch (...)
        {
            CHECK(!"QRPayload de lot ngoai le");
            note(preview(s));
            continue;
        }
        CHECK(!p.typeName.isEmpty());
        CHECK(p.secretField == -1 || (p.secretField >= 0 && p.secretField < p.fields.size()));
        CHECK(p.secretField == -1 || !p.fields[p.secretField].second.isEmpty());

        const bool isWifi = s.trimmed().startsWith("WIFI:", Qt::CaseInsensitive);
        CHECK(isWifi == (p.type == QRContentType::Wifi));
        if (!isWifi)
        {
            CHECK(masked == s); // loại khác: nguyên văn
            continue;
        }
        ++wifiSeen;
        secretsSeen += p.secretField >= 0 ? 1 : 0;

        // Bản che không bao giờ còn trường P đọc được, và phân tích lại nó chỉ ra mặt nạ (hoặc rỗng).
        const bool leak = hasVisibleWifiPassword(masked);
        CHECK(!leak);
        if (leak)
            note("maskSecrets con lo: " + preview(s));
        const ParsedPayload pm = QRPayload::parse(masked);
        CHECK(pm.fields.size() == 4 && (pm.fields[2].second.isEmpty() || pm.fields[2].second == QRPayload::secretMask()));
        // Các trường không nhạy cảm giữ nguyên qua việc che.
        CHECK(pm.fields[0].second == p.fields[0].second && pm.fields[1].second == p.fields[1].second &&
              pm.fields[3].second == p.fields[3].second);

        // Đúng thứ QRScanTab::showResult() đưa lên ô "NỘI DUNG GỐC" khi chưa tick "Hiện mật khẩu": che khi
        // parse() báo có trường nhạy cảm, ngược lại hiện nguyên văn - nguyên văn đó không được chứa mật khẩu.
        const QString shownRaw = p.secretField >= 0 ? masked : s;
        const bool rawLeak = hasVisibleWifiPassword(shownRaw);
        CHECK(!rawLeak);
        if (rawLeak)
            note("o noi dung goc lo mat khau (parse khong bao truong nhay cam): " + preview(s));
    }

    // (b) WIFI: do chính makeWifi() sinh ra: parse() trả lại đúng từng trường; maskSecrets() che đúng và đủ.
    const char* nasty = ";:\\,\" ;;::\\\\abcXYZ019_-";
    for (int i = 0; i < wifiCount; ++i)
    {
        WifiInfo info;
        info.security = QStringList{"WPA", "WEP", "nopass", "NOPASS", "SAE"}[rnd(0, 4)];
        info.hidden = chance(30);
        info.ssid = chance(60) ? randomFrom(nasty, rnd(1, 32)) : randomUnicode(rnd(1, 20), false);
        // Mật khẩu luôn chứa một dấu riêng không thể trùng phần còn lại của chuỗi -> kiểm "không lộ" bằng
        // tìm chuỗi con là chắc chắn.
        const QString marker = QString("#%1#").arg(g_rng.generate(), 8, 16, QChar('0'));
        const QString left = chance(60) ? randomFrom(nasty, rnd(0, 20)) : randomUnicode(rnd(0, 12), false);
        const QString right = chance(60) ? randomFrom(nasty, rnd(0, 20)) : randomUnicode(rnd(0, 12), false);
        info.password = left + marker + right;
        const bool open = info.security.compare("nopass", Qt::CaseInsensitive) == 0;

        QString s = QRPayload::makeWifi(info);
        // Biến thể chữ thường của tiền tố/khóa P mà nhiều ứng dụng khác sinh ra.
        const int variant = rnd(0, 3);
        if (variant == 1)
            s.replace(0, 5, "wifi:");
        else if (variant == 2 && !open)
            s.replace(";P:", ";p:");
        else if (variant == 3)
            s = "  \n" + s + "\r\n ";

        const ParsedPayload p = QRPayload::parse(s);
        CHECK(p.type == QRContentType::Wifi && p.fields.size() == 4);
        if (p.fields.size() != 4)
            continue;
        const bool fieldsOk = p.fields[0].second == info.ssid && p.fields[2].second == (open ? QString() : info.password) &&
                              p.fields[1].second == (open ? QString("nopass") : info.security) &&
                              p.fields[3].second == (info.hidden ? "Có" : "Không");
        CHECK(fieldsOk);
        if (!fieldsOk)
            note("makeWifi->parse lech: " + preview(s));
        CHECK(p.secretField == (open ? -1 : 2));

        const QString masked = QRPayload::maskSecrets(s);
        if (open)
        {
            CHECK(masked == s);
            continue;
        }
        const bool hidden = !masked.contains(marker) && !hasVisibleWifiPassword(masked) && masked != s;
        CHECK(hidden);
        if (!hidden)
            note("maskSecrets lo mat khau: " + preview(s));
        // Chỉ giá trị P đổi: dựng lại chuỗi mong đợi từ chính dữ liệu đầu vào.
        QString expect = QString(variant == 1 ? "wifi:" : "WIFI:") + "T:" + info.security + ";S:" + QRPayload::escapeWifi(info.ssid) +
                         (variant == 2 ? ";p:" : ";P:") + QRPayload::secretMask() + ";" + (info.hidden ? "H:true;" : "") + ";";
        CHECK(masked == expect);
        if (masked != expect)
            note("ban che khac mong doi: " + preview(masked) + " vs " + preview(expect));
        // Che hai lần = che một lần.
        CHECK(QRPayload::maskSecrets(masked) == masked);
    }

    // (c) Hồi quy cụ thể cho các biến thể dễ sót.
    {
        const QString m = QRPayload::secretMask();
        CHECK(QRPayload::maskSecrets("wifi:t:wpa;s:Nha;p:bimat123;;") == "wifi:t:wpa;s:Nha;p:" + m + ";;");
        CHECK(QRPayload::maskSecrets("WIFI:P:a\\;b\\;c;S:x;;") == "WIFI:P:" + m + ";S:x;;");
        CHECK(QRPayload::maskSecrets("WIFI:S:a\\;P:khong-phai-mat-khau;T:nopass;;") == "WIFI:S:a\\;P:khong-phai-mat-khau;T:nopass;;");
        CHECK(QRPayload::maskSecrets("WIFI:S:x;P:cuoi-chuoi-khong-co-cham-phay") == "WIFI:S:x;P:" + m);
        CHECK(QRPayload::maskSecrets("WIFI:S:x;P:dau\\") == "WIFI:S:x;P:" + m);
        CHECK(QRPayload::maskSecrets("WIFI:P:mot;P:hai;S:x;;") == "WIFI:P:" + m + ";P:" + m + ";S:x;;");
        // Hai trường P mà trường sau rỗng: parse() phải vẫn coi là CÓ mật khẩu (giao diện dựa vào secretField
        // để quyết định che ô "nội dung gốc"), không được để trường rỗng đứng sau xóa mất trường có giá trị.
        const ParsedPayload twoP = QRPayload::parse("WIFI:S:x;P:bimat-that;P:;;");
        CHECK(twoP.secretField == 2 && twoP.fields.value(2).second == "bimat-that");
        const ParsedPayload twoP2 = QRPayload::parse("WIFI:S:x;P:;P:bimat-that;;");
        CHECK(twoP2.secretField == 2 && twoP2.fields.value(2).second == "bimat-that");
        CHECK(QRPayload::parse("WIFI:S:x;P:;;").secretField == -1);
    }

    // (d) Chuỗi khổng lồ / bệnh lý: phải xong trong thời gian hợp lý.
    {
        QElapsedTimer t;
        t.start();
        const QStringList monsters = {
            "WIFI:" + QString(2000000, QChar(';')),
            "WIFI:P:" + QString(2000000, QChar('\\')),
            "WIFI:S:" + QString(1000000, QChar('a')) + ";P:" + QString(1000000, QChar('b')) + ";;",
            "BEGIN:VCARD\n" + QString("N:a;b\nFN:c\nADR:;;;;;;;\n").repeated(40000),
            "MECARD:" + QString("N:a,b;").repeated(100000),
            "MATMSG:" + QString("TO:a@b.c;SUB:\\;;").repeated(100000),
            "mailto:" + QString(500000, QChar('%')),
            "geo:" + QString(500000, QChar('9')) + "," + QString(500000, QChar(',')),
            "http://" + QString(2000000, QChar('a')),
            QString(2000000, QChar('+')),
            QString(1000000, QChar('9')),
            QString(300000, QChar('a')) + "@" + QString(300000, QChar('.')),
            QString(1000000, QChar(u'\0')),
        };
        for (const QString& s : monsters)
        {
            QElapsedTimer one;
            one.start();
            const ParsedPayload p = QRPayload::parse(s);
            const QString masked = QRPayload::maskSecrets(s);
            CHECK(!p.typeName.isEmpty());
            CHECK(!hasVisibleWifiPassword(masked));
            CHECK(one.elapsed() < 20000);
            if (one.elapsed() > 2000)
                std::printf("    chuoi khong lo \"%s...\" (%lld ky tu): %lld ms\n", qPrintable(s.left(12).simplified()),
                            static_cast<long long>(s.size()), static_cast<long long>(one.elapsed()));
        }
        CHECK(t.elapsed() < 60000);
    }

    // (e) Các hàm make* với đầu vào ngẫu nhiên: không crash, vCard tự sinh chỉ có MỘT dòng "Họ tên".
    for (int i = 0; i < 4000; ++i)
    {
        VCardInfo v{randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)),
                    randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10))};
        const ParsedPayload p = QRPayload::parse(QRPayload::makeVCard(v));
        CHECK(p.type == QRContentType::VCard);
        int names = 0;
        for (const auto& f : p.fields)
            names += f.first == "Họ tên" ? 1 : 0;
        CHECK(names == 1);

        const ParsedPayload mail = QRPayload::parse(QRPayload::makeEmail({"a@b.co", randomUnicode(rnd(0, 20)), randomUnicode(rnd(0, 20))}));
        CHECK(mail.type == QRContentType::Email && mail.fields.value(0).second == "a@b.co");
        CHECK(QRPayload::parse(QRPayload::makeSms({randomFrom("+0123456789 ()-.", rnd(1, 15)), randomUnicode(rnd(0, 20))})).type == QRContentType::Sms);
        CHECK(QRPayload::parse(QRPayload::makePhone(randomFrom("+0123456789 ()-.", rnd(1, 15)))).type == QRContentType::Phone);
        const GeoInfo g{rnd(-9000, 9000) / 100.0, rnd(-18000, 18000) / 100.0};
        const ParsedPayload geo = QRPayload::parse(QRPayload::makeGeo(g));
        CHECK(geo.type == QRContentType::Geo && !geo.actionUrl.isEmpty());
        QRPayload::makeUrl(randomUnicode(rnd(0, 30)));
    }

    std::printf("[payload] sup token %d (WIFI: %d, co mat khau %d) + makeWifi %d - %lld ms\n", soupCount, wifiSeen, secretsSeen,
                wifiCount, static_cast<long long>(timer.elapsed()));
}

// ================================================================== 4. QRHistoryStore

struct HistModel
{
    QString source, type, content;
};

static bool sameAsModel(const QList<QRHistoryEntry>& entries, const QList<HistModel>& model)
{
    if (entries.size() != model.size())
        return false;
    for (int i = 0; i < model.size(); ++i)
        if (entries[i].source != model[i].source || entries[i].typeName != model[i].type || entries[i].content != model[i].content)
            return false;
    return true;
}

static QStringList backupFiles(const QString& path)
{
    const QFileInfo info(path);
    return QDir(info.absolutePath()).entryList({info.fileName() + "*.bak"}, QDir::Files);
}

static void stressHistory(const QTemporaryDir& tmp, int ops, int corruptRounds)
{
    QElapsedTimer timer;
    timer.start();
    QRHistoryStore& store = QRHistoryStore::instance();
    const QString original = store.filePath();
    QDir().mkpath(tmp.filePath("hist"));
    const QString path = tmp.filePath("hist/h.json");
    store.setFilePath(path);
    CHECK(store.load() && store.entries().isEmpty());

    int saveFailed = 0, changed = 0;
    const auto c1 = QObject::connect(&store, &QRHistoryStore::saveFailed, [&saveFailed](const QString&) { ++saveFailed; });
    const auto c2 = QObject::connect(&store, &QRHistoryStore::changed, [&changed] { ++changed; });

    // (a) Hàng chục nghìn thao tác ngẫu nhiên, đối chiếu với mô hình tham chiếu; nạp lại từ đĩa định kỳ.
    QList<HistModel> model;
    int adds = 0, removes = 0, clears = 0, reloads = 0, maxSeen = 0;
    const QStringList sources = {"scan", "generate"};
    const QStringList types = {"Văn bản", "WiFi", "Liên kết (URL)", ""};
    QStringList pool; // nội dung lặp lại để nhánh "không ghi trùng liền kề" được chạy thường xuyên
    for (int i = 0; i < 40; ++i)
        pool << randomUnicode(rnd(1, 40));

    for (int i = 0; i < ops; ++i)
    {
        const int op = rnd(0, 999);
        if (op < 800)
        {
            const QString source = sources[rnd(0, 1)];
            const QString type = types[rnd(0, types.size() - 1)];
            QString content;
            const int kind = rnd(0, 19);
            if (kind == 0)       content = QString();                       // bị bỏ qua
            else if (kind == 1 && chance(20)) content = randomUnicode(rnd(500, 3000)); // dài như mã v40
            else if (kind < 10)  content = pool[rnd(0, pool.size() - 1)];
            else                 content = randomUnicode(rnd(1, 60));
            const int before = changed;
            store.add(source, type, content);
            ++adds;
            if (content.isEmpty())
            {
                CHECK(changed == before);
            }
            else
            {
                CHECK(changed == before + 1);
                if (model.isEmpty() || model.first().source != source || model.first().content != content)
                {
                    model.prepend({source, type, content});
                    while (model.size() > 300)
                        model.removeLast();
                }
            }
        }
        else if (op < 950)
        {
            const int index = rnd(-3, static_cast<int>(model.size()) + 3); // có cả chỉ số ngoài phạm vi
            store.removeAt(index);
            ++removes;
            if (index >= 0 && index < model.size())
                model.removeAt(index);
        }
        else if (op < 955)
        {
            store.clear();
            ++clears;
            model.clear();
        }
        else
        {
            CHECK(store.load());
            ++reloads;
            const bool same = sameAsModel(store.entries(), model);
            CHECK(same);
            if (!same)
            {
                note(QString("sau khi nap lai: dia %1 muc, mo hinh %2 muc (thao tac thu %3)").arg(store.entries().size()).arg(model.size()).arg(i));
                model.clear(); // đồng bộ lại để các vòng sau còn ý nghĩa
                for (const QRHistoryEntry& e : store.entries())
                    model.push_back({e.source, e.typeName, e.content});
            }
        }
        maxSeen = std::max(maxSeen, static_cast<int>(store.entries().size()));
        CHECK(store.entries().size() <= 300);
        CHECK(store.entries().size() == model.size());
        CHECK(store.lastSaveOk());
        if (i % 97 == 0)
            CHECK(sameAsModel(store.entries(), model));
    }
    CHECK(saveFailed == 0);
    CHECK(maxSeen == 300 || ops < 2000);
    CHECK(backupFiles(path).isEmpty()); // chưa có tệp hỏng nào -> không được sinh .bak
    CHECK(store.load() && sameAsModel(store.entries(), model));

    // Tệp trên đĩa luôn là JSON hợp lệ, đủ mục, không còn tệp tạm của QSaveFile nằm lại.
    {
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        CHECK(doc.isObject() && doc.object().value("entries").toArray().size() == store.entries().size());
        CHECK(QDir(tmp.filePath("hist")).entryList(QDir::Files).size() == 1);
    }

    // (b) Tệp bị cắt cụt / hỏng giữa chừng, lặp lại dồn dập: bản hỏng LUÔN được giữ lại nguyên vẹn trong một
    // tệp .bak riêng (kể cả nhiều lần hỏng trong cùng một giây), không lần nào bị lần ghi kế tiếp đè mất.
    int corruptSeen = 0, stillValid = 0, emptied = 0;
    QSet<QString> knownBackups;
    for (int round = 0; round < corruptRounds; ++round)
    {
        store.clear();
        for (int k = 0, n = rnd(1, 12); k < n; ++k)
            store.add("scan", "Văn bản", randomUnicode(rnd(1, 80)));
        QFile f(path);
        CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray good = f.readAll();
        f.close();

        QByteArray damaged = good;
        switch (rnd(0, 3))
        {
            case 0: damaged.truncate(rnd(0, static_cast<int>(good.size()) - 1)); break;                      // cắt cụt
            case 1: damaged[rnd(0, static_cast<int>(good.size()) - 1)] = static_cast<char>(rnd(0, 255)); break; // lật 1 byte
            case 2: damaged.insert(rnd(0, static_cast<int>(good.size())), QByteArray(rnd(1, 64), '\0')); break; // chèn rác
            default: damaged = damaged.mid(rnd(1, static_cast<int>(good.size()) - 1)); break;                // mất phần đầu
        }
        {
            QFile w(path);
            CHECK(w.open(QIODevice::WriteOnly | QIODevice::Truncate));
            w.write(damaged);
        }

        const bool blank = damaged.trimmed().isEmpty();
        const bool object = QJsonDocument::fromJson(damaged).isObject();
        const bool loaded = store.load();
        CHECK(store.entries().size() <= 300);
        if (blank)
        {
            ++emptied;
            CHECK(loaded && store.entries().isEmpty());
        }
        else if (object)
        {
            ++stillValid; // hỏng nhưng vẫn là JSON hợp lệ (vd lật 1 byte trong nội dung): nạp bình thường
            CHECK(loaded);
        }
        else
        {
            ++corruptSeen;
            CHECK(!loaded && store.entries().isEmpty());
            CHECK(!QFile::exists(path)); // đã được dời đi, lần ghi kế tiếp không đè lên nó
            QStringList fresh;
            for (const QString& name : backupFiles(path))
                if (!knownBackups.contains(name))
                    fresh << name;
            CHECK(fresh.size() == 1);
            if (fresh.size() != 1)
                note(QString("vong %1: so tep .bak moi = %2 (tong %3)").arg(round).arg(fresh.size()).arg(backupFiles(path).size()));
            for (const QString& name : fresh)
            {
                knownBackups.insert(name);
                QFile b(tmp.filePath("hist/" + name));
                CHECK(b.open(QIODevice::ReadOnly) && b.readAll() == damaged);
            }
        }

        // Sau sự cố vẫn dùng tiếp được bình thường.
        const int before = store.entries().size();
        store.add("generate", "Văn bản", "sau su co " + QString::number(round));
        CHECK(store.lastSaveOk() && store.entries().size() == std::min(before + 1, 300));
        CHECK(store.load() && store.entries().size() == std::min(before + 1, 300));
    }
    CHECK(backupFiles(path).size() == corruptSeen);

    // (c) Tệp JSON hợp lệ nhưng nội dung lạ / quá cỡ: không crash, không bao giờ quá 300 mục.
    {
        QJsonArray big;
        for (int i = 0; i < 5000; ++i)
            big.append(QJsonObject{{"time", "2026-01-01T00:00:00"}, {"source", "scan"}, {"type", "Văn bản"}, {"content", QString("muc %1").arg(i)}});
        const QList<QByteArray> docs = {
            QJsonDocument(QJsonObject{{"version", "1.0"}, {"entries", big}}).toJson(),
            "{}",
            "{\"entries\": 5}",
            "{\"entries\": {\"a\": 1}}",
            "{\"entries\": [1, \"x\", null, true, [], {}, {\"content\": 7}, {\"content\": \"\"}, {\"content\": \"con lai\", \"time\": 12, \"source\": []}]}",
            "{\"entries\": [{\"content\": \"\\ud800 surrogate le\"}, {\"content\": \"\\u0000nul\"}]}",
            "[]", "null", "12345", "\"chuoi\"", "\xEF\xBB\xBF{\"entries\": []}", "   \r\n\t  ",
            QByteArray(2000, '['), QByteArray(1 << 20, '{'), QByteArray(4 << 20, '\xFF'),
            "{\"entries\": [" + QByteArray("{\"content\":\"x\"},").repeated(200000) + "{\"content\":\"cuoi\"}]}",
        };
        for (const QByteArray& raw : docs)
        {
            QFile w(path);
            CHECK(w.open(QIODevice::WriteOnly | QIODevice::Truncate));
            w.write(raw);
            w.close();
            store.load();
            CHECK(store.entries().size() <= 300);
            for (const QRHistoryEntry& e : store.entries())
                CHECK(!e.content.isEmpty());
            store.add("scan", "Văn bản", "them sau tep la");
            CHECK(store.lastSaveOk() && store.entries().size() <= 300 && store.entries().first().content == "them sau tep la");
        }
        // Tệp 5000 mục: giữ đúng 300 mục MỚI NHẤT (đầu danh sách).
        QFile w(path);
        CHECK(w.open(QIODevice::WriteOnly | QIODevice::Truncate));
        w.write(docs.first());
        w.close();
        CHECK(store.load());
        CHECK(store.entries().size() == 300);
        CHECK(!store.entries().isEmpty() && store.entries().first().content == "muc 0");
        CHECK(!store.entries().isEmpty() && store.entries().last().content == "muc 299");
    }

    // (d) Nội dung có surrogate lẻ (không mã hóa UTF-8 được): không làm tệp thành hỏng.
    {
        store.clear();
        QString lone = "truoc";
        lone += QChar(0xD83D);
        lone += "sau";
        store.add("scan", "Văn bản", lone);
        store.add("scan", "Văn bản", "binh thuong");
        CHECK(store.lastSaveOk());
        CHECK(store.load() && store.entries().size() == 2);
        CHECK(store.entries().size() == 2 && store.entries().at(1).content.startsWith("truoc") && store.entries().at(1).content.endsWith("sau"));
    }

    std::printf("[history] %d thao tac (add %d, remove %d, clear %d, nap lai %d, dinh %d muc), hong %d lan (bak %d, van hop le %d, rong %d) - %lld ms\n",
                ops, adds, removes, clears, reloads, maxSeen, corruptRounds, corruptSeen, stillValid, emptied,
                static_cast<long long>(timer.elapsed()));

    QObject::disconnect(c1);
    QObject::disconnect(c2);
    store.setFilePath(original);
    store.load();
}

// ================================================================== 5. WlanProfileXml

/// Cùng quy tắc WlanController dùng để đổi byte SSID thành chuỗi hiển thị (UTF-8, không hợp lệ thì Latin-1).
static QString ssidDisplay(const QByteArray& bytes)
{
    QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
        text = QString::fromLatin1(bytes);
    return text;
}

static QString randomPassword()
{
    static const QStringList bits = {"<", ">", "&", "\"", "'", "]]>", "<![CDATA[", "&amp;", "&#x41;", "&lt;", "<!--", "-->", "<?xml", "?>",
                                     " ", "  ", "%", "\\", "/", "=", "</keyMaterial>", "<name>x</name>"};
    QString s;
    for (int i = 0, n = rnd(1, 10); i < n; ++i)
        s += chance(50) ? bits[rnd(0, bits.size() - 1)] : (chance(70) ? randomFrom("abcdefghijklmnopqrstuvwxyz0123456789ABCDEF!@#$^*()_+", rnd(1, 12))
                                                                      : randomUnicode(rnd(1, 6), false));
    return s;
}

static void stressProfileXml(int count)
{
    QElapsedTimer timer;
    timer.start();
    const QList<WifiSecurity> securities = {WifiSecurity::Open, WifiSecurity::Wep, WifiSecurity::WpaPsk, WifiSecurity::Wpa2Psk,
                                           WifiSecurity::Wpa3Sae, WifiSecurity::Enterprise, WifiSecurity::Unknown};
    int exact = 0, unsafeNames = 0, unsafeRejected = 0, unsafeSame = 0, unsafeDifferent = 0, nonUtf8 = 0;

    for (int i = 0; i < count; ++i)
    {
        QByteArray ssid(rnd(1, 32), '\0');
        const int flavor = rnd(0, 3);
        for (char& c : ssid)
        {
            if (flavor == 0)      c = static_cast<char>(rnd(0x20, 0x7E));   // ASCII in được
            else if (flavor == 1) c = static_cast<char>(rnd(0x20, 0xFF));   // có byte cao (thường không phải UTF-8)
            else                  c = static_cast<char>(rnd(0x00, 0xFF));   // mọi byte, kể cả NUL/ký tự điều khiển
        }
        if (flavor == 3)
            ssid = randomUnicode(rnd(1, 8), false).toUtf8().left(32); // UTF-8 thật, có thể bị cắt giữa ký tự
        const QString name = ssidDisplay(ssid);
        nonUtf8 += QString::fromUtf8(ssid).toUtf8() != ssid ? 1 : 0;

        const WifiSecurity security = securities[rnd(0, securities.size() - 1)];
        const QString password = randomPassword();
        const bool autoConnect = chance(50);
        const bool hidden = chance(30);

        QString xml;
        WifiProfile p;
        bool parsed = false;
        try
        {
            xml = WlanProfileXml::build(ssid, name, security, password, autoConnect, hidden);
            parsed = WlanProfileXml::parse(xml, &p);
        }
        catch (...)
        {
            CHECK(!"WlanProfileXml de lot ngoai le");
            continue;
        }

        // Chuỗi hex của SSID luôn đúng byte gốc, bất kể tên hiển thị ra sao.
        CHECK(xml.contains("<hex>" + WlanProfileXml::toHex(ssid) + "</hex>"));
        CHECK(WlanProfileXml::fromHex(WlanProfileXml::toHex(ssid)) == ssid);
        CHECK(xml.contains("<nonBroadcast>true</nonBroadcast>") == hidden);

        if (!WlanProfileXml::isSafeProfileName(name))
        {
            // Tên có ký tự mà XML không chở nguyên vẹn được (ký tự điều khiển, U+0000...). WlanController từ
            // chối các SSID này TRƯỚC khi dựng hồ sơ; ở đây chỉ thống kê điều gì sẽ xảy ra nếu không từ chối.
            ++unsafeNames;
            if (!parsed)              ++unsafeRejected;
            else if (p.name == name)  ++unsafeSame;
            else                      ++unsafeDifferent;
            continue;
        }

        ++exact;
        // Hồ sơ vừa dựng "thuộc" đúng SSID của nó và không thuộc SSID nào khác (so theo byte, không theo tên).
        CHECK(WlanProfileXml::describesSsid(xml, ssid));
        QByteArray other = ssid;
        other[rnd(0, static_cast<int>(other.size()) - 1)] ^= static_cast<char>(1 << rnd(0, 7));
        CHECK(!WlanProfileXml::describesSsid(xml, other));
        CHECK(!WlanProfileXml::describesSsid(xml, ssid + 'x') && !WlanProfileXml::describesSsid(xml, QByteArray()));
        bool ok = parsed && p.name == name && p.ssid == name && p.autoConnect == autoConnect && p.rawXml == xml && !p.keyProtected;
        const WifiSecurity expectSecurity = security == WifiSecurity::Unknown ? WifiSecurity::Open : security; // Unknown dựng như mạng mở
        ok = ok && p.security == expectSecurity;
        if (WifiSecurityUtil::requiresPassword(security))
            ok = ok && p.hasPassword && p.password == password;
        else
            ok = ok && !p.hasPassword && p.password.isEmpty();
        CHECK(ok);
        if (!ok)
            note(QString("xml round-trip: ssid=%1 sec=%2 parsed=%3 name=%4 pw=%5").arg(QString::fromLatin1(ssid.toHex())).arg(int(security))
                     .arg(parsed).arg(preview(p.name), preview(password)));

        // Cắt cụt XML hợp lệ ở vị trí bất kỳ: không crash; cắt thật sự (mất thẻ đóng gốc) thì không bao giờ "hợp lệ".
        if (i % 20 == 0)
        {
            const int cut = rnd(0, static_cast<int>(xml.size()) - 1);
            WifiProfile t;
            const bool ok2 = WlanProfileXml::parse(xml.left(cut), &t);
            CHECK(!ok2 || xml.left(cut).contains("</WLANProfile>"));
        }
    }

    // Tên an toàn thì không bao giờ bị XML làm đổi; tên không an toàn mà vẫn "parse được ra tên KHÁC" chính là
    // lý do phải từ chối chúng (hồ sơ sẽ được Windows tạo dưới một tên khác tên ta dùng để kết nối/hoàn tác).
    CHECK(WlanProfileXml::isSafeProfileName("Nhà <tôi> & \"bạn\""));
    CHECK(WlanProfileXml::isSafeProfileName(QString::fromLatin1("caf\xE9 \x85 \xFF")));
    CHECK(!WlanProfileXml::isSafeProfileName(QString()));
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a\rb")));
    CHECK(WlanProfileXml::isSafeProfileName(QString("a\nb\tc"))); // TAB/LF đi qua XML nguyên vẹn
    CHECK(WlanProfileXml::isSafeProfileName(QString::fromUtf8("emoji \xF0\x9F\x98\x80 ok")));
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a") + QChar(u'\0') + "b"));
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a") + QChar(0x01)));
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a") + QChar(0x1B) + "[31m"));
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a") + QChar(0xD800)));
    CHECK(!WlanProfileXml::isSafeProfileName(QString(QChar(0xDC00)) + "a"));
    {
        // Hồi quy: tên có ký tự điều khiển KHÔNG làm XML thành không hợp lệ - nó được dựng thành một hồ sơ mang
        // tên khác (ký tự bị bỏ / CR thành LF). Đây chính là điều isSafeProfileName() phải chặn từ trước.
        for (const QString& bad : {QString("Nha") + QChar(0x01), QString("Nha") + QChar(u'\0') + "x", QString("Nha\rB")})
        {
            WifiProfile p;
            CHECK(!WlanProfileXml::isSafeProfileName(bad));
            const bool parsedOther = WlanProfileXml::parse(WlanProfileXml::build(bad.toUtf8(), bad, WifiSecurity::Wpa2Psk, "matkhau123", true), &p);
            CHECK(!parsedOther || p.name != bad);
        }
        // describesSsid: tên hồ sơ không nói lên gì - chỉ <hex> mới quyết định.
        const QString foo2 = WlanProfileXml::build("Foo", "Foo 2", WifiSecurity::Wpa2Psk, "matkhau123", true);
        CHECK(WlanProfileXml::describesSsid(foo2, "Foo") && !WlanProfileXml::describesSsid(foo2, "Foo 2"));
        CHECK(WlanProfileXml::describesSsid(QString(foo2).replace("<hex>466F6F</hex>", "<hex> 466f6f </hex>"), "Foo")); // chữ thường/khoảng trắng
        CHECK(WlanProfileXml::describesSsid(QString(foo2).replace("<SSID>", "<SSID><hex>4142</hex></SSID><SSID>"), "AB")); // nhiều SSID
        CHECK(WlanProfileXml::describesSsid(QString(foo2).replace("<SSID>", "<SSID><hex>4142</hex></SSID><SSID>"), "Foo"));
        CHECK(!WlanProfileXml::describesSsid(QString(foo2).replace("<SSID>", "<SSID><hex>4142</hex></SSID><SSID>"), "Fo"));
        // Không đủ căn cứ (XML hỏng / không có <hex>) -> không kết luận "mạng khác".
        CHECK(WlanProfileXml::describesSsid(QString(), "Foo") && WlanProfileXml::describesSsid("<a", "Foo"));
        CHECK(WlanProfileXml::describesSsid(QString(foo2).remove("<hex>466F6F</hex>"), "Bar"));
        CHECK(WlanProfileXml::describesSsid(foo2.left(foo2.size() / 3), "Bar"));
        CHECK(!WlanProfileXml::describesSsid("<p><hex>466F6F</hex><SSID><hex>4142</hex></SSID></p>", "Foo")); // <hex> ngoài <SSID> không tính
    }
    CHECK(!WlanProfileXml::isSafeProfileName(QString("a") + QChar(0xFFFE)));

    // XML rác / khổng lồ / "bom" thực thể: không crash, xong trong thời gian hợp lý, không nhận bừa.
    {
        const QString valid = WlanProfileXml::build("Net", "Net", WifiSecurity::Wpa2Psk, "matkhau123", true);
        QString bomb = "<?xml version=\"1.0\"?><!DOCTYPE lolz [<!ENTITY a \"aaaaaaaaaa\">";
        for (int i = 0; i < 12; ++i)
            bomb += QString("<!ENTITY %1 \"&%2;&%2;&%2;&%2;&%2;&%2;&%2;&%2;&%2;&%2;\">").arg(QChar('b' + i)).arg(QChar('a' + i));
        bomb += "]><WLANProfile><name>&m;</name></WLANProfile>";
        QStringList junk = {
            QString(), " ", "<", "<a", "</a>", "<a></b>", "<?xml", "<WLANProfile/>", "<WLANProfile><name/></WLANProfile>",
            "<WLANProfile><name><x/></name></WLANProfile>", "<name>chi co ten</name>", "<a><name>t</name><name>u</name></a>",
            bomb,
            "<r>" + QString("<a>").repeated(200000) + "</r>",
            "<r>" + QString("<name>x</name>").repeated(200000) + "</r>",
            "<r><name>" + QString(8000000, QChar('x')) + "</name></r>",
            "<r " + QString("a%1=\"1\" ").repeated(1) + QString(2000000, QChar('b')) + "=\"\"/>",
            QString(3000000, QChar('<')),
            QString(3000000, QChar(u'\0')),
            valid + valid,
            valid.left(valid.size() / 2) + valid,
            QString(valid).replace("<keyMaterial>", "<keyMaterial><b>"),
            QString(valid).replace("false</protected>", "FALSE</protected>"),
            QString(valid).replace("false</protected>", "true</protected>"),
        };
        for (int i = 0; i < 3000; ++i)
        {
            if (chance(50))
                junk << randomUnicode(rnd(0, 200));
            else
            {
                // Đột biến một hồ sơ hợp lệ: xóa/chèn/thay vài ký tự.
                QString mutated = valid;
                for (int k = 0, n = rnd(1, 6); k < n; ++k)
                {
                    const int pos = rnd(0, static_cast<int>(mutated.size()) - 1);
                    const int what = rnd(0, 2);
                    if (what == 0)      mutated.remove(pos, rnd(1, 20));
                    else if (what == 1) mutated.insert(pos, randomFrom("<>&/\"'=! abc", rnd(1, 8)));
                    else                mutated[pos] = QChar(static_cast<char16_t>(rnd(0, 0x7F)));
                    if (mutated.isEmpty())
                        break;
                }
                junk << mutated;
            }
        }
        qint64 worst = 0;
        for (const QString& xml : junk)
        {
            QElapsedTimer one;
            one.start();
            WifiProfile p;
            const bool ok = WlanProfileXml::parse(xml, &p);
            WlanProfileXml::describesSsid(xml, "Net");
            WlanProfileXml::isSafeProfileName(xml);
            worst = std::max(worst, one.elapsed());
            if (ok)
            {
                CHECK(!p.name.isEmpty() && p.rawXml == xml);
                CHECK(!(p.hasPassword && p.keyProtected));
                CHECK(p.hasPassword == !p.password.isEmpty());
            }
        }
        CHECK(!WlanProfileXml::parse(bomb, nullptr));
        WifiProfile p;
        CHECK(!WlanProfileXml::parse(bomb, &p)); // thực thể lồng nhau bị chặn, không nở ra hàng GB
        CHECK(WlanProfileXml::parse(QString(valid).replace("false</protected>", "FALSE</protected>"), &p) && p.hasPassword);
        CHECK(WlanProfileXml::parse(QString(valid).replace("false</protected>", "true</protected>"), &p) && !p.hasPassword && p.keyProtected);
        CHECK(worst < 30000);
        std::printf("    XML rac: %lld mau, cham nhat %lld ms\n", static_cast<long long>(junk.size()), static_cast<long long>(worst));
    }

    std::printf("[xml] %d ho so: round-trip chinh xac %d (SSID khong phai UTF-8: %d); ten khong an toan %d -> XML bi tu choi %d, "
                "giu nguyen %d, RA TEN KHAC %d - %lld ms\n",
                count, exact, nonUtf8, unsafeNames, unsafeRejected, unsafeSame, unsafeDifferent, static_cast<long long>(timer.elapsed()));
}

// ================================================================== 6. SpeedTestRunner (qua proxy giả cục bộ)

/// "Proxy HTTP" giả ở 127.0.0.1: nhận lệnh CONNECT của QNetworkAccessManager rồi cư xử theo chế độ đang đặt.
/// Không chuyển tiếp gì ra ngoài - mọi yêu cầu của runner chết ở đây.
class FakeProxy : public QTcpServer
{
public:
    enum Mode { Hang, Close, Refuse502, Garbage };
    Mode mode{Hang};
    int connections{0};

    FakeProxy()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection())
            {
                ++connections;
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
                if (mode == Close)
                {
                    s->abort();
                    continue;
                }
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    s->readAll();
                    if (mode == Refuse502)
                    {
                        s->write("HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                        s->disconnectFromHost();
                    }
                    else if (mode == Garbage)
                    {
                        s->write(QByteArray(512, '\x01'));
                        s->disconnectFromHost();
                    }
                    // Hang: nhận rồi im lặng
                });
            }
        });
    }
};

struct RunnerProbe
{
    int finished{0}, errors{0}, phases{0}, pings{0}, transfers{0};
    void attach(SpeedTestRunner* r)
    {
        QObject::connect(r, &SpeedTestRunner::finished, [this] { ++finished; });
        QObject::connect(r, &SpeedTestRunner::errorOccurred, [this](const QString& m) { ++errors; CHECK(!m.isEmpty()); });
        QObject::connect(r, &SpeedTestRunner::phaseChanged, [this](const QString&) { ++phases; });
        QObject::connect(r, &SpeedTestRunner::pingUpdated, [this](double, double, double, double, double) { ++pings; });
        QObject::connect(r, &SpeedTestRunner::downloadFinished, [this](double) { ++transfers; });
        QObject::connect(r, &SpeedTestRunner::uploadFinished, [this](double) { ++transfers; });
    }
};

/// Ép QNetworkAccessManager RIÊNG của runner đi qua proxy giả. false = không tìm thấy -> KHÔNG được start().
static bool routeToFakeProxy(QObject* runner, const FakeProxy& proxy)
{
    auto* nam = runner->findChild<QNetworkAccessManager*>();
    if (!nam)
        return false;
    nam->setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", proxy.serverPort()));
    return true;
}

static bool waitFor(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

static void stressSpeedTest(int cycles)
{
    QElapsedTimer timer;
    timer.start();

    // Thống kê thuần: mẫu rỗng / một mẫu / giá trị cực đoan không sinh NaN hay chia cho 0.
    for (int i = 0; i < 20000; ++i)
    {
        QList<double> samples;
        for (int k = 0, n = rnd(0, 12); k < n; ++k)
            samples << (chance(5) ? 0.0 : (chance(5) ? 1e12 : rnd(0, 400000) / 100.0));
        const SpeedTestRunner::PingStats s = SpeedTestRunner::computePingStats(samples);
        const bool sane = s.minMs == s.minMs && s.avgMs == s.avgMs && s.maxMs == s.maxMs && s.jitterMs == s.jitterMs && // không NaN
                          s.minMs <= s.avgMs + 1e-3 && s.avgMs <= s.maxMs + 1e-3 && s.jitterMs >= 0 && s.minMs >= 0;
        CHECK(sane);
        if (samples.size() <= 1)
            CHECK(s.jitterMs == 0);
        if (samples.isEmpty())
            CHECK(s.minMs == 0 && s.avgMs == 0 && s.maxMs == 0);
    }

    FakeProxy proxy;
    CHECK(proxy.listen(QHostAddress::LocalHost, 0));
    if (!proxy.isListening())
    {
        std::printf("[speed] KHONG mo duoc cong cuc bo - bo qua phan may trang thai\n");
        return;
    }

    SpeedTestRunner runner;
    RunnerProbe probe;
    probe.attach(&runner);
    if (!routeToFakeProxy(&runner, proxy))
    {
        CHECK(!"khong tim thay QNetworkAccessManager cua SpeedTestRunner - khong start() de khoi ra Internet");
        return;
    }

    // (a) start/stop dồn dập, máy chủ nhận kết nối rồi im lặng: mỗi lần start() đúng MỘT finished(), không
    // bao giờ có errorOccurred() cho lần người dùng tự dừng, stop() xong là dừng hẳn ngay (đồng bộ).
    proxy.mode = FakeProxy::Hang;
    int expectedFinished = 0;
    for (int i = 0; i < cycles; ++i)
    {
        runner.start();
        CHECK(runner.isRunning() && !runner.wasStoppedByUser());
        switch (i % 6)
        {
            case 0: break;                       // dừng ngay, chưa kịp quay vòng lặp sự kiện
            case 1: pump(rnd(1, 20)); break;     // dừng giữa lúc đang bắt tay với proxy
            case 2: runner.start(); runner.start(); break; // start() chồng: bị bỏ qua
            case 3: pump(rnd(20, 60)); break;
            case 4: QCoreApplication::processEvents(); break;
            default: pump(1); break;
        }
        CHECK(runner.isRunning());
        runner.stop();
        ++expectedFinished;
        CHECK(!runner.isRunning());
        CHECK(runner.wasStoppedByUser());
        CHECK(probe.finished == expectedFinished);
        runner.stop(); // dừng thừa: vô hại
        runner.stop();
        CHECK(probe.finished == expectedFinished);
        if (i % 4 == 0)
            pump(2);
    }
    pump(100);
    CHECK(probe.finished == expectedFinished);
    CHECK(probe.errors == 0);
    CHECK(probe.pings == 0 && probe.transfers == 0);
    CHECK(probe.phases == expectedFinished); // mỗi lần đo chỉ tới được pha ping

    // (b) Khởi động lại NGAY trong slot finished() (dừng rồi đo lại liền tay), lặp nhiều lần.
    {
        int restarts = 50;
        const auto conn = QObject::connect(&runner, &SpeedTestRunner::finished, [&] {
            if (restarts > 0)
            {
                --restarts;
                runner.start();
            }
        });
        runner.start();
        int guard = 0;
        while (runner.isRunning() && ++guard < 1000)
        {
            if (guard % 3 == 0)
                pump(3);
            runner.stop();
        }
        QObject::disconnect(conn);
        CHECK(restarts == 0 && !runner.isRunning());
        expectedFinished += 51;
        CHECK(probe.finished == expectedFinished);
        CHECK(probe.errors == 0);
    }

    // (c) Máy chủ từ chối / đóng ngay / trả rác: báo lỗi đúng MỘT lần rồi kết thúc, không treo; lần đo sau
    // lỗi vẫn chạy được. (Không phải người dùng dừng -> wasStoppedByUser() = false.)
    int errorRuns = 0;
    for (FakeProxy::Mode mode : {FakeProxy::Close, FakeProxy::Refuse502, FakeProxy::Garbage})
    {
        QElapsedTimer modeTimer;
        modeTimer.start();
        for (int i = 0; i < 2; ++i)
        {
            proxy.mode = mode;
            const int errorsBefore = probe.errors, finishedBefore = probe.finished;
            runner.start();
            const bool ended = waitFor([&] { return !runner.isRunning(); }, 15000);
            CHECK(ended);
            if (!ended)
            {
                note(QString("che do proxy %1: khong ket thuc sau 15 s").arg(int(mode)));
                runner.stop();
            }
            ++errorRuns;
            CHECK(probe.finished == finishedBefore + 1);
            CHECK(probe.errors == errorsBefore + (ended ? 1 : 0));
            CHECK(!ended || !runner.wasStoppedByUser());
        }
        std::printf("    proxy che do %d: 2 lan do loi (moi lan cho toi han ping 4 s) het %lld ms\n", int(mode), static_cast<long long>(modeTimer.elapsed()));
    }

    // (d) Máy chủ im lặng và KHÔNG ai bấm dừng: ping tự hết giờ (4 s) -> báo lỗi, không treo vô hạn.
    {
        proxy.mode = FakeProxy::Hang;
        const int errorsBefore = probe.errors, finishedBefore = probe.finished;
        QElapsedTimer t;
        t.start();
        runner.start();
        const bool ended = waitFor([&] { return !runner.isRunning(); }, 12000);
        CHECK(ended);
        CHECK(probe.finished == finishedBefore + 1 && probe.errors == errorsBefore + 1);
        CHECK(t.elapsed() >= 3500 && t.elapsed() < 8000);
        if (!ended)
            runner.stop();
        std::printf("    ping het gio sau %lld ms\n", static_cast<long long>(t.elapsed()));
    }

    // (e) Dừng ngay sau một lần lỗi rồi đo lại; hủy runner khi đang đo: không tín hiệu nào phát ra trong/sau
    // khi hủy, không crash.
    {
        int lateSignals = 0;
        for (int i = 0; i < 60; ++i)
        {
            auto* r = new SpeedTestRunner;
            bool alive = true;
            QObject::connect(r, &SpeedTestRunner::finished, [&] { lateSignals += alive ? 0 : 1; });
            QObject::connect(r, &SpeedTestRunner::errorOccurred, [&](const QString&) { lateSignals += alive ? 0 : 1; });
            if (routeToFakeProxy(r, proxy))
            {
                proxy.mode = i % 2 ? FakeProxy::Hang : FakeProxy::Refuse502;
                r->start();
                if (i % 3)
                    pump(rnd(0, 15));
            }
            alive = false;
            delete r;
            pump(2);
        }
        pump(50);
        CHECK(lateSignals == 0);
    }

    CHECK(proxy.connections > 0); // lưu lượng thật sự đi vào proxy giả, không ra ngoài
    std::printf("[speed] %d vong start/stop + 51 lan tu khoi dong lai + %d lan loi + 60 lan huy khi dang do; proxy gia nhan %d ket noi - %lld ms\n",
                cycles, errorRuns, proxy.connections, static_cast<long long>(timer.elapsed()));
}

// ================================================================== 7. Giao diện (offscreen)

static QPushButton* findButton(QWidget* root, const QString& text)
{
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text)
            return b;
    return nullptr;
}

static void stressQrUi(const QTemporaryDir& tmp, int opens, int fuzzRounds)
{
    QElapsedTimer timer;
    timer.start();
    QRHistoryStore& store = QRHistoryStore::instance();
    store.setFilePath(tmp.filePath("ui_history.json"));
    store.load();
    store.clear();

    // (a) Tạo mới - hiện - đổi tab - đóng - hủy, lặp lại.
    g_lap.start();
    {
        QRWindow warmup; // nạp plugin/phông một lần để số đo bộ nhớ phía dưới không tính phần khởi tạo
        warmup.show();
        pump(20);
    }
    const double memBefore = privateMb();
    for (int i = 0; i < opens; ++i)
    {
        auto* win = new QRWindow;
        win->show();
        QCoreApplication::processEvents();
        for (int k = 0, n = rnd(1, 6); k < n; ++k)
        {
            win->tabs()->setCurrentIndex(rnd(0, 2));
            QCoreApplication::processEvents();
        }
        if (i % 3 == 0)
            win->generateTab()->setRawText(randomUnicode(rnd(1, 40), false));
        if (i % 5 == 0)
            win->scanTab()->scanImage(win->generateTab()->currentImage(), "stress");
        win->scanTab()->stopCamera(); // không có camera đang chạy: vô hại
        win->close();
        QCoreApplication::processEvents();
        CHECK(!win->isVisible());
        if (i % 2)
        {
            win->show(); // đóng chỉ ẩn - mở lại rồi mới hủy
            QCoreApplication::processEvents();
            win->close();
        }
        delete win;
    }
    pump(50);
    const double memAfter = privateMb();
    std::printf("    QRWindow x%d: bo nho rieng %.1f -> %.1f MB\n", opens, memBefore, memAfter);
    CHECK(memAfter - memBefore < 200.0);

    QRWindow win;
    win.show();
    QCoreApplication::processEvents();
    QRGenerateTab* gen = win.generateTab();
    QRScanTab* scan = win.scanTab();

    lap("QRWindow tao moi/huy");

    // (b) Một cửa sổ tái dùng: đóng/mở + đổi tab liên tục.
    for (int i = 0; i < opens; ++i)
    {
        win.close();
        CHECK(!win.isVisible());
        win.show();
        for (int k = 0; k < 10; ++k)
            win.tabs()->setCurrentIndex(rnd(0, 2));
        if (i % 10 == 0)
            QCoreApplication::processEvents();
    }
    CHECK(win.isVisible() && win.tabs()->count() == 3);

    lap("QRWindow tai dung");

    // (c) Nhồi dữ liệu ngẫu nhiên vào MỌI ô nhập của tab Tạo, mọi loại nội dung, mọi tùy chọn.
    auto* typeCombo = gen->findChild<QComboBox*>("typeCombo");
    auto* eccCombo = gen->findChild<QComboBox*>("eccCombo");
    auto* sizeSpin = gen->findChild<QSpinBox*>("sizeSpin");
    auto* swapButton = gen->findChild<QPushButton*>("swapButton");
    CHECK(typeCombo && eccCombo && sizeSpin && swapButton);
    int withContent = 0, selfReadable = 0;
    if (typeCombo && eccCombo && sizeSpin && swapButton)
    {
        win.tabs()->setCurrentWidget(gen);
        for (int i = 0; i < fuzzRounds; ++i)
        {
            typeCombo->setCurrentIndex(rnd(0, typeCombo->count() - 1));
            for (QLineEdit* e : gen->findChildren<QLineEdit*>())
                if (!qobject_cast<QAbstractSpinBox*>(e->parentWidget()) && chance(80))
                    e->setText(chance(15) ? QString() : (chance(50) ? randomUnicode(rnd(1, 24)) : randomFrom("abc@.:;,\\\"+0123456789 /?&=%", rnd(1, 30))));
            for (QPlainTextEdit* e : gen->findChildren<QPlainTextEdit*>())
                if (chance(80))
                    e->setPlainText(chance(10) ? randomUnicode(rnd(2000, 9000)) : randomUnicode(rnd(0, 200)));
            for (QDoubleSpinBox* s : gen->findChildren<QDoubleSpinBox*>())
                s->setValue(rnd(-200000, 200000) / 1000.0);
            for (QComboBox* c : gen->findChildren<QComboBox*>())
                if (c != typeCombo && c->count() > 0)
                    c->setCurrentIndex(rnd(0, c->count() - 1));
            for (QCheckBox* c : gen->findChildren<QCheckBox*>())
                c->setChecked(chance(40));
            for (QSpinBox* s : gen->findChildren<QSpinBox*>())
                s->setValue(rnd(s->minimum(), s == sizeSpin && !chance(4) ? 700 : s->maximum()));
            const bool inverted = chance(25);
            if (inverted)
                swapButton->click();
            gen->refresh();

            const QString content = gen->currentContent();
            const QImage img = gen->currentImage();
            CHECK(content.isEmpty() == img.isNull());
            CHECK(content.isEmpty() == !gen->currentMatrix().isValid());
            if (!content.isEmpty())
            {
                ++withContent;
                CHECK(img.width() == img.height() && img.width() >= gen->currentMatrix().size);
                // Mã vừa tạo đưa sang tab Quét: đọc được thì phải ra đúng nội dung (và có vào lịch sử).
                const int found = scan->scanImage(img, "tu tab Tao");
                if (found == 1)
                {
                    ++selfReadable;
                    const bool same = scan->results().first().text == QString::fromUtf8(content.toUtf8());
                    CHECK(same);
                    if (!same)
                        note("tab Tao -> tab Quet ra noi dung khac: " + preview(content));
                }
                if (i % 25 == 0)
                {
                    QString err;
                    for (const char* ext : {"png", "jpg", "bmp", "svg"})
                    {
                        const bool saved = gen->saveTo(tmp.filePath(QString("ui_out_%1.%2").arg(i % 50).arg(ext)), &err);
                        CHECK(saved);
                    }
                }
            }
            else
            {
                QString err;
                CHECK(!gen->saveTo(tmp.filePath("khong_co_ma.png"), &err) && !err.isEmpty());
            }
            if (inverted)
                swapButton->click();
            if (i % 20 == 0)
            {
                win.tabs()->setCurrentIndex(rnd(0, 2));
                QCoreApplication::processEvents();
                win.grab(); // ép vẽ thật (paintEvent của QRImageView, bảng...)
            }
            CHECK(store.entries().size() <= 300);
        }
    }

    lap("nhoi tab Tao");

    // (d) Tab Quét với ảnh bệnh lý: không crash, khung xem vẽ được, trạng thái báo đúng.
    {
        win.tabs()->setCurrentWidget(scan);
        const QList<QSize> sizes = {{1, 1}, {1, 3000}, {3000, 1}, {7, 7}, {640, 480}, {2600, 40}};
        for (int i = 0; i < 60; ++i)
        {
            QImage img(sizes[i % sizes.size()], i % 2 ? QImage::Format_ARGB32 : QImage::Format_RGB32);
            fillRandom(img);
            const int found = scan->scanImage(img, QString("<b>%1</b> %2 %1").arg("%1", randomUnicode(8)));
            CHECK(found == scan->results().size());
            CHECK(!scan->statusText().isEmpty());
            if (i % 6 == 0)
            {
                QCoreApplication::processEvents();
                win.grab();
            }
        }
        CHECK(scan->scanImage(QImage(), "null") == 0);
        CHECK(scan->statusText().contains("không hợp lệ"));
    }

    lap("tab Quet anh benh ly");

    // (e) "Tạo lại mã" phải đưa NGUYÊN VĂN nội dung đã quét sang tab Tạo (kể cả xuống dòng kiểu CRLF của
    // vCard, khoảng trắng không ngắt, U+2028...) - mã tạo lại phải mang đúng nội dung của mã gốc.
    {
        const QStringList probes = {
            "mot dong",
            "dong1\r\ndong2\r\n",
            "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Nguyen;An;;;\r\nFN:An Nguyen\r\nEND:VCARD",
            QString("khoang") + QChar(0x00A0) + "trang khong ngat",
            QString("phan cach dong") + QChar(0x2028) + "va doan" + QChar(0x2029) + "ket",
            "tab\tva\rCR don",
            QString("co") + QChar(u'\0') + "NUL",
            "  dau cuoi co khoang trang  \n",
            "WIFI:T:WPA;S:Nha\\;A;P:mk\\:1\\,2;;",
        };
        QPushButton* recreate = findButton(scan, "🔁 Tạo lại mã");
        CHECK(recreate != nullptr);
        for (const QString& text : probes)
        {
            QRStyle style;
            style.targetSize = 400;
            win.tabs()->setCurrentWidget(scan);
            const int found = scan->scanImage(QRCodec::render(QRCodec::encode(text, QREcc::Medium), style), "probe");
            CHECK(found == 1 && scan->results().first().text == text);
            if (found != 1 || !recreate)
                continue;
            recreate->click();
            CHECK(win.tabs()->currentWidget() == gen);
            const bool faithful = gen->currentContent() == text;
            CHECK(faithful);
            if (!faithful)
                note("Tao lai ma lam doi noi dung: " + preview(text) + " -> " + preview(gen->currentContent()));
            const QList<QRDecoded> again = QRCodec::decode(gen->currentImage());
            CHECK(again.size() == 1 && again.first().text == text);
        }
        // Sau khi người dùng SỬA ô văn bản thì nội dung là thứ đang hiện trong ô, không còn là bản gốc.
        QPlainTextEdit* textEdit = nullptr; // ô của trang "Văn bản" (tab Tạo còn hai QPlainTextEdit khác)
        for (QPlainTextEdit* e : gen->findChildren<QPlainTextEdit*>())
            if (e->placeholderText().startsWith("Nhập văn bản"))
                textEdit = e;
        CHECK(textEdit != nullptr);
        if (textEdit)
        {
            gen->setRawText("goc\r\nCRLF");
            CHECK(gen->currentContent() == "goc\r\nCRLF");
            textEdit->setPlainText("nguoi dung da sua");
            gen->refresh();
            CHECK(gen->currentContent() == "nguoi dung da sua");
            textEdit->setPlainText("goc\nCRLF"); // gõ lại bằng tay: chỉ còn LF như mọi ô nhập
            gen->refresh();
            CHECK(gen->currentContent() == "goc\nCRLF");
        }
    }

    lap("Tao lai ma");

    // (f) Tab Lịch sử dưới tải: 300 mục, xóa từng mục, bật/tắt hiện mật khẩu, mục WiFi luôn bị che mặc định.
    {
        // Nạp đầy khi tab Lịch sử đang ẨN - đúng như ứng dụng thật (mục mới chỉ sinh ra từ tab Tạo/Quét). Phần
        // thao tác khi tab đang HIỆN (xóa mục) làm vài chục lần và có đo thời gian. LƯU Ý: bản đầu của bộ test
        // này thêm 700 mục liên tiếp trong lúc tab Lịch sử đang hiện mà KHÔNG quay vòng lặp sự kiện giữa các
        // lần - chặng đó chạy hàng chục phút (CPU chạy thật, chưa rõ nguyên nhân); kịch bản ấy không xảy ra
        // được trong ứng dụng nên đã bỏ, nhưng đáng xem lại nếu sau này có đường thêm mục khi tab đang hiện.
        win.tabs()->setCurrentWidget(gen);
        auto* table = win.historyTab()->findChild<QTableWidget*>();
        auto* reveal = win.historyTab()->findChild<QCheckBox*>("historyRevealCheck");
        QPushButton* del = findButton(win.historyTab(), "Xóa mục");
        CHECK(table && reveal && del);
        if (table && reveal && del)
        {
            for (int i = 0; i < 700; ++i)
            {
                WifiInfo info;
                info.ssid = QString("Mang %1").arg(i);
                info.password = QString("MK-%1-bimat").arg(i);
                store.add(i % 2 ? "scan" : "generate", "WiFi", i % 3 ? QRPayload::makeWifi(info) : randomUnicode(rnd(1, 50)));
                CHECK(table->rowCount() == store.entries().size() && table->rowCount() <= 300);
            }
            CHECK(table->rowCount() == 300);
            int leaks = 0;
            for (int r = 0; r < table->rowCount(); ++r)
                leaks += (table->item(r, 3)->text().contains("-bimat") || table->item(r, 3)->toolTip().contains("-bimat")) ? 1 : 0;
            CHECK(leaks == 0);
            reveal->setChecked(true);
            int shown = 0;
            for (int r = 0; r < table->rowCount(); ++r)
                shown += table->item(r, 3)->text().contains("-bimat") ? 1 : 0;
            CHECK(shown > 100);
            reveal->setChecked(false);

            win.tabs()->setCurrentWidget(win.historyTab());
            QCoreApplication::processEvents();
            win.grab();
            QElapsedTimer visible;
            visible.start();
            for (int i = 0; i < 20; ++i) // xóa khi bảng đang hiển thị
            {
                table->setCurrentCell(rnd(0, table->rowCount() - 1), 0);
                del->click();
                QCoreApplication::processEvents();
            }
            CHECK(table->rowCount() == 280);
            std::printf("    tab Lich su DANG HIEN, 300 dong: 20 lan \"Xoa muc\" het %lld ms (%.0f ms/lan)\n",
                        static_cast<long long>(visible.elapsed()), visible.elapsed() / 20.0);
            win.tabs()->setCurrentWidget(gen);
            for (int i = 0; i < 300; ++i) // xóa quá số mục: các lần thừa vô hại
            {
                table->setCurrentCell(table->rowCount() ? rnd(0, table->rowCount() - 1) : -1, 0);
                if (del->isEnabled())
                    del->click();
            }
            CHECK(table->rowCount() == 0 && store.entries().isEmpty());
            CHECK(!del->isEnabled());
        }
    }

    lap("tab Lich su");

    win.close();
    std::printf("[ui-qr] mo/dong %d + tai dung %d, nhoi tab Tao %d vong (co ma %d, tu quet lai duoc %d) - %lld ms\n", opens, opens,
                fuzzRounds, withContent, selfReadable, static_cast<long long>(timer.elapsed()));
}

static void stressWifiUi(int opens)
{
    QElapsedTimer timer;
    timer.start();

    FakeProxy proxy; // cho nút "Bắt đầu đo" của tab Đo tốc độ - xem routeToFakeProxy()
    proxy.mode = FakeProxy::Hang;
    CHECK(proxy.listen(QHostAddress::LocalHost, 0));

    // (a) Tạo mới - hiện - đổi tab - đóng - (mở lại) - hủy. Chỉ đi qua các API WLAN ĐỌC.
    g_lap.start();
    {
        WifiWindow warmup;
        warmup.show();
        pump(20);
    }
    const double memBefore = privateMb();
    for (int i = 0; i < opens; ++i)
    {
        auto* win = new WifiWindow;
        win->show();
        QCoreApplication::processEvents();
        auto* tabs = win->findChild<QTabWidget*>();
        CHECK(tabs && tabs->count() == 3);
        for (int k = 0, n = rnd(1, 5); tabs && k < n; ++k)
        {
            tabs->setCurrentIndex(rnd(0, 2));
            if (k % 2)
                QCoreApplication::processEvents();
        }
        win->close();
        QCoreApplication::processEvents();
        CHECK(!win->isVisible());
        if (i % 2)
        {
            win->show();
            QCoreApplication::processEvents();
            win->close();
        }
        delete win; // hủy khi timer polling/theo dõi có thể vừa được bật lại
        if (i % 10 == 0)
            pump(5);
    }
    pump(50);
    const double memAfter = privateMb();
    std::printf("    WifiWindow x%d: bo nho rieng %.1f -> %.1f MB\n", opens, memBefore, memAfter);
    CHECK(memAfter - memBefore < 200.0);

    lap("WifiWindow tao moi/huy");

    // (b) Một cửa sổ tái dùng: đóng/mở + đổi tab liên tục; combo adapter không nhân đôi; đóng cửa sổ dừng
    // phép đo tốc độ đang chạy (đã ép qua proxy giả cục bộ).
    {
        WifiWindow win;
        win.show();
        QCoreApplication::processEvents();
        auto* tabs = win.findChild<QTabWidget*>();
        auto* combo = win.findChild<QComboBox*>();
        auto* runner = win.findChild<SpeedTestRunner*>();
        CHECK(tabs && combo && runner);
        if (tabs && combo && runner)
        {
            const int adapterCount = combo->count();
            const QString selected = combo->currentData().toString();
            std::printf("    adapter WiFi: %d, tab dang %s\n", adapterCount, tabs->isEnabled() ? "bat" : "khoa (khong co adapter/dich vu)");
            QPushButton* startStop = nullptr;
            for (QPushButton* b : tabs->widget(2)->findChildren<QPushButton*>())
                if (b->text().contains("Bắt đầu đo"))
                    startStop = b;
            CHECK(startStop != nullptr);
            const bool routed = proxy.isListening() && routeToFakeProxy(runner, proxy);
            CHECK(routed);

            for (int i = 0; i < opens; ++i)
            {
                for (int k = 0; k < 6; ++k)
                    tabs->setCurrentIndex(rnd(0, 2));
                if (routed && startStop && tabs->isEnabled() && i % 4 == 0)
                {
                    tabs->setCurrentIndex(2);
                    startStop->click(); // bắt đầu đo (đi vào proxy giả, treo ở pha ping)
                    CHECK(runner->isRunning());
                    if (i % 8 == 0)
                    {
                        pump(10);
                        startStop->click(); // người dùng bấm dừng
                        CHECK(!runner->isRunning());
                        pump(60);           // nút được mở lại sau 50 ms
                        CHECK(startStop->isEnabled() && startStop->text().contains("Bắt đầu đo"));
                    }
                }
                win.close();
                CHECK(!win.isVisible());
                CHECK(!runner->isRunning()); // closeEvent dừng phép đo
                CHECK(!startStop || startStop->text().contains("Bắt đầu đo"));
                if (i % 5 == 0)
                    QCoreApplication::processEvents();
                win.show();
                if (i % 5 == 0)
                    QCoreApplication::processEvents();
                CHECK(combo->count() == adapterCount);
                CHECK(combo->currentData().toString() == selected);
            }
        }
        win.close();
    }

    lap("WifiWindow tai dung");

    // (c) ConnectionWatcher + ConnectDialog với WlanController CHƯA open(): mọi lời gọi dừng ở ensureOpen(),
    // không lệnh WLAN nào tới hệ thống. Thử máy trạng thái/thông báo lỗi/đóng mở dồn dập.
    {
        WlanController closedController; // cố ý KHÔNG open()
        CHECK(!closedController.isOpen());
        QString err;
        WifiProfileBackup backup;
        backup.valid = true; // một bản sao lưu "hợp lệ" giả: phải bị chặn ở ensureOpen(), không tới WLAN
        backup.adapterGuid = "00000000-0000-0000-0000-000000000000";
        backup.profileName = "OneForAll-stress-khong-ton-tai";
        CHECK(!closedController.rollbackProfile(backup, &err) && !err.isEmpty());
        CHECK(closedController.rollbackProfile(WifiProfileBackup(), &err)); // không có gì để hoàn tác
        CHECK(closedController.adapters().isEmpty() && closedController.availableNetworks("x").isEmpty());
        CHECK(closedController.profiles("x", true).isEmpty() && !closedController.hasProfile("x", "y"));
        CHECK(!closedController.currentConnection("x").isConnected);
        CHECK(closedController.exportProfileXml("x", "y", true, &err).isEmpty());

        for (int i = 0; i < 2000; ++i)
        {
            ConnectionWatcher w(&closedController);
            w.watch("guid", randomUnicode(rnd(0, 10)), randomUnicode(rnd(0, 10)));
            CHECK(w.isActive());
            if (i % 2)
                w.watch("guid2", "a", "b"); // thay lần theo dõi đang dở
            if (i % 3)
            {
                w.cancel();
                CHECK(!w.isActive());
                w.cancel();
            }
        } // hủy khi đang theo dõi
        {
            ConnectionWatcher w(&closedController);
            int fired = 0;
            QObject::connect(&w, &ConnectionWatcher::succeeded, [&fired](const QString&) { ++fired; });
            QObject::connect(&w, &ConnectionWatcher::failed, [&fired](const QString&) { ++fired; });
            w.watch("guid", "Mang", "Mang");
            pump(1200); // vài nhịp polling: chưa tới hạn 15 s nên chưa được kết luận gì
            CHECK(w.isActive() && fired == 0);
            w.cancel();
            pump(600);
            CHECK(fired == 0);
        }
        for (int e = -5; e <= 40000; e += 250)
        {
            CHECK(ConnectionWatcher::shouldGiveUp(e, false) == (e >= ConnectionWatcher::kSoftLimitMs));
            CHECK(ConnectionWatcher::shouldGiveUp(e, true) == (e >= ConnectionWatcher::kHardLimitMs));
        }

        // Hộp thoại lỗi do chính ConnectDialog mở (modal) được một timer tự đóng để vòng lặp chạy tiếp.
        int boxesClosed = 0;
        QTimer closer;
        closer.setInterval(5);
        QObject::connect(&closer, &QTimer::timeout, [&boxesClosed] {
            for (QWidget* w : QApplication::topLevelWidgets())
                if (auto* box = qobject_cast<QMessageBox*>(w); box && box->isVisible())
                {
                    ++boxesClosed;
                    box->reject();
                }
        });
        closer.start();

        const QList<WifiSecurity> securities = {WifiSecurity::Open, WifiSecurity::Wep, WifiSecurity::WpaPsk, WifiSecurity::Wpa2Psk,
                                               WifiSecurity::Wpa3Sae, WifiSecurity::Enterprise, WifiSecurity::Unknown};
        int clicks = 0;
        for (int i = 0; i < 200; ++i)
        {
            const bool hiddenMode = i % 2;
            ConnectDialog* dlg = nullptr;
            if (hiddenMode)
            {
                dlg = ConnectDialog::forHiddenNetwork(&closedController, "khong-phai-guid", nullptr, securities[rnd(0, securities.size() - 1)]);
            }
            else
            {
                WifiNetwork net;
                net.ssidBytes = QByteArray(rnd(1, 32), static_cast<char>(rnd(0x20, 0xFF)));
                net.ssid = ssidDisplay(net.ssidBytes);
                net.security = securities[rnd(0, securities.size() - 1)];
                dlg = new ConnectDialog(&closedController, "khong-phai-guid", net);
            }
            dlg->show();
            QCoreApplication::processEvents();

            const QList<QLineEdit*> edits = dlg->findChildren<QLineEdit*>();
            for (QLineEdit* e : edits)
                if (!e->isReadOnly() && e->isEnabled() && chance(70))
                    e->setText(chance(20) ? QString(rnd(33, 80), QChar(0x1EC7)) : randomUnicode(rnd(0, 20), false));
            for (QComboBox* c : dlg->findChildren<QComboBox*>())
                c->setCurrentIndex(rnd(0, c->count() - 1));
            if (QPushButton* eye = findButton(dlg, "👁"))
            {
                eye->click();
                eye->click();
            }
            QPushButton* connectBtn = findButton(dlg, "Kết nối");
            CHECK(connectBtn != nullptr);
            for (int k = 0, n = rnd(0, 3); connectBtn && k < n; ++k)
            {
                const int before = boxesClosed;
                connectBtn->click(); // -> hộp thoại báo thiếu SSID / SSID quá dài / chưa kết nối dịch vụ WLAN
                ++clicks;
                CHECK(boxesClosed == before + 1); // đúng MỘT thông báo cho mỗi lần bấm, không lần nào im lặng
                CHECK(connectBtn->isEnabled());   // thất bại ngay -> hộp thoại trở lại trạng thái chờ
            }
            switch (i % 4)
            {
                case 0: dlg->reject(); break;
                case 1: dlg->close(); break;
                case 2: dlg->done(QDialog::Accepted); break;
                default: break; // hủy thẳng khi đang hiện
            }
            delete dlg;
        }
        closer.stop();
        std::printf("    ConnectDialog (controller chua mo): 200 hop thoai, %d lan bam Ket noi, %d thong bao loi tu dong\n", clicks, boxesClosed);
        CHECK(boxesClosed == clicks);
    }

    lap("ConnectionWatcher + ConnectDialog");
    std::printf("[ui-wifi] mo/dong %d + tai dung %d - %lld ms\n", opens, opens, static_cast<long long>(timer.elapsed()));
}

// ================================================================== main

int main(int argc, char** argv)
{
    // Chạy không cần màn hình (máy build/CI); đặt QT_QPA_PLATFORM=windows để xem bằng nền tảng thật. Tự tìm
    // plugin offscreen để Qt không bật hộp thoại lỗi khi thư mục chạy thiếu nó - xem tests/OffscreenPlatform.h.
    if (!TestPlatform::forceOffscreen(argv[0]))
        return 2;
    QApplication app(argc, argv);

    // PHẢI đứng trước mọi lời gọi tới QRHistoryStore/Logger: ép toàn bộ dữ liệu vào thư mục tạm để bộ test
    // không đọc/ghi/xóa lịch sử QR + log thật của người dùng trên máy đang chạy test.
    TestDataDir tmp;
    CHECK(tmp.isValid());
    AppPaths::setDataDirOverride(tmp.path());
    CHECK(QRHistoryStore::instance().filePath().startsWith(QDir(tmp.path()).absolutePath()));

    quint32 seed = QRandomGenerator::global()->generate();
    if (qEnvironmentVariableIsSet("QRWIFI_STRESS_SEED"))
        seed = qEnvironmentVariable("QRWIFI_STRESS_SEED").toUInt();
    g_rng.seed(seed);
    std::printf("qrwifi_stress_tests seed=%u (dat QRWIFI_STRESS_SEED=%u de chay lai dung chuoi nay)\n", seed, seed);

    QStringList groups = app.arguments().mid(1).join(',').split(',', Qt::SkipEmptyParts);
    const auto wanted = [&groups](const char* name) { return groups.isEmpty() || groups.contains(name); };

    QElapsedTimer total;
    total.start();
    try
    {
        if (wanted("qr"))      stressQrRoundTrip(3000);
        if (wanted("decode"))  stressDecode(2500, 1500);
        if (wanted("payload")) stressPayload(60000, 20000);
        if (wanted("history")) stressHistory(tmp, 20000, 300);
        if (wanted("xml"))     stressProfileXml(20000);
        if (wanted("speed"))   stressSpeedTest(200);
        if (wanted("ui"))
        {
            stressQrUi(tmp, 100, 400);
            stressWifiUi(100);
        }
    }
    catch (const std::exception& e)
    {
        ++g_fail;
        std::printf("FAIL: ngoai le lot ra ngoai: %s\n", e.what());
    }
    catch (...)
    {
        ++g_fail;
        std::printf("FAIL: ngoai le khong ro kieu lot ra ngoai\n");
    }

    std::printf("tong thoi gian %lld ms\n", static_cast<long long>(total.elapsed()));
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
