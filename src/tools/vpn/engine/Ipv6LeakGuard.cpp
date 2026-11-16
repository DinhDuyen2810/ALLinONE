#include "Ipv6LeakGuard.h"

#include <QHostAddress>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <fwpmu.h>
#include <rpc.h>

#include <cstring>
#include <string>
#include <vector>

namespace Ipv6LeakGuard
{
namespace
{
// MinGW không khai các GUID tầng/điều kiện WFP (chỉ Windows SDK có). Giá trị lấy từ fwpmu.h của Windows SDK, đối
// chiếu với wireguard-windows (tunnel/firewall/types_windows.go). Sai GUID thì FwpmFilterAdd0 trả lỗi
// FWP_E_LAYER_NOT_FOUND/FWP_E_CONDITION_NOT_FOUND - không bao giờ "chạy mà không chặn"; công cụ kiểm tra tay
// ipv6_guard_real_check thử chặn thật để xác nhận.
const GUID kLayerAleAuthConnectV6 = {0x4a72393b, 0x319f, 0x44bc, {0x84, 0xc3, 0xba, 0x54, 0xdc, 0xb3, 0xb6, 0xb4}};
const GUID kLayerAleAuthRecvAcceptV6 = {0xa3b42c97, 0x9f04, 0x4672, {0xb8, 0x7e, 0xce, 0xe9, 0xc4, 0x83, 0x25, 0x7f}};
const GUID kConditionIpRemoteAddress = {0xb235ae9a, 0x1d64, 0x49b8, {0xa4, 0x4c, 0x5f, 0xf3, 0xd9, 0x09, 0x50, 0x45}};
const GUID kConditionIpLocalInterface = {0x4cd62a49, 0x59c3, 0x4969, {0xb7, 0xf3, 0xbd, 0xa5, 0xd3, 0x28, 0x90, 0xa4}};
constexpr UINT32 kSessionFlagDynamic = 0x00000001; // FWPM_SESSION_FLAG_DYNAMIC
constexpr quint32 kIfTypeSoftwareLoopback = 24;    // IF_TYPE_SOFTWARE_LOOPBACK

Prefix makePrefix(std::initializer_list<quint8> leading, int length)
{
    Prefix p;
    int i = 0;
    for (quint8 b : leading)
        p.addr[i++] = b;
    p.length = length;
    return p;
}

Addr16 fromIn6(const IN6_ADDR& a)
{
    Addr16 out{};
    std::memcpy(out.data(), a.u.Byte, 16);
    return out;
}
} // namespace

QList<Prefix> blockedPrefixes()
{
    return {
        makePrefix({0x20, 0x00}, 3),                                  // 2000::/3
        makePrefix({0x00, 0x64, 0xff, 0x9b}, 96),                     // 64:ff9b::/96 (NAT64 chuẩn)
        makePrefix({0x00, 0x64, 0xff, 0x9b, 0x00, 0x01}, 48),         // 64:ff9b:1::/48 (NAT64 nội bộ, RFC 8215)
    };
}

bool inPrefix(const Addr16& address, const Prefix& prefix)
{
    if (prefix.length < 0 || prefix.length > 128)
        return false;
    const int fullBytes = prefix.length / 8;
    const int restBits = prefix.length % 8;
    for (int i = 0; i < fullBytes; ++i)
        if (address[i] != prefix.addr[i])
            return false;
    if (restBits == 0)
        return true;
    const quint8 mask = static_cast<quint8>(0xFF << (8 - restBits));
    return (address[fullBytes] & mask) == (prefix.addr[fullBytes] & mask);
}

bool isGlobalUnicast(const Addr16& address)
{
    return (address[0] & 0xE0) == 0x20;
}

bool isBlockedDestination(const Addr16& address)
{
    for (const Prefix& p : blockedPrefixes())
        if (inPrefix(address, p))
            return true;
    return false;
}

bool parseAddress(const QString& text, Addr16* out)
{
    QHostAddress a;
    if (!a.setAddress(text.trimmed()) || a.protocol() != QAbstractSocket::IPv6Protocol)
        return false;
    const Q_IPV6ADDR raw = a.toIPv6Address();
    for (int i = 0; i < 16; ++i)
        (*out)[i] = raw[i];
    return true;
}

QString formatAddress(const Addr16& address)
{
    Q_IPV6ADDR raw;
    for (int i = 0; i < 16; ++i)
        raw[i] = address[i];
    return QHostAddress(raw).toString();
}

bool parsePrefix(const QString& text, Prefix* out)
{
    const int slash = text.indexOf('/');
    if (slash <= 0)
        return false;
    bool ok = false;
    const int length = text.mid(slash + 1).toInt(&ok);
    if (!ok || length < 0 || length > 128)
        return false;
    Prefix p;
    if (!parseAddress(text.left(slash), &p.addr))
        return false;
    p.length = length;
    *out = p;
    return true;
}

Assessment assess(const QList<InterfaceInfo>& interfaces, const QString& tunnelAlias)
{
    Assessment result;
    for (const InterfaceInfo& itf : interfaces)
    {
        if (!itf.up || itf.ifType == kIfTypeSoftwareLoopback)
            continue;
        const bool isTunnel = !tunnelAlias.isEmpty() && itf.alias.compare(tunnelAlias, Qt::CaseInsensitive) == 0;
        const bool reachesInternet = !itf.globalAddresses.isEmpty() && itf.hasDefaultRoute;
        if (isTunnel)
        {
            result.tunnelLuid = itf.luid;
            result.tunnelHasIpv6 = reachesInternet;
            continue;
        }
        if (reachesInternet)
            result.exposedInterfaces << itf.alias;
    }
    result.exposure = result.exposedInterfaces.isEmpty() ? Exposure::NoIpv6 : Exposure::Exposed;
    return result;
}

QList<InterfaceInfo> snapshot(QString* error)
{
    QList<InterfaceInfo> result;
    ULONG size = 64 * 1024;
    std::vector<unsigned char> buffer;
    ULONG r = ERROR_BUFFER_OVERFLOW;
    for (int attempt = 0; attempt < 4 && r == ERROR_BUFFER_OVERFLOW; ++attempt)
    {
        buffer.resize(size);
        r = GetAdaptersAddresses(AF_INET6, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                 nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()), &size);
    }
    if (r == ERROR_NO_DATA)
        return result; // máy không có card nào bật IPv6
    if (r != NO_ERROR)
    {
        if (error)
            *error = QStringLiteral("Không đọc được danh sách card mạng: ") + describeError(r);
        return result;
    }
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()); a; a = a->Next)
    {
        InterfaceInfo info;
        info.luid = a->Luid.Value;
        info.alias = QString::fromWCharArray(a->FriendlyName ? a->FriendlyName : L"");
        info.ifType = a->IfType;
        info.up = a->OperStatus == IfOperStatusUp;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next)
        {
            if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET6)
                continue;
            if (u->DadState != IpDadStatePreferred && u->DadState != IpDadStateDeprecated)
                continue; // địa chỉ chưa dùng được (đang kiểm trùng/trùng/không hợp lệ)
            const Addr16 addr = fromIn6(reinterpret_cast<sockaddr_in6*>(u->Address.lpSockaddr)->sin6_addr);
            if (isGlobalUnicast(addr))
                info.globalAddresses << addr;
        }
        result << info;
    }

    PMIB_IPFORWARD_TABLE2 table = nullptr;
    const DWORD rr = GetIpForwardTable2(AF_INET6, &table);
    if (rr != NO_ERROR || !table)
    {
        if (error)
            *error = QStringLiteral("Không đọc được bảng định tuyến IPv6: ") + describeError(rr);
        return result;
    }
    const Prefix globalStart = makePrefix({0x20, 0x00}, 128); // địa chỉ 2000:: - tuyến nào chứa nó thì bao trùm Internet
    for (ULONG i = 0; i < table->NumEntries; ++i)
    {
        const MIB_IPFORWARD_ROW2& row = table->Table[i];
        const int length = row.DestinationPrefix.PrefixLength;
        if (length > 3)
            continue;
        Prefix route;
        route.addr = fromIn6(row.DestinationPrefix.Prefix.Ipv6.sin6_addr);
        route.length = length;
        if (!inPrefix(globalStart.addr, route))
            continue;
        for (InterfaceInfo& info : result)
            if (info.luid == row.InterfaceLuid.Value)
                info.hasDefaultRoute = true;
    }
    FreeMibTable(table);
    return result;
}

QString describeError(quint32 code)
{
    if (code == ERROR_ACCESS_DENIED)
        return QStringLiteral("cần quyền Administrator (mã 5)");
    if ((code & 0xFFFF0000u) == 0x80320000u)
        return QStringLiteral("lỗi Windows Filtering Platform 0x%1").arg(code, 8, 16, QLatin1Char('0'));
    wchar_t* text = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                       FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&text), 0, nullptr);
    QString msg = n && text ? QString::fromWCharArray(text, static_cast<int>(n)).trimmed() : QString();
    if (text)
        LocalFree(text);
    return msg.isEmpty() ? QStringLiteral("mã %1").arg(code) : QStringLiteral("%1 (mã %2)").arg(msg).arg(code);
}

// ---------------------------------------------------------------------------------------------------------------
// WfpBlock

WfpBlock::~WfpBlock()
{
    close();
}

void WfpBlock::close()
{
    if (m_engine)
        FwpmEngineClose0(static_cast<HANDLE>(m_engine)); // phiên động: Windows tự gỡ sublayer + mọi bộ lọc
    m_engine = nullptr;
    m_filterIds.clear();
    m_sublayerAdded = false;
    m_cfg = Config{};
}

bool WfpBlock::apply(const Config& cfg, QString* error)
{
    if (m_engine && !m_filterIds.isEmpty() && cfg == m_cfg)
        return true;

    HANDLE engine = static_cast<HANDLE>(m_engine);
    if (!engine)
    {
        FWPM_SESSION0 session{};
        session.flags = kSessionFlagDynamic;
        std::wstring sessionName = L"One for ALL - chặn rò rỉ IPv6";
        session.displayData.name = sessionName.data();
        const DWORD r = FwpmEngineOpen0(nullptr, RPC_C_AUTHN_WINNT, nullptr, &session, &engine);
        if (r != ERROR_SUCCESS)
        {
            if (error)
                *error = QStringLiteral("Không mở được Windows Filtering Platform: ") + describeError(r);
            return false;
        }
        m_engine = engine;
    }

    DWORD r = FwpmTransactionBegin0(engine, 0);
    if (r != ERROR_SUCCESS)
    {
        if (error)
            *error = QStringLiteral("Không bắt đầu được giao dịch WFP: ") + describeError(r);
        return false;
    }
    auto fail = [&](const QString& what, DWORD code) {
        FwpmTransactionAbort0(engine);
        if (error)
            *error = what + QStringLiteral(": ") + describeError(code);
        return false;
    };

    GUID sublayerKey;
    if (!m_sublayerAdded)
    {
        UuidCreate(&sublayerKey);
        std::memcpy(m_sublayerKey.data(), &sublayerKey, 16);
        FWPM_SUBLAYER0 sublayer{};
        sublayer.subLayerKey = sublayerKey;
        std::wstring name = L"One for ALL - chặn rò rỉ IPv6";
        sublayer.displayData.name = name.data();
        sublayer.weight = 0xFFFF; // ưu tiên cao nhất - như WireGuard for Windows
        r = FwpmSubLayerAdd0(engine, &sublayer, nullptr);
        if (r != ERROR_SUCCESS)
            return fail(QStringLiteral("Không tạo được lớp bộ lọc WFP"), r);
    }
    else
    {
        std::memcpy(&sublayerKey, m_sublayerKey.data(), 16);
    }

    for (quint64 id : std::as_const(m_filterIds))
    {
        r = FwpmFilterDeleteById0(engine, id);
        if (r != ERROR_SUCCESS)
            return fail(QStringLiteral("Không gỡ được bộ lọc cũ"), r);
    }

    QList<quint64> newIds;
    std::wstring filterName = L"One for ALL - chặn rò rỉ IPv6";
    auto addFilter = [&](const GUID& layer, FWP_ACTION_TYPE action, quint8 weight,
                         std::vector<FWPM_FILTER_CONDITION0>& conditions) -> DWORD {
        FWPM_FILTER0 filter{};
        filter.displayData.name = filterName.data();
        filter.layerKey = layer;
        filter.subLayerKey = sublayerKey;
        filter.weight.type = FWP_UINT8;
        filter.weight.uint8 = weight;
        filter.numFilterConditions = static_cast<UINT32>(conditions.size());
        filter.filterCondition = conditions.data();
        filter.action.type = action;
        UINT64 id = 0;
        const DWORD rr = FwpmFilterAdd0(engine, &filter, nullptr, &id);
        if (rr == ERROR_SUCCESS)
            newIds << id;
        return rr;
    };

    QList<Prefix> blocked = blockedPrefixes();
    blocked += cfg.extraBlockedForTest;
    std::vector<FWP_V6_ADDR_AND_MASK> masks(blocked.size());
    for (int i = 0; i < blocked.size(); ++i)
    {
        std::memcpy(masks[i].addr, blocked[i].addr.data(), 16);
        masks[i].prefixLength = static_cast<UINT8>(blocked[i].length);
    }
    UINT64 allowLuid = cfg.allowInterfaceLuid;
    std::vector<FWP_BYTE_ARRAY16> allowAddrs(cfg.allowRemote.size());
    for (int i = 0; i < cfg.allowRemote.size(); ++i)
        std::memcpy(allowAddrs[i].byteArray16, cfg.allowRemote[i].data(), 16);

    for (const GUID* layer : {&kLayerAleAuthConnectV6, &kLayerAleAuthRecvAcceptV6})
    {
        // CHẶN: địa chỉ đầu bên kia thuộc một dải Internet (các điều kiện CÙNG trường được Windows OR với nhau).
        std::vector<FWPM_FILTER_CONDITION0> blockConds(masks.size());
        for (size_t i = 0; i < masks.size(); ++i)
        {
            blockConds[i].fieldKey = kConditionIpRemoteAddress;
            blockConds[i].matchType = FWP_MATCH_EQUAL;
            blockConds[i].conditionValue.type = FWP_V6_ADDR_MASK;
            blockConds[i].conditionValue.v6AddrMask = &masks[i];
        }
        if ((r = addFilter(*layer, FWP_ACTION_BLOCK, 1, blockConds)) != ERROR_SUCCESS)
            return fail(QStringLiteral("Không thêm được bộ lọc chặn IPv6"), r);

        // CHO PHÉP (trọng số cao hơn - xét trước): qua card đường hầm VPN.
        if (allowLuid != 0)
        {
            std::vector<FWPM_FILTER_CONDITION0> conds(1);
            conds[0].fieldKey = kConditionIpLocalInterface;
            conds[0].matchType = FWP_MATCH_EQUAL;
            conds[0].conditionValue.type = FWP_UINT64;
            conds[0].conditionValue.uint64 = &allowLuid;
            if ((r = addFilter(*layer, FWP_ACTION_PERMIT, 10, conds)) != ERROR_SUCCESS)
                return fail(QStringLiteral("Không thêm được bộ lọc cho phép đường hầm"), r);
        }
        // CHO PHÉP: tới chính máy chủ VPN (để đường hầm tự dựng được nếu máy chủ có IPv6).
        if (!allowAddrs.empty())
        {
            std::vector<FWPM_FILTER_CONDITION0> conds(allowAddrs.size());
            for (size_t i = 0; i < allowAddrs.size(); ++i)
            {
                conds[i].fieldKey = kConditionIpRemoteAddress;
                conds[i].matchType = FWP_MATCH_EQUAL;
                conds[i].conditionValue.type = FWP_BYTE_ARRAY16_TYPE;
                conds[i].conditionValue.byteArray16 = &allowAddrs[i];
            }
            if ((r = addFilter(*layer, FWP_ACTION_PERMIT, 10, conds)) != ERROR_SUCCESS)
                return fail(QStringLiteral("Không thêm được bộ lọc cho phép máy chủ VPN"), r);
        }
    }

    r = FwpmTransactionCommit0(engine);
    if (r != ERROR_SUCCESS)
        return fail(QStringLiteral("Không áp dụng được bộ lọc WFP"), r);
    m_sublayerAdded = true;
    m_filterIds = newIds;
    m_cfg = cfg;
    return true;
}

} // namespace Ipv6LeakGuard
