#include "WlanController.h"
#include "WlanProfileXml.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wlanapi.h>

#include <QUuid>
#include <algorithm>
#include <string>

#pragma comment(lib, "wlanapi.lib")

// Các cờ sau được định nghĩa trong SDK đầy đủ của Microsoft nhưng vắng mặt trong
// header wlanapi.h đi kèm bản MinGW đang dùng; giá trị lấy từ tài liệu WLAN API.
#ifndef WLAN_PROFILE_GET_PLAINTEXT_KEY
#define WLAN_PROFILE_GET_PLAINTEXT_KEY 0x00000004
#endif
#ifndef WLAN_AVAILABLE_NETWORK_CONNECTED
#define WLAN_AVAILABLE_NETWORK_CONNECTED 0x00000001
#endif
#ifndef WLAN_AVAILABLE_NETWORK_HAS_PROFILE
#define WLAN_AVAILABLE_NETWORK_HAS_PROFILE 0x00000002
#endif
#ifndef WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES
#define WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES 0x00000002
#endif

namespace
{
constexpr DWORD kClientVersion = 2; // Vista trở lên

QString guidToString(const GUID& g)
{
    const QUuid u(g.Data1, g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3],
                 g.Data4[4], g.Data4[5], g.Data4[6], g.Data4[7]);
    return u.toString(QUuid::WithoutBraces);
}

bool stringToGuid(const QString& s, GUID* out)
{
    const QUuid u(s);
    if (u.isNull() && s.trimmed().compare("00000000-0000-0000-0000-000000000000") != 0)
        return false;
    out->Data1 = u.data1;
    out->Data2 = u.data2;
    out->Data3 = u.data3;
    memcpy(out->Data4, u.data4, 8);
    return true;
}

QString ssidToDisplayString(const DOT11_SSID& ssid)
{
    const QByteArray bytes(reinterpret_cast<const char*>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
    // SSID hiếm khi chứa byte không hợp lệ UTF-8; dự phòng về Latin-1 để không mất hiển thị.
    QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
        text = QString::fromLatin1(bytes);
    return text;
}

QByteArray ssidToBytes(const DOT11_SSID& ssid)
{
    return QByteArray(reinterpret_cast<const char*>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
}

WifiSecurity mapAuth(DOT11_AUTH_ALGORITHM algo, DOT11_CIPHER_ALGORITHM cipher, bool securityEnabled)
{
    if (!securityEnabled)
        return WifiSecurity::Open;

    switch (static_cast<int>(algo))
    {
        case DOT11_AUTH_ALGO_80211_OPEN:
            return cipher == DOT11_CIPHER_ALGO_NONE ? WifiSecurity::Open : WifiSecurity::Wep;
        case DOT11_AUTH_ALGO_80211_SHARED_KEY:
            return WifiSecurity::Wep;
        case DOT11_AUTH_ALGO_WPA:
        case DOT11_AUTH_ALGO_RSNA:
            return WifiSecurity::Enterprise;
        case DOT11_AUTH_ALGO_WPA_PSK:
            return WifiSecurity::WpaPsk;
        case DOT11_AUTH_ALGO_RSNA_PSK:
            return WifiSecurity::Wpa2Psk;
        default:
            // Giá trị >7 (chưa có tên trong header MinGW này) thường là WPA3-SAE trên Windows mới.
            return static_cast<int>(algo) > DOT11_AUTH_ALGO_RSNA_PSK ? WifiSecurity::Wpa3Sae : WifiSecurity::Unknown;
    }
}

QString phyTypeName(DOT11_PHY_TYPE t)
{
    // 8 (VHT/802.11ac) và 9 (HE/802.11ax) chưa có tên trong header wlanapi/windot11 bản MinGW
    // đang dùng (chỉ định nghĩa tới HT=7); ghi số thẳng theo tài liệu WLAN API của Microsoft.
    switch (static_cast<int>(t))
    {
        case dot11_phy_type_fhss:       return "FHSS";
        case dot11_phy_type_dsss:       return "DSSS";
        case dot11_phy_type_irbaseband: return "IR";
        case dot11_phy_type_ofdm:       return "OFDM (802.11a)";
        case dot11_phy_type_hrdsss:     return "HR-DSSS (802.11b)";
        case dot11_phy_type_erp:        return "ERP (802.11g)";
        case dot11_phy_type_ht:         return "HT (802.11n)";
        case 8:                         return "VHT (802.11ac)";
        case 9:                         return "HE (802.11ax)";
        default:                        return "802.11";
    }
}

QString dwordToErrorText(DWORD code)
{
    LPWSTR buf = nullptr;
    const DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                     nullptr, code, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    QString text = len > 0 ? QString::fromWCharArray(buf, static_cast<int>(len)).trimmed()
                           : QString("Mã lỗi Windows 0x%1").arg(code, 0, 16);
    if (buf)
        LocalFree(buf);
    return text;
}

void setErr(QString* error, const QString& text)
{
    if (error)
        *error = text;
}

bool ensureOpen(void* handle, QString* error)
{
    if (!handle)
    {
        setErr(error, "Chưa kết nối tới dịch vụ WLAN. Hãy khởi động lại cửa sổ WiFi.");
        return false;
    }
    return true;
}
} // namespace

WlanController::WlanController(QObject* parent)
    : QObject(parent)
{
}

WlanController::~WlanController()
{
    if (m_handle)
        WlanCloseHandle(static_cast<HANDLE>(m_handle), nullptr);
}

bool WlanController::open(QString* error)
{
    if (m_handle)
        return true;

    HANDLE h = nullptr;
    DWORD negotiated = 0;
    const DWORD res = WlanOpenHandle(kClientVersion, nullptr, &negotiated, &h);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không thể kết nối dịch vụ WLAN (WLAN AutoConfig). Chi tiết: " + dwordToErrorText(res));
        return false;
    }
    m_handle = h;
    return true;
}

QList<WifiAdapterInfo> WlanController::adapters(QString* error) const
{
    QList<WifiAdapterInfo> result;
    if (!ensureOpen(m_handle, error))
        return result;

    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    const DWORD res = WlanEnumInterfaces(static_cast<HANDLE>(m_handle), nullptr, &list);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không liệt kê được adapter WiFi: " + dwordToErrorText(res));
        return result;
    }

    for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
    {
        const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
        WifiAdapterInfo a;
        a.guidString = guidToString(info.InterfaceGuid);
        a.description = QString::fromWCharArray(info.strInterfaceDescription);
        a.connected = (info.isState == wlan_interface_state_connected);
        result.push_back(a);
    }
    WlanFreeMemory(list);
    return result;
}

bool WlanController::requestScan(const QString& adapterGuid, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    const DWORD res = WlanScan(static_cast<HANDLE>(m_handle), &guid, nullptr, nullptr, nullptr);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không yêu cầu quét được: " + dwordToErrorText(res));
        return false;
    }
    return true;
}

QList<WifiNetwork> WlanController::availableNetworks(const QString& adapterGuid, QString* error) const
{
    QList<WifiNetwork> result;
    if (!ensureOpen(m_handle, error))
        return result;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return result;
    }

    PWLAN_AVAILABLE_NETWORK_LIST list = nullptr;
    const DWORD res = WlanGetAvailableNetworkList(
        static_cast<HANDLE>(m_handle), &guid, WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES, nullptr, &list);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không lấy được danh sách mạng: " + dwordToErrorText(res));
        return result;
    }

    for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
    {
        const WLAN_AVAILABLE_NETWORK& n = list->Network[i];
        WifiNetwork w;
        w.ssid = ssidToDisplayString(n.dot11Ssid);
        w.ssidBytes = ssidToBytes(n.dot11Ssid);
        w.signalQuality = static_cast<int>(n.wlanSignalQuality);
        w.security = mapAuth(n.dot11DefaultAuthAlgorithm, n.dot11DefaultCipherAlgorithm, n.bSecurityEnabled);
        w.connected = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
        w.hasProfile = (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0;
        w.profileName = QString::fromWCharArray(n.strProfileName);
        w.numberOfBssids = static_cast<int>(n.uNumberOfBssids);
        // Mạng ẩn (quảng bá SSID trống) có thể lặp nhiều lần rỗng; vẫn giữ lại để người dùng
        // nhận ra "Mạng ẩn" nếu có hồ sơ khớp, nhưng gắn tên hiển thị dễ đọc.
        if (w.ssid.isEmpty())
            w.ssid = w.hasProfile ? QString("(Mạng ẩn - hồ sơ: %1)").arg(w.profileName) : "(Mạng ẩn)";
        result.push_back(w);
    }
    WlanFreeMemory(list);

    // Gộp các mục trùng SSID (nhiều BSSID của cùng 1 AP) lấy tín hiệu mạnh nhất, giữ cờ đã kết nối/có hồ sơ.
    QList<WifiNetwork> merged;
    for (const WifiNetwork& w : result)
    {
        bool found = false;
        for (WifiNetwork& m : merged)
        {
            if (m.ssid == w.ssid && m.security == w.security)
            {
                m.signalQuality = std::max(m.signalQuality, w.signalQuality);
                m.connected = m.connected || w.connected;
                m.hasProfile = m.hasProfile || w.hasProfile;
                m.numberOfBssids += w.numberOfBssids;
                found = true;
                break;
            }
        }
        if (!found)
            merged.push_back(w);
    }
    return merged;
}

QList<WifiProfile> WlanController::profiles(const QString& adapterGuid, bool withPlaintextPassword, QString* error) const
{
    QList<WifiProfile> result;
    if (!ensureOpen(m_handle, error))
        return result;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return result;
    }

    PWLAN_PROFILE_INFO_LIST list = nullptr;
    const DWORD res = WlanGetProfileList(static_cast<HANDLE>(m_handle), &guid, nullptr, &list);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không lấy được danh sách hồ sơ: " + dwordToErrorText(res));
        return result;
    }

    for (DWORD i = 0; i < list->dwNumberOfItems; ++i)
    {
        const QString name = QString::fromWCharArray(list->ProfileInfo[i].strProfileName);

        LPWSTR xmlRaw = nullptr;
        DWORD flags = withPlaintextPassword ? WLAN_PROFILE_GET_PLAINTEXT_KEY : 0;
        DWORD access = 0;
        DWORD getRes = WlanGetProfile(static_cast<HANDLE>(m_handle), &guid,
                                      reinterpret_cast<LPCWSTR>(name.utf16()), nullptr, &xmlRaw, &flags, &access);

        bool deniedPassword = false;
        if (getRes == ERROR_ACCESS_DENIED && withPlaintextPassword)
        {
            // Không có quyền đọc mật khẩu dạng chữ: thử lại không yêu cầu plaintext để ít nhất có SSID/bảo mật.
            deniedPassword = true;
            flags = 0;
            getRes = WlanGetProfile(static_cast<HANDLE>(m_handle), &guid,
                                    reinterpret_cast<LPCWSTR>(name.utf16()), nullptr, &xmlRaw, &flags, &access);
        }

        if (getRes != ERROR_SUCCESS)
            continue; // bỏ qua hồ sơ lỗi (hiếm), không làm hỏng toàn bộ danh sách

        const QString xml = QString::fromWCharArray(xmlRaw);
        WlanFreeMemory(xmlRaw);

        WifiProfile p;
        if (!WlanProfileXml::parse(xml, &p))
        {
            p.name = name;
            p.ssid = name;
        }
        p.passwordAccessDenied = deniedPassword;
        result.push_back(p);
    }
    WlanFreeMemory(list);
    return result;
}

bool WlanController::deleteProfile(const QString& adapterGuid, const QString& profileName, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    const DWORD res = WlanDeleteProfile(static_cast<HANDLE>(m_handle), &guid,
                                        reinterpret_cast<LPCWSTR>(profileName.utf16()), nullptr);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không xóa được hồ sơ: " + dwordToErrorText(res));
        return false;
    }
    return true;
}

QString WlanController::exportProfileXml(const QString& adapterGuid, const QString& profileName, QString* error) const
{
    if (!ensureOpen(m_handle, error))
        return QString();

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return QString();
    }

    LPWSTR xmlRaw = nullptr;
    DWORD flags = WLAN_PROFILE_GET_PLAINTEXT_KEY;
    DWORD access = 0;
    DWORD res = WlanGetProfile(static_cast<HANDLE>(m_handle), &guid, reinterpret_cast<LPCWSTR>(profileName.utf16()),
                               nullptr, &xmlRaw, &flags, &access);
    if (res == ERROR_ACCESS_DENIED)
    {
        flags = 0;
        res = WlanGetProfile(static_cast<HANDLE>(m_handle), &guid, reinterpret_cast<LPCWSTR>(profileName.utf16()),
                             nullptr, &xmlRaw, &flags, &access);
        if (res == ERROR_SUCCESS)
            setErr(error, "Đã xuất nhưng KHÔNG kèm mật khẩu (cần chạy với quyền Administrator để xuất mật khẩu).");
    }
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không xuất được hồ sơ: " + dwordToErrorText(res));
        return QString();
    }

    const QString xml = QString::fromWCharArray(xmlRaw);
    WlanFreeMemory(xmlRaw);
    return xml;
}

bool WlanController::importProfileXml(const QString& adapterGuid, const QString& xml, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    DWORD reason = 0;
    const DWORD res = WlanSetProfile(static_cast<HANDLE>(m_handle), &guid, 0,
                                     reinterpret_cast<LPCWSTR>(xml.utf16()), nullptr, TRUE, nullptr, &reason);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, QString("Không nhập được hồ sơ (mã lỗi lý do %1): %2").arg(reason).arg(dwordToErrorText(res)));
        return false;
    }
    return true;
}

bool WlanController::connectWithPassword(const QString& adapterGuid, const WifiNetwork& network, const QString& password,
                                         WifiSecurity securityOverride, bool autoConnect, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    if (!WifiSecurityUtil::isSupportedForQuickConnect(securityOverride))
    {
        setErr(error, "Loại bảo mật này (Enterprise/802.1X) cần chứng chỉ do quản trị mạng cấp, không thể kết nối chỉ bằng mật khẩu.");
        return false;
    }
    if (WifiSecurityUtil::requiresPassword(securityOverride) && password.isEmpty())
    {
        setErr(error, "Vui lòng nhập mật khẩu.");
        return false;
    }

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    const QString xml = WlanProfileXml::build(network.ssidBytes, network.ssid, securityOverride, password, autoConnect);

    DWORD reason = 0;
    const DWORD setRes = WlanSetProfile(static_cast<HANDLE>(m_handle), &guid, 0,
                                        reinterpret_cast<LPCWSTR>(xml.utf16()), nullptr, TRUE, nullptr, &reason);
    if (setRes != ERROR_SUCCESS)
    {
        setErr(error, QString("Không tạo được hồ sơ kết nối (mã lỗi lý do %1): %2").arg(reason).arg(dwordToErrorText(setRes)));
        return false;
    }

    WLAN_CONNECTION_PARAMETERS params{};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    const std::wstring profileNameW = network.ssid.toStdWString();
    params.strProfile = profileNameW.c_str();
    params.pDot11Ssid = nullptr;
    params.pDesiredBssidList = nullptr;
    params.dot11BssType = dot11_BSS_type_infrastructure;
    params.dwFlags = 0;

    const DWORD connRes = WlanConnect(static_cast<HANDLE>(m_handle), &guid, &params, nullptr);
    if (connRes != ERROR_SUCCESS)
    {
        setErr(error, "Không gửi được yêu cầu kết nối: " + dwordToErrorText(connRes));
        return false;
    }
    return true;
}

bool WlanController::connectToSavedProfile(const QString& adapterGuid, const QString& profileName, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    WLAN_CONNECTION_PARAMETERS params{};
    params.wlanConnectionMode = wlan_connection_mode_profile;
    const std::wstring profileNameW = profileName.toStdWString();
    params.strProfile = profileNameW.c_str();
    params.pDot11Ssid = nullptr;
    params.pDesiredBssidList = nullptr;
    params.dot11BssType = dot11_BSS_type_infrastructure;
    params.dwFlags = 0;

    const DWORD res = WlanConnect(static_cast<HANDLE>(m_handle), &guid, &params, nullptr);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không gửi được yêu cầu kết nối: " + dwordToErrorText(res));
        return false;
    }
    return true;
}

bool WlanController::disconnect(const QString& adapterGuid, QString* error)
{
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    const DWORD res = WlanDisconnect(static_cast<HANDLE>(m_handle), &guid, nullptr);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không ngắt kết nối được: " + dwordToErrorText(res));
        return false;
    }
    return true;
}

WifiCurrentConnection WlanController::currentConnection(const QString& adapterGuid, QString* error) const
{
    WifiCurrentConnection result;
    if (!ensureOpen(m_handle, error))
        return result;

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return result;
    }

    DWORD size = 0;
    WLAN_OPCODE_VALUE_TYPE valueType;
    PVOID data = nullptr;
    const DWORD res = WlanQueryInterface(static_cast<HANDLE>(m_handle), &guid, wlan_intf_opcode_current_connection,
                                         nullptr, &size, &data, &valueType);
    if (res != ERROR_SUCCESS)
    {
        // Không có kết nối hiện tại là tình huống bình thường (không phải lỗi hiển thị cho người dùng)
        if (res != ERROR_INVALID_STATE)
            setErr(error, "Không đọc được trạng thái kết nối: " + dwordToErrorText(res));
        return result;
    }

    const auto* attr = static_cast<const WLAN_CONNECTION_ATTRIBUTES*>(data);
    result.isConnected = (attr->isState == wlan_interface_state_connected);
    result.ssid = ssidToDisplayString(attr->wlanAssociationAttributes.dot11Ssid);
    const auto& mac = attr->wlanAssociationAttributes.dot11Bssid;
    result.bssid = QString("%1:%2:%3:%4:%5:%6")
                       .arg(mac[0], 2, 16, QChar('0')).arg(mac[1], 2, 16, QChar('0')).arg(mac[2], 2, 16, QChar('0'))
                       .arg(mac[3], 2, 16, QChar('0')).arg(mac[4], 2, 16, QChar('0')).arg(mac[5], 2, 16, QChar('0'))
                       .toUpper();
    result.signalQuality = static_cast<int>(attr->wlanAssociationAttributes.wlanSignalQuality);
    result.phyType = phyTypeName(attr->wlanAssociationAttributes.dot11PhyType);
    result.rxRateMbps = attr->wlanAssociationAttributes.ulRxRate / 1000.0;
    result.txRateMbps = attr->wlanAssociationAttributes.ulTxRate / 1000.0;
    result.profileName = QString::fromWCharArray(attr->strProfileName);
    result.security = mapAuth(attr->wlanSecurityAttributes.dot11AuthAlgorithm,
                              attr->wlanSecurityAttributes.dot11CipherAlgorithm,
                              attr->wlanSecurityAttributes.bSecurityEnabled);

    WlanFreeMemory(data);
    return result;
}
