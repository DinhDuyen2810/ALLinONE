// Kiểm thử ĐẦU-CUỐI "Mở PowerShell & dán lệnh" của Security Gateway (CommandLauncher + ConsolePaste).
//
// Mở powershell.exe THẬT bằng đúng CommandLauncher của ứng dụng, dán qua tiến trình phụ "--console-paste" (tệp
// test này tự đóng vai tiến trình phụ; một ca dùng đúng build\OneForAll.exe nếu đã build), rồi kiểm ĐỘC LẬP
// bằng một tiến trình phụ khác của test ("--dump-screen") đọc lại màn hình console:
//   - lệnh đã nằm ở dòng lệnh PowerShell,
//   - lệnh KHÔNG bị chạy: mỗi lệnh thử đều tự tạo một tệp đánh dấu nếu chạy - tệp đó không được xuất hiện.
//
// An toàn với máy đang chạy test (CLAUDE.md quy tắc 5): console ẨN (CREATE_NO_WINDOW) trừ một ca có cửa sổ
// thật nhưng thu nhỏ + không lấy focus (kiểm lỗi gốc v1.28.0: console không có cửa sổ); KHÔNG SendInput/tiêm
// phím chuột thật - phím Ctrl+V được ghi vào bộ đệm nhập của chính console do test tạo; mọi powershell.exe do
// test mở đều được đóng; tệp chỉ nằm trong thư mục tạm của test; Clipboard của người dùng được chụp lại và
// khôi phục nguyên vẹn mọi định dạng (ClipboardSnapshot.h).
//
// "--visible": chạy đúng đường của ứng dụng (cửa sổ PowerShell thật hiện lên + nạp profile của người dùng +
// đưa cửa sổ lên trước) - kiểm tra tay, KHÔNG dùng như test tự động.
#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QThread>
#include <QTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ClipboardSnapshot.h"
#include "TestDataDir.h"
#include "tools/security/engine/CommandLauncher.h"
#include "tools/security/engine/ConsolePaste.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

// ---------------------------------------------------------------------------------------------------------
// Tiến trình phụ của test: đọc màn hình console của <pid> ra tệp (UTF-8). Dòng đầu: "WINDOW=<0|1> VISIBLE=<0|1>
// OWNER=<pid chủ cửa sổ>" - để kiểm console có cửa sổ thật hay không.
static int dumpScreenMode(const char* pidArg, const char* outPath)
{
    const DWORD pid = std::strtoul(pidArg, nullptr, 10);
    FreeConsole();
    if (!AttachConsole(pid))
        return 2;
    QString text;
    HWND window = GetConsoleWindow();
    DWORD owner = 0;
    if (window)
        GetWindowThreadProcessId(window, &owner);
    text += QString("WINDOW=%1 VISIBLE=%2 OWNER=%3\n").arg(window ? 1 : 0).arg(window && IsWindowVisible(window) ? 1 : 0).arg(owner);
    HANDLE out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (out != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(out, &info))
    {
        std::wstring line(static_cast<size_t>(info.dwSize.X), L' ');
        for (SHORT y = 0; y <= info.dwCursorPosition.Y; ++y)
        {
            DWORD read = 0;
            ReadConsoleOutputCharacterW(out, line.data(), static_cast<DWORD>(info.dwSize.X), COORD{0, y}, &read);
            QString row = QString::fromWCharArray(line.data(), static_cast<qsizetype>(read));
            while (row.endsWith(' '))
                row.chop(1);
            text += row + '\n';
        }
    }
    if (out != INVALID_HANDLE_VALUE)
        CloseHandle(out);
    FreeConsole();
    QFile file(QString::fromLocal8Bit(outPath));
    if (!file.open(QIODevice::WriteOnly))
        return 3;
    file.write(text.toUtf8());
    return 0;
}

/// Tiến trình phụ của test: bấm Enter (ghi phím vào bộ đệm nhập) trong console ẨN của <pid> mà chính test đã
/// tạo - CHỈ dùng cho ca kiểm "lệnh dán vào có đúng từng ký tự không" với một lệnh vô hại.
static int pressEnterMode(const char* pidArg)
{
    const DWORD pid = std::strtoul(pidArg, nullptr, 10);
    FreeConsole();
    if (!AttachConsole(pid))
        return 2;
    HANDLE in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    INPUT_RECORD records[2]{};
    for (int i = 0; i < 2; ++i)
    {
        records[i].EventType = KEY_EVENT;
        records[i].Event.KeyEvent.bKeyDown = i == 0;
        records[i].Event.KeyEvent.wRepeatCount = 1;
        records[i].Event.KeyEvent.wVirtualKeyCode = VK_RETURN;
        records[i].Event.KeyEvent.wVirtualScanCode = static_cast<WORD>(MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC));
        records[i].Event.KeyEvent.uChar.UnicodeChar = L'\r';
    }
    DWORD written = 0;
    const bool ok = in != INVALID_HANDLE_VALUE && WriteConsoleInputW(in, records, 2, &written) && written == 2;
    if (in != INVALID_HANDLE_VALUE)
        CloseHandle(in);
    FreeConsole();
    return ok ? 0 : 3;
}

// ---------------------------------------------------------------------------------------------------------
struct LaunchOutcome
{
    int signalCount{0};
    bool ok{false};
    QString message;
    qint64 ms{0};
    quint32 pid{0};
};

static LaunchOutcome launchAndWait(CommandLauncher& launcher, const QString& command, int maxWaitMs = 45000)
{
    LaunchOutcome r;
    QEventLoop loop;
    const auto conn = QObject::connect(&launcher, &CommandLauncher::finished, &loop, [&](bool ok, const QString& message) {
        ++r.signalCount;
        r.ok = ok;
        r.message = message;
        loop.quit();
    });
    QElapsedTimer timer;
    timer.start();
    launcher.launchAndPaste(command);
    if (r.signalCount == 0)
    {
        QTimer::singleShot(maxWaitMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    r.ms = timer.elapsed();
    r.pid = launcher.lastPowerShellPid();
    // Thêm một quãng ngắn để bắt tín hiệu finished() thứ hai (phải đúng MỘT lần).
    QElapsedTimer drain;
    drain.start();
    while (drain.elapsed() < 300)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    QObject::disconnect(conn);
    return r;
}

static void waitMs(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
}

static QString g_tempDir;

static QString dumpScreen(quint32 pid)
{
    const QString file = g_tempDir + QString("/screen_%1.txt").arg(pid);
    QFile::remove(file);
    QProcess p;
    p.start(QCoreApplication::applicationFilePath(), {"--dump-screen", QString::number(pid), QDir::toNativeSeparators(file)});
    p.waitForFinished(15000);
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return QString("<khong doc duoc man hinh, ma %1>").arg(p.exitCode());
    return QString::fromUtf8(f.readAll());
}

/// Lệnh PowerShell tạo tệp đánh dấu nếu bị CHẠY.
static QString markerPath(const QString& name)
{
    return QDir::toNativeSeparators(g_tempDir + "/marker_" + name + ".txt");
}
static QString markerCommand(const QString& name)
{
    return QString("Set-Content -LiteralPath '%1' -Value ran").arg(markerPath(name));
}
static bool markerExists(const QString& name)
{
    return QFileInfo::exists(markerPath(name));
}

/// Chuỗi để in ra console/pipe: ký tự ngoài ASCII viết thành \uXXXX (in thẳng qua trang mã của console sẽ
/// thành "?" - không phân biệt được ký tự nào thật sự nằm trên màn hình PowerShell).
static QByteArray printable(const QString& text)
{
    QByteArray out;
    for (const QChar c : text)
    {
        if (c.unicode() < 0x80)
            out += static_cast<char>(c.unicode());
        else
            out += QString("\\u%1").arg(static_cast<uint>(c.unicode()), 4, 16, QChar('0')).toLatin1();
    }
    return out;
}

static void printOutcome(const char* label, const LaunchOutcome& r)
{
    std::printf("[%s] ok=%d signalCount=%d %lld ms: %s\n", label, r.ok, r.signalCount, static_cast<long long>(r.ms),
                printable(r.message.left(160)).constData());
}

static CommandLauncher::TestOptions hiddenOptions()
{
    CommandLauncher::TestOptions o;
    o.helperProgram = QCoreApplication::applicationFilePath();
    o.powershellArgs = {"-NoProfile"};
    o.hiddenConsole = true;
    return o;
}

/// Chạy một lệnh, kiểm dán thành công + KHÔNG chạy (các tệp đánh dấu trong `markers` không xuất hiện), trả về màn hình.
static QString expectPastedNotRun(const char* label, CommandLauncher& launcher, const QString& command,
                                  const QStringList& markers, const QString& mustAppear)
{
    const LaunchOutcome r = launchAndWait(launcher, command);
    printOutcome(label, r);
    CHECK(r.signalCount == 1);
    CHECK(r.ok);
    CHECK(r.message.contains(QString::fromUtf8("✓")));
    CHECK(r.pid != 0);
    waitMs(1500); // đủ để một lệnh lỡ bị chạy kịp tạo tệp
    for (const QString& m : markers)
    {
        if (markerExists(m))
            std::printf("  !!! LENH DA BI CHAY: %s\n", qPrintable(markerPath(m)));
        CHECK(!markerExists(m));
    }
    const QString screen = dumpScreen(r.pid);
    if (!mustAppear.isEmpty())
    {
        if (!screen.contains(mustAppear))
            std::printf("  can thay: %s\n  man hinh:\n%s\n", printable(mustAppear).constData(), printable(screen).constData());
        CHECK(screen.contains(mustAppear));
    }
    CHECK(!screen.contains("^V"));
    launcher.terminateLastPowerShellForTest();
    return screen;
}

static int runVisibleManualCheck()
{
    std::printf("CHE DO --visible: mo PowerShell THAT (cua so hien len, nap profile) nhu ung dung.\n");
    CommandLauncher launcher;
    CommandLauncher::TestOptions o;
    const QString realHelper = QCoreApplication::applicationDirPath() + "/OneForAll.exe";
    o.helperProgram = QFileInfo::exists(realHelper) ? realHelper : QCoreApplication::applicationFilePath();
    std::printf("Tien trinh phu: %s\n", qPrintable(o.helperProgram));
    launcher.setTestOptions(o);
    expectPastedNotRun("visible-1dong", launcher, markerCommand("v1"), {"v1"}, "Set-Content");
    expectPastedNotRun("visible-3dong", launcher,
                       markerCommand("v2a") + "\n" + markerCommand("v2b") + "\n" + markerCommand("v2c"),
                       {"v2a", "v2b", "v2c"}, ">> ");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::strcmp(argv[1], ConsolePaste::kHelperSwitch) == 0)
        return ConsolePaste::runHelperMode(argc, argv);
    if (argc >= 4 && std::strcmp(argv[1], "--dump-screen") == 0)
        return dumpScreenMode(argv[2], argv[3]);
    if (argc >= 3 && std::strcmp(argv[1], "--press-enter") == 0)
        return pressEnterMode(argv[2]);

    // Nền tảng "windows" mặc định (không offscreen): CommandLauncher phải đặt Clipboard THẬT của Windows -
    // PSReadLine đọc từ đó. Test không tạo cửa sổ Qt nào.
    QGuiApplication app(argc, argv);
    TestDataDir dataDir;
    g_tempDir = dataDir.path();
    ClipboardSnapshot clipboard; // khôi phục Clipboard của người dùng khi main kết thúc (kể cả khi test fail)
    std::printf("Clipboard cua nguoi dung: da chup %zu dinh dang (se khoi phuc)\n", clipboard.formatCount());
    CHECK(clipboard.saved());

    if (argc >= 2 && std::strcmp(argv[1], "--visible") == 0)
    {
        runVisibleManualCheck();
        clipboard.restore();
        std::printf("passed=%d failed=%d\n", g_pass, g_fail);
        return g_fail == 0 ? 0 : 1;
    }

    // ---- 1. Tiến trình phụ: tham số sai / không có lệnh qua stdin / PID không tồn tại ----
    {
        auto runHelper = [](const QStringList& args, const QByteArray& input) {
            QProcess p;
            p.start(QCoreApplication::applicationFilePath(), args);
            p.waitForStarted(10000);
            if (!input.isEmpty())
                p.write(input);
            p.closeWriteChannel();
            p.waitForFinished(20000);
            return p.exitCode();
        };
        CHECK(runHelper({"--console-paste", "abc"}, "Get-Date") == static_cast<int>(ConsolePaste::Outcome::BadArguments));
        CHECK(runHelper({"--console-paste", "0"}, "Get-Date") == static_cast<int>(ConsolePaste::Outcome::BadArguments));
        CHECK(runHelper({"--console-paste", "12x"}, "Get-Date") == static_cast<int>(ConsolePaste::Outcome::BadArguments));
        CHECK(runHelper({"--console-paste", "1234"}, "") == static_cast<int>(ConsolePaste::Outcome::BadArguments));
        CHECK(runHelper({"--console-paste", "1234"}, "  \n\t ") == static_cast<int>(ConsolePaste::Outcome::BadArguments));
        // PID không thể tồn tại (bội số của 4 gần 2^32) -> không mở được tiến trình = coi như đã thoát.
        CHECK(runHelper({"--console-paste", "4294967292"}, "Get-Date") == static_cast<int>(ConsolePaste::Outcome::ProcessExited));
        std::printf("[helper-args] xong\n");
    }

    CommandLauncher launcher;
    launcher.setTestOptions(hiddenOptions());

    // ---- 2. Lệnh trống: báo lỗi NGAY, không mở tiến trình nào ----
    {
        const LaunchOutcome r = launchAndWait(launcher, "  \r\n\t \n");
        printOutcome("trong", r);
        CHECK(r.signalCount == 1);
        CHECK(!r.ok);
        CHECK(r.pid == 0);
        CHECK(r.ms < 1000);
        CHECK(!launcher.isBusy());
    }

    // ---- 3. Lệnh một dòng: dán đúng, KHÔNG chạy; Clipboard chứa đúng lệnh đã chuẩn hóa ----
    {
        const QString cmd = markerCommand("single");
        const QString screen = expectPastedNotRun("1-dong", launcher, cmd, {"single"}, "Set-Content -LiteralPath");
        CHECK(QGuiApplication::clipboard()->text() == cmd);
        CHECK(screen.startsWith("WINDOW=0")); // console ẩn trong chế độ test
        CHECK(!launcher.isBusy());
    }

    // ---- 4. Lệnh 3 dòng: PSReadLine dán thành khối nhiều dòng (dấu nhắc nối ">> "), KHÔNG dòng nào chạy ----
    {
        const QString cmd = markerCommand("m1") + "\r\n" + markerCommand("m2") + "\r\n" + markerCommand("m3");
        const QString screen = expectPastedNotRun("3-dong", launcher, cmd, {"m1", "m2", "m3"}, ">> ");
        CHECK(screen.count("Set-Content") >= 3);
    }

    // ---- 5. Dòng trắng ở đầu + xuống dòng ở CUỐI: xuống dòng cuối bị bỏ nên không thành "bấm Enter hộ" ----
    expectPastedNotRun("xuong-dong-cuoi", launcher, "\r\n\r\n" + markerCommand("trailing") + "\r\n\r\n\r\n",
                       {"trailing"}, "Set-Content");

    // ---- 6. Tiếng Việt có dấu, nháy cong, tab, dấu chấm phẩy: dán + xác nhận được dù PSReadLine 2.0 VẼ chữ
    //         có dấu ngoài bảng mã console thành "?" (lỗi gặp thật lần chạy đầu: báo "chưa thấy lệnh" sau 20 giây) ----
    expectPastedNotRun("unicode", launcher,
                       QString::fromUtf8("Write-Output 'Tiếng Việt có dấu – “nháy cong”';\t$x = 1; ") + markerCommand("unicode"),
                       {"unicode"}, "Write-Output 'Ti");

    // ---- 6b. Lệnh dán vào phải ĐÚNG TỪNG KÝ TỰ (cái người dùng đã cho phân tích = cái sẽ chạy). Lệnh VÔ HẠI ghi
    //          mã UTF-16 của một chuỗi khó ra tệp; test tự bấm Enter trong console ẩn của nó rồi so mã ----
    {
        struct Case
        {
            const char* label;
            QString text;
            QString expected; ///< chuỗi PowerShell thật sự nhận được
        };
        const QString hard = QString::fromUtf8("Tiếng Việt có dấu – “nháy” 漢字 ") + QString::fromUcs4(U"\U0001F600") +
                             QString::fromUtf8(" $x `n $(1+1) end") + QChar(0x00A0) + "!";
        const QList<Case> cases = {
            {"ky-tu-kho", hard, hard},
            // PSReadLine (chức năng Paste) đổi Tab thành dấu cách - ghi nhận hành vi thật để thông báo cho đúng
            {"tab", "a\tb", "a    b"},
        };
        for (const Case& c : cases)
        {
            const QString out = QDir::toNativeSeparators(g_tempDir + "/codes_" + c.label + ".txt");
            const QString cmd = QString("([int[]][char[]]'%1') -join ',' | Set-Content -LiteralPath '%2'").arg(c.text, out);
            const LaunchOutcome r = launchAndWait(launcher, cmd);
            printOutcome(c.label, r);
            CHECK(r.ok);
            waitMs(800);
            CHECK(!QFileInfo::exists(out)); // chưa ai bấm Enter thì chưa chạy
            QProcess enter;
            enter.start(QCoreApplication::applicationFilePath(), {"--press-enter", QString::number(r.pid)});
            enter.waitForFinished(10000);
            CHECK(enter.exitCode() == 0);
            QElapsedTimer t;
            t.start();
            while (!QFileInfo::exists(out) && t.elapsed() < 15000)
                waitMs(100);
            waitMs(300);
            QFile f(out);
            QStringList expectedCodes;
            for (const QChar ch : c.expected)
                expectedCodes << QString::number(ch.unicode());
            const QString got = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString("<khong co tep>");
            if (got != expectedCodes.join(','))
                std::printf("  ma nhan duoc: %s\n  ma mong doi : %s\n", qPrintable(got), qPrintable(expectedCodes.join(',')));
            CHECK(got == expectedCodes.join(','));
            launcher.terminateLastPowerShellForTest();
        }
    }

    // ---- 7. Lệnh dài ~3000 ký tự trên một dòng (console tự gãy dòng) ----
    expectPastedNotRun("dai", launcher, "$s = '" + QString(3000, 'a') + "'; " + markerCommand("long"), {"long"}, "$s = 'aaaa");

    // ---- 8. Lệnh trông "Nguy hiểm" (download cradle tới cổng đóng của máy mình) - dán, không chạy. Ghép từ
    //         mảnh lúc chạy: chuỗi liền mạch kiểu này từng khiến Defender cách ly tệp nguồn test (xem
    //         security_tests.cpp, hàm S()). ----
    expectPastedNotRun("nguy-hiem", launcher,
                       QString("I") + "EX (New-Object Net.Web" + "Client).Download" + "String('http://127.0.0.1:9/x'); " +
                           markerCommand("danger"),
                       {"danger"}, QString("Download") + "String");

    // ---- 9. Lệnh không có chữ/số nào: không tự đối chiếu được -> báo "đã gửi", vẫn ok ----
    {
        const LaunchOutcome r = launchAndWait(launcher, "$_ | % {$_}");
        printOutcome("ky-hieu", r);
        CHECK(r.signalCount == 1);
        CHECK(r.ok);
        CHECK(r.message.contains(QString::fromUtf8("không tự xác nhận được")));
        waitMs(1500);
        CHECK(dumpScreen(r.pid).contains("$_ | % {$_}"));
        launcher.terminateLastPowerShellForTest();
    }

    // ---- 10. PowerShell KHÔNG có PSReadLine: phím dán bị bỏ qua (không chèn "^V", không chạy), báo tự dán ----
    {
        auto o = hiddenOptions();
        o.powershellArgs = {"-NoProfile", "-NoExit", "-Command", "Remove-Module PSReadLine"};
        launcher.setTestOptions(o);
        const QString cmd = markerCommand("np1") + "\n" + markerCommand("np2");
        const LaunchOutcome r = launchAndWait(launcher, cmd);
        printOutcome("khong-psreadline", r);
        CHECK(r.signalCount == 1);
        CHECK(!r.ok);
        CHECK(r.message.contains("PSReadLine"));
        CHECK(r.message.contains(QString::fromUtf8("chuột phải"))); // lệnh nhiều dòng: cảnh báo dán bằng chuột phải
        CHECK(r.ms < 15000); // nhận ra sớm nhờ "dấu nhắc đứng yên", không đợi hết 20 giây
        waitMs(1500);
        CHECK(!markerExists("np1"));
        CHECK(!markerExists("np2"));
        const QString screen = dumpScreen(r.pid);
        CHECK(!screen.contains("^V"));
        CHECK(!screen.contains("Set-Content"));
        launcher.terminateLastPowerShellForTest();
        launcher.setTestOptions(hiddenOptions());
    }

    // ---- 11. PowerShell bận (dấu nhắc chưa hiện) lâu hơn thời gian chờ: báo "chưa thấy lệnh", và phím dán đã
    //          gửi vẫn được xử lý SAU ĐÓ (đúng như thông báo "nếu lát nữa lệnh tự hiện") - vẫn không chạy ----
    {
        auto o = hiddenOptions();
        o.powershellArgs = {"-NoProfile", "-NoExit", "-Command", "Start-Sleep -Seconds 6"};
        o.helperTimeoutMs = 2000;
        launcher.setTestOptions(o);
        const LaunchOutcome r = launchAndWait(launcher, markerCommand("slow"));
        printOutcome("cham", r);
        CHECK(r.signalCount == 1);
        CHECK(!r.ok);
        CHECK(r.message.contains(QString::fromUtf8("chưa thấy lệnh")));
        waitMs(8000);
        CHECK(!markerExists("slow"));
        CHECK(dumpScreen(r.pid).contains("Set-Content -LiteralPath"));
        launcher.terminateLastPowerShellForTest();
        launcher.setTestOptions(hiddenOptions());
    }

    // ---- 12. PowerShell thoát ngay sau khi mở ----
    {
        auto o = hiddenOptions();
        o.powershellArgs = {"-NoProfile", "-Command", "exit"};
        launcher.setTestOptions(o);
        const LaunchOutcome r = launchAndWait(launcher, markerCommand("exit"));
        printOutcome("thoat-ngay", r);
        CHECK(r.signalCount == 1);
        CHECK(!r.ok);
        CHECK(r.message.contains(QString::fromUtf8("đóng")));
        CHECK(!markerExists("exit"));
        launcher.terminateLastPowerShellForTest();
        launcher.setTestOptions(hiddenOptions());
    }

    // ---- 13. Không chạy được tiến trình phụ: báo lỗi + hướng dẫn tự dán, không treo trạng thái bận ----
    {
        auto o = hiddenOptions();
        o.helperProgram = g_tempDir + "/khong-ton-tai.exe";
        launcher.setTestOptions(o);
        const LaunchOutcome r = launchAndWait(launcher, markerCommand("nohelper"));
        printOutcome("thieu-tien-trinh-phu", r);
        CHECK(r.signalCount == 1);
        CHECK(!r.ok);
        CHECK(r.message.contains(QString::fromUtf8("không chạy được bước dán")));
        CHECK(r.message.contains("Ctrl+V"));
        CHECK(!launcher.isBusy());
        launcher.terminateLastPowerShellForTest();
        launcher.setTestOptions(hiddenOptions());
    }

    // ---- 14. Bấm hai lần liên tiếp: lần thứ hai bị bỏ qua (không mở cửa sổ thứ hai, chỉ MỘT tín hiệu) ----
    {
        int signalCount = 0;
        bool lastOk = false;
        QEventLoop loop;
        const auto conn = QObject::connect(&launcher, &CommandLauncher::finished, &loop, [&](bool ok, const QString&) {
            ++signalCount;
            lastOk = ok;
            loop.quit();
        });
        launcher.launchAndPaste(markerCommand("busy1"));
        const quint32 firstPid = launcher.lastPowerShellPid();
        CHECK(launcher.isBusy());
        launcher.launchAndPaste(markerCommand("busy2"));
        CHECK(launcher.lastPowerShellPid() == firstPid);
        QTimer::singleShot(45000, &loop, &QEventLoop::quit);
        loop.exec();
        waitMs(500);
        QObject::disconnect(conn);
        std::printf("[bam-2-lan] signalCount=%d ok=%d\n", signalCount, lastOk);
        CHECK(signalCount == 1);
        CHECK(lastOk);
        CHECK(QGuiApplication::clipboard()->text() == markerCommand("busy1"));
        waitMs(1000);
        CHECK(!markerExists("busy1"));
        CHECK(!markerExists("busy2"));
        launcher.terminateLastPowerShellForTest();
    }

    // ---- 15. Năm lần liên tiếp trên cùng một CommandLauncher (không rò trạng thái giữa các lần) ----
    for (int i = 0; i < 5; ++i)
    {
        const QString name = QString("loop%1").arg(i);
        const LaunchOutcome r = launchAndWait(launcher, markerCommand(name));
        CHECK(r.signalCount == 1);
        CHECK(r.ok);
        waitMs(800);
        CHECK(!markerExists(name));
        launcher.terminateLastPowerShellForTest();
    }
    std::printf("[5-lan-lien-tiep] xong\n");

    // ---- 16. Hồi quy lỗi gốc (v1.28.0 trở về trước): console PHẢI có cửa sổ thật, Windows báo chủ cửa sổ là
    //          chính powershell.exe (nên tìm được để đưa lên trước). Cửa sổ thu nhỏ, không lấy focus. ----
    {
        auto o = hiddenOptions();
        o.hiddenConsole = false;
        o.minimizedNoActivate = true;
        launcher.setTestOptions(o);
        const QString screen = expectPastedNotRun("co-cua-so", launcher, markerCommand("window"), {"window"}, "Set-Content");
        const QString firstLine = screen.section('\n', 0, 0);
        std::printf("  %s\n", qPrintable(firstLine));
        CHECK(firstLine.startsWith("WINDOW=1"));
        launcher.setTestOptions(hiddenOptions());
    }
    {
        // Kiểm riêng chủ cửa sổ (cần PID còn sống nên không dùng expectPastedNotRun).
        auto o = hiddenOptions();
        o.hiddenConsole = false;
        o.minimizedNoActivate = true;
        launcher.setTestOptions(o);
        const LaunchOutcome r = launchAndWait(launcher, "Get-Date");
        const QString firstLine = dumpScreen(r.pid).section('\n', 0, 0);
        CHECK(r.ok);
        CHECK(firstLine.contains(QString("OWNER=%1").arg(r.pid)));
        // Không cướp focus của người dùng: cửa sổ đang ở phía trước không phải cửa sổ PowerShell vừa mở.
        DWORD foregroundPid = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
        CHECK(foregroundPid != r.pid);
        launcher.terminateLastPowerShellForTest();
        launcher.setTestOptions(hiddenOptions());
    }

    // ---- 17. Đúng tiến trình phụ của ứng dụng thật: build\OneForAll.exe --console-paste ----
    {
        const QString realHelper = QCoreApplication::applicationDirPath() + "/OneForAll.exe";
        if (QFileInfo::exists(realHelper))
        {
            auto o = hiddenOptions();
            o.helperProgram = realHelper;
            launcher.setTestOptions(o);
            expectPastedNotRun("OneForAll.exe", launcher, markerCommand("real1") + "\n" + markerCommand("real2"),
                               {"real1", "real2"}, ">> ");
            launcher.setTestOptions(hiddenOptions());
        }
        else
        {
            std::printf("[OneForAll.exe] BO QUA: chua build %s\n", qPrintable(realHelper));
        }
    }

    clipboard.restore();
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
