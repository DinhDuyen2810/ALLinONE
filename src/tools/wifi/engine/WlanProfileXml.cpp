#include "WlanProfileXml.h"

#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace
{
struct AuthEnc
{
    const char* authentication;
    const char* encryption;
    bool useOneX;
};

AuthEnc authEncFor(WifiSecurity s)
{
    switch (s)
    {
        case WifiSecurity::Open:       return {"open", "none", false};
        case WifiSecurity::Wep:        return {"open", "WEP", false};
        case WifiSecurity::WpaPsk:     return {"WPAPSK", "TKIP", false};
        case WifiSecurity::Wpa2Psk:    return {"WPA2PSK", "AES", false};
        case WifiSecurity::Wpa3Sae:    return {"WPA3SAE", "AES", false};
        case WifiSecurity::Enterprise: return {"WPA2", "AES", true};
        case WifiSecurity::Unknown:    return {"open", "none", false};
    }
    return {"open", "none", false};
}

WifiSecurity securityFrom(const QString& authentication, const QString& encryption, bool useOneX)
{
    const QString a = authentication.trimmed();
    const QString e = encryption.trimmed();

    if (useOneX)
        return WifiSecurity::Enterprise;
    if (a.compare("WPA3SAE", Qt::CaseInsensitive) == 0)
        return WifiSecurity::Wpa3Sae;
    if (a.compare("WPA2PSK", Qt::CaseInsensitive) == 0)
        return WifiSecurity::Wpa2Psk;
    if (a.compare("WPAPSK", Qt::CaseInsensitive) == 0)
        return WifiSecurity::WpaPsk;
    if (a.compare("open", Qt::CaseInsensitive) == 0)
    {
        if (e.compare("WEP", Qt::CaseInsensitive) == 0)
            return WifiSecurity::Wep;
        if (e.compare("none", Qt::CaseInsensitive) == 0 || e.isEmpty())
            return WifiSecurity::Open;
        return WifiSecurity::Unknown;
    }
    if (a.compare("shared", Qt::CaseInsensitive) == 0)
        return WifiSecurity::Wep;
    return WifiSecurity::Unknown;
}
} // namespace

QString WlanProfileXml::toHex(const QByteArray& bytes)
{
    return QString::fromLatin1(bytes.toHex()).toUpper();
}

QByteArray WlanProfileXml::fromHex(const QString& hex)
{
    return QByteArray::fromHex(hex.toLatin1());
}

bool WlanProfileXml::isSafeProfileName(const QString& name)
{
    if (name.isEmpty())
        return false;
    for (qsizetype i = 0; i < name.size(); ++i)
    {
        const QChar c = name[i];
        if (c.isHighSurrogate())
        {
            if (i + 1 >= name.size() || !name[i + 1].isLowSurrogate())
                return false;
            ++i; // cặp surrogate hợp lệ (emoji...) - bỏ qua nửa sau
            continue;
        }
        if (c.isLowSurrogate())
            return false;
        const char16_t u = c.unicode();
        if (u < 0x20 && u != u'\t' && u != u'\n')
            return false;
        if (u == 0xFFFE || u == 0xFFFF)
            return false;
    }
    return true;
}

bool WlanProfileXml::describesSsid(const QString& xml, const QByteArray& ssidBytes)
{
    QXmlStreamReader r(xml);
    bool inSsid = false;
    bool sawHex = false;
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            if (r.name() == QLatin1String("SSID"))
                inSsid = true;
            else if (inSsid && r.name() == QLatin1String("hex"))
            {
                const QString hex = r.readElementText().trimmed();
                if (hex.isEmpty())
                    continue;
                sawHex = true;
                if (fromHex(hex) == ssidBytes)
                    return true; // một hồ sơ có thể liệt kê nhiều SSID - khớp một cái là đủ
            }
        }
        else if (r.isEndElement() && r.name() == QLatin1String("SSID"))
        {
            inSsid = false;
        }
    }
    return r.hasError() || !sawHex;
}

QString WlanProfileXml::build(const QByteArray& ssidBytes, const QString& ssidDisplayName, WifiSecurity security,
                              const QString& password, bool autoConnect, bool nonBroadcast)
{
    const AuthEnc ae = authEncFor(security);
    // Phần tử gốc WLANProfile LUÔN ở namespace v1, kể cả hồ sơ WPA3: theo tài liệu "WLAN_profile schema"
    // của Microsoft ("The WLANProfile element is in the namespace .../WLAN/profile/v1"), WPA3SAE chỉ là
    // một giá trị mới của <authentication> trong chính schema đó; các namespace v2/v3/v4 chỉ dùng cho vài
    // phần tử mở rộng riêng lẻ (FIPSMode, transitionMode...). Trước đây hồ sơ WPA3 bị ghi gốc ".../v3".
    const QString ns = "http://www.microsoft.com/networking/WLAN/profile/v1";

    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement("WLANProfile");
    w.writeDefaultNamespace(ns);

    w.writeTextElement("name", ssidDisplayName);

    w.writeStartElement("SSIDConfig");
    w.writeStartElement("SSID");
    w.writeTextElement("hex", toHex(ssidBytes));
    w.writeTextElement("name", ssidDisplayName);
    w.writeEndElement(); // SSID
    if (nonBroadcast)
        w.writeTextElement("nonBroadcast", "true");
    w.writeEndElement(); // SSIDConfig

    w.writeTextElement("connectionType", "ESS");
    w.writeTextElement("connectionMode", autoConnect ? "auto" : "manual");

    w.writeStartElement("MSM");
    w.writeStartElement("security");
    w.writeStartElement("authEncryption");
    w.writeTextElement("authentication", ae.authentication);
    w.writeTextElement("encryption", ae.encryption);
    w.writeTextElement("useOneX", ae.useOneX ? "true" : "false");
    w.writeEndElement(); // authEncryption

    if (WifiSecurityUtil::requiresPassword(security))
    {
        w.writeStartElement("sharedKey");
        w.writeTextElement("keyType", security == WifiSecurity::Wep ? "networkKey" : "passPhrase");
        w.writeTextElement("protected", "false");
        w.writeTextElement("keyMaterial", password);
        w.writeEndElement(); // sharedKey
    }

    w.writeEndElement(); // security
    w.writeEndElement(); // MSM

    w.writeEndElement(); // WLANProfile
    w.writeEndDocument();
    return xml;
}

bool WlanProfileXml::parse(const QString& xml, WifiProfile* out)
{
    if (!out)
        return false;

    QXmlStreamReader r(xml);
    QString name, ssidName, connectionMode, authentication, encryption, useOneXStr, keyMaterial;
    bool sawSsid = false;
    bool inSsid = false;
    bool inSharedKey = false;
    QString protectedStr;

    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            const QString tag = r.name().toString();
            if (tag == "SSID")
                inSsid = true;
            else if (tag == "sharedKey")
                inSharedKey = true;
            else if (tag == "name" && !sawSsid && !inSsid)
                name = r.readElementText();
            else if (tag == "name" && inSsid)
            {
                ssidName = r.readElementText();
                sawSsid = true;
            }
            else if (tag == "connectionMode")
                connectionMode = r.readElementText();
            else if (tag == "authentication")
                authentication = r.readElementText();
            else if (tag == "encryption" && !inSharedKey)
                encryption = r.readElementText();
            else if (tag == "useOneX")
                useOneXStr = r.readElementText();
            else if (tag == "protected" && inSharedKey)
                protectedStr = r.readElementText();
            else if (tag == "keyMaterial")
                keyMaterial = r.readElementText();
        }
        else if (r.isEndElement())
        {
            const QString tag = r.name().toString();
            if (tag == "SSID")
                inSsid = false;
            else if (tag == "sharedKey")
                inSharedKey = false;
        }
    }

    if (r.hasError() || name.isEmpty())
        return false;

    out->name = name;
    out->ssid = sawSsid ? ssidName : name;
    out->autoConnect = connectionMode.compare("auto", Qt::CaseInsensitive) == 0;
    out->security = securityFrom(authentication, encryption, useOneXStr.compare("true", Qt::CaseInsensitive) == 0);
    // Khi không xin WLAN_PROFILE_GET_PLAINTEXT_KEY (hoặc bị từ chối), Windows vẫn trả <keyMaterial>
    // nhưng là một chuỗi đã mã hóa DPAPI, không phải mật khẩu thật - nhận biết qua <protected>true</protected>.
    // Chỉ coi là mật khẩu thật khi protected rõ ràng là "false".
    const bool isPlaintext = protectedStr.compare("false", Qt::CaseInsensitive) == 0;
    out->password.clear();
    out->hasPassword = false;
    out->keyProtected = false;
    if (!keyMaterial.isEmpty() && isPlaintext)
    {
        out->password = keyMaterial;
        out->hasPassword = true;
    }
    else if (!keyMaterial.isEmpty())
    {
        out->keyProtected = true;
    }
    out->rawXml = xml;
    return true;
}
