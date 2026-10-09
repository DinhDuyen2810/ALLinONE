#include "QRPayload.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace
{
QString escapeVCard(const QString& s)
{
    QString out;
    for (const QChar c : s)
    {
        if (c == '\\' || c == ';' || c == ',')
            out += '\\';
        if (c == '\n')
        {
            out += "\\n";
            continue;
        }
        if (c == '\r')
            continue;
        out += c;
    }
    return out;
}

QString unescapeVCard(const QString& s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            out += (s[i] == 'n' || s[i] == 'N') ? QChar('\n') : s[i];
        }
        else
        {
            out += s[i];
        }
    }
    return out;
}

QString percentEncode(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

/// Tách theo ký tự phân cách, bỏ qua ký tự bị escape bằng '\'.
QStringList splitUnescaped(const QString& s, QChar sep)
{
    QStringList parts;
    QString cur;
    for (int i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            cur += s[i];
            cur += s[++i];
        }
        else if (s[i] == sep)
        {
            parts << cur;
            cur.clear();
        }
        else
        {
            cur += s[i];
        }
    }
    parts << cur;
    return parts;
}

bool looksLikeUrl(const QString& s)
{
    static const QRegularExpression re(R"(^(https?|ftp)://\S+$)", QRegularExpression::CaseInsensitiveOption);
    return re.match(s.trimmed()).hasMatch();
}

bool looksLikePhone(const QString& s)
{
    static const QRegularExpression re(R"(^\+?[0-9][0-9 ().\-]{5,18}[0-9]$)");
    return re.match(s.trimmed()).hasMatch();
}

bool looksLikeEmail(const QString& s)
{
    static const QRegularExpression re(R"(^[A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}$)");
    return re.match(s.trimmed()).hasMatch();
}
} // namespace

QString QRPayload::typeName(QRContentType type)
{
    switch (type)
    {
        case QRContentType::Text:  return "Văn bản";
        case QRContentType::Url:   return "Liên kết (URL)";
        case QRContentType::Wifi:  return "WiFi";
        case QRContentType::Email: return "Email";
        case QRContentType::Phone: return "Điện thoại";
        case QRContentType::Sms:   return "SMS";
        case QRContentType::Geo:   return "Vị trí (Geo)";
        case QRContentType::VCard: return "Danh bạ (vCard)";
    }
    return "Văn bản";
}

QString QRPayload::escapeWifi(const QString& s)
{
    QString out;
    for (const QChar c : s)
    {
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

QString QRPayload::unescapeWifi(const QString& s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
            ++i;
        out += s[i];
    }
    return out;
}

QString QRPayload::maskSecrets(const QString& content)
{
    const QString text = content.trimmed();
    if (!text.startsWith("WIFI:", Qt::CaseInsensitive))
        return content;

    // splitUnescaped() giữ nguyên ký tự escape trong từng phần nên join(';') dựng lại đúng chuỗi gốc,
    // chỉ khác ở giá trị P: đã bị thay.
    QStringList parts = splitUnescaped(text.mid(5), ';');
    bool changed = false;
    for (QString& part : parts)
    {
        if (part.size() > 2 && part[1] == ':' && part[0].toUpper() == QChar('P'))
        {
            part = part.left(2) + secretMask();
            changed = true;
        }
    }
    return changed ? text.left(5) + parts.join(';') : content;
}

QString QRPayload::makeUrl(const QString& url)
{
    const QString u = url.trimmed();
    if (u.isEmpty())
        return u;
    static const QRegularExpression scheme(R"(^[A-Za-z][A-Za-z0-9+.\-]*:)");
    if (scheme.match(u).hasMatch())
        return u;
    return "https://" + u;
}

QString QRPayload::makeWifi(const WifiInfo& info)
{
    const QString security = info.security.compare("nopass", Qt::CaseInsensitive) == 0 ? "nopass" : info.security;
    QString s = "WIFI:T:" + security + ";S:" + escapeWifi(info.ssid) + ";";
    if (security != "nopass")
        s += "P:" + escapeWifi(info.password) + ";";
    if (info.hidden)
        s += "H:true;";
    s += ";";
    return s;
}

QString QRPayload::makeEmail(const EmailInfo& info)
{
    QString s = "mailto:" + info.to.trimmed();
    QStringList q;
    if (!info.subject.isEmpty())
        q << "subject=" + percentEncode(info.subject);
    if (!info.body.isEmpty())
        q << "body=" + percentEncode(info.body);
    if (!q.isEmpty())
        s += "?" + q.join("&");
    return s;
}

QString QRPayload::makePhone(const QString& number)
{
    QString n = number.trimmed();
    n.remove(QRegularExpression(R"([ ().\-])"));
    return "tel:" + n;
}

QString QRPayload::makeSms(const SmsInfo& info)
{
    QString n = info.number.trimmed();
    n.remove(QRegularExpression(R"([ ().\-])"));
    return "SMSTO:" + n + ":" + info.message;
}

QString QRPayload::makeGeo(const GeoInfo& info)
{
    return QString("geo:%1,%2").arg(info.latitude, 0, 'f', 6).arg(info.longitude, 0, 'f', 6);
}

QString QRPayload::makeVCard(const VCardInfo& info)
{
    QStringList l;
    l << "BEGIN:VCARD" << "VERSION:3.0";
    l << "N:" + escapeVCard(info.lastName) + ";" + escapeVCard(info.firstName) + ";;;";
    l << "FN:" + escapeVCard((info.firstName + " " + info.lastName).trimmed());
    if (!info.organization.isEmpty()) l << "ORG:" + escapeVCard(info.organization);
    if (!info.title.isEmpty())        l << "TITLE:" + escapeVCard(info.title);
    if (!info.phone.isEmpty())        l << "TEL:" + escapeVCard(info.phone);
    if (!info.email.isEmpty())        l << "EMAIL:" + escapeVCard(info.email);
    if (!info.url.isEmpty())          l << "URL:" + info.url;
    if (!info.address.isEmpty())      l << "ADR:;;" + escapeVCard(info.address) + ";;;;";
    l << "END:VCARD";
    return l.join("\r\n");
}

ParsedPayload QRPayload::parse(const QString& content)
{
    ParsedPayload p;
    const QString text = content.trimmed();
    p.typeName = typeName(QRContentType::Text);

    auto setType = [&p](QRContentType t) {
        p.type = t;
        p.typeName = typeName(t);
    };

    // ---- WiFi ----
    if (text.startsWith("WIFI:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Wifi);
        const QStringList parts = splitUnescaped(text.mid(5), ';');
        QString ssid, pass, sec, hidden;
        for (const QString& part : parts)
        {
            if (part.size() < 2 || part[1] != ':')
                continue;
            const QString value = unescapeWifi(part.mid(2));
            switch (part[0].toUpper().unicode())
            {
                case 'S': ssid = value; break;
                // Trường P lặp lại (mã tự dựng/lỗi): giữ giá trị KHÁC RỖNG. Trước đây trường sau luôn thắng -
                // "P:<mật khẩu>;P:;" cho ra mật khẩu rỗng, secretField = -1, và giao diện (dựa vào secretField
                // để che) hiện nguyên văn chuỗi gốc CÒN mật khẩu ở ô "nội dung gốc".
                case 'P': if (!value.isEmpty()) pass = value; break;
                case 'T': sec = value; break;
                case 'H': hidden = value; break;
            }
        }
        p.fields << qMakePair(QString("Tên mạng (SSID)"), ssid)
                 << qMakePair(QString("Bảo mật"), sec.isEmpty() ? QString("nopass") : sec)
                 << qMakePair(QString("Mật khẩu"), pass)
                 << qMakePair(QString("Mạng ẩn"), hidden.compare("true", Qt::CaseInsensitive) == 0 ? QString("Có") : QString("Không"));
        if (!pass.isEmpty())
            p.secretField = 2; // "Mật khẩu" - giao diện che mặc định
        return p;
    }

    // ---- vCard ----
    if (text.startsWith("BEGIN:VCARD", Qt::CaseInsensitive))
    {
        setType(QRContentType::VCard);
        const QStringList lines = text.split(QRegularExpression(R"(\r\n|\n|\r)"), Qt::SkipEmptyParts);
        // Chỉ MỘT dòng "Họ tên": FN (tên hiển thị đầy đủ) được ưu tiên hơn N (họ;tên rời), bất kể dòng nào
        // đứng trước. Trước đây chỉ nhánh N kiểm tra trùng và chỉ nhìn trường đầu tiên - vCard do chính
        // makeVCard() tạo ra (N đứng TRƯỚC FN) hiện thành hai dòng "Họ tên".
        int nameIndex = -1;
        for (const QString& line : lines)
        {
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QString key = line.left(colon).split(';').first().toUpper();
            const QString value = line.mid(colon + 1);
            if (key == "FN")
            {
                const QString fullName = unescapeVCard(value);
                if (nameIndex < 0)
                {
                    nameIndex = p.fields.size();
                    p.fields << qMakePair(QString("Họ tên"), fullName);
                }
                else if (!fullName.trimmed().isEmpty())
                {
                    p.fields[nameIndex].second = fullName;
                }
            }
            else if (key == "N")
            {
                if (nameIndex >= 0)
                    continue;
                const QStringList n = splitUnescaped(value, ';');
                nameIndex = p.fields.size();
                p.fields << qMakePair(QString("Họ tên"),
                                      unescapeVCard((n.value(1) + " " + n.value(0)).trimmed()));
            }
            else if (key == "ORG")   p.fields << qMakePair(QString("Công ty"), unescapeVCard(splitUnescaped(value, ';').join(" ")));
            else if (key == "TITLE") p.fields << qMakePair(QString("Chức danh"), unescapeVCard(value));
            else if (key == "TEL")   p.fields << qMakePair(QString("Điện thoại"), unescapeVCard(value));
            else if (key == "EMAIL") p.fields << qMakePair(QString("Email"), unescapeVCard(value));
            else if (key == "URL")   p.fields << qMakePair(QString("Website"), value);
            else if (key == "ADR")
            {
                QStringList a = splitUnescaped(value, ';');
                a.removeAll(QString());
                p.fields << qMakePair(QString("Địa chỉ"), unescapeVCard(a.join(", ")));
            }
        }
        return p;
    }

    // ---- MECARD (một số app cũ) ----
    if (text.startsWith("MECARD:", Qt::CaseInsensitive))
    {
        setType(QRContentType::VCard);
        for (const QString& part : splitUnescaped(text.mid(7), ';'))
        {
            const int colon = part.indexOf(':');
            if (colon < 0)
                continue;
            const QString key = part.left(colon).toUpper();
            const QString value = unescapeWifi(part.mid(colon + 1));
            if (key == "N")          p.fields << qMakePair(QString("Họ tên"), value.split(',').join(" ").trimmed());
            else if (key == "TEL")   p.fields << qMakePair(QString("Điện thoại"), value);
            else if (key == "EMAIL") p.fields << qMakePair(QString("Email"), value);
            else if (key == "URL")   p.fields << qMakePair(QString("Website"), value);
            else if (key == "ADR")   p.fields << qMakePair(QString("Địa chỉ"), value);
            else if (key == "ORG")   p.fields << qMakePair(QString("Công ty"), value);
        }
        return p;
    }

    // ---- mailto ----
    if (text.startsWith("mailto:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Email);
        const QUrl url(text);
        p.fields << qMakePair(QString("Người nhận"), url.path());
        const QUrlQuery q(url);
        if (q.hasQueryItem("subject")) p.fields << qMakePair(QString("Tiêu đề"), q.queryItemValue("subject", QUrl::FullyDecoded));
        if (q.hasQueryItem("body"))    p.fields << qMakePair(QString("Nội dung"), q.queryItemValue("body", QUrl::FullyDecoded));
        p.actionUrl = text;
        p.actionLabel = "Soạn email";
        return p;
    }
    if (text.startsWith("MATMSG:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Email);
        QString recipient;
        for (const QString& part : splitUnescaped(text.mid(7), ';'))
        {
            const int colon = part.indexOf(':');
            if (colon < 0)
                continue;
            const QString key = part.left(colon).toUpper();
            const QString value = unescapeWifi(part.mid(colon + 1));
            if (key == "TO")
            {
                p.fields << qMakePair(QString("Người nhận"), value);
                if (recipient.isEmpty())
                    recipient = value.trimmed();
            }
            else if (key == "SUB")  p.fields << qMakePair(QString("Tiêu đề"), value);
            else if (key == "BODY") p.fields << qMakePair(QString("Nội dung"), value);
        }
        // Người nhận lấy từ đúng trường TO, không phải "trường đầu tiên" - MATMSG không quy định thứ tự,
        // mã có SUB/BODY đứng trước TO từng khiến tiêu đề/nội dung bị dùng làm địa chỉ mailto:.
        if (!recipient.isEmpty())
        {
            p.actionUrl = "mailto:" + recipient;
            p.actionLabel = "Soạn email";
        }
        return p;
    }

    // ---- tel ----
    if (text.startsWith("tel:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Phone);
        p.fields << qMakePair(QString("Số điện thoại"), text.mid(4));
        p.actionUrl = text;
        p.actionLabel = "Gọi";
        return p;
    }

    // ---- SMS ----
    if (text.startsWith("smsto:", Qt::CaseInsensitive) || text.startsWith("sms:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Sms);
        QString number, message;
        if (text.startsWith("smsto:", Qt::CaseInsensitive))
        {
            const QString rest = text.mid(6);
            const int colon = rest.indexOf(':');
            number = colon < 0 ? rest : rest.left(colon);
            message = colon < 0 ? QString() : rest.mid(colon + 1);
        }
        else
        {
            const QUrl url(text);
            number = url.path();
            message = QUrlQuery(url).queryItemValue("body", QUrl::FullyDecoded);
        }
        p.fields << qMakePair(QString("Số điện thoại"), number) << qMakePair(QString("Tin nhắn"), message);
        return p;
    }

    // ---- geo ----
    if (text.startsWith("geo:", Qt::CaseInsensitive))
    {
        setType(QRContentType::Geo);
        const QStringList c = text.mid(4).split('?').first().split(',');
        bool okLat = false, okLon = false;
        const double lat = c.value(0).toDouble(&okLat);
        const double lon = c.value(1).toDouble(&okLon);
        p.fields << qMakePair(QString("Vĩ độ"), c.value(0)) << qMakePair(QString("Kinh độ"), c.value(1));
        if (okLat && okLon)
        {
            p.actionUrl = QString("https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=17/%1/%2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
            p.actionLabel = "Mở bản đồ";
        }
        return p;
    }

    // ---- URL ----
    if (looksLikeUrl(text))
    {
        setType(QRContentType::Url);
        p.fields << qMakePair(QString("Liên kết"), text);
        p.actionUrl = text;
        p.actionLabel = "Mở liên kết";
        return p;
    }

    // ---- Nhận diện ngầm: email / điện thoại trần ----
    if (looksLikeEmail(text))
    {
        setType(QRContentType::Email);
        p.fields << qMakePair(QString("Người nhận"), text);
        p.actionUrl = "mailto:" + text;
        p.actionLabel = "Soạn email";
        return p;
    }
    if (looksLikePhone(text))
    {
        setType(QRContentType::Phone);
        p.fields << qMakePair(QString("Số điện thoại"), text);
        return p;
    }

    p.fields << qMakePair(QString("Nội dung"), content);
    return p;
}
