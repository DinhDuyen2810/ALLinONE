#include "ConsolePaste.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QStringList>

#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
using ConsolePaste::Outcome;

/// Số dòng tối đa (tính ngược từ dòng chứa con trỏ) được đọc lại mỗi lần - đủ cho lệnh rất dài đã tự xuống
/// dòng, không đọc cả bộ đệm 9000 dòng mỗi 150 ms.
constexpr SHORT kRowsToRead = 400;
/// PowerShell đứng yên ở dấu nhắc lâu chừng này mà lệnh chưa hiện ra = phím Ctrl+V không được xử lý.
constexpr qint64 kIdlePromptMs = 3000;
/// Thời gian tối đa chờ console của PowerShell sẵn sàng cho AttachConsole (tiến trình vừa được tạo).
constexpr qint64 kAttachWaitMs = 5000;
constexpr DWORD kPollMs = 150;

struct ScreenSnapshot
{
    QString rows;            ///< Các dòng nối liền nhau (không chèn dấu xuống dòng - dòng tự gãy vẫn liền mạch)
    QString cursorLineBefore; ///< Phần dòng hiện tại nằm TRƯỚC con trỏ
    bool ok{false};
};

ScreenSnapshot readScreen(HANDLE out)
{
    ScreenSnapshot snap;
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(out, &info) || info.dwSize.X <= 0)
        return snap;

    const SHORT width = info.dwSize.X;
    const SHORT cursorY = info.dwCursorPosition.Y;
    const SHORT firstRow = cursorY >= kRowsToRead ? static_cast<SHORT>(cursorY - kRowsToRead + 1) : 0;
    std::wstring line(static_cast<size_t>(width), L' ');
    for (SHORT y = firstRow; y <= cursorY; ++y)
    {
        DWORD read = 0;
        if (!ReadConsoleOutputCharacterW(out, line.data(), static_cast<DWORD>(width), COORD{0, y}, &read))
            return snap;
        const QString row = QString::fromWCharArray(line.data(), static_cast<qsizetype>(read));
        snap.rows += row;
        if (y == cursorY)
            snap.cursorLineBefore = row.left(info.dwCursorPosition.X);
    }
    snap.ok = true;
    return snap;
}

void fillKey(INPUT_RECORD& rec, bool down, WORD vk, DWORD controlState)
{
    rec = INPUT_RECORD{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = down ? TRUE : FALSE;
    rec.Event.KeyEvent.wRepeatCount = 1;
    rec.Event.KeyEvent.wVirtualKeyCode = vk;
    rec.Event.KeyEvent.wVirtualScanCode = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    // KHÔNG kèm ký tự (0x16): PSReadLine vẫn nhận ra Ctrl+V qua mã phím + trạng thái Ctrl, còn bộ đọc dòng thường
    // của console (không có PSReadLine) thì bỏ qua phím không mang ký tự thay vì chèn "^V" vào dòng lệnh.
    rec.Event.KeyEvent.uChar.UnicodeChar = 0;
    rec.Event.KeyEvent.dwControlKeyState = controlState;
}

/// Ctrl xuống, V xuống, V lên, Ctrl lên - đúng trình tự bàn phím thật sinh ra.
bool writeCtrlV(HANDLE in)
{
    INPUT_RECORD records[4];
    fillKey(records[0], true, VK_CONTROL, LEFT_CTRL_PRESSED);
    fillKey(records[1], true, 'V', LEFT_CTRL_PRESSED);
    fillKey(records[2], false, 'V', LEFT_CTRL_PRESSED);
    fillKey(records[3], false, VK_CONTROL, 0);
    DWORD written = 0;
    return WriteConsoleInputW(in, records, 4, &written) && written == 4;
}

bool processExited(HANDLE process)
{
    return process == nullptr || WaitForSingleObject(process, 0) == WAIT_OBJECT_0;
}

/// Gắn vào console của `pid` trong suốt phạm vi; tự rời khi ra khỏi phạm vi.
class AttachedConsole
{
public:
    AttachedConsole() = default;
    ~AttachedConsole() { detach(); }
    AttachedConsole(const AttachedConsole&) = delete;
    AttachedConsole& operator=(const AttachedConsole&) = delete;

    bool attach(DWORD pid)
    {
        if (!AttachConsole(pid))
            return false;
        m_attached = true;
        // Ctrl+C người dùng bấm trong cửa sổ PowerShell lúc tiến trình phụ đang gắn không được làm nó thoát
        // giữa chừng (cờ này chỉ áp cho tiến trình phụ, nó không tạo tiến trình con nào).
        SetConsoleCtrlHandler(nullptr, TRUE);
        m_in = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
        m_out = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr);
        if (m_in != INVALID_HANDLE_VALUE && m_out != INVALID_HANDLE_VALUE)
            return true;
        detach(); // gắn được nhưng chưa mở được bộ đệm (console đang khởi tạo) - rời ra để lần sau thử lại sạch
        return false;
    }

    void detach()
    {
        if (m_in != INVALID_HANDLE_VALUE)
            CloseHandle(m_in);
        if (m_out != INVALID_HANDLE_VALUE)
            CloseHandle(m_out);
        m_in = m_out = INVALID_HANDLE_VALUE;
        if (m_attached)
            FreeConsole();
        m_attached = false;
    }

    HANDLE in() const { return m_in; }
    HANDLE out() const { return m_out; }

private:
    bool m_attached{false};
    HANDLE m_in{INVALID_HANDLE_VALUE};
    HANDLE m_out{INVALID_HANDLE_VALUE};
};
} // namespace

namespace ConsolePaste
{

namespace internal
{
QString asciiLettersAndDigits(const QString& text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar c : text)
    {
        const char16_t u = c.unicode();
        if ((u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9'))
            out += c;
    }
    return out;
}

QString verificationKey(const QString& text, int maxChars)
{
    const QStringList lines = text.split('\n');
    for (const QString& line : lines)
    {
        if (line.trimmed().isEmpty())
            continue;
        return asciiLettersAndDigits(line).left(qMax(0, maxChars));
    }
    return {};
}

int countOccurrences(const QString& haystack, const QString& needle)
{
    if (needle.isEmpty())
        return 0;
    int count = 0;
    for (qsizetype from = haystack.indexOf(needle); from >= 0; from = haystack.indexOf(needle, from + needle.size()))
        ++count;
    return count;
}

bool looksLikeIdlePrompt(const QString& cursorLineBeforeCursor)
{
    const QString t = cursorLineBeforeCursor.trimmed();
    return !t.isEmpty() && t.endsWith('>');
}
} // namespace internal

Outcome pasteAndVerify(quint32 pid, const QString& expected, int timeoutMs)
{
    if (pid == 0)
        return Outcome::BadArguments;

    HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process == nullptr)
        return Outcome::ProcessExited;
    struct CloseOnExit
    {
        HANDLE h;
        ~CloseOnExit() { CloseHandle(h); }
    } closeProcess{process};

    FreeConsole(); // tiến trình phụ (ứng dụng GUI) vốn không có console - phòng khi được gọi từ nơi có

    QElapsedTimer clock;
    clock.start();
    const qint64 deadline = qMax(timeoutMs, 1000);

    // Tiến trình vừa được tạo có thể chưa kịp có console - thử lại tới khi gắn được hoặc nó đã thoát.
    AttachedConsole console;
    bool attached = false;
    while (true)
    {
        attached = console.attach(pid);
        if (attached || processExited(process))
            break;
        if (clock.elapsed() >= qMin<qint64>(kAttachWaitMs, deadline))
            break;
        Sleep(50);
    }
    if (!attached)
        return processExited(process) ? Outcome::ProcessExited : Outcome::AttachFailed;

    const QString key = internal::verificationKey(expected);
    const ScreenSnapshot before = readScreen(console.out());
    const int baseline = internal::countOccurrences(internal::asciiLettersAndDigits(before.rows), key);

    if (!writeCtrlV(console.in()))
        return processExited(process) ? Outcome::ProcessExited : Outcome::WriteFailed;
    if (key.isEmpty())
        return Outcome::SentUnverified;

    QString lastRows;
    qint64 idleSince = -1;
    while (clock.elapsed() < deadline)
    {
        Sleep(kPollMs);
        if (processExited(process))
            return Outcome::ProcessExited;

        const ScreenSnapshot now = readScreen(console.out());
        if (!now.ok)
            continue;
        if (internal::countOccurrences(internal::asciiLettersAndDigits(now.rows), key) > baseline)
            return Outcome::Pasted;

        // Màn hình đứng yên ở dấu nhắc: PowerShell đã sẵn sàng nhận phím từ lâu mà lệnh vẫn không hiện ra.
        if (now.rows == lastRows && internal::looksLikeIdlePrompt(now.cursorLineBefore))
        {
            if (idleSince < 0)
                idleSince = clock.elapsed();
            else if (clock.elapsed() - idleSince >= kIdlePromptMs)
                return Outcome::PromptIdle;
        }
        else
        {
            idleSince = -1;
        }
        lastRows = now.rows;
    }
    return Outcome::NotSeen;
}

int runHelperMode(int argc, char** argv)
{
    if (argc < 3)
        return static_cast<int>(Outcome::BadArguments);
    char* end = nullptr;
    const unsigned long pid = std::strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || pid == 0)
        return static_cast<int>(Outcome::BadArguments);
    int timeoutMs = kDefaultTimeoutMs;
    if (argc >= 4)
    {
        const long t = std::strtol(argv[3], &end, 10);
        if (end != argv[3] && *end == '\0' && t >= 1000 && t <= 120000)
            timeoutMs = static_cast<int>(t);
    }

    // Đọc HẾT stdin (lệnh cần đối chiếu, UTF-8) TRƯỚC khi gắn vào console nào.
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    if (input == nullptr || input == INVALID_HANDLE_VALUE)
        return static_cast<int>(Outcome::BadArguments);
    QByteArray data;
    char buffer[8192];
    while (data.size() < 8 * 1024 * 1024)
    {
        DWORD read = 0;
        if (!ReadFile(input, buffer, sizeof(buffer), &read, nullptr) || read == 0)
            break;
        data.append(buffer, static_cast<qsizetype>(read));
    }
    const QString expected = QString::fromUtf8(data);
    if (expected.trimmed().isEmpty())
        return static_cast<int>(Outcome::BadArguments);

    return static_cast<int>(pasteAndVerify(static_cast<quint32>(pid), expected, timeoutMs));
}

} // namespace ConsolePaste
