#include "ThirdPartyVpnScanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace ThirdPartyVpnScanner
{
namespace internal
{

QString normalizeServer(const QString& raw)
{
    return raw.trimmed();
}

QString parseOvpnRemote(const QByteArray& ovpn)
{
    const QString text = QString::fromUtf8(ovpn);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    for (const QString& rawLine : lines)
    {
        QString line = rawLine.trimmed();
        if (line.startsWith(QChar('#')) || line.startsWith(QChar(';')))
            continue;
        if (!line.startsWith(QStringLiteral("remote "), Qt::CaseInsensitive))
            continue;
        // "remote <host> [port] [proto]" - lấy host + port (nếu có).
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        const QString host = parts[1];
        if (parts.size() >= 3)
        {
            bool ok = false;
            const int port = parts[2].toInt(&ok);
            if (ok && port > 0)
                return host + ":" + QString::number(port);
        }
        return host;
    }
    return {};
}

QList<QPair<QString, QString>> parseCiscoProfileXml(const QByteArray& xml)
{
    QList<QPair<QString, QString>> out;
    QXmlStreamReader r(xml);
    QString name, address;
    bool inHostEntry = false;
    while (!r.atEnd())
    {
        const auto t = r.readNext();
        if (t == QXmlStreamReader::StartElement)
        {
            const QString el = r.name().toString();
            if (el == QStringLiteral("HostEntry"))
            {
                inHostEntry = true;
                name.clear();
                address.clear();
            }
            else if (inHostEntry && el == QStringLiteral("HostName"))
                name = r.readElementText().trimmed();
            else if (inHostEntry && el == QStringLiteral("HostAddress"))
                address = r.readElementText().trimmed();
        }
        else if (t == QXmlStreamReader::EndElement && r.name().toString() == QStringLiteral("HostEntry"))
        {
            inHostEntry = false;
            if (!name.isEmpty() || !address.isEmpty())
                out.append(qMakePair(name.isEmpty() ? address : name, address.isEmpty() ? name : address));
        }
    }
    return out;
}

QString firstExistingPath(const QStringList& candidates)
{
    for (const QString& c : candidates)
        if (!c.isEmpty() && QFileInfo::exists(c))
            return c;
    return {};
}

} // namespace internal

// =============================================================================================== Win32
namespace
{
#ifdef Q_OS_WIN
QString regReadString(HKEY root, const wchar_t* subkey, const wchar_t* valueName, REGSAM extra = 0)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS)
        return {};
    wchar_t buf[1024] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    const LONG res = RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    RegCloseKey(key);
    if (res != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
        return {};
    return QString::fromWCharArray(buf).trimmed();
}

QStringList regEnumSubkeys(HKEY root, const wchar_t* subkey, REGSAM extra = 0)
{
    QStringList out;
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | extra, &key) != ERROR_SUCCESS)
        return out;
    for (DWORD i = 0;; ++i)
    {
        wchar_t name[256] = {};
        DWORD len = 256;
        const LONG res = RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (res != ERROR_SUCCESS)
            break;
        out << QString::fromWCharArray(name, static_cast<int>(len));
    }
    RegCloseKey(key);
    return out;
}
#endif

QString envDir(const char* name)
{
    return qEnvironmentVariable(name);
}

// ---- FortiClient ----
void scanFortiClient(QList<ThirdPartyVpnProfile>& out)
{
#ifdef Q_OS_WIN
    const QString exe = internal::firstExistingPath({
        envDir("ProgramFiles") + "/Fortinet/FortiClient/FortiClient.exe",
        envDir("ProgramW6432") + "/Fortinet/FortiClient/FortiClient.exe",
        envDir("ProgramFiles(x86)") + "/Fortinet/FortiClient/FortiClient.exe",
    });

    // FortiClient lưu HAI loại tunnel ở hai nơi khác nhau:
    //  - SSL-VPN: địa chỉ ở giá trị "Server" (host:port) NGAY trong khóa tunnel.
    //  - IPsec:   địa chỉ ở giá trị "RemoteGW" trong SUBKEY "<tunnel>\P1" (xác nhận thật trên máy có FortiClient
    //    7.4: tunnel IPsec "VPN_CVTLT" nằm ở ...\FortiClient\IPSec\Tunnels\VPN_CVTLT\P1\RemoteGW). Trước đây chỉ
    //    đọc Sslvpn nên bỏ sót mọi tunnel IPsec.
    struct Base { HKEY root; const wchar_t* path; bool ipsec; };
    const Base bases[] = {
        {HKEY_CURRENT_USER, L"Software\\Fortinet\\FortiClient\\Sslvpn\\Tunnels", false},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Fortinet\\FortiClient\\Sslvpn\\Tunnels", false},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Fortinet\\FortiClient\\Sslvpn\\Tunnels", false},
        {HKEY_CURRENT_USER, L"Software\\Fortinet\\SslvpnClient\\Tunnels", false},  // bản FortiClient cũ hơn
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Fortinet\\SslvpnClient\\Tunnels", false}, // bản FortiClient cũ hơn
        {HKEY_CURRENT_USER, L"Software\\Fortinet\\FortiClient\\IPSec\\Tunnels", true},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\Fortinet\\FortiClient\\IPSec\\Tunnels", true},
        {HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Fortinet\\FortiClient\\IPSec\\Tunnels", true},
    };
    // Dedup theo TÊN tunnel, nhưng ƯU TIÊN bản CÓ địa chỉ máy chủ: cùng một tunnel xuất hiện ở cả HKCU lẫn
    // HKLM, nhưng giá trị "Server"/"RemoteGW" thường chỉ nằm ở HKLM (HKCU chỉ giữ cờ hiển thị) - nếu gặp bản
    // rỗng trước rồi bỏ qua bản có địa chỉ thì cột Máy chủ sẽ trống.
    QList<ThirdPartyVpnProfile> forti;
    QHash<QString, int> idx; // tên (thường) -> vị trí trong forti
    for (const Base& b : bases)
    {
        for (const QString& tunnel : regEnumSubkeys(b.root, b.path))
        {
            QString server;
            if (b.ipsec)
            {
                const std::wstring p1 = (QString::fromWCharArray(b.path) + "\\" + tunnel + "\\P1").toStdWString();
                server = regReadString(b.root, p1.c_str(), L"RemoteGW");
            }
            else
            {
                const std::wstring sub = (QString::fromWCharArray(b.path) + "\\" + tunnel).toStdWString();
                server = regReadString(b.root, sub.c_str(), L"Server");
            }
            server = internal::normalizeServer(server);
            const QString source = (b.root == HKEY_CURRENT_USER ? "HKCU\\" : "HKLM\\") + QString::fromWCharArray(b.path) + "\\" + tunnel;

            const QString key = tunnel.toLower();
            const auto it = idx.constFind(key);
            if (it != idx.constEnd())
            {
                // Đã có: bổ sung địa chỉ nếu bản trước còn trống.
                if (forti[it.value()].server.isEmpty() && !server.isEmpty())
                {
                    forti[it.value()].server = server;
                    forti[it.value()].source = source;
                }
                continue;
            }
            ThirdPartyVpnProfile p;
            p.clientName = b.ipsec ? "FortiClient (IPsec)" : "FortiClient";
            p.name = tunnel;
            p.server = server;
            p.source = source;
            p.exePath = exe;
            // SSL-VPN kết nối trực tiếp được trong ứng dụng; IPsec (IKEv1 + XAuth của FortiGate) thì chưa.
            p.direct = b.ipsec ? DirectProtocol::None : DirectProtocol::FortinetSsl;
            idx.insert(key, forti.size());
            forti << p;
        }
    }
    out += forti;
#else
    Q_UNUSED(out)
#endif
}

// ---- GlobalProtect ----
void scanGlobalProtect(QList<ThirdPartyVpnProfile>& out)
{
#ifdef Q_OS_WIN
    const QString exe = internal::firstExistingPath({
        envDir("ProgramFiles") + "/Palo Alto Networks/GlobalProtect/PanGPA.exe",
        envDir("ProgramW6432") + "/Palo Alto Networks/GlobalProtect/PanGPA.exe",
        envDir("ProgramFiles(x86)") + "/Palo Alto Networks/GlobalProtect/PanGPA.exe",
    });
    QStringList portals;
    const QString p1 = regReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Palo Alto Networks\\GlobalProtect\\PanSetup", L"Portal");
    const QString p2 = regReadString(HKEY_CURRENT_USER, L"SOFTWARE\\Palo Alto Networks\\GlobalProtect\\Settings", L"LastUrl");
    for (const QString& p : {p1, p2})
        if (!p.isEmpty() && !portals.contains(p, Qt::CaseInsensitive))
            portals << p;
    for (const QString& portal : portals)
    {
        ThirdPartyVpnProfile pr;
        pr.clientName = "GlobalProtect";
        pr.name = portal;
        pr.server = portal;
        pr.source = "registry: Palo Alto Networks\\GlobalProtect";
        pr.exePath = exe;
        pr.direct = DirectProtocol::GlobalProtect;
        out << pr;
    }
    // Có cài GlobalProtect nhưng chưa có portal nào lưu: vẫn liệt kê một mục để người dùng mở client tự nhập.
    if (portals.isEmpty() && !exe.isEmpty())
    {
        ThirdPartyVpnProfile pr;
        pr.clientName = "GlobalProtect";
        pr.name = "(chưa có portal lưu sẵn)";
        pr.source = "đã cài GlobalProtect";
        pr.exePath = exe;
        out << pr;
    }
#else
    Q_UNUSED(out)
#endif
}

// ---- OpenVPN GUI ----
void scanOpenVpn(QList<ThirdPartyVpnProfile>& out)
{
#ifdef Q_OS_WIN
    const QString gui = internal::firstExistingPath({
        envDir("ProgramFiles") + "/OpenVPN/bin/openvpn-gui.exe",
        envDir("ProgramW6432") + "/OpenVPN/bin/openvpn-gui.exe",
        envDir("ProgramFiles(x86)") + "/OpenVPN/bin/openvpn-gui.exe",
    });
    QString configDir = regReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\OpenVPN", L"config_dir");
    if (configDir.isEmpty())
        configDir = envDir("ProgramFiles") + "/OpenVPN/config";
    QStringList dirs = {configDir, envDir("USERPROFILE") + "/OpenVPN/config"};
    QStringList seen;
    for (const QString& d : dirs)
    {
        QDir dir(d);
        if (!dir.exists())
            continue;
        const QFileInfoList files = dir.entryInfoList({"*.ovpn"}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files)
        {
            const QString base = fi.completeBaseName();
            if (seen.contains(base, Qt::CaseInsensitive))
                continue;
            seen << base;
            QFile f(fi.absoluteFilePath());
            QString server;
            if (f.open(QIODevice::ReadOnly))
            {
                server = internal::parseOvpnRemote(f.read(64 * 1024));
                f.close();
            }
            ThirdPartyVpnProfile p;
            p.clientName = "OpenVPN";
            p.name = base;
            p.server = server;
            p.source = fi.absoluteFilePath();
            p.exePath = gui;
            // openvpn-gui --connect <tên>.ovpn (có/không đuôi đều được theo tài liệu OpenVPN GUI).
            if (!gui.isEmpty())
                p.launchArg = "--connect \"" + fi.fileName() + "\"";
            out << p;
        }
    }
#else
    Q_UNUSED(out)
#endif
}

// ---- Cisco AnyConnect / Secure Client ----
void scanCisco(QList<ThirdPartyVpnProfile>& out)
{
#ifdef Q_OS_WIN
    const QString exe = internal::firstExistingPath({
        envDir("ProgramFiles(x86)") + "/Cisco/Cisco Secure Client/UI/csc_ui.exe",
        envDir("ProgramFiles") + "/Cisco/Cisco Secure Client/UI/csc_ui.exe",
        envDir("ProgramFiles(x86)") + "/Cisco/Cisco AnyConnect Secure Mobility Client/vpnui.exe",
        envDir("ProgramFiles") + "/Cisco/Cisco AnyConnect Secure Mobility Client/vpnui.exe",
    });
    const QString programData = envDir("ProgramData");
    const QStringList profileDirs = {
        programData + "/Cisco/Cisco Secure Client/VPN/Profile",
        programData + "/Cisco/Cisco AnyConnect Secure Mobility Client/Profile",
    };
    QStringList seen;
    for (const QString& d : profileDirs)
    {
        QDir dir(d);
        if (!dir.exists())
            continue;
        for (const QFileInfo& fi : dir.entryInfoList({"*.xml"}, QDir::Files, QDir::Name))
        {
            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly))
                continue;
            const QByteArray xml = f.read(256 * 1024);
            f.close();
            for (const auto& host : internal::parseCiscoProfileXml(xml))
            {
                const QString key = host.first + "|" + host.second;
                if (seen.contains(key, Qt::CaseInsensitive))
                    continue;
                seen << key;
                ThirdPartyVpnProfile p;
                p.clientName = "Cisco Secure Client";
                p.name = host.first;
                p.server = host.second;
                p.source = fi.absoluteFilePath();
                p.exePath = exe;
                out << p;
            }
        }
    }
#else
    Q_UNUSED(out)
#endif
}
} // namespace

QList<ThirdPartyVpnProfile> scan()
{
    QList<ThirdPartyVpnProfile> out;
    scanFortiClient(out);
    scanGlobalProtect(out);
    scanOpenVpn(out);
    scanCisco(out);
    return out;
}

bool launch(const ThirdPartyVpnProfile& profile, QString* error)
{
#ifdef Q_OS_WIN
    if (profile.exePath.isEmpty() || !QFileInfo::exists(profile.exePath))
    {
        if (error)
            *error = "Không tìm thấy chương trình \"" + profile.clientName + "\" trên máy. Hãy mở nó thủ công để kết nối.";
        return false;
    }
    const std::wstring exeW = QDir::toNativeSeparators(profile.exePath).toStdWString();
    const std::wstring argsW = profile.launchArg.toStdWString();
    const std::wstring dirW = QDir::toNativeSeparators(QFileInfo(profile.exePath).absolutePath()).toStdWString();
    const HINSTANCE r = ShellExecuteW(nullptr, L"open", exeW.c_str(), profile.launchArg.isEmpty() ? nullptr : argsW.c_str(),
                                      dirW.c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(r) <= 32)
    {
        if (error)
            *error = "Không mở được \"" + profile.clientName + "\" (mã lỗi " + QString::number(reinterpret_cast<INT_PTR>(r)) + ").";
        return false;
    }
    return true;
#else
    Q_UNUSED(profile)
    if (error) *error = "Chỉ hỗ trợ trên Windows.";
    return false;
#endif
}

} // namespace ThirdPartyVpnScanner
