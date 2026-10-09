// Kiểm thử CHỊU TẢI cho phần lõi dùng chung (AppPaths, Logger, PowerShellRunner, WinProcessTree, tự cập
// nhật) + Auto Click + Android Phone Control: nhiều luồng, dữ liệu ngẫu nhiên số lượng lớn, lặp lại thao
// tác hàng trăm/nghìn lần - bắt lớp lỗi mà các bộ test "một ca một lần" không thấy (đua luồng, rò handle,
// treo ống dẫn, chỉ số ngoài miền, chèn lệnh qua chuỗi lạ).
//
// CÓ chạy powershell.exe THẬT nhưng chỉ với lệnh vô hại (Write-Output, Start-Sleep, phân tích cú pháp bằng
// [Parser]::ParseInput - KHÔNG thực thi script trợ giúp cập nhật). TUYỆT ĐỐI KHÔNG gửi chuột/phím thật:
// mọi chuỗi Auto Click được chạy ở đây chỉ gồm hành động "Gõ văn bản" với văn bản RỖNG (InputController::
// typeText() thoát trước khi tới SendInput) - có bước tự kiểm tra lại điều đó trước mỗi lần bấm "Chạy".
// Không cài hook toàn cục, không ghép đôi/kết nối adb, không chạy scrcpy. Dữ liệu ghi vào thư mục tạm.
//
// Chạy: core_stress_tests.exe [seed] [nhóm]
//   seed: không truyền (hoặc 0) thì lấy theo đồng hồ và in ra để chạy lại được đúng ca đó.
//   nhóm: chỉ chạy các kịch bản có khóa chứa chuỗi này (logger, apppaths, powershell, process, update,
//         serializer, runner, hotkey, android, ui) - mặc định chạy tất cả.
#include "OffscreenPlatform.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <climits>
#include <cstdio>
#include <functional>
#include <random>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/PowerShellRunner.h"
#include "core/ToolManager.h"
#include "core/WinProcessTree.h"
#include "core/update/UpdateChecker.h"
#include "core/update/UpdateInstaller.h"
#include "tools/android/AndroidControlTool.h"
#include "tools/android/AndroidControlWindow.h"
#include "tools/android/DevicesTab.h"
#include "tools/android/WirelessPairDialog.h"
#include "tools/android/engine/AdbController.h"
#include "tools/android/engine/AdbDeviceLister.h"
#include "tools/android/engine/ScrcpyLauncher.h"
#include "tools/autoclick/AutoClickTool.h"
#include "tools/autoclick/AutoClickWindow.h"
#include "tools/autoclick/engine/ActionRunner.h"
#include "tools/autoclick/engine/StopHotkey.h"
#include "tools/autoclick/model/ActionChain.h"
#include "tools/autoclick/storage/ActionSerializer.h"
#include "tools/autoclick/widgets/ActionEditorWidget.h"
#include "tools/autoclick/widgets/ActionListWidget.h"
#include "tools/autoclick/widgets/ChainListWidget.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

namespace
{
std::mt19937 g_rng;
int g_dismissedDialogs = 0;
bool g_autoDismissDialogs = true; // tạm tắt khi kịch bản cần tự điều khiển một hộp thoại modal
QString g_onlyGroup;              // đối số [nhóm] của dòng lệnh

bool wanted(const char* group)
{
    return g_onlyGroup.isEmpty() || QString::fromLatin1(group).contains(g_onlyGroup, Qt::CaseInsensitive);
}

int rnd(int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(g_rng);
}

int rndIn(std::mt19937& rng, int lo, int hi)
{
    return std::uniform_int_distribution<int>(lo, hi)(rng);
}

/// In tên kịch bản + thời gian chạy + số kiểm tra hỏng của riêng kịch bản đó.
struct Section
{
    explicit Section(const char* sectionName) : name(sectionName), failBefore(g_fail), passBefore(g_pass)
    {
        std::printf("[%s] bat dau\n", name);
        std::fflush(stdout);
        timer.start();
    }
    ~Section()
    {
        std::printf("[%s] xong: %lld ms, +%d pass, +%d fail\n", name, static_cast<long long>(timer.elapsed()),
                    g_pass - passBefore, g_fail - failBefore);
        std::fflush(stdout);
    }
    const char* name;
    int failBefore;
    int passBefore;
    QElapsedTimer timer;
};

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    return f.write(data) == data.size();
}

QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/// Xử lý sự kiện cho tới khi `pred` đúng hoặc hết giờ.
bool spinUntil(const std::function<bool()>& pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred())
    {
        if (t.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(1);
    }
    return true;
}

void spinFor(int ms)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
}

QPushButton* buttonByText(QWidget* parent, const QString& text)
{
    for (auto* b : parent->findChildren<QPushButton*>())
        if (b->text() == text)
            return b;
    return nullptr;
}

DWORD handleCount()
{
    DWORD count = 0;
    GetProcessHandleCount(GetCurrentProcess(), &count);
    return count;
}

/// PID các tiến trình CON TRỰC TIẾP của tiến trình test có tên tệp ảnh `exeName`.
std::vector<DWORD> childProcessesNamed(const wchar_t* exeName)
{
    std::vector<DWORD> result;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ParentProcessID == GetCurrentProcessId() && _wcsicmp(entry.szExeFile, exeName) == 0)
            {
                // Bỏ qua mục đã thoát mà snapshot còn liệt kê (còn handle chưa đóng ở đâu đó).
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                if (h)
                {
                    if (WaitForSingleObject(h, 0) == WAIT_TIMEOUT)
                        result.push_back(entry.th32ProcessID);
                    CloseHandle(h);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return result;
}

/// Mảnh ghép cho chuỗi ngẫu nhiên: ký tự thường + mọi thứ hay làm hỏng việc thoát chuỗi/đường dẫn.
const QStringList& nastyPieces()
{
    static const QStringList pieces = {
        "a", "Z", "0", "9", " ", "  ", "'", "''", "\"", QString(QChar(0x2018)), QString(QChar(0x2019)),
        QString(QChar(0x201A)), QString(QChar(0x201B)), "`", "$", "$x", "$(calc)", "${env:TEMP}", "%1", "%2", "%",
        "%TEMP%", "&", "&&", "|", ";", "{", "}", "(", ")", "[", "]", "#", "@", "@'", "!", "~", "+", "=", ",", "-",
        "--", ".", "..x", "<", ">", "^", "*", "?", QString::fromUtf8("ế"), QString::fromUtf8("ữ"),
        QString::fromUtf8("Đ"), QString::fromUtf8("日本"), QString::fromUtf8("🙂"), QString(QChar(0x00A0)),
        QString(QChar(0x200B)), "\t", "Write-Output INJECTED", "'; calc; '", "\"; calc; \"",
    };
    return pieces;
}

QString randomText(std::mt19937& rng, int maxPieces, const QStringList& exclude = {})
{
    const QStringList& pieces = nastyPieces();
    QString out;
    const int n = rndIn(rng, 0, maxPieces);
    for (int i = 0; i < n; ++i)
    {
        const QString& p = pieces[rndIn(rng, 0, static_cast<int>(pieces.size()) - 1)];
        bool skip = false;
        for (const QString& e : exclude)
            if (p.contains(e))
                skip = true;
        if (!skip)
            out += p;
    }
    return out;
}

// =====================================================================================================
// 1. Logger
// =====================================================================================================

struct LogScan
{
    qint64 lines{0};
    qint64 badLines{0};      // dòng không đúng khuôn (bị cắt/đứt/trộn giữa hai luồng)
    qint64 orderErrors{0};   // số thứ tự của một luồng không liền mạch
    QHash<int, int> lastSeq; // luồng -> số thứ tự cuối cùng đã thấy
};

void scanLogFile(const QString& path, int padLength, LogScan& scan)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    static const QRegularExpression lineRe(QStringLiteral(
        "\\A\\[\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}\\.\\d{3}\\] \\[(?:DEBUG|INFO|WARN|ERROR)\\] "
        "\\[(?:Stress|AutoClick)\\] (?:T(\\d+)-(\\d+)-(x+)END|xa-bo-dem)\\z"));
    while (!f.atEnd())
    {
        QByteArray raw = f.readLine();
        if (raw.endsWith('\n'))
            raw.chop(1);
        if (raw.endsWith('\r'))
            raw.chop(1);
        ++scan.lines;
        const QRegularExpressionMatch m = lineRe.match(QString::fromUtf8(raw));
        if (!m.hasMatch())
        {
            if (scan.badLines < 3)
                std::printf("  dong hong trong %s: %.120s\n", qPrintable(QFileInfo(path).fileName()), raw.constData());
            ++scan.badLines;
            continue;
        }
        if (m.captured(1).isEmpty())
            continue; // dòng "xa-bo-dem"
        if (m.captured(3).size() != padLength)
        {
            ++scan.badLines;
            continue;
        }
        const int thread = m.captured(1).toInt();
        const int seq = m.captured(2).toInt();
        if (scan.lastSeq.contains(thread) && scan.lastSeq.value(thread) + 1 != seq)
            ++scan.orderErrors;
        scan.lastSeq[thread] = seq;
    }
}

void stressLogger()
{
    Section section("Logger: 8 luong x 40000 dong, xoay vong nhieu lan");
    constexpr int kThreads = 8;
    constexpr int kLines = 40000;
    constexpr int kPad = 150;
    const QString pad(kPad, QChar('x'));

    // Thứ tự các luồng ghi XONG dòng cuối của mình - luồng xong sớm có thể đã bị xoay vòng ra khỏi hai tệp
    // còn giữ lại, nên chỉ đòi dòng cuối của luồng xong SAU CÙNG phải còn trên đĩa.
    std::atomic<int> finishCounter{0};
    std::vector<int> finishOrder(kThreads, -1);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([t, &pad, &finishCounter, &finishOrder]() {
            const QString module = (t % 2) ? QStringLiteral("AutoClick") : QStringLiteral("Stress");
            for (int i = 0; i < kLines; ++i)
            {
                const LogLevel level = (i % 997 == 0) ? LogLevel::Warning : ((i % 5 == 0) ? LogLevel::Debug : LogLevel::Info);
                Logger::instance().log(level, module, QString("T%1-%2-%3END").arg(t).arg(i).arg(pad));
            }
            finishOrder[static_cast<size_t>(t)] = finishCounter++;
        });
    }
    for (auto& th : threads)
        th.join();
    // Info/Debug chỉ đẩy xuống đĩa mỗi 20 dòng - một dòng Warning cuối ép đẩy cả hai tệp.
    Logger::instance().warning("AutoClick", "xa-bo-dem");

    for (const QString& name : {QStringLiteral("app.log"), QStringLiteral("autoclick.log")})
    {
        const QString current = AppPaths::logFile(name);
        const QString rotated = current + ".1";
        CHECK(QFileInfo::exists(current));
        CHECK(QFileInfo::exists(rotated)); // đã xoay vòng ít nhất một lần

        // Trần 10 MB, kiểm tra mỗi 500 dòng -> mỗi tệp chỉ được vượt trần tối đa cỡ 500 dòng.
        const qint64 limit = 10 * 1024 * 1024 + 600 * 260;
        CHECK(QFileInfo(current).size() <= limit);
        CHECK(QFileInfo(rotated).size() <= limit);
        CHECK(QFileInfo(rotated).size() > 10 * 1024 * 1024);

        LogScan scan;
        scanLogFile(rotated, kPad, scan); // bản cũ trước, rồi tới bản hiện tại: số thứ tự phải nối liền
        scanLogFile(current, kPad, scan);
        std::printf("  %s: %lld dong con lai tren dia, %lld dong hong, %lld loi thu tu\n", qPrintable(name),
                    static_cast<long long>(scan.lines), static_cast<long long>(scan.badLines),
                    static_cast<long long>(scan.orderErrors));
        CHECK(scan.lines > 40000);
        CHECK(scan.badLines == 0);
        CHECK(scan.orderErrors == 0);
        // Không mất dòng ở đuôi: dòng cuối cùng của luồng ghi xong sau cùng (trong các luồng ghi vào tệp này)
        // phải có mặt; luồng nào còn dòng trên đĩa thì dòng cuối của nó cũng phải là dòng cuối nó đã ghi.
        bool tailsOk = true;
        int lastWriter = -1;
        for (int t = 0; t < kThreads; ++t)
        {
            const bool writesHere = (name == "app.log") || (t % 2 == 1);
            if (!writesHere)
            {
                if (scan.lastSeq.contains(t))
                    tailsOk = false; // autoclick.log không được chứa dòng của module khác
                continue;
            }
            if (lastWriter < 0 || finishOrder[static_cast<size_t>(t)] > finishOrder[static_cast<size_t>(lastWriter)])
                lastWriter = t;
            if (scan.lastSeq.contains(t) && scan.lastSeq.value(t) != kLines - 1)
                tailsOk = false;
        }
        CHECK(lastWriter >= 0 && scan.lastSeq.value(lastWriter, -1) == kLines - 1);
        CHECK(tailsOk);
    }
}

// =====================================================================================================
// 2. AppPaths
// =====================================================================================================

void setEnvDataDir(const QString& value)
{
    // qEnvironmentVariable() đọc môi trường CRT dạng wide - đặt bằng _wputenv_s để đường dẫn có dấu đi
    // qua nguyên vẹn (qputenv ghi theo codepage ANSI).
    _wputenv_s(L"ONEFORALL_DATA_DIR", value.toStdWString().c_str());
}

void stressAppPathsThreads(const QString& mainOverride)
{
    Section section("AppPaths: 8 luong doc trong khi luong chinh doi thu muc ep");
    QTemporaryDir dirA, dirB;
    const QString a = QDir(dirA.path()).absolutePath();
    const QString b = QDir(dirB.path()).absolutePath();
    AppPaths::setDataDirOverride(a);

    std::atomic_bool stop{false};
    std::atomic<qint64> calls{0}, wrong{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t)
    {
        threads.emplace_back([&, t]() {
            const QString file = QString("luong_%1.json").arg(t);
            while (!stop.load())
            {
                const QString data = AppPaths::dataDir();
                const QString profile = AppPaths::profileFile(file);
                const QString log = AppPaths::logFile("x.log");
                const QString profiles = AppPaths::profilesDir();
                const auto under = [&](const QString& p) { return p.startsWith(a + "/") || p.startsWith(b + "/"); };
                if ((data != a && data != b) || !under(profile) || !under(log) || !under(profiles) ||
                    !profile.endsWith("/profiles/" + file) || !log.endsWith("/logs/x.log"))
                {
                    ++wrong;
                }
                ++calls;
            }
        });
    }
    QElapsedTimer toggleTimer;
    toggleTimer.start();
    int toggles = 0;
    while (toggleTimer.elapsed() < 1500 || toggles < 4000)
        AppPaths::setDataDirOverride((toggles++ % 2) ? a : b);
    stop = true;
    for (auto& th : threads)
        th.join();
    std::printf("  %d lan doi thu muc ep, %lld lan goi tu 8 luong, %lld ket qua sai\n", toggles,
                static_cast<long long>(calls.load()), static_cast<long long>(wrong.load()));
    CHECK(calls.load() > 1000);
    CHECK(wrong.load() == 0);
    AppPaths::setDataDirOverride(mainOverride);
}

void stressAppPathsMigration(const QString& mainOverride)
{
    Section section("AppPaths: di tru + removeLegacyProfileCopies (bien moi truong, 8.3, junction, tuong doi)");
    QTemporaryDir root;
    CHECK(root.isValid());
    const QString oldCwd = QDir::currentPath();
    const QByteArray oldEnv = qgetenv("ONEFORALL_DATA_DIR");
    AppPaths::setDataDirOverride(QString()); // bỏ ép: đi đúng nhánh biến môi trường + di trú như bản phát hành

    const QString rootPath = QDir(root.path()).absolutePath();
    // Tên có dấu, dấu cách, '&', '%1', dấu nháy cong - đúng kiểu tên hồ sơ người dùng Windows hay gặp.
    const QString odd = QString::fromUtf8("dữ liệu & %1 ") + QChar(0x2019) + QString::fromUtf8("thử") + QChar(0x2019);

    // ---- M1: tệp nguồn LỚN, một luồng khác đọc tệp đích ngay trong lúc đang chép ----
    {
        const QString dataDir = rootPath + "/m1/" + odd;
        const QString cwd = rootPath + "/m1/cwd " + odd;
        CHECK(QDir().mkpath(cwd + "/profiles"));
        const QString fileName = QString::fromUtf8("core_stress hồ sơ lớn & %2.bin");
        const QString source = cwd + "/profiles/" + fileName;
        constexpr qint64 kSize = 96ll * 1024 * 1024;
        {
            QFile f(source);
            CHECK(f.open(QIODevice::WriteOnly));
            QByteArray block(1024 * 1024, '\0');
            for (int i = 0; i < block.size(); ++i)
                block[i] = static_cast<char>((i * 31 + 7) & 0xFF);
            for (qint64 written = 0; written < kSize; written += block.size())
            {
                block[0] = static_cast<char>(written >> 20);
                f.write(block);
            }
        }
        CHECK(QFileInfo(source).size() == kSize);

        setEnvDataDir(QDir::toNativeSeparators(dataDir));
        CHECK(QDir::setCurrent(cwd));
        const QString target = dataDir + "/profiles/" + fileName;

        std::atomic_bool stop{false};
        std::atomic<int> sawPartial{0}, sawFull{0}, openFailed{0};
        std::thread reader([&]() {
            while (!stop.load())
            {
                if (!QFileInfo::exists(target))
                    continue;
                QFile f(target);
                if (!f.open(QIODevice::ReadOnly))
                {
                    ++openFailed;
                    continue;
                }
                if (f.size() == kSize)
                    ++sawFull;
                else
                    ++sawPartial;
            }
        });

        QElapsedTimer copyTimer;
        copyTimer.start();
        const QString returned = AppPaths::profileFile(fileName);
        const qint64 copyMs = copyTimer.elapsed();
        stop = true;
        reader.join();

        std::printf("  chep 96 MB: %lld ms; luong doc: %d lan thay du, %d lan thay THIEU, %d lan khong mo duoc\n",
                    static_cast<long long>(copyMs), sawFull.load(), sawPartial.load(), openFailed.load());
        CHECK(QDir::cleanPath(returned) == QDir::cleanPath(target));
        CHECK(QFileInfo(target).size() == kSize);
        CHECK(UpdateInstallerInternal::sha256OfFile(target) == UpdateInstallerInternal::sha256OfFile(source));
        CHECK(QFileInfo::exists(source)); // di trú KHÔNG xóa bản cũ
        // Tệp đích chỉ được "tồn tại" khi đã đủ nội dung: nơi gọi khác thấy tệp có là đọc luôn.
        CHECK(sawPartial.load() == 0);
        CHECK(openFailed.load() == 0);

        // Gọi lại: không chép lần nữa (tệp đích giữ nguyên thời điểm sửa), không để lại tệp tạm.
        const QDateTime modified = QFileInfo(target).lastModified();
        QElapsedTimer again;
        again.start();
        for (int i = 0; i < 200; ++i)
            AppPaths::profileFile(fileName);
        CHECK(again.elapsed() < 2000);
        CHECK(QFileInfo(target).lastModified() == modified);
        CHECK(QDir(dataDir + "/profiles").entryList(QDir::Files | QDir::Hidden).size() == 1);

        // Bản đang dùng đã sửa khác bản cũ: di trú không được chép đè lên nó.
        CHECK(writeFile(target, "ban moi"));
        AppPaths::profileFile(fileName);
        CHECK(readFile(target) == "ban moi");
        QDir::setCurrent(oldCwd);
    }

    // ---- M2: nhiều luồng cùng di trú MỘT tệp - đúng một bản, nội dung nguyên vẹn ----
    {
        const QString dataDir = rootPath + "/m2/data";
        const QString cwd = rootPath + "/m2/cwd";
        CHECK(QDir().mkpath(cwd + "/profiles"));
        const QByteArray content(3 * 1024 * 1024, 'k');
        CHECK(writeFile(cwd + "/profiles/core_stress_chung.json", content));
        setEnvDataDir(dataDir);
        CHECK(QDir::setCurrent(cwd));
        std::vector<std::thread> threads;
        std::atomic<int> badPath{0};
        for (int t = 0; t < 8; ++t)
            threads.emplace_back([&]() {
                for (int i = 0; i < 50; ++i)
                    if (!AppPaths::profileFile("core_stress_chung.json").endsWith("/m2/data/profiles/core_stress_chung.json"))
                        ++badPath;
            });
        for (auto& th : threads)
            th.join();
        CHECK(badPath.load() == 0);
        CHECK(readFile(dataDir + "/profiles/core_stress_chung.json") == content);
        CHECK(QDir(dataDir + "/profiles").entryList(QDir::Files | QDir::Hidden).size() == 1);
        QDir::setCurrent(oldCwd);
    }

    // Dùng chung cho M3-M6: thư mục dữ liệu và "vị trí cũ" (<CWD>/profiles) là CÙNG một thư mục vật lý,
    // chỉ khác cách viết đường dẫn -> removeLegacyProfileCopies() không được xóa bản đang dùng.
    const auto sameDirSurvives = [&](const char* label, const QString& physicalDir, const QString& envValue) {
        CHECK(QDir().mkpath(physicalDir + "/profiles"));
        const QString file = physicalDir + "/profiles/core_stress_khoa_bi_mat.json";
        CHECK(writeFile(file, "khoa dang dung"));
        CHECK(QDir::setCurrent(physicalDir));
        setEnvDataDir(envValue);
        const QString resolved = AppPaths::profileFile("core_stress_khoa_bi_mat.json");
        AppPaths::removeLegacyProfileCopies("core_stress_khoa_bi_mat.json");
        const bool survived = QFileInfo::exists(file) && readFile(file) == "khoa dang dung";
        std::printf("  %-28s ONEFORALL_DATA_DIR=%s -> %s, tep dang dung %s\n", label, qPrintable(envValue),
                    qPrintable(resolved), survived ? "con nguyen" : "DA BI XOA");
        QDir::setCurrent(oldCwd);
        return survived;
    };

    // ---- M3: đường dẫn tuyệt đối viết bình thường / khác hoa-thường / dấu gạch ngược + dấu gạch cuối ----
    CHECK(sameDirSurvives("M3a tuyet doi", rootPath + "/m3", rootPath + "/m3"));
    CHECK(sameDirSurvives("M3b hoa-thuong + gach nguoc", rootPath + "/m3",
                          QDir::toNativeSeparators(rootPath + "/m3").toUpper() + "\\"));

    // ---- M4: biến môi trường TƯƠNG ĐỐI (kiểu "chạy portable": dữ liệu nằm cạnh exe) ----
    CHECK(sameDirSurvives("M4a tuong doi '.'", rootPath + "/m4", "."));
    {
        CHECK(QDir().mkpath(rootPath + "/m4b/sub"));
        CHECK(QDir::setCurrent(rootPath + "/m4b"));
        setEnvDataDir("sub");
        // Đường dẫn dữ liệu phải TUYỆT ĐỐI: nếu không, nó đổi nghĩa mỗi khi tiến trình đổi thư mục làm việc.
        const QString first = AppPaths::dataDir();
        CHECK(QFileInfo(first).isAbsolute());
        CHECK(QDir::cleanPath(first).compare(QDir::cleanPath(rootPath + "/m4b/sub"), Qt::CaseInsensitive) == 0);
        CHECK(QFileInfo(AppPaths::profileFile("core_stress_a.json")).isAbsolute());
        CHECK(QFileInfo(AppPaths::logFile("a.log")).isAbsolute());
        QDir::setCurrent(oldCwd);
        CHECK(AppPaths::dataDir() == first); // đổi thư mục làm việc: thư mục dữ liệu KHÔNG đổi theo
    }

    // ---- M5: tên ngắn 8.3 của cùng thư mục ----
    {
        const QString longDir = rootPath + "/m5 thu muc ten dai";
        CHECK(QDir().mkpath(longDir));
        wchar_t shortBuf[MAX_PATH + 1] = {};
        const DWORD len = GetShortPathNameW(QDir::toNativeSeparators(longDir).toStdWString().c_str(), shortBuf, MAX_PATH);
        const QString shortDir = (len > 0 && len < MAX_PATH) ? QString::fromWCharArray(shortBuf, static_cast<int>(len)) : QString();
        if (!shortDir.isEmpty() && shortDir.compare(QDir::toNativeSeparators(longDir), Qt::CaseInsensitive) != 0)
            CHECK(sameDirSurvives("M5 ten ngan 8.3", longDir, shortDir));
        else
            std::printf("  M5 BO QUA: o dia nay khong sinh ten ngan 8.3\n");
    }

    // ---- M6: junction trỏ vào chính thư mục đó ----
    {
        const QString target = rootPath + "/m6 dich";
        const QString link = rootPath + "/m6 lien ket";
        CHECK(QDir().mkpath(target));
        const int rc = QProcess::execute(AppPaths::systemExecutable("cmd.exe"),
                                         {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
        if (rc == 0 && QFileInfo::exists(link))
        {
            CHECK(sameDirSurvives("M6 junction", target, link));
            QDir::setCurrent(oldCwd);
            QProcess::execute(AppPaths::systemExecutable("cmd.exe"), {"/c", "rmdir", QDir::toNativeSeparators(link)});
            CHECK(QFileInfo::exists(target)); // gỡ junction không được đụng tới thư mục đích
        }
        else
        {
            std::printf("  M6 BO QUA: khong tao duoc junction (ma %d)\n", rc);
        }
    }

    // ---- M7: vị trí cũ THẬT SỰ khác vị trí mới: chép sang, rồi removeLegacyProfileCopies xóa đúng bản cũ ----
    {
        const QString dataDir = rootPath + "/m7/data";
        const QString cwd = rootPath + "/m7/cwd";
        CHECK(QDir().mkpath(cwd + "/profiles"));
        CHECK(writeFile(cwd + "/profiles/core_stress_khoa_cu.json", "khoa cu"));
        setEnvDataDir(dataDir);
        CHECK(QDir::setCurrent(cwd));
        const QString current = AppPaths::profileFile("core_stress_khoa_cu.json");
        CHECK(readFile(current) == "khoa cu");
        CHECK(QFileInfo::exists(cwd + "/profiles/core_stress_khoa_cu.json"));
        AppPaths::removeLegacyProfileCopies("core_stress_khoa_cu.json");
        CHECK(!QFileInfo::exists(cwd + "/profiles/core_stress_khoa_cu.json"));
        CHECK(readFile(current) == "khoa cu");
        AppPaths::removeLegacyProfileCopies("core_stress_khoa_cu.json"); // gọi lại khi không còn gì để xóa
        AppPaths::removeLegacyProfileCopies("core_stress_khong_ton_tai.json");
        CHECK(readFile(current) == "khoa cu");
        QDir::setCurrent(oldCwd);
    }

    // ---- M8: thư mục dữ liệu KHÔNG tạo được (nằm dưới một TỆP) - trả đường dẫn, không crash, không treo ----
    {
        CHECK(writeFile(rootPath + "/m8_la_tep", "x"));
        CHECK(QDir().mkpath(rootPath + "/m8_cwd"));
        CHECK(QDir::setCurrent(rootPath + "/m8_cwd"));
        setEnvDataDir(rootPath + "/m8_la_tep/data");
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 200; ++i)
        {
            AppPaths::dataDir();
            AppPaths::profileFile("core_stress_x.json");
            AppPaths::logFile("x.log");
            AppPaths::removeLegacyProfileCopies("core_stress_x.json");
        }
        CHECK(t.elapsed() < 5000);
        CHECK(!QFileInfo(rootPath + "/m8_la_tep/data").exists());
        CHECK(readFile(rootPath + "/m8_la_tep") == "x");
    }

    QDir::setCurrent(oldCwd);
    if (oldEnv.isEmpty())
        _wputenv_s(L"ONEFORALL_DATA_DIR", L"");
    else
        setEnvDataDir(QString::fromLocal8Bit(oldEnv));
    AppPaths::setDataDirOverride(mainOverride);

    CHECK(AppPaths::systemExecutable("cmd.exe").endsWith("/cmd.exe", Qt::CaseInsensitive));
    CHECK(QFileInfo::exists(AppPaths::systemExecutable("cmd.exe")));
    CHECK(QFileInfo::exists(AppPaths::powershellExecutable()));
}

// =====================================================================================================
// 3. PowerShellRunner (powershell.exe THẬT, chỉ lệnh vô hại) + WinProcessTree
// =====================================================================================================

void stressPowerShellOutput()
{
    Section section("PowerShellRunner: dau ra lon (bat treo ong dan), ma thoat, thong bao loi");
    // ~8 MB stdout: nếu vòng chờ không rút ống dẫn thì powershell.exe kẹt ở lệnh ghi và ta chờ tới hết giờ.
    {
        QElapsedTimer t;
        t.start();
        bool ok = false;
        QString error;
        const QByteArray out = PowerShellRunner::run(
            "$s = 'x' * 1000; for ($i = 0; $i -lt 8000; $i++) { [Console]::Out.WriteLine($s) }", &ok, &error, 120000);
        std::printf("  stdout 8 MB: %lld ms, %lld byte, ok=%d %s\n", static_cast<long long>(t.elapsed()),
                    static_cast<long long>(out.size()), ok ? 1 : 0, qPrintable(error));
        CHECK(ok);
        CHECK(out.size() == 8000ll * 1002);
        CHECK(t.elapsed() < 90000);
    }
    // stdout + stderr xen kẽ, mỗi bên ~3 MB: cả hai ống đều phải được rút.
    {
        QElapsedTimer t;
        t.start();
        bool ok = false;
        QString error;
        const QByteArray out = PowerShellRunner::run(
            "$s = 'o' * 1000; $e = 'e' * 1000; for ($i = 0; $i -lt 3000; $i++) { [Console]::Out.WriteLine($s); "
            "[Console]::Error.WriteLine($e) }",
            &ok, &error, 120000);
        std::printf("  stdout+stderr 3+3 MB: %lld ms, %lld byte, ok=%d\n", static_cast<long long>(t.elapsed()),
                    static_cast<long long>(out.size()), ok ? 1 : 0);
        CHECK(ok);
        CHECK(out.size() == 3000ll * 1002);
        CHECK(t.elapsed() < 90000);
    }
    // Đường ống đối tượng của PowerShell (Write-Output) với 5 MB.
    {
        QElapsedTimer t;
        t.start();
        bool ok = false;
        QString error;
        const QByteArray out = PowerShellRunner::run("$s = 'y' * 500; 1..10000 | ForEach-Object { $s }", &ok, &error, 120000);
        // In đủ để chẩn đoán: ca này từng treo tới hết hạn 120 giây ở 2/4 lần chạy khi máy đang rất bận.
        std::printf("  Write-Output 5 MB: %lld ms, %lld byte, ok=%d %s\n", static_cast<long long>(t.elapsed()),
                    static_cast<long long>(out.size()), ok ? 1 : 0, qPrintable(error));
        CHECK(ok);
        CHECK(out.size() == 10000ll * 502);
    }
    // Script rỗng / chỉ khoảng trắng.
    {
        bool ok = false;
        QString error;
        CHECK(PowerShellRunner::run("", &ok, &error).isEmpty());
        CHECK(ok);
        CHECK(PowerShellRunner::run("   \n\n", &ok, &error).isEmpty());
        CHECK(ok);
    }
    // Mã thoát khác 0: ok=false, vẫn trả stdout đã in, lỗi do script tự ghi được giữ nguyên (có dấu).
    {
        bool ok = true;
        QString error;
        const QByteArray out = PowerShellRunner::run(
            QString::fromUtf8("Write-Output 'truoc'; [Console]::Error.WriteLine('lỗi tiếng Việt'); exit 3"), &ok, &error);
        CHECK(!ok);
        CHECK(out.trimmed() == "truoc");
        CHECK(error == QString::fromUtf8("lỗi tiếng Việt"));

        ok = true;
        error.clear();
        PowerShellRunner::run("exit 7", &ok, &error);
        CHECK(!ok);
        CHECK(error.contains("7"));
    }
    // Lỗi script KHÔNG tự bắt (throw / cmdlet lỗi): PowerShell ghi ra stderr dạng CLIXML - người dùng vẫn phải
    // thấy được NỘI DUNG lỗi (không phải mã XML thô, cũng không phải bị nuốt mất chỉ còn "mã lỗi 1").
    {
        bool ok = true;
        QString error;
        PowerShellRunner::run(QString::fromUtf8("throw 'đĩa đầy & <hỏng>'"), &ok, &error);
        std::printf("  loi throw     -> \"%s\"\n", qPrintable(error));
        CHECK(!ok);
        CHECK(error.contains(QString::fromUtf8("đĩa đầy & <hỏng>")));
        CHECK(!error.contains("CLIXML") && !error.contains("<Objs") && !error.contains("_x000D_") && !error.contains("<S S="));

        ok = true;
        error.clear();
        PowerShellRunner::run("Get-Item 'C:\\khong\\co\\thu-muc-nay' -ErrorAction Stop", &ok, &error);
        std::printf("  loi cmdlet    -> \"%s\"\n", qPrintable(error));
        CHECK(!ok);
        CHECK(error.contains("C:\\khong\\co\\thu-muc-nay"));
        CHECK(!error.contains("CLIXML") && !error.contains("<Objs") && !error.contains("_x000D_"));
        CHECK(error.size() < 600); // thông báo gọn, không kèm cả khối vị trí/CategoryInfo

        ok = true;
        error.clear();
        PowerShellRunner::run("this is not valid powershell ((", &ok, &error);
        std::printf("  loi cu phap   -> \"%s\"\n", qPrintable(error.left(160)));
        CHECK(!ok);
        CHECK(!error.isEmpty());
        CHECK(!error.contains("<Objs"));
    }
    // Script QUÁ DÀI cho một dòng lệnh Windows (-EncodedCommand ~2,7 ký tự cho mỗi ký tự script, trần
    // 32767): phải báo lỗi ngay, không treo, không crash.
    {
        QElapsedTimer t;
        t.start();
        bool ok = true;
        QString error;
        PowerShellRunner::run("Write-Output '" + QString(40000, QChar('a')) + "'", &ok, &error);
        std::printf("  script 40000 ky tu: %lld ms, ok=%d, loi=\"%s\"\n", static_cast<long long>(t.elapsed()), ok ? 1 : 0,
                    qPrintable(error));
        CHECK(!ok);
        CHECK(!error.isEmpty());
        CHECK(t.elapsed() < 8000);
    }
    // Hết giờ: trả về gần đúng hạn, tiến trình con bị dừng.
    {
        QElapsedTimer t;
        t.start();
        bool ok = true;
        QString error;
        PowerShellRunner::run("Start-Sleep -Seconds 60", &ok, &error, 600);
        CHECK(!ok);
        CHECK(error.contains(QString::fromUtf8("Hết thời gian")));
        CHECK(t.elapsed() < 20000); // 600 ms + thời gian khởi động/dừng powershell.exe (máy bận có thể vài giây)
        ok = true;
        PowerShellRunner::run("Start-Sleep -Seconds 60", &ok, &error, 0); // hạn 0/âm: không được chờ vô hạn
        CHECK(!ok);
        ok = true;
        PowerShellRunner::run("Start-Sleep -Seconds 60", &ok, &error, -5);
        CHECK(!ok);
        CHECK(t.elapsed() < 60000);
    }
}

void stressPowerShellQuoting()
{
    Section section("PowerShellRunner: quoteLiteral -> echo lai qua PowerShell that");
    int mismatches = 0, total = 0;
    for (int round = 0; round < 6; ++round)
    {
        QStringList values;
        // Bộ cố định: mọi kiểu dấu nháy, cố tình chèn lệnh, Unicode ngoài BMP.
        values << "" << "abc" << "O'Brien" << "'" << "''" << "'''" << QString(QChar(0x2019)) << QString(QChar(0x2018)) + "x" + QChar(0x2019)
               << QString("a") + QChar(0x201A) + QChar(0x201B) + "b" << QString("'") + QChar(0x2019) + "'" + QChar(0x2018)
               << "'; Write-Output INJECTED; '" << QString(QChar(0x2019)) + "; Write-Output INJECTED; " + QChar(0x2019)
               << "$(Write-Output INJECTED)" << "`$x `n `t" << "$env:USERNAME" << "\"; Write-Output INJECTED; \""
               << QString::fromUtf8("Hà Lan – Nước Đức ‘’ “” 日本語 🙂🚀") << "@' x '@" << "#comment" << "a;b|c&d>e<f";
        for (int i = 0; i < 40; ++i)
            values << randomText(g_rng, 12);

        QString script;
        for (int i = 0; i < values.size(); ++i)
        {
            // Bọc giữa hai mốc để khoảng trắng đầu/cuối không bị lẫn với việc xuống dòng của PowerShell.
            const QString quoted = "'S<" + PowerShellRunner::quoteLiteral(values[i]) + ">E'";
            script += (i % 2) ? ("Write-Output " + quoted + "\n") : ("[Console]::Out.WriteLine(" + quoted + ")\n");
        }
        bool ok = false;
        QString error;
        const QByteArray out = PowerShellRunner::run(script, &ok, &error, 60000);
        CHECK(ok);
        QStringList lines = QString::fromUtf8(out).split("\r\n");
        if (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
        CHECK(lines.size() == values.size()); // không dòng thừa: không lệnh nào bị chèn chạy được
        for (int i = 0; i < values.size() && i < lines.size(); ++i)
        {
            ++total;
            if (lines[i] != "S<" + values[i] + ">E")
            {
                if (mismatches < 5)
                    std::printf("  LECH: gui \"%s\" nhan \"%s\"\n", qPrintable(values[i]), qPrintable(lines[i]));
                ++mismatches;
            }
        }
    }
    std::printf("  %d chuoi di ve, %d chuoi lech\n", total, mismatches);
    CHECK(total >= 300);
    CHECK(mismatches == 0);
}

void stressPowerShellConcurrency()
{
    Section section("PowerShellRunner: 30 lan song song + 30 lan huy ngau nhien, khong con tien trinh mo coi");
    const DWORD handlesBefore = handleCount();

    // ---- 30 lần chạy song song từ 6 luồng ----
    {
        std::atomic<int> good{0}, bad{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 6; ++t)
        {
            threads.emplace_back([t, &good, &bad]() {
                for (int i = 0; i < 5; ++i)
                {
                    const QString token = QString::fromUtf8("luồng-%1-lần-%2-’ok’").arg(t).arg(i);
                    bool ok = false;
                    QString error;
                    const QByteArray out = PowerShellRunner::run(
                        "Write-Output '" + PowerShellRunner::quoteLiteral(token) + "'", &ok, &error, 120000);
                    if (ok && QString::fromUtf8(out).trimmed() == token)
                        ++good;
                    else
                        ++bad;
                }
            });
        }
        for (auto& th : threads)
            th.join();
        std::printf("  song song: %d dung, %d sai\n", good.load(), bad.load());
        CHECK(good.load() == 30);
        CHECK(bad.load() == 0);
    }

    // ---- 30 lần hủy ở thời điểm ngẫu nhiên (kể cả hủy TRƯỚC khi bắt đầu) ----
    {
        std::atomic<int> canceledOk{0}, wrong{0}, tooSlow{0};
        std::vector<std::thread> threads;
        for (int t = 0; t < 6; ++t)
        {
            const unsigned seed = static_cast<unsigned>(g_rng());
            threads.emplace_back([seed, &canceledOk, &wrong, &tooSlow]() {
                std::mt19937 rng(seed);
                for (int i = 0; i < 5; ++i)
                {
                    const int delayMs = (i == 0) ? 0 : rndIn(rng, 0, 2500);
                    std::atomic_bool cancel{delayMs == 0};
                    std::thread canceler([&cancel, delayMs]() {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                        cancel = true;
                    });
                    QElapsedTimer timer;
                    timer.start();
                    bool ok = true;
                    QString error;
                    PowerShellRunner::runCancelable("Start-Sleep -Seconds 120", &cancel, &ok, &error, 100000);
                    const qint64 elapsed = timer.elapsed();
                    canceler.join();
                    if (!ok && error == QString::fromUtf8("Đã hủy."))
                        ++canceledOk;
                    else
                        ++wrong;
                    if (elapsed > delayMs + 25000) // khởi động powershell (máy bận: vài giây) + 200ms/nhịp + kill
                    {
                        ++tooSlow;
                        std::printf("  QUA CHAM: hen huy sau %d ms, runCancelable tra ve sau %lld ms\n", delayMs,
                                    static_cast<long long>(elapsed));
                    }
                }
            });
        }
        for (auto& th : threads)
            th.join();
        std::printf("  huy: %d dung, %d sai, %d qua cham\n", canceledOk.load(), wrong.load(), tooSlow.load());
        CHECK(canceledOk.load() == 30);
        CHECK(wrong.load() == 0);
        CHECK(tooSlow.load() == 0);
    }

    // Không còn powershell.exe con nào của tiến trình test sống sót (chờ tối đa 5 giây cho Windows dọn).
    std::vector<DWORD> leftovers;
    QElapsedTimer waitTimer;
    waitTimer.start();
    do
    {
        leftovers = childProcessesNamed(L"powershell.exe");
        if (leftovers.empty())
            break;
        QThread::msleep(100);
    } while (waitTimer.elapsed() < 5000);
    std::printf("  powershell.exe con con song: %d\n", static_cast<int>(leftovers.size()));
    CHECK(leftovers.empty());
    // Chẩn đoán cho tiến trình sót: chờ nó thoát rồi đọc mã thoát. 0xF291 = đúng là bị QProcess::kill() dừng
    // (TerminateProcess có hiệu lực nhưng TRỄ - hệ thống giữ tiến trình lại); 0 = lệnh dừng không hề có tác
    // dụng, tiến trình tự chạy hết Start-Sleep 120 giây.
    for (const DWORD pid : leftovers)
    {
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
        if (!h)
            continue;
        QElapsedTimer extra;
        extra.start();
        const DWORD waited = WaitForSingleObject(h, 150000);
        DWORD exitCode = 0;
        GetExitCodeProcess(h, &exitCode);
        std::printf("  tien trinh sot PID %lu: %s sau khi cho them %lld ms, ma thoat 0x%lX\n", static_cast<unsigned long>(pid),
                    waited == WAIT_OBJECT_0 ? "da thoat" : "VAN CON SONG", static_cast<long long>(extra.elapsed()),
                    static_cast<unsigned long>(exitCode));
        CloseHandle(h);
    }

    const DWORD handlesAfter = handleCount();
    std::printf("  handle cua tien trinh test: truoc %lu, sau %lu\n", static_cast<unsigned long>(handlesBefore),
                static_cast<unsigned long>(handlesAfter));
    CHECK(handlesAfter < handlesBefore + 200);
}

void stressWinProcessTree()
{
    Section section("WinProcessTree: quet 400 lan (ro handle), tim + dung dung tien trinh con");
    const qint64 self = QCoreApplication::applicationPid();

    QProcess child;
    child.setProgram(AppPaths::powershellExecutable());
    child.setArguments({"-NoProfile", "-NonInteractive", "-Command", "Start-Sleep -Seconds 120"});
    child.start();
    CHECK(child.waitForStarted(10000));
    const qint64 childPid = child.processId();

    const std::vector<qint64> descendants = WinProcessTree::findDescendants(self);
    bool found = false, containsSelf = false;
    for (qint64 pid : descendants)
    {
        if (pid == childPid) found = true;
        if (pid == self) containsSelf = true;
    }
    CHECK(found);
    CHECK(!containsSelf);
    CHECK(WinProcessTree::findDescendants(0).empty());
    CHECK(WinProcessTree::findDescendants(-5).empty());
    CHECK(WinProcessTree::findDescendants(0x7FFFFFF0).empty()); // PID không tồn tại

    const DWORD handlesBefore = handleCount();
    QElapsedTimer t;
    t.start();
    for (int i = 0; i < 400; ++i)
    {
        WinProcessTree::findDescendants(self);
        if (i % 10 == 0)
            WinProcessTree::terminateProcessList({0, -1, 0x7FFFFFF0}); // PID vô nghĩa: bỏ qua lặng lẽ
    }
    const DWORD handlesAfter = handleCount();
    std::printf("  400 lan quet: %lld ms; handle truoc %lu, sau %lu\n", static_cast<long long>(t.elapsed()),
                static_cast<unsigned long>(handlesBefore), static_cast<unsigned long>(handlesAfter));
    CHECK(handlesAfter <= handlesBefore + 16);

    // Dừng đúng tiến trình con vừa tạo (và hậu duệ của riêng nó), không đụng tới thứ gì khác.
    WinProcessTree::terminateDescendants(childPid);
    WinProcessTree::terminateProcessList({childPid});
    CHECK(child.waitForFinished(5000));
    CHECK(child.state() == QProcess::NotRunning);
    WinProcessTree::terminateDescendants(childPid); // gọi lại khi tiến trình gốc đã thoát
    WinProcessTree::terminateProcessList({});
    WinProcessTree::terminateProcessList({childPid}); // PID đã thoát
}

// =====================================================================================================
// 4. Tự cập nhật
// =====================================================================================================

int sign(int v)
{
    return v < 0 ? -1 : (v > 0 ? 1 : 0);
}

void fuzzCompareVersions()
{
    Section section("UpdateChecker::compareVersions: 60000 cap ngau nhien");
    using UpdateCheckerInternal::compareVersions;
    const QStringList junk = {"", "v", "V", "vv", ".", "..", "...", "-", "+", " ", "\t", "x", "1x", "-1", "+1", "0x10",
                              "99999999999999999999", "2147483647", "2147483648", "007", QString::fromUtf8("١٢"),
                              QString::fromUtf8("１"), "1e3", "1,5", "beta", "rc1", "1-rc", "\n"};
    const auto randomVersion = [&]() -> QString {
        QString s;
        if (rnd(0, 5) == 0) s += rnd(0, 1) ? "v" : "V";
        const int parts = rnd(0, 5);
        for (int i = 0; i < parts; ++i)
        {
            if (i) s += ".";
            s += (rnd(0, 6) == 0) ? junk[rnd(0, static_cast<int>(junk.size()) - 1)] : QString::number(rnd(0, 30));
            if (rnd(0, 12) == 0) s += junk[rnd(0, static_cast<int>(junk.size()) - 1)];
        }
        if (rnd(0, 10) == 0) s = " " + s + " ";
        return s;
    };

    int asym = 0, refl = 0, trans = 0, numeric = 0;
    for (int i = 0; i < 60000; ++i)
    {
        const QString a = randomVersion(), b = randomVersion(), c = randomVersion();
        const int ab = sign(compareVersions(a, b)), ba = sign(compareVersions(b, a));
        if (ab != -ba) ++asym;
        if (compareVersions(a, a) != 0) ++refl;
        const int bc = sign(compareVersions(b, c)), ac = sign(compareVersions(a, c));
        if (ab <= 0 && bc <= 0 && ac > 0) ++trans; // a<=b, b<=c mà a>c
        if (ab >= 0 && bc >= 0 && ac < 0) ++trans;

        // Bộ ba số thuần: phải đúng thứ tự số học.
        const int x[3] = {rnd(0, 40), rnd(0, 40), rnd(0, 40)}, y[3] = {rnd(0, 40), rnd(0, 40), rnd(0, 40)};
        int expected = 0;
        for (int k = 0; k < 3 && expected == 0; ++k)
            expected = sign(x[k] - y[k]);
        const QString xs = QString("%1.%2.%3").arg(x[0]).arg(x[1]).arg(x[2]);
        const QString ys = QString("v%1.%2.%3").arg(y[0]).arg(y[1]).arg(y[2]);
        if (sign(compareVersions(xs, ys)) != expected) ++numeric;
    }
    CHECK(asym == 0);
    CHECK(refl == 0);
    CHECK(trans == 0);
    CHECK(numeric == 0);
    // Thành phần quá lớn để là số nguyên coi là 0 (không tràn thành số âm, không thành "rất mới").
    CHECK(compareVersions("1.99999999999999999999.0", "1.0.0") == 0);
    CHECK(compareVersions("4294967297.5.0", "0.5.0") == 0);
}

void fuzzParseLatestRelease()
{
    Section section("UpdateChecker::parseLatestRelease: 30000 JSON ngau nhien/bi bien dang");
    using UpdateCheckerInternal::parseLatestRelease;
    static const QRegularExpression hex64(QStringLiteral("\\A[0-9a-f]{64}\\z"));

    const QStringList names = {"OneForAll_Setup.exe", "oneforall_setup.EXE", "OneForAll_Setup.msi", "ONEFORALL_SETUP.MSI",
                               "OneForAll_Setup.exe.sha256", "XOneForAll_Setup.exe", "OneForAll_Setup_exe.zip", "", "source.zip",
                               " OneForAll_Setup.exe", "OneForAll_Setup.exe "};
    const QStringList digests = {"sha256:" + QString(64, 'a'), "SHA256:" + QString(64, 'B'), "sha256:" + QString(63, 'a'),
                                 "sha256:" + QString(65, 'a'), "sha256:" + QString(64, 'g'), "md5:abcd", "", "sha256:",
                                 " sha256: " + QString(64, '0') + " ", "sha512:" + QString(128, 'a')};
    const auto randomValue = [&]() -> QJsonValue {
        switch (rnd(0, 11))
        {
            case 0: return QJsonValue(rnd(-1000, 1000000));
            case 1: return QJsonValue(1e300);
            case 2: return QJsonValue(-1e300);
            case 3: return QJsonValue(9007199254740993.0);
            case 4: return QJsonValue(1.5);
            case 5: return QJsonValue(QString("123"));
            case 6: return QJsonValue(true);
            case 7: return QJsonValue(QJsonValue::Null);
            case 8: return QJsonValue(QJsonArray{1, 2});
            case 9: return QJsonValue(QJsonObject{{"a", 1}});
            case 10: return QJsonValue(9.3e18);
            default: return QJsonValue(static_cast<double>(rnd(0, 2000000000)) * 4.0);
        }
    };

    int violations = 0, accepted = 0;
    const auto check = [&](const QByteArray& json) {
        const UpdateInfo info = parseLatestRelease(json);
        bool bad = false;
        if (!info.version.isEmpty())
        {
            ++accepted;
            bad = info.downloadUrl.isEmpty(); // "có bản mới" thì phải có thứ để tải
        }
        if (!info.sha256.isEmpty() && !hex64.match(info.sha256).hasMatch()) bad = true;
        if (!info.msiSha256.isEmpty() && !hex64.match(info.msiSha256).hasMatch()) bad = true;
        if (info.downloadSize < 0 || info.msiSize < 0) bad = true; // kích thước không bao giờ âm
        if (bad)
        {
            if (violations < 5)
                std::printf("  VI PHAM: size=%lld msiSize=%lld ver=\"%s\" json=%.200s\n", static_cast<long long>(info.downloadSize),
                            static_cast<long long>(info.msiSize), qPrintable(info.version), json.constData());
            ++violations;
        }
    };

    for (int i = 0; i < 30000; ++i)
    {
        QJsonObject root;
        if (rnd(0, 9) != 0)
            root["tag_name"] = (rnd(0, 9) == 0) ? randomValue() : QJsonValue(QString("v%1.%2.%3").arg(rnd(0, 3)).arg(rnd(0, 30)).arg(rnd(0, 9)));
        if (rnd(0, 3) != 0)
            root["body"] = (rnd(0, 9) == 0) ? randomValue() : QJsonValue(randomText(g_rng, 20));
        QJsonArray assets;
        const int count = rnd(0, 5);
        for (int a = 0; a < count; ++a)
        {
            if (rnd(0, 14) == 0)
            {
                assets.append(randomValue()); // phần tử không phải đối tượng
                continue;
            }
            QJsonObject asset;
            asset["name"] = (rnd(0, 14) == 0) ? randomValue() : QJsonValue(names[rnd(0, static_cast<int>(names.size()) - 1)]);
            asset["browser_download_url"] = (rnd(0, 14) == 0) ? randomValue() : QJsonValue("https://github.com/x/y/" + randomText(g_rng, 4));
            asset["size"] = randomValue();
            if (rnd(0, 2))
                asset["digest"] = (rnd(0, 9) == 0) ? randomValue() : QJsonValue(digests[rnd(0, static_cast<int>(digests.size()) - 1)]);
            assets.append(asset);
        }
        root["assets"] = (rnd(0, 19) == 0) ? randomValue() : QJsonValue(assets);
        QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
        check(json);

        // Biến dạng mức byte: cắt cụt / lật byte / chèn rác.
        if (!json.isEmpty())
        {
            QByteArray mutated = json;
            switch (rnd(0, 3))
            {
                case 0: mutated.truncate(rnd(0, static_cast<int>(mutated.size()))); break;
                case 1: mutated[rnd(0, static_cast<int>(mutated.size()) - 1)] = static_cast<char>(rnd(0, 255)); break;
                case 2: mutated.insert(rnd(0, static_cast<int>(mutated.size())), QByteArray(rnd(1, 8), static_cast<char>(rnd(0, 255)))); break;
                default: mutated = mutated + mutated; break;
            }
            check(mutated);
        }
    }
    std::printf("  %d JSON duoc nhan la ban phat hanh hop le, %d vi pham bat bien\n", accepted, violations);
    CHECK(accepted > 1000);
    CHECK(violations == 0);

    // JSON bệnh lý: lồng rất sâu, thân mô tả 4 MB, 50000 asset - không crash, không treo.
    {
        QElapsedTimer t;
        t.start();
        CHECK(parseLatestRelease(QByteArray(200000, '[')).version.isEmpty());
        CHECK(parseLatestRelease(QByteArray(200000, '{')).version.isEmpty());
        QJsonObject root;
        root["tag_name"] = "v9.9.9";
        root["body"] = QString(4 * 1024 * 1024, QChar('b'));
        QJsonArray assets;
        for (int i = 0; i < 50000; ++i)
            assets.append(QJsonObject{{"name", QString("rac_%1.bin").arg(i)}, {"browser_download_url", "https://github.com/x"}, {"size", i}});
        assets.append(QJsonObject{{"name", "OneForAll_Setup.exe"}, {"browser_download_url", "https://github.com/x/y/OneForAll_Setup.exe"}, {"size", 5}});
        root["assets"] = assets;
        const UpdateInfo info = parseLatestRelease(QJsonDocument(root).toJson(QJsonDocument::Compact));
        CHECK(info.version == "9.9.9");
        CHECK(info.downloadSize == 5);
        CHECK(info.releaseNotes.size() == 4 * 1024 * 1024);
        CHECK(t.elapsed() < 20000);
    }
}

void fuzzTrustedUrl()
{
    Section section("UpdateChecker::isTrustedDownloadUrl: bo lach co dinh + 60000 URL bien dang");
    using UpdateCheckerInternal::isTrustedDownloadUrl;
    const QString good = "https://github.com/DinhDuyen2810/ALLinONE/releases/download/v1.19.0/OneForAll_Setup.exe";
    CHECK(isTrustedDownloadUrl(good));
    CHECK(isTrustedDownloadUrl("HTTPS://GitHub.COM/a/b"));
    CHECK(isTrustedDownloadUrl("https://github.com:443/a/b")); // cổng mặc định viết tường minh

    // Mỗi chuỗi dưới đây hoặc KHÔNG tới github.com, hoặc không phải https, hoặc mang userinfo/cổng lạ.
    const QStringList mustReject = {
        "https://github.com.evil.example/a.exe",
        "https://github.com@evil.example/a.exe",
        "https://github.com:443@evil.example/a.exe",
        "https://user:pass@github.com/a.exe",
        "https://evil.example\\@github.com/a.exe",
        "https://evil.example%2f@github.com/a.exe",
        "https://github.com\\.evil.example/a.exe",
        "https://github.com%2eevil.example/a.exe",
        "https://github.com%00.evil.example/a.exe",
        "https://github.com.evil.example%2f..%2fgithub.com/a.exe",
        "https://evil.example/github.com/a.exe",
        "https://evil.example/?u=https://github.com/a.exe",
        "https://evil.example#https://github.com/a.exe",
        "https://evil.example#@github.com/a.exe",
        "https://github.com./a.exe",
        "https://github.com../a.exe",
        "https://.github.com/a.exe",
        "https://www.github.com/a.exe",
        "https://raw.githubusercontent.com/a.exe",
        "https://github.com:8443/a.exe",
        "https://github.com:80/a.exe",
        "https://github.com:0/a.exe",
        "https://github.com:65536/a.exe",
        "https://github.com:-1/a.exe",
        "https://github.com:44 3/a.exe",
        "https://github.com :443/a.exe",
        "https://github.com /a.exe",
        "https://github .com/a.exe",
        "https://github.com\t/a.exe",
        "https://github.com\n.evil.example/a.exe",
        " https://github.com/a.exe",
        "\thttps://github.com/a.exe",
        "https ://github.com/a.exe",
        "https:///github.com/a.exe",
        "https:/github.com/a.exe",
        "https:github.com/a.exe",
        "https://",
        "https://:443/a.exe",
        "http://github.com/a.exe",
        "httpss://github.com/a.exe",
        "ftp://github.com/a.exe",
        "file://github.com/a.exe",
        "//github.com/a.exe",
        "github.com/a.exe",
        "javascript:alert(1)//github.com",
        QString::fromUtf8("https://g\u0131thub.com/a.exe"),    // i không chấm
        QString::fromUtf8("https://github.c\u043Em/a.exe"),    // chữ o Cyrillic
        QString::fromUtf8("https://gith\u00FCb.com/a.exe"),
        "https://xn--github-.com/a.exe",
        "https://github.com.xn--p1ai/a.exe",
        "https://[::1]/github.com/a.exe",
        "https://127.0.0.1/github.com/a.exe",
        "",
        " ",
    };
    int bypass = 0;
    for (const QString& url : mustReject)
    {
        if (isTrustedDownloadUrl(url))
        {
            std::printf("  LACH DUOC: \"%s\" (host that: \"%s\", cong %d)\n", qPrintable(url), qPrintable(QUrl(url).host()), QUrl(url).port());
            ++bypass;
        }
    }
    CHECK(bypass == 0);

    // Biến dạng ngẫu nhiên. Bất biến: URL được nhận thì yêu cầu mạng THẬT (FileDownloader dựng QUrl ở chế
    // độ khoan dung từ đúng chuỗi đó) phải tới https://github.com cổng 443, không userinfo.
    const QStringList inserts = {"@", "\\", "/", ":", ".", "%", "#", "?", " ", "\t", "\n", QString(QChar(0)), "%2e", "%2f", "%40", "%00",
                                 "evil.example", "github.com", "443", "8443", "..", "//", QString(QChar(0x3002)), QString(QChar(0xFF0E)),
                                 QString(QChar(0xFF47)), QString(QChar(0x0131)), QString(QChar(0x212A)), "xn--", "[", "]", "user:pw@", "https://"};
    int accepted = 0, unsafe = 0;
    for (int i = 0; i < 60000; ++i)
    {
        QString url = (rnd(0, 4) == 0) ? mustReject[rnd(0, static_cast<int>(mustReject.size()) - 1)] : good.left(rnd(18, 40));
        const int edits = rnd(1, 3);
        for (int e = 0; e < edits; ++e)
        {
            const int pos = rnd(0, static_cast<int>(qMin<qsizetype>(url.size(), 30)));
            switch (rnd(0, 2))
            {
                case 0: url.insert(pos, inserts[rnd(0, static_cast<int>(inserts.size()) - 1)]); break;
                case 1: if (pos < url.size()) url.remove(pos, 1); break;
                default: if (pos < url.size()) url[pos] = inserts[rnd(0, static_cast<int>(inserts.size()) - 1)].at(0); break;
            }
        }
        if (!isTrustedDownloadUrl(url))
            continue;
        ++accepted;
        const QUrl real(url); // đúng cách FileDownloader::startNetworkRequest dựng yêu cầu
        const bool safe = real.isValid() && real.scheme().toLower() == "https" && real.host().toLower() == "github.com" &&
                          real.userInfo().isEmpty() && (real.port() == -1 || real.port() == 443);
        if (!safe)
        {
            if (unsafe < 5)
                std::printf("  KHONG AN TOAN: \"%s\" -> host \"%s\" cong %d userinfo \"%s\"\n", qPrintable(url),
                            qPrintable(real.host()), real.port(), qPrintable(real.userInfo()));
            ++unsafe;
        }
    }
    std::printf("  %d URL bien dang duoc nhan, %d cai khong an toan\n", accepted, unsafe);
    CHECK(unsafe == 0);
}

void fuzzInstallClassification()
{
    Section section("UpdateInstaller: classifyInstall 20000 ca + sha256OfFile tep lon");
    using UpdateInstallerInternal::classifyInstall;
    using K = UpdateInstaller::InstallKind;
    const auto variant = [&](const QString& dir) {
        QString v;
        for (const QChar c : dir)
            v += (rnd(0, 1) ? c.toUpper() : c.toLower());
        if (rnd(0, 1)) v = QDir::toNativeSeparators(v);
        if (rnd(0, 2) == 0) v += rnd(0, 1) ? "/" : "\\";
        if (rnd(0, 3) == 0) v = " " + v + " ";
        return v;
    };
    int wrong = 0;
    for (int i = 0; i < 20000; ++i)
    {
        const QString base = "C:/Users/" + QString("nguoi dung %1").arg(rnd(0, 99)) + "/AppData/Local";
        const QString msiDir = base + "/One for ALL";
        const QString innoDir = base + "/Programs/One for ALL";
        if (classifyInstall(true, true, variant(msiDir), variant(msiDir), variant(innoDir)) != K::Msi) ++wrong;
        if (classifyInstall(true, true, variant(innoDir), variant(msiDir), variant(innoDir)) != K::InnoSetup) ++wrong;
        if (classifyInstall(false, true, variant(msiDir), variant(msiDir), variant(innoDir)) != K::Portable) ++wrong;
        if (classifyInstall(true, false, variant(innoDir), variant(msiDir), variant(innoDir)) != K::Portable) ++wrong;
        if (classifyInstall(true, true, variant(base + "/Khac"), variant(msiDir), variant(innoDir)) != K::Portable) ++wrong;
        if (classifyInstall(true, true, variant(msiDir + " 2"), variant(msiDir), variant(innoDir)) != K::Portable) ++wrong;
        if (classifyInstall(true, true, " ", variant(msiDir), "") != K::Portable) ++wrong;
        if (classifyInstall(true, true, variant(msiDir), "", "") != K::Portable) ++wrong;
    }
    CHECK(wrong == 0);

    const QString path = AppPaths::dataDir() + "/sha_lon.bin";
    QCryptographicHash expected(QCryptographicHash::Sha256);
    {
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly));
        QByteArray block(1024 * 1024, '\0');
        for (int i = 0; i < 80; ++i)
        {
            block.fill(static_cast<char>(i * 3 + 1));
            f.write(block);
            expected.addData(block);
        }
    }
    CHECK(UpdateInstallerInternal::sha256OfFile(path) == QString::fromLatin1(expected.result().toHex()));
    CHECK(UpdateInstallerInternal::sha256OfFile(AppPaths::dataDir()).isEmpty()); // thư mục, không phải tệp
    QFile::remove(path);
}

void fuzzHelperScript()
{
    Section section("UpdateInstaller::buildHelperScript: 400 duong dan ngau nhien -> PowerShell that PHAN TICH CU PHAP");
    using UpdateInstallerInternal::buildHelperScript;
    using K = UpdateInstaller::InstallKind;

    struct Case
    {
        K kind;
        QString installer;
        QString appExe;
        qint64 pid;
        QString script;
    };
    std::vector<Case> cases;
    const auto randomDir = [&]() {
        QString path = "C:";
        const int depth = rnd(1, 4);
        for (int d = 0; d < depth; ++d)
        {
            QString component = randomText(g_rng, 6, {"/", "\\", "\t"});
            if (rnd(0, 30) == 0) component += "\n" + randomText(g_rng, 2, {"/", "\\", "\t"}); // xuống dòng giữa đường dẫn
            if (component.trimmed().isEmpty() || component.startsWith('.') || component.endsWith('.') || component.endsWith(' '))
                component = "x" + component + "x";
            path += "/" + component;
        }
        return path;
    };
    for (int i = 0; i < 400; ++i)
    {
        Case c;
        c.kind = (i % 2) ? K::Msi : K::InnoSetup;
        c.installer = randomDir() + ((i % 2) ? "/OneForAll_Setup.msi" : "/OneForAll_Setup.exe");
        c.appExe = randomDir() + "/OneForAll.exe";
        c.pid = (i % 7 == 0) ? 0 : static_cast<qint64>(rnd(1, 2000000000));
        c.script = buildHelperScript(c.kind, c.installer, c.appExe, c.pid);
        cases.push_back(c);
    }
    // Vài ca cố định dễ đọc khi hỏng.
    {
        const QString curly(QChar(0x2019));
        const QString nasty = "C:/Users/O'Bri" + curly + "en & Co %1 %TEMP% $(calc) `x` ;{}/Te" + QString(QChar(0x2018)) + "mp";
        cases.push_back({K::InnoSetup, nasty + "/OneForAll_Setup.exe", nasty + "/app/OneForAll.exe", 4321, {}});
        cases.push_back({K::Msi, nasty + "/OneForAll_Setup.msi", nasty + "/app/OneForAll.exe", 4321, {}});
        cases.push_back({K::InnoSetup, "C:/a'; Start-Process calc; '/s.exe", "C:/b" + curly + "; Start-Process calc; " + curly + "/OneForAll.exe", 1, {}});
        for (size_t i = cases.size() - 3; i < cases.size(); ++i)
            cases[i].script = buildHelperScript(cases[i].kind, cases[i].installer, cases[i].appExe, cases[i].pid);
    }

    // Mỗi dòng = Base64(UTF-16LE) của một script - đúng giá trị sẽ truyền cho -EncodedCommand.
    const QString listPath = AppPaths::dataDir() + "/helper_scripts.txt";
    {
        QByteArray lines;
        for (const Case& c : cases)
            lines += PowerShellRunner::encodedCommand(c.script).toLatin1() + "\r\n";
        CHECK(writeFile(listPath, lines));
    }

    // CHỈ phân tích cú pháp (Parser::ParseInput) - không thực thi một dòng nào của script trợ giúp.
    const QString analyzer =
        "$ErrorActionPreference = 'Stop'\n"
        "$L = [System.Management.Automation.Language.Parser]\n"
        "foreach ($line in [IO.File]::ReadAllLines('" + PowerShellRunner::quoteLiteral(QDir::toNativeSeparators(listPath)) + "')) {\n"
        "  if (-not $line) { continue }\n"
        "  $src = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String($line))\n"
        "  $tokens = $null; $errs = $null\n"
        "  $ast = $L::ParseInput($src, [ref]$tokens, [ref]$errs)\n"
        "  $cmds = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.CommandAst] }, $true) | ForEach-Object { $_.GetCommandName() })\n"
        "  $strs = @($ast.FindAll({ param($n) $n -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | "
        "ForEach-Object { [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($_.Value)) })\n"
        "  [Console]::Out.WriteLine(('{0}|{1}|{2}|{3}' -f $errs.Count, $ast.EndBlock.Statements.Count, ($cmds -join ','), ($strs -join ',')))\n"
        "}\n";
    bool ok = false;
    QString error;
    const QByteArray out = PowerShellRunner::run(analyzer, &ok, &error, 180000);
    if (!ok)
        std::printf("  trinh phan tich loi: %s\n", qPrintable(error));
    CHECK(ok);
    QStringList lines = QString::fromUtf8(out).split("\r\n", Qt::SkipEmptyParts);
    CHECK(lines.size() == static_cast<qsizetype>(cases.size()));

    const auto b64 = [](const QString& s) { return QString::fromLatin1(s.toUtf8().toBase64()); };
    int syntaxErrors = 0, wrongStatements = 0, wrongCommands = 0, wrongStrings = 0;
    for (size_t i = 0; i < cases.size() && static_cast<qsizetype>(i) < lines.size(); ++i)
    {
        const Case& c = cases[i];
        const QStringList fields = lines[static_cast<qsizetype>(i)].split('|');
        if (fields.size() != 4)
        {
            ++syntaxErrors;
            continue;
        }
        bool caseBad = false;
        if (fields[0] != "0") { ++syntaxErrors; caseBad = true; }
        // Đúng 5 câu lệnh: gán $ErrorActionPreference, Wait-Process, Start-Sleep, $p = Start-Process, Start-Process.
        if (fields[1] != "5") { ++wrongStatements; caseBad = true; }
        if (fields[2] != "Wait-Process,Start-Sleep,Start-Process,Start-Process") { ++wrongCommands; caseBad = true; }

        // Mọi đường dẫn phải nằm NGUYÊN VẸN trong đúng một hằng chuỗi.
        const QStringList strings = fields[3].split(',');
        const QString appNative = QDir::toNativeSeparators(c.appExe);
        const QString appDirNative = QDir::toNativeSeparators(QFileInfo(c.appExe).absolutePath());
        const QString installerNative = QDir::toNativeSeparators(c.installer);
        bool stringsOk = strings.contains(b64(appNative)) && strings.contains(b64(appDirNative));
        if (c.kind == K::Msi)
            stringsOk = stringsOk && strings.contains(b64("\"" + installerNative + "\"")) &&
                        strings.contains(b64(QDir::toNativeSeparators(AppPaths::systemExecutable("msiexec.exe"))));
        else
            stringsOk = stringsOk && strings.contains(b64(installerNative));
        if (!stringsOk) { ++wrongStrings; caseBad = true; }
        if (caseBad && syntaxErrors + wrongStatements + wrongCommands + wrongStrings <= 4)
            std::printf("  CA HONG #%d: %s\n    script: %s\n", static_cast<int>(i), qPrintable(lines[static_cast<qsizetype>(i)].left(120)),
                        qPrintable(QString(c.script).replace('\n', " <NL> ")));
        if (!c.script.contains(QString("Wait-Process -Id %1 -Timeout 60\n").arg(c.pid)))
            ++wrongStatements;
    }
    std::printf("  %d script: %d loi cu phap, %d sai so cau lenh, %d sai ten lenh, %d sai hang chuoi\n",
                static_cast<int>(cases.size()), syntaxErrors, wrongStatements, wrongCommands, wrongStrings);
    CHECK(syntaxErrors == 0);
    CHECK(wrongStatements == 0);
    CHECK(wrongCommands == 0);
    CHECK(wrongStrings == 0);
    QFile::remove(listPath);
}

// =====================================================================================================
// 5. Auto Click: ActionSerializer
// =====================================================================================================

Action randomAction(std::mt19937& rng)
{
    const auto anyInt = [&]() {
        switch (rndIn(rng, 0, 5))
        {
            case 0: return 0;
            case 1: return INT_MAX;
            case 2: return INT_MIN;
            case 3: return -1;
            default: return rndIn(rng, -40000, 40000);
        }
    };
    Action a;
    a.type = static_cast<ActionType>(rndIn(rng, 0, 6));
    a.enabled = rndIn(rng, 0, 1);
    a.waitBefore = std::chrono::milliseconds(rndIn(rng, 0, 3600000));
    a.waitAfter = std::chrono::milliseconds(rndIn(rng, 0, 3600000));
    a.duration = std::chrono::milliseconds(rndIn(rng, 0, 3) == 0 ? INT_MAX : rndIn(rng, 0, 3600000));
    const auto coord = [&]() {
        switch (rndIn(rng, 0, 4))
        {
            case 0: return -32768;
            case 1: return 32767;
            default: return rndIn(rng, -32768, 32767);
        }
    };
    a.x = coord();
    a.y = coord();
    a.startX = coord();
    a.startY = coord();
    a.endX = coord();
    a.endY = coord();
    a.mouseButton = static_cast<MouseButtonType>(rndIn(rng, 0, 2));
    a.text = randomText(rng, 10).toStdString();
    if (rndIn(rng, 0, 9) == 0)
        a.text += "\n\r\t\\\"\x01";
    a.textMode = static_cast<TextTypeMode>(rndIn(rng, 0, 1));
    a.keyCode = anyInt();
    a.keyName = randomText(rng, 3).toStdString();
    a.modCtrl = rndIn(rng, 0, 1);
    a.modAlt = rndIn(rng, 0, 1);
    a.modShift = rndIn(rng, 0, 1);
    a.modWin = rndIn(rng, 0, 1);
    a.scrollDirection = static_cast<ScrollDirection>(rndIn(rng, 0, 3));
    a.scrollAmount = anyInt();
    return a;
}

bool sameAction(const Action& a, const Action& b)
{
    return a.type == b.type && a.enabled == b.enabled && a.waitBefore == b.waitBefore && a.waitAfter == b.waitAfter &&
           a.duration == b.duration && a.x == b.x && a.y == b.y && a.startX == b.startX && a.startY == b.startY &&
           a.endX == b.endX && a.endY == b.endY && a.mouseButton == b.mouseButton && a.text == b.text &&
           a.textMode == b.textMode && a.keyCode == b.keyCode && a.keyName == b.keyName && a.modCtrl == b.modCtrl &&
           a.modAlt == b.modAlt && a.modShift == b.modShift && a.modWin == b.modWin &&
           a.scrollDirection == b.scrollDirection && a.scrollAmount == b.scrollAmount;
}

bool sameChains(const std::vector<ActionChain>& a, const std::vector<ActionChain>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].description != b[i].description ||
            a[i].enabled != b[i].enabled || a[i].repeatCount != b[i].repeatCount || a[i].actions.size() != b[i].actions.size())
            return false;
        for (size_t k = 0; k < a[i].actions.size(); ++k)
            if (!sameAction(a[i].actions[k], b[i].actions[k]))
                return false;
    }
    return true;
}

std::vector<ActionChain> randomProfile(std::mt19937& rng, int maxChains, int maxActions)
{
    std::vector<ActionChain> chains;
    const int count = rndIn(rng, 0, maxChains);
    for (int c = 0; c < count; ++c)
    {
        ActionChain chain;
        chain.id = randomText(rng, 3).toStdString();
        chain.name = randomText(rng, 6).toStdString();
        chain.description = randomText(rng, 8).toStdString();
        chain.enabled = rndIn(rng, 0, 1);
        chain.repeatCount = rndIn(rng, 0, 3) == 0 ? -1 : rndIn(rng, 1, 999999);
        const int actions = rndIn(rng, 0, maxActions);
        for (int a = 0; a < actions; ++a)
            chain.actions.push_back(randomAction(rng));
        chains.push_back(chain);
    }
    return chains;
}

void fuzzActionSerializer()
{
    Section section("ActionSerializer: khu hoi 4000 ho so ngau nhien, JSON rac/cut cut/khong lo, so ngoai mien");
    int roundTripFailures = 0, fileFailures = 0;
    const QString filePath = AppPaths::dataDir() + "/" + QString::fromUtf8("khứ hồi ’thử’ & %1.json");
    for (int i = 0; i < 4000; ++i)
    {
        const std::vector<ActionChain> original = randomProfile(g_rng, 5, 20);
        std::vector<ActionChain> loaded;
        loaded.push_back(ActionChain{}); // phải bị thay thế hoàn toàn khi nạp thành công
        QString error;
        if (!ActionSerializer::fromJsonString(ActionSerializer::toJsonString(original), loaded, &error) || !sameChains(original, loaded))
        {
            if (roundTripFailures < 3)
                std::printf("  KHU HOI HONG (%s): %.300s\n", qPrintable(error), qPrintable(ActionSerializer::toJsonString(original)));
            ++roundTripFailures;
        }
        if (i % 20 == 0) // qua tệp thật (ghi nguyên tử, chế độ Text đổi \n <-> \r\n)
        {
            std::vector<ActionChain> fromFile;
            if (!ActionSerializer::saveToFile(filePath, original, &error) || !ActionSerializer::loadFromFile(filePath, fromFile, &error) ||
                !sameChains(original, fromFile))
            {
                if (fileFailures < 3)
                    std::printf("  QUA TEP HONG: %s\n", qPrintable(error));
                ++fileFailures;
            }
        }
    }
    std::printf("  khu hoi: %d hong trong bo nho, %d hong qua tep\n", roundTripFailures, fileFailures);
    CHECK(roundTripFailures == 0);
    CHECK(fileFailures == 0);
    // Ghi nguyên tử: sau 200 lần ghi đè chỉ còn đúng MỘT tệp, không sót tệp tạm.
    CHECK(QDir(AppPaths::dataDir()).entryList({"*.json*"}, QDir::Files | QDir::Hidden).size() == 1);

    // ---- Cắt cụt ở MỌI vị trí: không bao giờ được "nạp thành công" một hồ sơ thiếu, đầu ra giữ nguyên ----
    {
        std::mt19937 rng(12345);
        const QByteArray json = ActionSerializer::toJsonString(randomProfile(rng, 3, 6)).toUtf8().trimmed();
        const std::vector<ActionChain> sentinel = {ActionChain{}};
        int acceptedTruncated = 0, outputTouched = 0;
        for (int cut = 0; cut < json.size(); ++cut)
        {
            std::vector<ActionChain> out = sentinel;
            QString error;
            if (ActionSerializer::fromJsonString(QString::fromUtf8(json.left(cut)), out, &error))
                ++acceptedTruncated;
            else if (!sameChains(out, sentinel) || error.isEmpty())
                ++outputTouched;
        }
        std::printf("  cat cut %d vi tri: %d lan nhan nham, %d lan dung toi dau ra/thieu thong bao\n", static_cast<int>(json.size()),
                    acceptedTruncated, outputTouched);
        CHECK(acceptedTruncated == 0);
        CHECK(outputTouched == 0);
    }

    // ---- Rác ngẫu nhiên + lật byte: không crash; nạp được thì mọi trường phải nằm trong miền hợp lệ ----
    {
        int outOfDomain = 0, accepted = 0;
        const auto checkDomain = [&](const std::vector<ActionChain>& chains) {
            for (const ActionChain& chain : chains)
            {
                if (chain.repeatCount != -1 && chain.repeatCount < 1) ++outOfDomain;
                for (const Action& a : chain.actions)
                {
                    const int type = static_cast<int>(a.type), button = static_cast<int>(a.mouseButton);
                    const int mode = static_cast<int>(a.textMode), dir = static_cast<int>(a.scrollDirection);
                    if (type < 0 || type > 6 || button < 0 || button > 2 || mode < 0 || mode > 1 || dir < 0 || dir > 3) ++outOfDomain;
                    // Thời gian âm/khổng lồ làm tràn phép cộng mốc thời gian của ActionRunner/InputController.
                    for (const auto ms : {a.waitBefore.count(), a.waitAfter.count(), a.duration.count()})
                        if (ms < 0 || ms > INT_MAX) ++outOfDomain;
                    // Tọa độ ngoài màn hình ảo làm tràn phép nội suy (end - start) lúc kéo chuột.
                    for (const int c : {a.x, a.y, a.startX, a.startY, a.endX, a.endY})
                        if (c < -32768 || c > 32767) ++outOfDomain;
                }
            }
        };
        const QByteArray base = ActionSerializer::toJsonString(randomProfile(g_rng, 3, 5)).toUtf8();
        for (int i = 0; i < 20000; ++i)
        {
            QByteArray data = base;
            const int edits = rnd(1, 6);
            for (int e = 0; e < edits && !data.isEmpty(); ++e)
            {
                const int pos = rnd(0, static_cast<int>(data.size()) - 1);
                switch (rnd(0, 3))
                {
                    case 0: data[pos] = static_cast<char>(rnd(0, 255)); break;
                    case 1: data.remove(pos, rnd(1, 4)); break;
                    case 2: data.insert(pos, QByteArray::number(rnd(0, 1) ? 1e300 : -9.9e18)); break;
                    default: data.insert(pos, rnd(0, 1) ? "-99999999999999999999" : "9223372036854775807"); break;
                }
            }
            std::vector<ActionChain> out;
            if (ActionSerializer::fromJsonString(QString::fromUtf8(data), out))
            {
                ++accepted;
                checkDomain(out);
            }
        }
        // JSON đúng cấu trúc nhưng mọi con số đều ngoài miền / sai kiểu.
        const QString hostile = R"({"chains":[{"id":5,"name":null,"repeatCount":0,"actions":[
            {"type":"Scroll","startX":-2147483648,"endX":2147483647,"waitBefore":-5,"waitAfter":9223372036854775807,"duration":1e300,"mouseButton":99,"textMode":-3,
             "scrollDirection":1000,"scrollAmount":-7,"keyCode":70000,"x":1e40,"y":-1e40,"text":123,"keyName":[1]},
            {"type":12,"waitBefore":"abc","waitAfter":-9223372036854775808,"duration":-1,"mouseButton":-1,"textMode":2},
            17, null, "chuoi", [1,2]]},
            {"repeatCount":-2,"actions":"khong phai mang"},{"repeatCount":-1},{"repeatCount":2147483648}, 5, null]})";
        std::vector<ActionChain> out;
        QString error;
        CHECK(ActionSerializer::fromJsonString(hostile, out, &error));
        CHECK(out.size() == 6);
        checkDomain(out);
        if (out.size() == 6)
        {
            CHECK(out[0].actions.size() == 6);
            CHECK(out[0].repeatCount == 1 && out[1].repeatCount == 1 && out[2].repeatCount == -1 && out[3].repeatCount == 1);
            if (out[0].actions.size() == 6)
            {
                CHECK(out[0].actions[0].waitBefore.count() == 0);                 // âm -> 0
                CHECK(out[0].actions[0].waitAfter.count() == INT_MAX);            // quá lớn -> trần
                CHECK(out[0].actions[1].waitAfter.count() == 0);
                CHECK(out[0].actions[1].duration.count() == 0);
            }
        }
        std::printf("  rac: %d ban bien dang van nap duoc, %d truong ngoai mien\n", accepted, outOfDomain);
        CHECK(outOfDomain == 0);
    }

    // ---- Khổng lồ: 300 chuỗi x 200 hành động (~25 MB JSON), lồng sâu, chuỗi rất dài ----
    {
        std::mt19937 rng(777);
        std::vector<ActionChain> big;
        for (int c = 0; c < 300; ++c)
        {
            ActionChain chain;
            chain.name = "lon " + std::to_string(c);
            for (int a = 0; a < 200; ++a)
                chain.actions.push_back(randomAction(rng));
            big.push_back(chain);
        }
        big[0].actions[0].text = std::string(2 * 1024 * 1024, 'z');
        QElapsedTimer t;
        t.start();
        const QString json = ActionSerializer::toJsonString(big);
        std::vector<ActionChain> out;
        CHECK(ActionSerializer::fromJsonString(json, out));
        CHECK(sameChains(big, out));
        std::printf("  ho so 60000 hanh dong: %lld ky tu JSON, khu hoi %lld ms\n", static_cast<long long>(json.size()),
                    static_cast<long long>(t.elapsed()));
        CHECK(t.elapsed() < 60000);

        std::vector<ActionChain> untouched = {ActionChain{}};
        QString error;
        CHECK(!ActionSerializer::fromJsonString(QString(100000, QChar('[')), untouched, &error));
        CHECK(!ActionSerializer::fromJsonString("{\"chains\":" + QString(5000, QChar('[')) + QString(5000, QChar(']')) + "}", untouched, &error));
        CHECK(untouched.size() == 1);
        CHECK(!ActionSerializer::loadFromFile(AppPaths::dataDir(), untouched, &error)); // thư mục, không phải tệp
        CHECK(!ActionSerializer::saveToFile(AppPaths::dataDir(), big, &error));
        CHECK(!error.isEmpty());
    }
    QFile::remove(filePath);
}

// =====================================================================================================
// 6. Auto Click: ActionRunner (chỉ hành động "Gõ văn bản" RỖNG - không bao giờ tới SendInput) + StopHotkey
// =====================================================================================================

/// Hành động duy nhất được phép chạy trong bộ test này: gõ chuỗi rỗng = InputController::typeText() thoát
/// ngay ở dòng đầu, không gọi Win32 nào.
Action safeAction(int waitBeforeMs, int waitAfterMs, bool enabled = true)
{
    Action a;
    a.type = ActionType::TypeText;
    a.text.clear();
    a.enabled = enabled;
    a.waitBefore = std::chrono::milliseconds(waitBeforeMs);
    a.waitAfter = std::chrono::milliseconds(waitAfterMs);
    a.duration = std::chrono::milliseconds(0);
    return a;
}

bool chainIsSafe(const ActionChain& chain)
{
    for (const Action& a : chain.actions)
        if (a.type != ActionType::TypeText || !a.text.empty())
            return false;
    return true;
}

struct RunnerProbe
{
    explicit RunnerProbe(ActionRunner& runner)
    {
        // Nối TRỰC TIẾP: bộ đếm chạy ngay trên luồng phát tín hiệu, không phụ thuộc vòng lặp sự kiện.
        QObject::connect(&runner, &ActionRunner::chainFinished, &runner, [this]() { ++finished; }, Qt::DirectConnection);
        QObject::connect(&runner, &ActionRunner::executionStopped, &runner, [this]() { ++stopped; }, Qt::DirectConnection);
        QObject::connect(&runner, &ActionRunner::errorOccurred, &runner, [this](const QString&) { ++errors; }, Qt::DirectConnection);
        QObject::connect(&runner, &ActionRunner::roundStarted, &runner, [this](int, int) { ++rounds; }, Qt::DirectConnection);
        QObject::connect(&runner, &ActionRunner::actionStarted, &runner,
                         [this](int, int, int, int, const QString&, const QString&, int, int, int sourceIndex) {
                             ++actions;
                             lastSourceIndex = sourceIndex;
                         },
                         Qt::DirectConnection);
    }
    std::atomic<int> finished{0}, stopped{0}, errors{0}, rounds{0}, actions{0}, lastSourceIndex{-1};
};

void stressActionRunner()
{
    Section section("ActionRunner: chuoi rong/toan tat, start+stop 500 lan, lap vo han, tam dung, moc thoi gian");
    ActionRunner runner;
    RunnerProbe probe(runner);

    // ---- Chuỗi rỗng / toàn hành động tắt: kết thúc ngay, không chạy hành động nào ----
    {
        ActionChain empty;
        runner.setChain(empty);
        runner.start();
        CHECK(runner.wait(3000));
        CHECK(probe.finished == 1 && probe.stopped == 0 && probe.actions == 0);
        CHECK(runner.runnerState() == RunnerState::Finished);

        ActionChain allOff;
        allOff.repeatCount = -1; // lặp vô hạn nhưng không có gì để chạy: KHÔNG được quay vòng mãi
        for (int i = 0; i < 50; ++i)
            allOff.actions.push_back(safeAction(0, 0, false));
        CHECK(chainIsSafe(allOff));
        runner.setChain(allOff);
        runner.start();
        CHECK(runner.wait(3000));
        CHECK(probe.finished == 2 && probe.actions == 0 && probe.rounds == 0);
    }

    // ---- Hành động tắt xen kẽ: chỉ số gốc báo về phải là của hành động BẬT ----
    {
        ActionChain mixed;
        mixed.repeatCount = 3;
        for (int i = 0; i < 9; ++i)
            mixed.actions.push_back(safeAction(0, 0, i % 3 == 2)); // bật: 2, 5, 8
        CHECK(chainIsSafe(mixed));
        const int actionsBefore = probe.actions, roundsBefore = probe.rounds;
        runner.setChain(mixed);
        runner.start();
        CHECK(runner.wait(5000));
        CHECK(probe.actions - actionsBefore == 9); // 3 hành động bật x 3 vòng
        CHECK(probe.rounds - roundsBefore == 3);
        CHECK(probe.lastSourceIndex == 8);
        CHECK(probe.errors == 0);
    }

    // ---- start() rồi requestStop() ngay, 500 lần, độ trễ ngẫu nhiên 0-3 ms ----
    {
        ActionChain chain;
        chain.repeatCount = -1;
        chain.actions = {safeAction(1, 1), safeAction(0, 2), safeAction(0, 0, false)};
        CHECK(chainIsSafe(chain));
        int notStopped = 0, wrongSignalCount = 0, wrongState = 0;
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < 500; ++i)
        {
            const int before = probe.finished + probe.stopped;
            if (i % 50 == 0)
                chain.repeatCount = 1; // thỉnh thoảng một chuỗi tự chạy xong trước/giữa lúc bấm dừng
            else
                chain.repeatCount = -1;
            runner.setChain(chain);
            runner.start();
            if (i % 3 == 1)
                QThread::usleep(static_cast<unsigned long>(rnd(0, 3000)));
            else if (i % 3 == 2)
                QThread::yieldCurrentThread();
            runner.requestStop();
            if (i % 7 == 0)
                runner.requestStop(); // bấm dừng hai lần liền
            if (!runner.wait(3000))
                ++notStopped;
            if (probe.finished + probe.stopped - before != 1)
                ++wrongSignalCount; // mỗi lần chạy phải báo kết thúc ĐÚNG một lần
            const RunnerState state = runner.runnerState();
            if (state != RunnerState::Idle && state != RunnerState::Finished)
                ++wrongState; // luồng đã thoát mà trạng thái còn "Đang chạy/Đang dừng"
        }
        std::printf("  500 lan start/stop: %lld ms, %d lan khong dung, %d lan sai so tin hieu ket thuc, %d lan ket trang thai\n",
                    static_cast<long long>(t.elapsed()), notStopped, wrongSignalCount, wrongState);
        CHECK(notStopped == 0);
        CHECK(wrongSignalCount == 0);
        CHECK(wrongState == 0);
        CHECK(probe.errors == 0);
    }

    // ---- Bấm dừng đúng khoảnh khắc chuỗi vừa TỰ chạy xong (luồng chưa thoát hẳn): trạng thái cuối phải là
    // "Hoàn tất", không được kẹt lại ở "Đang dừng" ----
    {
        ActionChain chain;
        chain.repeatCount = 1;
        chain.actions = {safeAction(0, 0)};
        int stuckStopping = 0, notStopped = 0;
        for (int i = 0; i < 400; ++i)
        {
            const int before = probe.finished;
            runner.setChain(chain);
            runner.start();
            QElapsedTimer guard;
            guard.start();
            while (probe.finished == before && guard.elapsed() < 3000)
                QThread::yieldCurrentThread(); // run() vừa phát chainFinished nhưng chưa trả về
            runner.requestStop();
            if (!runner.wait(3000))
                ++notStopped;
            if (runner.runnerState() != RunnerState::Finished)
                ++stuckStopping;
        }
        std::printf("  bam dung dung luc vua chay xong, 400 lan: %d lan trang thai ket o \"Dang dung\"\n", stuckStopping);
        CHECK(notStopped == 0);
        CHECK(stuckStopping == 0);
    }

    // ---- Chuỗi lặp vô hạn KHÔNG có khoảng chờ nào: luồng chạy phải tự giữ nhịp, không quay hết tốc lực
    // (mỗi hành động kéo theo vài tín hiệu sang luồng giao diện + một dòng log) ----
    {
        ActionChain chain;
        chain.repeatCount = -1;
        chain.actions = {safeAction(0, 0)};
        const int roundsBefore = probe.rounds;
        runner.setChain(chain);
        runner.start();
        QThread::msleep(500);
        runner.requestStop();
        CHECK(runner.wait(3000));
        const int rounds = probe.rounds - roundsBefore;
        std::printf("  lap vo han khong cho: %d vong trong 500 ms\n", rounds);
        CHECK(rounds >= 5);
        CHECK(rounds <= 700); // >= 1 ms mỗi hành động
    }

    // ---- Lặp vô hạn + dừng giữa một khoảng chờ DÀI: phải thoát trong ~50 ms, không chờ hết 1 giờ ----
    {
        ActionChain chain;
        chain.repeatCount = -1;
        chain.actions = {safeAction(3600000, 0)};
        runner.setChain(chain);
        runner.start();
        QThread::msleep(150);
        QElapsedTimer t;
        t.start();
        runner.requestStop();
        CHECK(runner.wait(3000));
        CHECK(t.elapsed() < 500);
        CHECK(runner.runnerState() == RunnerState::Idle);
    }

    // ---- Mốc thời gian: chờ 400 ms phải thật sự ~400 ms; tạm dừng không tính vào thời gian chờ ----
    {
        ActionChain chain;
        chain.repeatCount = 1;
        chain.actions = {safeAction(400, 0)};
        runner.setChain(chain);
        QElapsedTimer t;
        t.start();
        runner.start();
        CHECK(runner.wait(5000));
        const qint64 plain = t.elapsed();
        CHECK(plain >= 395 && plain < 900);

        runner.setChain(chain);
        t.restart();
        runner.start();
        QThread::msleep(100);
        runner.requestPause();
        QThread::msleep(500);
        CHECK(runner.isRunning()); // đang tạm dừng: chưa được chạy tiếp/kết thúc
        runner.requestResume();
        CHECK(runner.wait(5000));
        const qint64 paused = t.elapsed();
        std::printf("  cho 400 ms: that te %lld ms; co tam dung 500 ms: %lld ms\n", static_cast<long long>(plain),
                    static_cast<long long>(paused));
        CHECK(paused >= 850 && paused < 1600);

        // Dừng trong lúc đang tạm dừng: thoát ngay.
        chain.actions = {safeAction(5000, 0)};
        runner.setChain(chain);
        runner.start();
        QThread::msleep(60);
        runner.requestPause();
        QThread::msleep(60);
        t.restart();
        runner.requestStop();
        CHECK(runner.wait(3000));
        CHECK(t.elapsed() < 500);
        runner.requestPause();  // gọi khi không chạy: không được đổi trạng thái
        runner.requestResume();
        const RunnerState state = runner.runnerState();
        CHECK(state == RunnerState::Idle || state == RunnerState::Finished);
    }

    // ---- Hủy đối tượng khi đang chạy: destructor tự dừng, không "QThread: Destroyed while still running" ----
    for (int i = 0; i < 100; ++i)
    {
        ActionRunner temp;
        ActionChain chain;
        chain.repeatCount = -1;
        chain.actions = {safeAction(rnd(0, 20), rnd(0, 20))};
        temp.setChain(chain);
        temp.start();
        if (i % 2)
            QThread::usleep(static_cast<unsigned long>(rnd(0, 2000)));
    }
    CHECK(true); // tới được đây tức là 100 lần hủy-khi-đang-chạy không làm tiến trình dừng
}

void stressStopHotkey()
{
    Section section("StopHotkey: dang ky/huy 1000 lan tren mot cua so that (message-only)");
    // Cửa sổ chỉ-nhận-thông-điệp: có HWND thật để RegisterHotKey gắn vào mà không hiện gì lên màn hình
    // (winId() của nền tảng "offscreen" không phải HWND thật).
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"core_stress_hotkey", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    CHECK(hwnd != nullptr);
    if (!hwnd)
        return;
    const WId id = reinterpret_cast<WId>(hwnd);

    if (!StopHotkey::registerFor(id))
    {
        // Tổ hợp đang bị chương trình khác trên máy giữ - không kiểm được vòng đăng ký THÀNH CÔNG, nhưng
        // đường thất bại (đúng cái ứng dụng thật gặp trên máy này) vẫn phải ổn định và không rò.
        const DWORD lastError = GetLastError();
        std::printf("  CHU Y: %s dang bi chuong trinh khac giu (RegisterHotKey loi %lu) - chi kiem duoc duong THAT BAI\n",
                    qPrintable(StopHotkey::label()), static_cast<unsigned long>(lastError));
        const DWORD handlesBefore = handleCount();
        int unexpectedSuccess = 0;
        for (int i = 0; i < 1000; ++i)
        {
            if (StopHotkey::registerFor(id))
                ++unexpectedSuccess;
            StopHotkey::unregisterFor(id);
        }
        std::printf("  1000 lan dang ky/huy khi bi giu: %d lan bat ngo thanh cong\n", unexpectedSuccess);
        CHECK(handleCount() <= handlesBefore + 8);
        DestroyWindow(hwnd);
        return;
    }
    CHECK(!StopHotkey::registerFor(id)); // đăng ký trùng: Windows từ chối, không được "thành công" hai lần
    StopHotkey::unregisterFor(id);

    const DWORD handlesBefore = handleCount();
    int failures = 0;
    for (int i = 0; i < 1000; ++i)
    {
        if (!StopHotkey::registerFor(id))
            ++failures;
        StopHotkey::unregisterFor(id);
        if (i % 100 == 0)
            StopHotkey::unregisterFor(id); // hủy thừa: không lỗi, không ảnh hưởng lần đăng ký sau
    }
    CHECK(failures == 0);
    CHECK(handleCount() <= handlesBefore + 8);

    // Nhận dạng thông điệp.
    MSG msg{};
    CHECK(!StopHotkey::isStopMessage(&msg));
    msg.message = WM_HOTKEY;
    msg.wParam = 0x4F41;
    CHECK(StopHotkey::isStopMessage(&msg));
    msg.wParam = 1;
    CHECK(!StopHotkey::isStopMessage(&msg));
    msg.message = WM_KEYDOWN;
    msg.wParam = 0x4F41;
    CHECK(!StopHotkey::isStopMessage(&msg));

    // Handle không hợp lệ / cửa sổ đã hủy: trả false, không crash.
    DestroyWindow(hwnd);
    CHECK(!StopHotkey::registerFor(id));
    StopHotkey::unregisterFor(id);
    CHECK(!StopHotkey::registerFor(static_cast<WId>(0x7FFFFFF0)));
}

// =====================================================================================================
// 7. Android: AdbController / AdbDeviceLister / ScrcpyLauncher (không có adb.exe cạnh tệp test -> đường lỗi)
// =====================================================================================================

void fuzzAndroidCore()
{
    Section section("Android: parse 40000 dong ngau nhien, kiem IP:cong/ma 80000 chuoi, lister/launcher don dap");
    using namespace AdbController;

    // ---- parseDevicesOutput ----
    {
        const QStringList tokens = {"device", "unauthorized", "offline", "no", "permissions", "authorizing", "connecting", "host",
                                    "R58M80ABCDE", "192.168.1.23:5555", "emulator-5554", "model:Pixel_7", "model:", ":x", "product:a:b",
                                    "transport_id:9", "transport_id:", "usb:1-1", "*", "* daemon not running; starting now at tcp:5037",
                                    "List of devices attached", "adb.exe:", "error:", "failed", "\t", " ", "\r", "", "<b>x</b>",
                                    QString::fromUtf8("thiết_bị"), "-s", "--help", QString(QChar(0)), "device\tproduct:x"};
        int invalid = 0, total = 0;
        for (int i = 0; i < 4000; ++i)
        {
            QString output;
            const int lineCount = rnd(0, 10);
            for (int l = 0; l < lineCount; ++l)
            {
                const int n = rnd(0, 7);
                for (int k = 0; k < n; ++k)
                    output += tokens[rnd(0, static_cast<int>(tokens.size()) - 1)] + (rnd(0, 3) ? " " : "\t");
                output += rnd(0, 4) ? "\n" : "\r\n";
            }
            for (const AndroidDeviceInfo& d : internal::parseDevicesOutput(output))
            {
                ++total;
                const bool knownState = internal::isKnownDeviceState(d.state) || d.state == "no permissions";
                if (!knownState || d.serial.isEmpty() || d.serial.contains(QRegularExpression("\\s")) || d.serial.startsWith('*'))
                    ++invalid;
                if (d.isReady() != (d.state == "device"))
                    ++invalid;
            }
        }
        std::printf("  parse: %d thiet bi tu du lieu ngau nhien, %d ban ghi khong hop le\n", total, invalid);
        CHECK(total > 100);
        CHECK(invalid == 0);
        CHECK(internal::parseDevicesOutput(QString(2 * 1024 * 1024, QChar('a'))).isEmpty()); // một "dòng" 2 MB
        CHECK(internal::parseDevicesOutput(QString("x device\n").repeated(20000)).size() == 20000);
    }

    // ---- isValidIpAndPort / isValidPairingCode ----
    {
        const QStringList parts = {"192.168.1.23", "10.0.0.5", "pixel-7.local", "[fe80::1]", "[::1]", "a", "-", "--help", "-s", ".", ":",
                                   "::", "5555", "0", "65535", "65536", "99999", "00001", " ", "\n", "\t", "\r", QString(QChar(0)), "/",
                                   "\\", "&", "|", ";", "\"", "'", "%", QString::fromUtf8("٥"), QString::fromUtf8("５"), "x:1 y:2", "a:1\n"};
        static const QRegularExpression allowed(QStringLiteral("\\A[A-Za-z0-9.:\\-\\[\\]]+\\z"));
        int accepted = 0, unsafe = 0;
        for (int i = 0; i < 80000; ++i)
        {
            QString s;
            if (rnd(0, 3) == 0)
            {
                s = parts[rnd(0, 4)] + ":" + QString::number(rnd(-2, 70000)); // dạng đúng, cổng có thể ngoài miền
                if (rnd(0, 4) == 0)
                    s.insert(rnd(0, static_cast<int>(s.size())), parts[rnd(0, static_cast<int>(parts.size()) - 1)]);
            }
            else
            {
                const int n = rnd(0, 5);
                for (int k = 0; k < n; ++k)
                    s += parts[rnd(0, static_cast<int>(parts.size()) - 1)];
            }
            if (isValidIpAndPort(s))
            {
                ++accepted;
                const int colon = s.lastIndexOf(':');
                bool portOk = false;
                const int port = s.mid(colon + 1).toInt(&portOk);
                // Chuỗi được nhận sẽ thành MỘT đối số của adb.exe: không khoảng trắng, không bắt đầu bằng '-'.
                if (s.startsWith('-') || !allowed.match(s).hasMatch() || colon <= 0 || !portOk || port < 1 || port > 65535)
                {
                    if (unsafe < 5)
                        std::printf("  NHAN NHAM dia chi: \"%s\"\n", qPrintable(s));
                    ++unsafe;
                }
            }
            const QString code = (rnd(0, 2) == 0) ? QString::number(rnd(0, 9999999)) : s.left(7);
            if (isValidPairingCode(code))
            {
                static const QRegularExpression six(QStringLiteral("\\A[0-9]{6}\\z"));
                if (code.size() != 6 || !six.match(code).hasMatch())
                    ++unsafe;
            }
            // Đầu vào không hợp lệ không bao giờ được tới bước gọi adb (ở đây adb cũng không tồn tại).
            if (i % 2000 == 0 && adbExecutablePath().isEmpty())
            {
                QString error;
                if (pairWireless(s, code, &error) || connectWireless(s, &error) || disconnectWireless(s, &error))
                    ++unsafe;
                if (error.isEmpty())
                    ++unsafe;
            }
        }
        std::printf("  IP:cong: %d chuoi duoc nhan, %d cai khong an toan\n", accepted, unsafe);
        CHECK(accepted > 50);
        CHECK(unsafe == 0);
    }

    // ---- So đường dẫn tệp ảnh: chính exe test viết theo nhiều kiểu; dừng theo đường dẫn không đụng nhầm ----
    {
        const QString self = QCoreApplication::applicationFilePath();
        CHECK(internal::isSameExecutablePath(self, QDir::toNativeSeparators(self).toUpper()));
        CHECK(internal::isSameExecutablePath(self, "\\\\?\\" + QDir::toNativeSeparators(self)));
        CHECK(internal::isSameExecutablePath(self, QFileInfo(self).absolutePath() + "/./" + QFileInfo(self).fileName()));
        // Tên ngắn 8.3: dùng một tệp giả trong thư mục tạm (ổ chứa %TEMP% thường có bật 8.3, ổ build thì không).
        QTemporaryDir shortNameDir;
        const QString longFile = QDir(shortNameDir.path()).absolutePath() + "/thu muc ten rat dai cho adb/adb.exe";
        CHECK(QDir().mkpath(QFileInfo(longFile).absolutePath()));
        CHECK(writeFile(longFile, "khong phai exe that"));
        wchar_t shortBuf[MAX_PATH + 1] = {};
        const DWORD len = GetShortPathNameW(QDir::toNativeSeparators(longFile).toStdWString().c_str(), shortBuf, MAX_PATH);
        const QString shortFile = (len > 0 && len < MAX_PATH) ? QString::fromWCharArray(shortBuf, static_cast<int>(len)) : QString();
        if (!shortFile.isEmpty() && shortFile.compare(QDir::toNativeSeparators(longFile), Qt::CaseInsensitive) != 0)
        {
            const bool same = internal::isSameExecutablePath(longFile, shortFile);
            std::printf("  ten ngan 8.3 \"%s\" %s la cung tep\n", qPrintable(shortFile), same ? "DUOC nhan" : "KHONG duoc nhan");
            CHECK(same);
        }
        else
        {
            std::printf("  ten ngan 8.3: o dia nay khong sinh ten ngan - bo qua\n");
        }
        // Cùng tệp nhìn qua một junction.
        const QString junction = QDir(shortNameDir.path()).absolutePath() + "/lien ket";
        if (QProcess::execute(AppPaths::systemExecutable("cmd.exe"),
                              {"/c", "mklink", "/J", QDir::toNativeSeparators(junction),
                               QDir::toNativeSeparators(QFileInfo(longFile).absolutePath())}) == 0)
        {
            CHECK(internal::isSameExecutablePath(longFile, junction + "/adb.exe"));
            QProcess::execute(AppPaths::systemExecutable("cmd.exe"), {"/c", "rmdir", QDir::toNativeSeparators(junction)});
        }
        // Tệp KHÁC cùng tên ở thư mục khác: không bao giờ được coi là một.
        const QString otherFile = QDir(shortNameDir.path()).absolutePath() + "/cong cu khac/adb.exe";
        CHECK(QDir().mkpath(QFileInfo(otherFile).absolutePath()));
        CHECK(writeFile(otherFile, "khong phai exe that"));
        CHECK(!internal::isSameExecutablePath(longFile, otherFile));
        CHECK(!internal::isSameExecutablePath(longFile, QFileInfo(longFile).absolutePath())); // thư mục, không phải tệp
        CHECK(!internal::isSameExecutablePath(self, self + ".khac"));
        CHECK(!internal::isSameExecutablePath(self, AppPaths::powershellExecutable()));

        const DWORD handlesBefore = handleCount();
        int killed = 0;
        for (int i = 0; i < 300; ++i)
        {
            killed += internal::terminateProcessesByImagePath(self); // chính nó: không bao giờ tự dừng
            killed += internal::terminateProcessesByImagePath("C:/khong/ton/tai/adb.exe");
            killed += stopBundledAdbServer();
        }
        CHECK(killed == 0);
        CHECK(handleCount() <= handlesBefore + 16);
    }

    // ---- AdbDeviceLister khi KHÔNG có adb.exe: mỗi refresh() báo lỗi đúng một lần, cancel() dồn dập ----
    if (adbExecutablePath().isEmpty())
    {
        int results = 0, withError = 0;
        {
            AdbDeviceLister lister;
            QObject::connect(&lister, &AdbDeviceLister::listed, &lister, [&](const QList<AndroidDeviceInfo>& devices, const QString& error) {
                ++results;
                if (!error.isEmpty() && devices.isEmpty())
                    ++withError;
            });
            for (int i = 0; i < 3000; ++i)
            {
                if (!lister.refresh())
                    --results; // không được từ chối: không có lệnh nào đang chạy
                if (i % 3 == 0) lister.cancel();
                if (i % 5 == 0) lister.cancel();
                if (lister.isBusy())
                    --results;
            }
            QCoreApplication::processEvents();
        }
        CHECK(results == 3000);
        CHECK(withError == 3000);
        const DWORD handlesBefore = handleCount();
        for (int i = 0; i < 500; ++i)
        {
            AdbDeviceLister temp;
            temp.refresh();
            temp.cancel();
        }
        CHECK(handleCount() <= handlesBefore + 16);

        // ScrcpyLauncher: thiếu scrcpy.exe -> từ chối có lý do, không tín hiệu "đã chạy", stop() vô hại.
        ScrcpyLauncher launcher;
        int started = 0;
        QObject::connect(&launcher, &ScrcpyLauncher::started, &launcher, [&]() { ++started; });
        int accepted = 0;
        for (int i = 0; i < 500; ++i)
        {
            ScrcpyOptions options;
            options.serial = randomText(g_rng, 3);
            QString error;
            if (launcher.start(options, &error) || error.isEmpty() || launcher.isRunning())
                ++accepted;
            launcher.stop();
        }
        CHECK(accepted == 0);
        CHECK(started == 0);
    }
    else
    {
        std::printf("  CO adb.exe canh tep test - bo qua kich ban \"khong co adb\" cua lister/launcher\n");
    }

    // ---- buildArguments: serial lạ luôn là MỘT đối số riêng ngay sau -s, không đối số rỗng ----
    {
        int bad = 0;
        for (int i = 0; i < 5000; ++i)
        {
            ScrcpyOptions o;
            o.serial = (rnd(0, 3) == 0) ? QString() : randomText(g_rng, 4);
            o.switchToWireless = rnd(0, 1);
            o.maxSize = rnd(-5, 4000);
            o.bitRateMbps = rnd(-5, 100);
            o.maxFps = rnd(-5, 240);
            o.recordFilePath = rnd(0, 1) ? QString() : ("C:/" + randomText(g_rng, 4) + ".mp4");
            o.windowTitle = rnd(0, 3) ? o.windowTitle : randomText(g_rng, 3);
            const QStringList args = ScrcpyLauncherInternal::buildArguments(o);
            const int s = args.indexOf("-s");
            if (!o.serial.isEmpty() && (s < 0 || args.value(s + 1) != o.serial))
                ++bad;
            if (o.serial.isEmpty() && s >= 0)
                ++bad;
            for (const QString& a : args)
            {
                if (a.isEmpty()) ++bad;
                if (a.startsWith("--max-size=") && o.maxSize <= 0) ++bad;
                if (a.startsWith("--max-fps=") && o.maxFps <= 0) ++bad;
                if (a.startsWith("--video-bit-rate=") && o.bitRateMbps <= 0) ++bad;
            }
        }
        CHECK(bad == 0);
    }
}

// =====================================================================================================
// 8. Giao diện (offscreen): cửa sổ Auto Click + Android qua ITool::createWindow()
// =====================================================================================================

/// Ghi hồ sơ mặc định CHỈ gồm hành động an toàn rồi đọc lại xác nhận - cửa sổ Auto Click dựng sau đó sẽ
/// nạp đúng hồ sơ này, nên bấm "Chạy" không thể gửi chuột/phím thật.
bool writeSafeDefaultProfile(int waitAfterMs)
{
    ActionChain chain;
    chain.id = "an_toan";
    chain.name = "An toan";
    chain.repeatCount = -1;
    chain.actions = {safeAction(0, waitAfterMs), safeAction(0, 0, false), safeAction(0, waitAfterMs)};
    QString error;
    const QString path = ActionSerializer::getDefaultProfilePath();
    if (!ActionSerializer::saveToFile(path, {chain}, &error))
        return false;
    std::vector<ActionChain> back;
    if (!ActionSerializer::loadFromFile(path, back, &error) || back.size() != 1 || back[0].actions.size() != 3)
        return false;
    return chainIsSafe(back[0]);
}

/// Khóa an toàn thứ hai, kiểm trên chính giao diện: mọi hàng của bảng hành động đang hiện phải là "Gõ văn
/// bản" với nội dung rỗng.
bool windowShowsOnlySafeActions(QWidget* window)
{
    auto* actionList = window->findChild<ActionListWidget*>();
    auto* chainList = window->findChild<ChainListWidget*>();
    if (!actionList || !chainList)
        return false;
    auto* table = actionList->findChild<QTableWidget*>();
    auto* list = chainList->findChild<QListWidget*>();
    if (!table || !list || list->count() != 1 || table->rowCount() != 3)
        return false;
    for (int r = 0; r < table->rowCount(); ++r)
    {
        if (table->item(r, 1)->text() != QString::fromUtf8("Gõ văn bản"))
            return false;
        if (!table->item(r, 2)->text().startsWith(QString::fromUtf8("Gõ \"\" (")))
            return false;
    }
    return true;
}

void stressAutoClickUi()
{
    Section section("Giao dien Auto Click: tao/dong/mo lai 30 vong + chay/dung chuoi an toan, bao su kien, 3000 thao tac ngau nhien");
    ITool* tool = ToolManager::instance().getTool("autoclick");
    CHECK(tool != nullptr);
    if (!tool)
        return;
    const QString runText = QString::fromUtf8("▶ Bắt đầu chạy (RUN)");
    const QString stopText = QString::fromUtf8("■ Dừng lại (STOP)");

    // ---- 30 vòng: tạo (hoặc tái dùng) -> hiện -> chạy chuỗi an toàn -> dừng -> đóng -> (mỗi 3 vòng) hủy ----
    CHECK(writeSafeDefaultProfile(15));
    int notStopped = 0, unsafe = 0, stuckStatus = 0;
    for (int round = 0; round < 30; ++round)
    {
        QWidget* window = tool->createWindow();
        CHECK(window == tool->createWindow()); // một cửa sổ duy nhất, tái dùng
        window->showNormal();
        QCoreApplication::processEvents();
        QPushButton* runBtn = buttonByText(window, runText);
        QPushButton* stopBtn = buttonByText(window, stopText);
        if (!runBtn || !stopBtn || !windowShowsOnlySafeActions(window))
        {
            ++unsafe; // KHÔNG bấm Chạy nếu chưa xác nhận được chuỗi an toàn
            delete window;
            continue;
        }

        runBtn->click();
        spinFor(rnd(0, 120));
        if (round % 2 == 0)
            stopBtn->click();
        else
            ToolManager::instance().stopAllBackgroundWorkForQuit(); // đường thoát ứng dụng: dừng không hỏi
        if (!spinUntil([&]() { return runBtn->isEnabled() && !stopBtn->isEnabled(); }, 5000))
            ++notStopped;
        for (auto* label : window->findChildren<QLabel*>())
            if (label->text().contains(QString::fromUtf8("Đang dừng")) || label->text().contains(QString::fromUtf8("Đang thực thi")))
                ++stuckStatus;

        // Bấm Chạy hai lần liền + bấm Dừng dồn dập: không được sinh hai lượt chạy chồng nhau.
        runBtn->click();
        runBtn->click();
        for (int k = 0; k < 5; ++k)
            stopBtn->click();
        if (!spinUntil([&]() { return runBtn->isEnabled(); }, 5000))
            ++notStopped;

        window->close();
        QCoreApplication::processEvents();
        CHECK(!window->isVisible());
        if (round % 3 == 2)
        {
            delete window; // QPointer trong AutoClickTool tự về null -> vòng sau tạo cửa sổ mới
            QCoreApplication::processEvents();
        }
    }
    std::printf("  30 vong: %d lan khong dung duoc, %d lan khong xac nhan duoc chuoi an toan, %d nhan trang thai bi ket\n",
                notStopped, unsafe, stuckStatus);
    CHECK(notStopped == 0);
    CHECK(unsafe == 0);
    CHECK(stuckStatus == 0);

    // ---- Chuỗi lặp vô hạn KHÔNG có khoảng chờ nào: luồng chạy không được làm nghẹt luồng giao diện ----
    {
        delete tool->createWindow();
        CHECK(writeSafeDefaultProfile(0));
        QWidget* window = tool->createWindow();
        window->showNormal();
        QCoreApplication::processEvents();
        QPushButton* runBtn = buttonByText(window, runText);
        QPushButton* stopBtn = buttonByText(window, stopText);
        CHECK(runBtn && stopBtn && windowShowsOnlySafeActions(window));
        if (runBtn && stopBtn && windowShowsOnlySafeActions(window))
        {
            runBtn->click();
            // Trong 2 giây chạy, đo độ trễ của luồng giao diện: một timer 20 ms phải tiếp tục được phục vụ.
            int ticks = 0;
            QTimer probe;
            probe.setInterval(20);
            QObject::connect(&probe, &QTimer::timeout, &probe, [&ticks]() { ++ticks; });
            probe.start();
            QElapsedTimer t;
            t.start();
            qint64 worstGap = 0, last = 0;
            while (t.elapsed() < 2000)
            {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                const qint64 now = t.elapsed();
                worstGap = qMax(worstGap, now - last);
                last = now;
            }
            probe.stop();
            QString roundText;
            for (auto* label : QApplication::allWidgets())
                if (auto* l = qobject_cast<QLabel*>(label); l && l->text().startsWith(QString::fromUtf8("Vòng ")))
                    roundText = l->text();
            QElapsedTimer drain;
            drain.start();
            stopBtn->click();
            const bool stopped = spinUntil([&]() { return runBtn->isEnabled(); }, 60000);
            const qint64 drainMs = drain.elapsed();
            const qint64 logBytes = QFileInfo(AppPaths::logFile("autoclick.log")).size();
            std::printf("  lap vo han khong cho, 2 giay: HUD \"%s\", timer 20ms chay %d/100 lan, khoang dung hinh dai nhat %lld ms, "
                        "bam Dung -> giao dien nhan ra sau %lld ms (autoclick.log %lld KB)\n",
                        qPrintable(roundText), ticks, static_cast<long long>(worstGap), static_cast<long long>(drainMs),
                        static_cast<long long>(logBytes / 1024));
            CHECK(stopped);
            CHECK(drainMs < 1500);  // bấm Dừng phải có tác dụng gần như ngay
            CHECK(ticks >= 50);     // luồng giao diện không bị các tín hiệu của luồng chạy chiếm hết
            CHECK(worstGap < 500);
        }
        window->close();
        delete window;
    }

    // ---- 3000 thao tác ngẫu nhiên lên danh sách chuỗi/hành động/editor, kể cả chỉ số ngoài miền ----
    {
        QFile::remove(ActionSerializer::getDefaultProfilePath()); // nạp chuỗi mẫu (KHÔNG chạy nó)
        QWidget* window = tool->createWindow();
        window->showNormal();
        QCoreApplication::processEvents();
        auto* chainList = window->findChild<ChainListWidget*>();
        auto* actionList = window->findChild<ActionListWidget*>();
        auto* editor = window->findChild<ActionEditorWidget*>();
        CHECK(chainList && actionList && editor);
        if (chainList && actionList && editor)
        {
            auto* list = chainList->findChild<QListWidget*>();
            auto* table = actionList->findChild<QTableWidget*>();
            QPushButton* addChain = buttonByText(chainList, QString::fromUtf8("+ Thêm"));
            QPushButton* cloneChain = buttonByText(chainList, QString::fromUtf8("Nhân bản"));
            QPushButton* deleteChain = buttonByText(chainList, QString::fromUtf8("Xóa"));
            QPushButton* addAction = buttonByText(actionList, QString::fromUtf8("+ Thêm hành động"));
            QPushButton* cloneAction = buttonByText(actionList, QString::fromUtf8("Nhân bản"));
            QPushButton* deleteAction = buttonByText(actionList, QString::fromUtf8("Xóa"));
            QPushButton* apply = buttonByText(editor, QString::fromUtf8("✓ Lưu hành động (Apply)"));
            QPushButton* save = buttonByText(window, QString::fromUtf8("💾 Lưu cấu hình (Save)"));
            CHECK(list && table && addChain && cloneChain && deleteChain && addAction && cloneAction && deleteAction && apply && save);
            if (list && table && addChain && cloneChain && deleteChain && addAction && cloneAction && deleteAction && apply && save)
            {
                int mismatches = 0, badSelection = 0;
                const int dialogsBefore = g_dismissedDialogs;
                for (int op = 0; op < 3000; ++op)
                {
                    const int wild = rnd(-3, table->rowCount() + 3); // chỉ số có thể ngoài miền
                    switch (rnd(0, 13))
                    {
                        case 0: if (list->count() < 12) addChain->click(); break;
                        case 1: if (list->count() < 12) cloneChain->click(); break;
                        case 2: deleteChain->click(); break; // còn 1 chuỗi thì hiện cảnh báo (tự đóng) và giữ nguyên
                        case 3: list->setCurrentRow(rnd(0, list->count() - 1)); break;
                        case 4: if (table->rowCount() < 40) addAction->click(); break;
                        case 5: if (table->rowCount() < 40) cloneAction->click(); break;
                        case 6: deleteAction->click(); break;
                        case 7: emit actionList->actionMoved(rnd(-3, table->rowCount() + 3), wild); break;
                        case 8:
                            emit actionList->deleteActionClicked(wild);
                            emit actionList->cloneActionClicked(rnd(-3, table->rowCount() + 3));
                            emit actionList->editActionClicked(rnd(-3, table->rowCount() + 3));
                            break;
                        case 9:
                            for (auto* spin : editor->findChildren<QSpinBox*>())
                                if (rnd(0, 2) == 0)
                                    spin->setValue(rnd(spin->minimum(), spin->maximum()));
                            for (auto* combo : editor->findChildren<QComboBox*>())
                                if (rnd(0, 3) == 0 && combo->count() > 0)
                                    combo->setCurrentIndex(rnd(0, combo->count() - 1));
                            apply->click();
                            break;
                        case 10: if (table->rowCount() > 0) table->setCurrentCell(rnd(0, table->rowCount() - 1), rnd(0, 5)); break;
                        case 11:
                            emit chainList->deleteChainClicked(rnd(-3, list->count() + 3));
                            emit chainList->cloneChainClicked(list->count() < 12 ? rnd(-3, list->count() + 3) : -1);
                            emit chainList->renameChainClicked(rnd(-3, list->count() + 3), randomText(g_rng, 4));
                            break;
                        case 12:
                            editor->setAction(randomAction(g_rng), wild); // editor trỏ tới hàng không tồn tại
                            if (table->rowCount() < 40)
                                apply->click();
                            break;
                        default:
                            actionList->setRunningRow(wild);
                            actionList->setRowDirty(rnd(-3, table->rowCount() + 3), rnd(0, 1));
                            actionList->setSelectedActionIndex(wild);
                            chainList->setSelectedChainIndex(rnd(-3, list->count() + 3));
                            break;
                    }
                    if (op % 16 == 0)
                        QCoreApplication::processEvents();
                    if (list->count() < 1 || list->currentRow() < 0 || list->currentRow() >= list->count())
                        ++badSelection;
                    if (table->rowCount() > 0 && (table->currentRow() < 0 || table->currentRow() >= table->rowCount()))
                        ++badSelection;

                    if (op % 60 == 59)
                    {
                        // Đối chiếu giao diện với dữ liệu thật: lưu hồ sơ (hộp thoại "đã lưu" tự đóng) rồi đọc lại.
                        save->click();
                        std::vector<ActionChain> onDisk;
                        if (!ActionSerializer::loadFromFile(ActionSerializer::getDefaultProfilePath(), onDisk) ||
                            static_cast<int>(onDisk.size()) != list->count() || list->currentRow() < 0 ||
                            list->currentRow() >= static_cast<int>(onDisk.size()) ||
                            static_cast<int>(onDisk[static_cast<size_t>(list->currentRow())].actions.size()) != table->rowCount())
                        {
                            ++mismatches;
                        }
                    }
                }
                QCoreApplication::processEvents();
                std::printf("  3000 thao tac: %d lan giao dien lech du lieu, %d lan mat lua chon, %d hop thoai tu dong\n", mismatches,
                            badSelection, g_dismissedDialogs - dialogsBefore);
                CHECK(mismatches == 0);
                CHECK(badSelection == 0);

                // Mô tả hành động "Gõ văn bản" có chứa %1/%2 (vd chuỗi mã hóa URL) phải hiện nguyên văn.
                Action percent;
                percent.type = ActionType::TypeText;
                percent.text = "a%2Fb %1 100%";
                CHECK(percent.description() == QString::fromUtf8("Gõ \"a%2Fb %1 100%\" (tức thì)"));

                // Editor: tọa độ màn hình ảo vượt 10000 px (3 màn 4K đặt ngang = 11520 px) phải giữ nguyên.
                Action wide;
                wide.type = ActionType::MouseClick;
                wide.x = 11500;
                wide.y = -12000;
                wide.startX = 15000;
                wide.endY = 20000;
                editor->setAction(wide, 0);
                const Action back = editor->getAction();
                CHECK(back.x == 11500 && back.y == -12000 && back.startX == 15000 && back.endY == 20000);
            }
        }
        window->close();
        QCoreApplication::processEvents();
        delete window;
    }
}

void stressAndroidUi()
{
    Section section("Giao dien Android: tao/an/dong/mo lai 30 vong, 3000 lan cap nhat danh sach thiet bi, hop thoai ghep doi");
    ITool* tool = ToolManager::instance().getTool("android");
    CHECK(tool != nullptr);
    if (!tool)
        return;

    const DWORD handlesBefore = handleCount();
    for (int round = 0; round < 30; ++round)
    {
        QWidget* window = tool->createWindow();
        CHECK(window == tool->createWindow());
        auto* timer = window->findChild<QTimer*>("autoRefreshTimer");
        window->showNormal();
        QCoreApplication::processEvents();
        CHECK(timer && timer->isActive());
        window->hide();
        QCoreApplication::processEvents();
        CHECK(timer && !timer->isActive());
        window->showNormal();
        window->showMinimized();
        window->showNormal();
        QCoreApplication::processEvents();
        if (round % 4 == 0)
            ToolManager::instance().stopAllBackgroundWorkForQuit();
        window->close();
        QCoreApplication::processEvents();
        CHECK(timer && !timer->isActive());
        CHECK(!ToolManager::instance().anyToolWindowBusy());
        if (round % 3 == 2)
        {
            delete window;
            QCoreApplication::processEvents();
        }
    }
    CHECK(handleCount() <= handlesBefore + 64);

    QWidget* window = tool->createWindow();
    window->showNormal();
    QCoreApplication::processEvents();
    auto* tab = window->findChild<DevicesTab*>();
    auto* lister = window->findChild<AdbDeviceLister*>();
    CHECK(tab && lister);
    if (tab && lister)
    {
        auto* table = tab->findChild<QTableWidget*>();
        QPushButton* control = buttonByText(tab, QString::fromUtf8("▶ Điều khiển"));
        CHECK(table && control);
        const auto selectedSerial = [&]() -> QString {
            const auto rows = table->selectionModel()->selectedRows();
            return rows.size() == 1 ? table->item(rows.first().row(), 0)->text() : QString();
        };
        const auto device = [](const QString& serial, const QString& state) {
            AndroidDeviceInfo d;
            d.serial = serial;
            d.state = state;
            d.model = "<b>Model</b>";
            return d;
        };

        // Ca cụ thể: đang chọn A, A bị rút, hàng đó giờ là B - lựa chọn KHÔNG được âm thầm nhảy sang B.
        emit lister->listed({device("AAA", "device"), device("BBB", "device")}, QString());
        table->selectRow(0);
        CHECK(selectedSerial() == "AAA");
        emit lister->listed({device("BBB", "device")}, QString());
        CHECK(selectedSerial().isEmpty());
        CHECK(control && !control->isEnabled());
        // Đổi thứ tự: lựa chọn đi theo thiết bị, không đi theo số hàng.
        emit lister->listed({device("AAA", "device"), device("BBB", "device")}, QString());
        table->selectRow(0);
        emit lister->listed({device("BBB", "device"), device("CCC", "offline"), device("AAA", "device")}, QString());
        CHECK(selectedSerial() == "AAA");

        const QStringList serials = {"AAA", "BBB", "CCC", "192.168.1.9:5555", "emulator-5554", "<i>html</i>"};
        const QStringList states = {"device", "unauthorized", "offline", "no permissions", "connecting"};
        int jumped = 0, wrongButton = 0;
        for (int i = 0; i < 3000; ++i)
        {
            const QString before = selectedSerial();
            QList<AndroidDeviceInfo> devices;
            QString selectedState;
            for (const QString& s : serials)
                if (rnd(0, 2))
                    devices.insert(rnd(0, static_cast<int>(devices.size())), device(s, states[rnd(0, static_cast<int>(states.size()) - 1)]));
            emit lister->listed(devices, rnd(0, 9) == 0 ? randomText(g_rng, 5) : QString());

            const QString after = selectedSerial();
            bool stillThere = false;
            for (const AndroidDeviceInfo& d : devices)
            {
                if (d.serial == before) stillThere = true;
                if (d.serial == after) selectedState = d.state;
            }
            if (after != (stillThere ? before : QString()))
                ++jumped;
            if (control && control->isEnabled() != (!after.isEmpty() && selectedState == "device"))
                ++wrongButton;
            if (table->rowCount() != devices.size())
                ++jumped;
            if (rnd(0, 2) == 0 && table->rowCount() > 0)
                table->selectRow(rnd(0, table->rowCount() - 1));
            else if (rnd(0, 9) == 0)
                table->clearSelection();
            if (i % 64 == 0)
                QCoreApplication::processEvents();
        }
        std::printf("  3000 lan cap nhat danh sach: %d lan lua chon nhay sang thiet bi khac, %d lan nut Dieu khien sai trang thai\n",
                    jumped, wrongButton);
        CHECK(jumped == 0);
        CHECK(wrongButton == 0);
    }
    window->close();
    QCoreApplication::processEvents();
    delete window;

    // ---- Hộp thoại ghép đôi: 5000 cặp chuỗi ngẫu nhiên - chỉ "Accepted" khi cả hai đúng định dạng ----
    {
        WirelessPairDialog dialog;
        const auto edits = dialog.findChildren<QLineEdit*>();
        CHECK(edits.size() == 2);
        int wronglyAccepted = 0, accepted = 0;
        if (edits.size() == 2)
        {
            const QStringList addr = {"192.168.1.23:41234", "10.0.0.5:5555", " 10.0.0.5:5555 ", "--help", "-s:1", "a:0", "a:65536", "[::1]:7", ""};
            for (int i = 0; i < 5000; ++i)
            {
                dialog.setResult(QDialog::Rejected);
                edits[0]->setText(rnd(0, 1) ? addr[rnd(0, static_cast<int>(addr.size()) - 1)] : randomText(g_rng, 4));
                edits[1]->setText(rnd(0, 1) ? QString::number(rnd(0, 999999)).rightJustified(rnd(4, 6), '0') : randomText(g_rng, 3));
                dialog.accept();
                if (dialog.result() == QDialog::Accepted)
                {
                    ++accepted;
                    if (!AdbController::isValidIpAndPort(dialog.ipAndPort()) || !AdbController::isValidPairingCode(dialog.pairingCode()))
                        ++wronglyAccepted;
                }
            }
        }
        CHECK(accepted > 100);
        CHECK(wronglyAccepted == 0);
    }
}

/// Hộp thoại tiến độ tải bản cập nhật của MainWindow: bấm "Ẩn" thì phải ẨN HẲN cho tới khi tải xong - dựng
/// lại đúng cấu hình QProgressDialog + cách nối tín hiệu của MainWindow::onUpdateAvailable().
void checkUpdateProgressDialogHide()
{
    Section section("Hop thoai tien do cap nhat: bam An xong khong duoc tu hien lai");
    g_autoDismissDialogs = false; // hộp thoại ở đây là modal theo cửa sổ - tự bấm nút của nó, không để bị tự đóng
    QWidget parent;
    QProgressDialog progress("Dang tai...", "An", 0, 100, &parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QObject::connect(&progress, &QProgressDialog::canceled, &progress, &QProgressDialog::hide);

    // Cùng logic với lambda nối UpdateInstaller::progress trong MainWindow.cpp.
    const auto onProgress = [&progress](qint64 received, qint64 total) {
        if (progress.wasCanceled())
            return;
        if (total > 0)
        {
            progress.setMaximum(static_cast<int>(total / 1024));
            progress.setValue(static_cast<int>(received / 1024));
        }
        else
        {
            progress.setMaximum(0);
        }
    };

    onProgress(100 * 1024, 5000 * 1024);
    onProgress(200 * 1024, 5000 * 1024);
    spinFor(30);
    CHECK(progress.isVisible());

    auto* hideButton = progress.findChild<QPushButton*>();
    CHECK(hideButton != nullptr);
    if (hideButton)
        hideButton->click();
    QCoreApplication::processEvents();
    CHECK(!progress.isVisible());
    for (int kb = 300; kb <= 5000; kb += 100)
    {
        onProgress(kb * 1024, 5000 * 1024);
        QCoreApplication::processEvents();
    }
    onProgress(0, 0);
    spinFor(30);
    CHECK(!progress.isVisible()); // tải tiếp/tải xong: hộp thoại đã ẩn thì vẫn ẩn

    // Hành vi của chính Qt mà bản cũ vấp phải (không có kiểm tra wasCanceled): setValue() sau khi hủy tự
    // gọi show() lại - ghi nhận để nếu Qt đổi hành vi thì biết.
    QProgressDialog raw("x", "An", 0, 100, &parent);
    raw.setMinimumDuration(0);
    raw.setAutoClose(false);
    raw.setAutoReset(false);
    QObject::connect(&raw, &QProgressDialog::canceled, &raw, &QProgressDialog::hide);
    raw.setValue(10);
    raw.setValue(20);
    spinFor(30);
    if (auto* b = raw.findChild<QPushButton*>())
        b->click();
    QCoreApplication::processEvents();
    const bool hiddenAfterClick = !raw.isVisible();
    raw.setValue(30);
    spinFor(30);
    std::printf("  QProgressDialog tran: sau khi bam An %s; setValue() tiep theo lam no %s\n", hiddenAfterClick ? "an" : "VAN HIEN",
                raw.isVisible() ? "HIEN LAI" : "van an");
    raw.close();
    g_autoDismissDialogs = true;
}

// Tệp log không mở được (lỗi tạm thời) phải được thử mở lại có giới hạn tần suất - trước đây mở thất bại lúc
// khởi tạo là tắt log tệp cho cả phiên. Kèm closeFiles()/reopenFiles() dành cho test (xem tests/TestDataDir.h).
void checkLoggerReopen(const QString& mainOverride)
{
    std::printf("[logger] thu mo lai tep log sau loi tam thoi\n");
    QTemporaryDir blocked;
    CHECK(blocked.isValid());
    const QString appLog = blocked.path() + "/logs/app.log";
    // "Lỗi tạm thời" dựng bằng một THƯ MỤC trùng tên tệp log: mở tệp chắc chắn thất bại cho tới khi gỡ nó đi.
    CHECK(QDir().mkpath(appLog));
    AppPaths::setDataDirOverride(blocked.path());
    Logger::instance().reopenFiles();

    const int every = Logger::kReopenRetryEveryCalls;
    for (int i = 0; i < every * 3; ++i) // 3 lượt thử mở lại, đều thất bại: không crash, không treo
        Logger::instance().info("Core", "dong-bi-mat");
    CHECK(QFileInfo(appLog).isDir());

    CHECK(QDir().rmdir(appLog)); // lỗi tạm thời đã hết
    for (int i = 0; i < every - 1; ++i)
        Logger::instance().info("Core", "chua-toi-luot-thu");
    CHECK(!QFileInfo::exists(appLog)); // chưa tới lượt thử: không gọi hệ thống cho từng dòng log
    Logger::instance().warning("Core", "da-mo-lai"); // lần thứ `every`: mở lại được, Warning ghi + flush ngay
    CHECK(QFileInfo(appLog).isFile());
    {
        QFile f(appLog);
        CHECK(f.open(QIODevice::ReadOnly));
        const QByteArray content = f.readAll();
        CHECK(content.contains("da-mo-lai"));
        CHECK(!content.contains("dong-bi-mat") && !content.contains("chua-toi-luot-thu"));
    }

    // closeFiles(): đóng THẬT (Windows cho xóa tệp), không ghi nữa và không tự mở lại.
    Logger::instance().closeFiles();
    CHECK(QFile::remove(appLog));
    for (int i = 0; i < every * 2 + 5; ++i)
        Logger::instance().warning("AutoClick", "sau-khi-dong");
    CHECK(!QFileInfo::exists(appLog));

    AppPaths::setDataDirOverride(mainOverride);
    Logger::instance().reopenFiles();
    Logger::instance().warning("Core", "tro-lai-thu-muc-chinh");
    CHECK(QFileInfo(mainOverride + "/logs/app.log").size() > 0);
    CHECK(QDir(blocked.path()).removeRecursively()); // không còn tệp nào bị Logger giữ trong thư mục cũ
}

// Hủy/hết giờ phải dừng cả tiến trình do script sinh ra, không chỉ powershell.exe (trước đây tiến trình con
// mồ côi chạy tiếp ngầm). Tiến trình con ở đây vô hại: ping vòng lặp nội bộ, tự thoát sau ~100 giây.
void checkPowerShellKillsDescendants()
{
    std::printf("[powershell] huy/het gio dung ca tien trinh chau\n");
    for (int mode = 0; mode < 2; ++mode) // 0 = hủy bằng cờ, 1 = hết thời gian chờ
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString marker = QDir::toNativeSeparators(tmp.path() + "/child.pid");
        const QString script =
            QString("$p = Start-Process -FilePath (Join-Path $env:SystemRoot 'System32\\PING.EXE') "
                    "-ArgumentList '-n','100','127.0.0.1' -WindowStyle Hidden -PassThru; "
                    "[IO.File]::WriteAllText('%1', [string]$p.Id); Start-Sleep -Seconds 100")
                .arg(PowerShellRunner::quoteLiteral(marker));

        std::atomic_bool cancel{false};
        std::atomic_bool done{false};
        std::atomic_bool aliveBefore{false};
        std::atomic<HANDLE> child{nullptr};
        // Luồng theo dõi: chờ script ghi PID của tiến trình con, giữ một handle tới nó (handle giữ đúng tiến
        // trình đó dù PID có bị tái sử dụng), xác nhận nó ĐANG chạy, rồi (chế độ 0) mới bật cờ hủy.
        std::thread watcher([&]() {
            QElapsedTimer t;
            t.start();
            while (!done.load() && t.elapsed() < 60000)
            {
                QFile f(marker);
                const DWORD pid = f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readAll()).trimmed().toULong() : 0;
                if (pid != 0)
                {
                    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, pid);
                    if (h)
                    {
                        aliveBefore = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
                        child = h;
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (mode == 0)
                cancel = true;
        });

        QElapsedTimer timer;
        timer.start();
        bool ok = true;
        QString error;
        PowerShellRunner::runCancelable(script, mode == 0 ? &cancel : nullptr, &ok, &error, mode == 0 ? 100000 : 20000);
        const qint64 elapsed = timer.elapsed();
        done = true;
        watcher.join();

        CHECK(!ok);
        CHECK(error == QString::fromUtf8(mode == 0 ? "Đã hủy." : "Hết thời gian chờ PowerShell"));
        HANDLE h = child.load();
        CHECK(h != nullptr);       // script đã kịp sinh tiến trình con trước khi bị dừng
        CHECK(aliveBefore.load()); // và nó đang chạy thật ngay trước lúc hủy
        bool childGone = false;
        if (h)
        {
            childGone = WaitForSingleObject(h, 5000) == WAIT_OBJECT_0;
            CHECK(childGone);
            if (!childGone)
                TerminateProcess(h, 1); // dọn, không để ping chạy tiếp sau bộ test
            CloseHandle(h);
        }
        std::printf("  %s: tra ve sau %lld ms, tien trinh con %s\n", mode == 0 ? "huy" : "het gio",
                    static_cast<long long>(elapsed), !h ? "KHONG THAY" : (childGone ? "da dung" : "VAN CHAY"));
    }
}

} // namespace

int main(int argc, char** argv)
{
    // Không bao giờ vẽ cửa sổ thật lên màn hình của máy đang chạy test (và không để Qt bật hộp thoại lỗi
    // khi thư mục chạy thiếu plugin offscreen - xem tests/OffscreenPlatform.h).
    if (!TestPlatform::forceOffscreen(argv[0]))
        return 2;

    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false); // đóng cửa sổ cuối cùng giữa chừng không được kết thúc bộ test

    TestDataDir dataDir;
    CHECK(dataDir.isValid());
    const QString mainOverride = dataDir.path();
    AppPaths::setDataDirOverride(mainOverride); // TRƯỚC mọi thứ đụng Logger/tệp hồ sơ

    // Logger in MỌI dòng ra kênh debug - hàng trăm nghìn dòng trong bộ này chỉ làm ngập đầu ra.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});

    unsigned seed = (argc > 1) ? QString::fromLocal8Bit(argv[1]).toUInt() : 0;
    if (seed == 0)
        seed = static_cast<unsigned>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF) | 1u;
    if (argc > 2)
        g_onlyGroup = QString::fromLocal8Bit(argv[2]);
    g_rng.seed(seed);
    std::printf("core_stress_tests seed=%u nhom=%s (chay lai dung ca nay: core_stress_tests.exe %u)\n", seed,
                g_onlyGroup.isEmpty() ? "tat-ca" : qPrintable(g_onlyGroup), seed);

    // Hộp thoại modal bật lên giữa các kịch bản giao diện (cảnh báo "phải giữ ít nhất một chuỗi", "đã lưu"...)
    // không có ai bấm - tự đóng để bộ test không treo, và đếm lại.
    QTimer dialogCloser;
    dialogCloser.setInterval(5);
    QObject::connect(&dialogCloser, &QTimer::timeout, &app, []() {
        QWidget* modal = g_autoDismissDialogs ? QApplication::activeModalWidget() : nullptr;
        if (modal)
        {
            ++g_dismissedDialogs;
            if (auto* dialog = qobject_cast<QDialog*>(modal))
                dialog->reject();
            else
                modal->close();
        }
    });
    dialogCloser.start();

    QElapsedTimer total;
    total.start();

    if (wanted("logger"))
        stressLogger();
    if (wanted("logger"))
        checkLoggerReopen(mainOverride);
    if (wanted("apppaths"))
    {
        stressAppPathsThreads(mainOverride);
        stressAppPathsMigration(mainOverride);
    }
    if (wanted("powershell"))
    {
        stressPowerShellOutput();
        stressPowerShellQuoting();
        stressPowerShellConcurrency();
        checkPowerShellKillsDescendants();
    }
    if (wanted("process"))
        stressWinProcessTree();
    if (wanted("update"))
    {
        fuzzCompareVersions();
        fuzzParseLatestRelease();
        fuzzTrustedUrl();
        fuzzInstallClassification();
        fuzzHelperScript();
    }
    if (wanted("serializer"))
        fuzzActionSerializer();
    if (wanted("runner"))
        stressActionRunner();
    if (wanted("hotkey"))
        stressStopHotkey();
    if (wanted("android"))
        fuzzAndroidCore();

    // Các công cụ đăng ký qua ToolManager đúng như MainWindow::registerTools(). CHỈ hai công cụ thuộc phạm
    // vi bộ test này: bảy công cụ còn lại khi dựng cửa sổ sẽ quét WiFi/mở camera/gọi Defender/phát UDP...
    // và đã có bộ *_ui_tests riêng.
    ToolManager::instance().registerTool(std::make_unique<AutoClickTool>());
    ToolManager::instance().registerTool(std::make_unique<AndroidControlTool>());
    CHECK(ToolManager::instance().getAllTools().size() == 2);
    CHECK(ToolManager::instance().getTool("khong-co") == nullptr);
    ToolManager::instance().registerTool(nullptr); // bỏ qua, không thêm phần tử rỗng
    CHECK(ToolManager::instance().getAllTools().size() == 2);
    QString busyReason = "giu-nguyen";
    CHECK(!ToolManager::instance().anyToolWindowBusy(&busyReason) && busyReason == "giu-nguyen");
    ToolManager::instance().stopAllBackgroundWorkForQuit(); // chưa có cửa sổ nào: không làm gì, không crash

    if (wanted("ui autoclick"))
        stressAutoClickUi();
    if (wanted("ui android"))
        stressAndroidUi();
    if (wanted("ui update"))
        checkUpdateProgressDialogHide();

    ToolManager::instance().stopAllBackgroundWorkForQuit();
    dialogCloser.stop();

    std::printf("tong thoi gian: %lld ms, hop thoai tu dong: %d\n", static_cast<long long>(total.elapsed()), g_dismissedDialogs);
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: bo nay KHONG gui chuot/phim that, khong cai hook, khong chay script cap nhat/trinh cai dat,\n");
    std::printf("khong goi adb/scrcpy that - cac duong do van phai kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
