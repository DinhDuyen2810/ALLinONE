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

QString WlanProfileXml::build(const QByteArray& ssidBytes, const QString& ssidDisplayName, WifiSecurity security,
                              const QString& password, bool autoConnect)
{
    const AuthEnc ae = authEncFor(security);
    const bool v3 = (security == WifiSecurity::Wpa3Sae);
    const QString ns = v3 ? "http://www.microsoft.com/networking/WLAN/profile/v3"
                          : "http://www.microsoft.com/networking/WLAN/profile/v1";

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
    if (!keyMaterial.isEmpty() && isPlaintext)
    {
        out->password = keyMaterial;
        out->hasPassword = true;
    }
    out->rawXml = xml;
    return true;
}
