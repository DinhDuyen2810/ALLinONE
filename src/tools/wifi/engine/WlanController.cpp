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
#ifndef WLAN_PROFILE_GROUP_POLICY
#define WLAN_PROFILE_GROUP_POLICY 0x00000001
#endif
#ifndef WLAN_PROFILE_USER
#define WLAN_PROFILE_USER 0x00000002
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
    // Bảng ánh xạ nằm ở WifiSecurityUtil::fromDot11() (thuần số, kiểm thử được không cần Win32).
    return WifiSecurityUtil::fromDot11(static_cast<unsigned>(algo), static_cast<unsigned>(cipher), securityEnabled);
}

/// Đọc XML một hồ sơ. Trả mã lỗi Win32 (ERROR_SUCCESS / ERROR_NOT_FOUND / ERROR_ACCESS_DENIED...).
/// requestFlags: 0 hoặc WLAN_PROFILE_GET_PLAINTEXT_KEY. outFlags nhận cờ loại hồ sơ (USER/GROUP_POLICY).
DWORD readProfileXml(HANDLE handle, const GUID& guid, const QString& profileName, DWORD requestFlags,
                     QString* xml, DWORD* outFlags)
{
    LPWSTR xmlRaw = nullptr;
    DWORD flags = requestFlags;
    DWORD access = 0;
    const DWORD res = WlanGetProfile(handle, &guid, reinterpret_cast<LPCWSTR>(profileName.utf16()), nullptr,
                                     &xmlRaw, &flags, &access);
    if (res == ERROR_SUCCESS && xmlRaw)
    {
        if (xml)
            *xml = QString::fromWCharArray(xmlRaw);
        if (outFlags)
            *outFlags = flags;
    }
    if (xmlRaw)
        WlanFreeMemory(xmlRaw);
    return res;
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
        // Mạng ẩn (quảng bá SSID trống): vẫn giữ lại để người dùng nhận ra, nhưng ssid để RỖNG + cờ hidden
        // - tên trang trí "(Mạng ẩn)" chỉ do WifiNetwork::displayName() sinh ra lúc hiển thị.
        w.hidden = w.ssid.isEmpty();
        result.push_back(w);
    }
    WlanFreeMemory(list);

    // Gộp các mục trùng SSID (nhiều BSSID của cùng 1 AP, mục có/không gắn hồ sơ).
    return WifiNetworkUtil::mergeDuplicates(result);
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
        // Không đủ quyền đọc mật khẩu dạng chữ có HAI biểu hiện: WlanGetProfile trả ERROR_ACCESS_DENIED,
        // hoặc (thường gặp hơn) vẫn trả THÀNH CÔNG nhưng <keyMaterial> ở dạng mã hóa (<protected>true).
        // Trước đây chỉ nhận biết trường hợp đầu, nên ở trường hợp sau giao diện cứ hiện "••••••••" dù đã
        // tick "Hiện mật khẩu", không một lời giải thích.
        p.passwordAccessDenied = deniedPassword || (withPlaintextPassword && p.keyProtected);
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

bool WlanController::hasProfile(const QString& adapterGuid, const QString& profileName) const
{
    GUID guid{};
    if (!m_handle || !stringToGuid(adapterGuid, &guid))
        return false;
    return readProfileXml(static_cast<HANDLE>(m_handle), guid, profileName, 0, nullptr, nullptr) == ERROR_SUCCESS;
}

QString WlanController::exportProfileXml(const QString& adapterGuid, const QString& profileName,
                                         bool includePlaintextKey, QString* error) const
{
    if (!ensureOpen(m_handle, error))
        return QString();

    GUID guid{};
    if (!stringToGuid(adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return QString();
    }

    const HANDLE handle = static_cast<HANDLE>(m_handle);
    QString xml;
    DWORD res = readProfileXml(handle, guid, profileName, includePlaintextKey ? WLAN_PROFILE_GET_PLAINTEXT_KEY : 0,
                               &xml, nullptr);
    if (res == ERROR_ACCESS_DENIED && includePlaintextKey)
        res = readProfileXml(handle, guid, profileName, 0, &xml, nullptr);
    if (res != ERROR_SUCCESS)
    {
        setErr(error, "Không xuất được hồ sơ: " + dwordToErrorText(res));
        return QString();
    }

    if (includePlaintextKey)
    {
        // Không đủ quyền thì Windows hoặc từ chối (đã thử lại ở trên), hoặc vẫn trả thành công nhưng
        // khóa ở dạng mã hóa - cả hai đều phải báo cho người dùng biết tệp KHÔNG chứa mật khẩu đọc được.
        WifiProfile parsed;
        if (WlanProfileXml::parse(xml, &parsed) && parsed.keyProtected)
            setErr(error, "Đã xuất nhưng KHÔNG kèm mật khẩu dạng chữ (cần chạy với quyền Administrator). "
                          "Khóa trong tệp ở dạng mã hóa, chỉ nhập lại được trên chính máy này.");
    }
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
                                         WifiSecurity securityOverride, bool autoConnect, WifiProfileBackup* backup,
                                         QString* error)
{
    if (backup)
        *backup = WifiProfileBackup();
    if (!ensureOpen(m_handle, error))
        return false;
    if (network.ssid.isEmpty() || network.ssidBytes.isEmpty() || network.ssidBytes.size() > 32)
    {
        setErr(error, "Tên mạng (SSID) không hợp lệ (phải từ 1 đến 32 byte).");
        return false;
    }
    // SSID là dữ liệu KHÔNG TIN CẬY (điểm phát nào cũng tự đặt được 32 byte tùy ý) và ở đây nó còn được dùng
    // làm TÊN HỒ SƠ. Tên chứa ký tự điều khiển/U+0000/CR không đi nguyên vẹn qua XML hồ sơ (QXmlStreamWriter
    // lặng lẽ BỎ ký tự không hợp lệ, bộ đọc XML đổi CR thành LF) lẫn tham số chuỗi của WLAN API: Windows sẽ
    // tạo hồ sơ dưới một tên KHÁC tên dùng bên dưới để sao lưu/kết nối/hoàn tác - tức ghi đè KHÔNG sao lưu
    // lên hồ sơ mang tên đã bị rút gọn đó (vd SSID "Nha<0x01>" đè hồ sơ "Nha" đang dùng tốt), rồi việc hoàn
    // tác lại đi xóa một tên không tồn tại. Từ chối trước khi đụng tới bất kỳ hồ sơ nào.
    if (!WlanProfileXml::isSafeProfileName(network.ssid))
    {
        setErr(error, "Tên mạng (SSID) này chứa ký tự điều khiển không hiển thị được nên không thể tạo hồ sơ kết nối "
                      "an toàn từ đây. Nếu đây đúng là mạng của bạn, hãy kết nối bằng biểu tượng WiFi của Windows.");
        return false;
    }

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

    const HANDLE handle = static_cast<HANDLE>(m_handle);
    const QString profileName = network.ssid;

    // ---- Sao lưu hồ sơ cùng tên (nếu có) TRƯỚC khi ghi đè ----
    // WlanSetProfile(bOverwrite=TRUE) bên dưới thay hẳn hồ sơ cũ bằng hồ sơ mang mật khẩu vừa nhập, TRƯỚC
    // khi biết mật khẩu đó có đúng không. Không sao lưu thì một lần gõ nhầm là mất mật khẩu đúng đang lưu.
    // Ưu tiên bản có khóa dạng chữ (chắc chắn nhập lại được); không đủ quyền thì dùng bản khóa mã hóa mà
    // Windows trả về (nhập lại được trên chính máy này).
    WifiProfileBackup saved;
    saved.adapterGuid = adapterGuid;
    saved.profileName = profileName;
    {
        QString oldXml;
        DWORD oldFlags = 0;
        DWORD getRes = readProfileXml(handle, guid, profileName, WLAN_PROFILE_GET_PLAINTEXT_KEY, &oldXml, &oldFlags);
        if (getRes == ERROR_ACCESS_DENIED)
            getRes = readProfileXml(handle, guid, profileName, 0, &oldXml, &oldFlags);

        if (getRes == ERROR_SUCCESS)
        {
            if (oldFlags & WLAN_PROFILE_GROUP_POLICY)
            {
                setErr(error, "Hồ sơ của mạng này do chính sách nhóm (Group Policy) quản lý, không thể thay đổi.");
                return false;
            }
            // Hồ sơ trùng TÊN nhưng thuộc một mạng KHÁC (xem WlanProfileXml::describesSsid): ghi đè sẽ xóa mất
            // hồ sơ của mạng kia, kể cả khi lần kết nối này THÀNH CÔNG - lúc đó không có hoàn tác nào cứu nó.
            if (!WlanProfileXml::describesSsid(oldXml, network.ssidBytes))
            {
                setErr(error, QString("Máy này đã có một hồ sơ tên \"%1\" nhưng của một mạng KHÁC. Không ghi đè để khỏi mất "
                                      "hồ sơ đó - hãy xuất hoặc xóa nó ở tab \"Hồ sơ đã lưu\" rồi thử lại.").arg(profileName));
                return false;
            }
            saved.existed = true;
            saved.perUser = (oldFlags & WLAN_PROFILE_USER) != 0;
            saved.xml = oldXml;
        }
        else if (getRes != ERROR_NOT_FOUND)
        {
            // Có hồ sơ nhưng không đọc được để sao lưu: thà không kết nối còn hơn ghi đè không đường lui.
            setErr(error, "Không sao lưu được hồ sơ hiện có của mạng này nên không ghi đè: " + dwordToErrorText(getRes));
            return false;
        }
    }
    saved.valid = true;
    const DWORD profileFlags = saved.perUser ? WLAN_PROFILE_USER : 0;

    const QString xml = WlanProfileXml::build(network.ssidBytes, network.ssid, securityOverride, password, autoConnect,
                                              network.hidden);

    DWORD reason = 0;
    const DWORD setRes = WlanSetProfile(handle, &guid, profileFlags,
                                        reinterpret_cast<LPCWSTR>(xml.utf16()), nullptr, TRUE, nullptr, &reason);
    if (setRes != ERROR_SUCCESS)
    {
        setErr(error, QString("Không tạo được hồ sơ kết nối (mã lỗi lý do %1): %2").arg(reason).arg(dwordToErrorText(setRes)));
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

    const DWORD connRes = WlanConnect(handle, &guid, &params, nullptr);
    if (connRes != ERROR_SUCCESS)
    {
        // Hồ sơ đã bị ghi đè/tạo mới nhưng không gửi nổi yêu cầu kết nối: hoàn tác ngay tại đây.
        rollbackProfile(saved, nullptr);
        setErr(error, "Không gửi được yêu cầu kết nối: " + dwordToErrorText(connRes));
        return false;
    }
    if (backup)
        *backup = saved;
    return true;
}

bool WlanController::rollbackProfile(const WifiProfileBackup& backup, QString* error)
{
    if (!backup.valid)
        return true;
    if (!ensureOpen(m_handle, error))
        return false;

    GUID guid{};
    if (!stringToGuid(backup.adapterGuid, &guid))
    {
        setErr(error, "GUID adapter không hợp lệ.");
        return false;
    }

    const HANDLE handle = static_cast<HANDLE>(m_handle);
    if (backup.existed)
    {
        DWORD reason = 0;
        const DWORD res = WlanSetProfile(handle, &guid, backup.perUser ? WLAN_PROFILE_USER : 0,
                                         reinterpret_cast<LPCWSTR>(backup.xml.utf16()), nullptr, TRUE, nullptr, &reason);
        if (res != ERROR_SUCCESS)
        {
            setErr(error, QString("Không khôi phục được hồ sơ cũ \"%1\" (mã lỗi lý do %2): %3")
                              .arg(backup.profileName).arg(reason).arg(dwordToErrorText(res)));
            return false;
        }
        return true;
    }

    const DWORD res = WlanDeleteProfile(handle, &guid, reinterpret_cast<LPCWSTR>(backup.profileName.utf16()), nullptr);
    if (res != ERROR_SUCCESS && res != ERROR_NOT_FOUND)
    {
        setErr(error, QString("Không xóa được hồ sơ tạm \"%1\": %2").arg(backup.profileName, dwordToErrorText(res)));
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
    result.connecting = (attr->isState == wlan_interface_state_associating ||
                         attr->isState == wlan_interface_state_discovering ||
                         attr->isState == wlan_interface_state_authenticating);
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
