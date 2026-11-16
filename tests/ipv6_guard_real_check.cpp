// KIỂM TRA TAY (cần Administrator - KHÔNG chạy như test tự động, quy tắc 5): bộ lọc chặn rò rỉ IPv6 có THẬT SỰ chặn
// không, và có THẬT SỰ tự biến mất khi phiên/tiến trình giữ nó kết thúc không.
//
// Máy build không có IPv6 ra Internet, nên ngoài các dải thật (2000::/3, NAT64) công cụ chặn THÊM ::1/128 (loopback) làm
// vật thử: kết nối TCP tới một cổng đang nghe trên [::1] phải bị Windows từ chối (WSAEACCES 10013) khi bộ lọc bật, và
// thông lại ngay khi bộ lọc gỡ. Cùng cơ chế (tầng ALE, điều kiện địa chỉ đích) áp cho 2000::/3 trên máy có IPv6.
// Bộ lọc nằm trong phiên WFP ĐỘNG - mọi thứ tự gỡ khi công cụ thoát/crash; chỉ ảnh hưởng [::1] vài giây.
//
// Chạy: Start-Process build\ipv6_guard_real_check.exe -ArgumentList <tệp-nhật-ký.txt> -Verb RunAs -Wait
// (OneForAll.exe phải nằm cùng thư mục - dùng cho ca tiến trình phụ thật "--ipv6-guard").
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTimer>

#include "core/WinElevation.h"
#include "tools/vpn/engine/Ipv6GuardSession.h"
#include "tools/vpn/engine/Ipv6LeakGuard.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <string>

static int g_fail = 0, g_pass = 0;
static FILE* g_log = nullptr;

static void logf(const char* fmt, const std::string& a = std::string())
{
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt, a.c_str());
    std::fputs(buf, stdout);
    if (g_log)
    {
        std::fputs(buf, g_log);
        std::fflush(g_log);
    }
}

#define CHECK(cond)                                                                                                  \
    do                                                                                                               \
    {                                                                                                                \
        if (cond)                                                                                                    \
            ++g_pass;                                                                                                \
        else                                                                                                         \
        {                                                                                                            \
            ++g_fail;                                                                                                \
            logf("FAIL line %s: " #cond "\n", std::to_string(__LINE__));                                              \
        }                                                                                                            \
    } while (0)

namespace
{
/// Thử kết nối TCP (không chặn, chờ tối đa 1,5 giây). Trả về 0 nếu nối được, ngược lại mã WSA.
int tryConnect(const char* ip, unsigned short port, bool v6)
{
    SOCKET s = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET)
        return WSAGetLastError();
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);
    int rc;
    if (v6)
    {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_port = htons(port);
        inet_pton(AF_INET6, ip, &a.sin6_addr);
        rc = connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    else
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(port);
        inet_pton(AF_INET, ip, &a.sin_addr);
        rc = connect(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }
    int result = 0;
    if (rc == SOCKET_ERROR)
    {
        result = WSAGetLastError();
        if (result == WSAEWOULDBLOCK)
        {
            fd_set w, e;
            FD_ZERO(&w);
            FD_ZERO(&e);
            FD_SET(s, &w);
            FD_SET(s, &e);
            timeval tv{1, 500000};
            const int n = select(0, nullptr, &w, &e, &tv);
            if (n == 0)
                result = WSAETIMEDOUT;
            else if (FD_ISSET(s, &e))
            {
                int err = 0;
                int len = sizeof(err);
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
                result = err ? err : WSAECONNREFUSED;
            }
            else
                result = 0;
        }
    }
    closesocket(s);
    return result;
}

SOCKET listenOn(bool v6, unsigned short* port)
{
    SOCKET s = socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (v6)
    {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6;
        a.sin6_addr = in6addr_loopback;
        bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        int len = sizeof(a);
        getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
        *port = ntohs(a.sin6_port);
    }
    else
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        int len = sizeof(a);
        getsockname(s, reinterpret_cast<sockaddr*>(&a), &len);
        *port = ntohs(a.sin_port);
    }
    listen(s, 16);
    return s;
}

quint64 loopbackLuid()
{
    for (const auto& i : Ipv6LeakGuard::snapshot())
        if (i.ifType == 24)
            return i.luid;
    return 0;
}

/// PID các tiến trình OneForAll.exe đang chạy (để tìm tiến trình phụ vừa sinh).
QList<DWORD> oneForAllPids()
{
    QList<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
        do
        {
            if (_wcsicmp(pe.szExeFile, L"OneForAll.exe") == 0)
                pids << pe.th32ProcessID;
        } while (Process32NextW(snap, &pe));
    CloseHandle(snap);
    return pids;
}

void spin(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Chờ tới khi kết nối [::1]:port cho kết quả mong muốn (0 = nối được) hoặc hết giờ; trả về mã cuối cùng.
int waitConnect(unsigned short port, bool wantOk, int ms)
{
    QElapsedTimer t;
    t.start();
    int rc = tryConnect("::1", port, true);
    while ((rc == 0) != wantOk && t.elapsed() < ms)
    {
        spin(200);
        rc = tryConnect("::1", port, true);
    }
    return rc;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc >= 2)
        g_log = std::fopen(argv[1], "w");
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    if (!WinElevation::isElevated())
    {
        logf("CAN quyen Administrator: Start-Process ipv6_guard_real_check.exe -ArgumentList <log> -Verb RunAs -Wait\n");
        return 2;
    }
    unsigned short p6 = 0, p4 = 0;
    SOCKET l6 = listenOn(true, &p6);
    SOCKET l4 = listenOn(false, &p4);
    logf("Nghe tren [::1]:%s", std::to_string(p6));
    logf(" va 127.0.0.1:%s\n", std::to_string(p4));

    CHECK(tryConnect("::1", p6, true) == 0);
    CHECK(tryConnect("127.0.0.1", p4, false) == 0);
    const int beforeGlobal = tryConnect("2001:4860:4860::8888", 53, true);
    logf("Truoc khi chan: [2001:4860:4860::8888]:53 -> ma %s (khong co IPv6 thi la 10051/10065)\n",
         std::to_string(beforeGlobal));

    Ipv6LeakGuard::Prefix loop128;
    Ipv6LeakGuard::parsePrefix("::1/128", &loop128);

    // ---- 1) WfpBlock trong tiến trình: chặn ----
    {
        Ipv6LeakGuard::WfpBlock block;
        Ipv6LeakGuard::WfpBlock::Config cfg;
        cfg.extraBlockedForTest = {loop128};
        QString err;
        const bool ok = block.apply(cfg, &err);
        logf("apply(chan ::1/128 + dai that): %s", ok ? "OK" : "LOI ");
        logf("%s\n", err.toStdString());
        CHECK(ok && block.isActive() && block.filterCount() == 2);
        const int rc = tryConnect("::1", p6, true);
        logf("  [::1] khi dang chan -> ma %s (mong 10013 WSAEACCES)\n", std::to_string(rc));
        CHECK(rc == WSAEACCES);
        CHECK(tryConnect("127.0.0.1", p4, false) == 0); // IPv4 không bị đụng tới
        const int g = tryConnect("2001:4860:4860::8888", 53, true);
        logf("  [2001:4860:4860::8888]:53 khi dang chan -> ma %s\n", std::to_string(g));

        // ---- 2) cho phép qua card loopback (như cho phép card đường hầm VPN) ----
        cfg.allowInterfaceLuid = loopbackLuid();
        CHECK(cfg.allowInterfaceLuid != 0);
        CHECK(block.apply(cfg, &err) && block.filterCount() == 4);
        const int rcAllowIf = tryConnect("::1", p6, true);
        logf("  [::1] khi cho phep card loopback -> ma %s (mong 0)\n", std::to_string(rcAllowIf));
        CHECK(rcAllowIf == 0);

        // ---- 3) cho phép địa chỉ máy chủ VPN ----
        cfg.allowInterfaceLuid = 0;
        Ipv6LeakGuard::Addr16 one{};
        Ipv6LeakGuard::parseAddress("::1", &one);
        cfg.allowRemote = {one};
        CHECK(block.apply(cfg, &err) && block.filterCount() == 4);
        const int rcAllowRemote = tryConnect("::1", p6, true);
        logf("  [::1] khi cho phep dia chi may chu -> ma %s (mong 0)\n", std::to_string(rcAllowRemote));
        CHECK(rcAllowRemote == 0);

        cfg.allowRemote.clear();
        CHECK(block.apply(cfg, &err));
        CHECK(tryConnect("::1", p6, true) == WSAEACCES);

        // ---- 4) close() = gỡ hết ----
        block.close();
        const int rcAfter = tryConnect("::1", p6, true);
        logf("  [::1] sau close() -> ma %s (mong 0)\n", std::to_string(rcAfter));
        CHECK(rcAfter == 0);
    }

    const QString exe = QCoreApplication::applicationDirPath() + "/OneForAll.exe";
    if (!QFile::exists(exe))
    {
        logf("THIEU %s - bo qua ca tien trinh phu.\n", exe.toStdString());
        ++g_fail;
    }
    else
    {
        Ipv6LeakGuard::Addr16 g{};
        Ipv6LeakGuard::parseAddress("2405:4802:1::10", &g);
        Ipv6LeakGuard::InterfaceInfo fakeWifi;
        fakeWifi.alias = "Wi-Fi";
        fakeWifi.luid = 1;
        fakeWifi.ifType = 71;
        fakeWifi.up = true;
        fakeWifi.globalAddresses = {g};
        fakeWifi.hasDefaultRoute = true;
        auto makeSession = [&](Ipv6GuardSession& s) {
            Ipv6GuardSession::TestHooks h;
            h.snapshot = [&]() { return QList<Ipv6LeakGuard::InterfaceInfo>{fakeWifi}; }; // ép "có IPv6"
            h.helperProgram = exe;
            h.launchWithoutElevation = true; // công cụ đã là Admin - tiến trình con thừa hưởng
            h.extraBlock = {"::1/128"};
            s.setTestHooks(h);
        };
        auto waitReady = [](Ipv6GuardSession& s, bool* ok) {
            QEventLoop loop;
            bool got = false;
            QObject::connect(&s, &Ipv6GuardSession::ready, &loop, [&](bool k, QString) {
                *ok = k;
                got = true;
                loop.quit();
            });
            QTimer::singleShot(15000, &loop, &QEventLoop::quit);
            loop.exec();
            return got;
        };

        // ---- 5) Tiến trình phụ thật: chặn, rồi "release" gỡ ngay ----
        {
            Ipv6GuardSession s;
            makeSession(s);
            bool ok = false;
            s.begin("OneForAll-IPv6-RealCheck-KhongTonTai", {});
            CHECK(waitReady(s, &ok) && ok);
            CHECK(s.state() == Ipv6GuardSession::State::Active);
            const int rc = waitConnect(p6, false, 3000);
            logf("Tien trinh phu: [::1] khi Active -> ma %s (mong 10013)\n", std::to_string(rc));
            CHECK(rc == WSAEACCES);
            s.release();
            const int rc2 = waitConnect(p6, true, 4000);
            logf("Tien trinh phu: [::1] sau release -> ma %s (mong 0)\n", std::to_string(rc2));
            CHECK(rc2 == 0);
        }
        // ---- 6) Ứng dụng "crash" (đóng kênh, không release) + VPN không tồn tại -> tiến trình phụ tự thoát, gỡ chặn ----
        {
            const QList<DWORD> before = oneForAllPids();
            auto* s = new Ipv6GuardSession;
            makeSession(*s);
            bool ok = false;
            s->begin("OneForAll-IPv6-RealCheck-KhongTonTai", {});
            CHECK(waitReady(*s, &ok) && ok);
            CHECK(waitConnect(p6, false, 3000) == WSAEACCES);
            delete s; // giống đóng ứng dụng: KHÔNG gửi release
            const int rc = waitConnect(p6, true, 8000);
            logf("Ung dung dong + VPN vang: [::1] sau -> ma %s (mong 0 trong ~3-4 giay)\n", std::to_string(rc));
            CHECK(rc == 0);
            spin(1500);
            QList<DWORD> extra = oneForAllPids();
            for (DWORD p : before)
                extra.removeAll(p);
            logf("  tien trinh phu con lai: %s (mong 0)\n", std::to_string(extra.size()));
            CHECK(extra.isEmpty());
        }
        // ---- 7) Tiến trình phụ bị GIẾT cưỡng bức -> Windows tự gỡ bộ lọc (phiên WFP động) ----
        {
            const QList<DWORD> before = oneForAllPids();
            Ipv6GuardSession s;
            makeSession(s);
            bool ok = false;
            s.begin("OneForAll-IPv6-RealCheck-KhongTonTai", {});
            CHECK(waitReady(s, &ok) && ok);
            CHECK(waitConnect(p6, false, 3000) == WSAEACCES);
            QList<DWORD> extra = oneForAllPids();
            for (DWORD p : before)
                extra.removeAll(p);
            CHECK(extra.size() == 1);
            for (DWORD pid : extra)
            {
                HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
                if (h)
                {
                    TerminateProcess(h, 99);
                    WaitForSingleObject(h, 5000);
                    CloseHandle(h);
                }
            }
            const int rc = waitConnect(p6, true, 4000);
            logf("Giet tien trinh phu: [::1] sau -> ma %s (mong 0)\n", std::to_string(rc));
            CHECK(rc == 0);
            spin(500);
            CHECK(s.state() == Ipv6GuardSession::State::Failed); // ứng dụng biết việc chặn đã mất
        }
    }

    closesocket(l6);
    closesocket(l4);
    WSACleanup();
    logf("passed=%s", std::to_string(g_pass));
    logf(" failed=%s\n", std::to_string(g_fail));
    if (g_log)
        std::fclose(g_log);
    return g_fail == 0 ? 0 : 1;
}
