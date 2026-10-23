#include "WintunAdapter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include <cstring>

namespace
{
// Kiểu hàm theo wintun.h 0.14 (WINAPI).
using CreateAdapterFn = void*(WINAPI*)(LPCWSTR, LPCWSTR, const GUID*);
using OpenAdapterFn = void*(WINAPI*)(LPCWSTR);
using CloseAdapterFn = VOID(WINAPI*)(void*);
using GetLuidFn = VOID(WINAPI*)(void*, NET_LUID*);
using StartSessionFn = void*(WINAPI*)(void*, DWORD);
using EndSessionFn = VOID(WINAPI*)(void*);
using GetReadWaitEventFn = HANDLE(WINAPI*)(void*);
using ReceiveFn = BYTE*(WINAPI*)(void*, DWORD*);
using ReleaseReceiveFn = VOID(WINAPI*)(void*, const BYTE*);
using AllocateSendFn = BYTE*(WINAPI*)(void*, DWORD);
using SendFn = VOID(WINAPI*)(void*, const BYTE*);

constexpr DWORD kRingCapacity = 0x400000; // 4 MiB (giữa MIN 128KiB và MAX 64MiB)
// GUID cố định cho adapter "OneForAll VPN" - Windows tạo hồ sơ mạng (NLA) ổn định thay vì mỗi lần một mạng mới.
constexpr GUID kAdapterGuid = {0x6f4a1e52, 0x3c1d, 0x4b9a, {0x9e, 0x51, 0x0a, 0x11, 0xf0, 0x7a, 0x55, 0x01}};

QString winError(DWORD e)
{
    wchar_t buf[512] = {};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, buf, 512, nullptr);
    const QString t = QString::fromWCharArray(buf).trimmed();
    return t.isEmpty() ? QStringLiteral("mã %1").arg(e) : QStringLiteral("%1 (mã %2)").arg(t).arg(e);
}

SOCKADDR_INET v4(quint32 hostOrder)
{
    SOCKADDR_INET a;
    std::memset(&a, 0, sizeof(a));
    a.Ipv4.sin_family = AF_INET;
    a.Ipv4.sin_addr.s_addr = htonl(hostOrder);
    return a;
}
} // namespace

struct WintunAdapter::Fns
{
    CreateAdapterFn create{nullptr};
    OpenAdapterFn open{nullptr};
    CloseAdapterFn close{nullptr};
    GetLuidFn getLuid{nullptr};
    StartSessionFn startSession{nullptr};
    EndSessionFn endSession{nullptr};
    GetReadWaitEventFn readEvent{nullptr};
    ReceiveFn receive{nullptr};
    ReleaseReceiveFn releaseReceive{nullptr};
    AllocateSendFn allocateSend{nullptr};
    SendFn send{nullptr};
};

QString WintunAdapter::dllPath()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/wintun/wintun.dll");
}

bool WintunAdapter::isAvailable()
{
    return QFileInfo::exists(dllPath());
}

WintunAdapter::~WintunAdapter()
{
    close();
}

bool WintunAdapter::open(const QString& name, QString* error)
{
    close();
    const std::wstring path = QDir::toNativeSeparators(dllPath()).toStdWString();
    // Đường dẫn TUYỆT ĐỐI + chỉ tìm phụ thuộc trong thư mục hệ thống (chống nạp DLL giả mạo).
    HMODULE dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dll)
    {
        if (error)
            *error = QStringLiteral("Không nạp được wintun.dll (%1): %2").arg(dllPath(), winError(GetLastError()));
        return false;
    }
    m_dll = dll;
    m_fn = new Fns;
    auto get = [dll](const char* n) { return reinterpret_cast<void*>(GetProcAddress(dll, n)); };
    m_fn->create = reinterpret_cast<CreateAdapterFn>(get("WintunCreateAdapter"));
    m_fn->open = reinterpret_cast<OpenAdapterFn>(get("WintunOpenAdapter"));
    m_fn->close = reinterpret_cast<CloseAdapterFn>(get("WintunCloseAdapter"));
    m_fn->getLuid = reinterpret_cast<GetLuidFn>(get("WintunGetAdapterLUID"));
    m_fn->startSession = reinterpret_cast<StartSessionFn>(get("WintunStartSession"));
    m_fn->endSession = reinterpret_cast<EndSessionFn>(get("WintunEndSession"));
    m_fn->readEvent = reinterpret_cast<GetReadWaitEventFn>(get("WintunGetReadWaitEvent"));
    m_fn->receive = reinterpret_cast<ReceiveFn>(get("WintunReceivePacket"));
    m_fn->releaseReceive = reinterpret_cast<ReleaseReceiveFn>(get("WintunReleaseReceivePacket"));
    m_fn->allocateSend = reinterpret_cast<AllocateSendFn>(get("WintunAllocateSendPacket"));
    m_fn->send = reinterpret_cast<SendFn>(get("WintunSendPacket"));
    if (!m_fn->create || !m_fn->open || !m_fn->close || !m_fn->getLuid || !m_fn->startSession || !m_fn->endSession ||
        !m_fn->readEvent || !m_fn->receive || !m_fn->releaseReceive || !m_fn->allocateSend || !m_fn->send)
    {
        if (error)
            *error = QStringLiteral("wintun.dll thiếu hàm cần dùng (sai phiên bản?).");
        close();
        return false;
    }

    const std::wstring nameW = name.toStdWString();
    // Adapter cùng tên còn sót từ lần trước (tiến trình bị dừng đột ngột): mở rồi đóng để xóa.
    if (void* stale = m_fn->open(nameW.c_str()))
        m_fn->close(stale);

    m_adapter = m_fn->create(nameW.c_str(), L"OneForAll", &kAdapterGuid);
    if (!m_adapter && GetLastError() != ERROR_ACCESS_DENIED)
        m_adapter = m_fn->create(nameW.c_str(), L"OneForAll", nullptr); // GUID cố định đang bị adapter cũ giữ
    if (!m_adapter)
    {
        const DWORD e = GetLastError();
        if (error)
            *error = e == ERROR_ACCESS_DENIED
                         ? QStringLiteral("Tạo adapter VPN cần quyền Administrator (tiến trình chưa được nâng quyền).")
                         : QStringLiteral("Không tạo được adapter VPN (wintun): %1").arg(winError(e));
        close();
        return false;
    }
    NET_LUID luid;
    m_fn->getLuid(m_adapter, &luid);
    m_luid = luid.Value;

    m_session = m_fn->startSession(m_adapter, kRingCapacity);
    if (!m_session)
    {
        if (error)
            *error = QStringLiteral("Không mở được phiên adapter VPN: %1").arg(winError(GetLastError()));
        close();
        return false;
    }
    m_quitEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return true;
}

void WintunAdapter::startReader(std::function<void(QByteArray)> onPacket)
{
    if (!m_session || m_reader.joinable())
        return;
    m_onPacket = std::move(onPacket);
    m_stop = false;
    m_reader = std::thread([this]() { readerLoop(); });
}

void WintunAdapter::readerLoop()
{
    HANDLE events[2] = {m_fn->readEvent(m_session), static_cast<HANDLE>(m_quitEvent)};
    while (!m_stop)
    {
        DWORD size = 0;
        BYTE* pkt = m_fn->receive(m_session, &size);
        if (pkt)
        {
            QByteArray copy(reinterpret_cast<const char*>(pkt), static_cast<int>(size));
            m_fn->releaseReceive(m_session, pkt);
            // Chỉ chuyển gói IPv4 (nibble phiên bản = 4) - đường hầm không cấu hình IPv6.
            if (!copy.isEmpty() && (static_cast<quint8>(copy[0]) >> 4) == 4 && m_onPacket)
                m_onPacket(copy);
            continue;
        }
        const DWORD e = GetLastError();
        if (e == ERROR_NO_MORE_ITEMS)
        {
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0)
                break; // sự kiện thoát
            continue;
        }
        break; // ERROR_HANDLE_EOF (adapter đang đóng) hoặc lỗi khác
    }
}

bool WintunAdapter::sendPacket(const QByteArray& ip)
{
    if (!m_session || ip.isEmpty() || ip.size() > 0xFFFF)
        return false;
    BYTE* out = m_fn->allocateSend(m_session, static_cast<DWORD>(ip.size()));
    if (!out)
        return false; // vòng đệm đầy - bỏ gói (TCP tự gửi lại)
    std::memcpy(out, ip.constData(), static_cast<size_t>(ip.size()));
    m_fn->send(m_session, out);
    return true;
}

void WintunAdapter::close()
{
    m_stop = true;
    if (m_quitEvent)
        SetEvent(static_cast<HANDLE>(m_quitEvent));
    if (m_reader.joinable())
        m_reader.join();
    if (m_session && m_fn)
        m_fn->endSession(m_session);
    m_session = nullptr;
    if (m_adapter && m_fn)
        m_fn->close(m_adapter); // xóa adapter khỏi hệ thống
    m_adapter = nullptr;
    if (m_quitEvent)
        CloseHandle(static_cast<HANDLE>(m_quitEvent));
    m_quitEvent = nullptr;
    delete m_fn;
    m_fn = nullptr;
    if (m_dll)
        FreeLibrary(static_cast<HMODULE>(m_dll));
    m_dll = nullptr;
    m_luid = 0;
}

// ---------------------------------------------------------------------------------------------------------

NetConfigurator::~NetConfigurator()
{
    revert();
}

bool NetConfigurator::captureUnderlay(quint32 serverIp, QString* error)
{
    SOCKADDR_INET dest = v4(serverIp);
    MIB_IPFORWARD_ROW2 best;
    SOCKADDR_INET source;
    const DWORD r = GetBestRoute2(nullptr, 0, nullptr, &dest, 0, &best, &source);
    if (r != NO_ERROR)
    {
        if (error)
            *error = QStringLiteral("Không xác định được đường mạng hiện tại tới máy chủ VPN: %1").arg(winError(r));
        return false;
    }
    m_underlayLuid = best.InterfaceLuid.Value;
    m_underlayNextHop = ntohl(best.NextHop.Ipv4.sin_addr.s_addr);
    m_haveUnderlay = true;
    return true;
}

bool NetConfigurator::addRoute(quint64 luid, const IpRoute& dest, quint32 nextHop, quint32 metric, QString* error)
{
    MIB_IPFORWARD_ROW2 row;
    InitializeIpForwardEntry(&row);
    row.InterfaceLuid.Value = luid;
    row.DestinationPrefix.Prefix = v4(dest.network);
    row.DestinationPrefix.PrefixLength = static_cast<UINT8>(dest.prefix);
    row.NextHop = v4(nextHop); // 0.0.0.0 = trên liên kết (on-link) - đúng cho adapter TUN
    row.Metric = metric;
    row.Protocol = static_cast<NL_ROUTE_PROTOCOL>(MIB_IPPROTO_NETMGMT);
    const DWORD r = CreateIpForwardEntry2(&row);
    if (r == ERROR_OBJECT_ALREADY_EXISTS)
        return true; // đã có (không phải của ta) - không ghi vào danh sách gỡ
    if (r != NO_ERROR)
    {
        if (error)
            *error = QStringLiteral("Không thêm được tuyến %1: %2").arg(DirectVpn::routeToString(dest), winError(r));
        return false;
    }
    m_added << AddedRoute{luid, dest, nextHop};
    return true;
}

bool NetConfigurator::apply(quint64 adapterLuid, const TunnelConfig& cfg, quint32 serverIp, int mtu, QString* error)
{
    quint32 ip = 0;
    if (!DirectVpn::parseIpv4(cfg.ipv4, &ip))
    {
        if (error)
            *error = QStringLiteral("Địa chỉ IP máy chủ cấp không hợp lệ: \"%1\"").arg(cfg.ipv4);
        return false;
    }
    NET_LUID luid;
    luid.Value = adapterLuid;

    // 1) Địa chỉ IP trên adapter.
    MIB_UNICASTIPADDRESS_ROW addr;
    InitializeUnicastIpAddressEntry(&addr);
    addr.InterfaceLuid = luid;
    addr.Address = v4(ip);
    addr.OnLinkPrefixLength = static_cast<UINT8>(cfg.prefixLength > 0 && cfg.prefixLength <= 32 ? cfg.prefixLength : 32);
    addr.DadState = IpDadStatePreferred;
    DWORD r = CreateUnicastIpAddressEntry(&addr);
    if (r != NO_ERROR && r != ERROR_OBJECT_ALREADY_EXISTS)
    {
        if (error)
            *error = QStringLiteral("Không gán được địa chỉ %1 cho adapter VPN: %2").arg(cfg.ipv4, winError(r));
        return false;
    }

    // 2) MTU + metric thấp (ưu tiên adapter VPN, kể cả DNS của nó).
    MIB_IPINTERFACE_ROW ifRow;
    InitializeIpInterfaceEntry(&ifRow);
    ifRow.InterfaceLuid = luid;
    ifRow.Family = AF_INET;
    if (GetIpInterfaceEntry(&ifRow) == NO_ERROR)
    {
        ifRow.UseAutomaticMetric = FALSE;
        ifRow.Metric = 1;
        if (mtu >= 576 && mtu <= 1500)
            ifRow.NlMtu = static_cast<ULONG>(mtu);
        ifRow.SitePrefixLength = 0; // bắt buộc = 0 với IPv4 khi gọi SetIpInterfaceEntry
        SetIpInterfaceEntry(&ifRow);  // lỗi ở đây không chặn kết nối
    }

    // 3) Tuyến. Tuyến /32 tới máy chủ qua đường GỐC phải có TRƯỚC tuyến mặc định của VPN.
    if (m_haveUnderlay)
    {
        if (!addRoute(m_underlayLuid, IpRoute{serverIp, 32}, m_underlayNextHop, 0, error))
            return false;
        for (const IpRoute& ex : cfg.excludeRoutes)
            addRoute(m_underlayLuid, ex, m_underlayNextHop, 0, nullptr);
    }
    if (cfg.fullTunnel())
    {
        // Hai nửa 0/1 + 128/1 phủ toàn bộ IPv4 và "thắng" tuyến mặc định 0/0 (dài hơn) mà KHÔNG xóa nó -
        // ngắt VPN là mạng tự trở lại như cũ.
        if (!addRoute(adapterLuid, IpRoute{0x00000000u, 1}, 0, 0, error) ||
            !addRoute(adapterLuid, IpRoute{0x80000000u, 1}, 0, 0, error))
            return false;
    }
    else
    {
        for (const IpRoute& in : cfg.includeRoutes)
            if (!addRoute(adapterLuid, in, 0, 0, error))
                return false;
        // Chia tuyến: máy chủ DNS NỘI BỘ mà máy chủ VPN cấp (vd 10.45.1.20) thường KHÔNG nằm trong các tuyến
        // riêng - không có tuyến /32 thì truy vấn DNS đi ra Wi-Fi và thất bại, tên miền nội bộ không phân giải
        // được (đã gặp THẬT với FortiGate của người dùng: ping 10.45.1.20 đi qua Wi-Fi). Chỉ thêm cho địa chỉ
        // dải riêng (RFC 1918) - DNS công cộng như 8.8.8.8 vẫn đi đường thường.
        for (const QString& d : cfg.dns)
        {
            quint32 a = 0;
            if (!DirectVpn::parseIpv4(d, &a) || !DirectVpn::isPrivateIpv4(a))
                continue;
            bool covered = false;
            for (const IpRoute& in : cfg.includeRoutes)
            {
                const quint32 mask = in.prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - in.prefix));
                covered = covered || (a & mask) == in.network;
            }
            if (!covered)
                addRoute(adapterLuid, IpRoute{a, 32}, 0, 0, nullptr);
        }
    }

    // 4) DNS (SetInterfaceDnsSettings - Windows 10 2004+; nạp động vì header MinGW chưa có).
    if (!cfg.dns.isEmpty())
    {
        struct DnsSettings
        {
            ULONG Version;
            ULONG64 Flags;
            PWSTR Domain;
            PWSTR NameServer;
            PWSTR SearchList;
            ULONG RegistrationEnabled;
            ULONG RegisterAdapterName;
            ULONG EnableLLMNR;
            ULONG QueryAdapterName;
            PWSTR ProfileNameServer;
        };
        using SetDnsFn = DWORD(WINAPI*)(GUID, const DnsSettings*);
        HMODULE iphlp = GetModuleHandleW(L"iphlpapi.dll");
        auto setDns = iphlp ? reinterpret_cast<SetDnsFn>(reinterpret_cast<void*>(GetProcAddress(iphlp, "SetInterfaceDnsSettings")))
                            : nullptr;
        GUID guid;
        if (setDns && ConvertInterfaceLuidToGuid(&luid, &guid) == NO_ERROR)
        {
            std::wstring servers = cfg.dns.join(',').toStdWString();
            std::wstring search = cfg.dnsSuffix.join(',').toStdWString();
            DnsSettings s;
            std::memset(&s, 0, sizeof(s));
            s.Version = 1;                    // DNS_INTERFACE_SETTINGS_VERSION1
            s.Flags = 0x0002;                 // DNS_SETTING_NAMESERVER
            s.NameServer = servers.data();
            if (!search.empty())
            {
                s.Flags |= 0x0004;            // DNS_SETTING_SEARCHLIST
                s.SearchList = search.data();
            }
            setDns(guid, &s);                 // lỗi DNS không chặn kết nối (vẫn truy cập được bằng IP)
        }
    }
    return true;
}

void NetConfigurator::revert()
{
    for (const AddedRoute& a : m_added)
    {
        MIB_IPFORWARD_ROW2 row;
        InitializeIpForwardEntry(&row);
        row.InterfaceLuid.Value = a.luid;
        row.DestinationPrefix.Prefix = v4(a.dest.network);
        row.DestinationPrefix.PrefixLength = static_cast<UINT8>(a.dest.prefix);
        row.NextHop = v4(a.nextHop);
        DeleteIpForwardEntry2(&row);
    }
    m_added.clear();
}
