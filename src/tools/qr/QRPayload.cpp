#include "QRPayload.h"

#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

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

/// Tìm MỌI email xuất hiện RẢI RÁC trong một đoạn văn bản tự do (không neo ^$ như looksLikeEmail) - dùng
/// khi cả chuỗi KHÔNG phải là một email trần mà chỉ CHỨA một/nhiều địa chỉ email ở đâu đó bên trong (yêu
/// cầu người dùng: "trong văn bản bắt được gmail hay sđt gì đấy").
QStringList findEmbeddedEmails(const QString& text)
{
    static const QRegularExpression re(R"([A-Za-z0-9._%+\-]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,})");
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext())
    {
        const QString m = it.next().captured(0);
        if (!out.contains(m, Qt::CaseInsensitive))
            out << m;
    }
    return out;
}

/// Tương tự findEmbeddedEmails() nhưng cho số điện thoại - cùng kiểu ký tự chấp nhận (chữ số + khoảng
/// trắng/().-) với looksLikePhone() nhưng không neo ^$, đủ dài (>=8 chữ số) để giảm nhận nhầm số ngẫu nhiên.
QStringList findEmbeddedPhones(const QString& text)
{
    static const QRegularExpression re(R"(\+?[0-9][0-9 ().\-]{6,17}[0-9])");
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext())
    {
        const QString m = it.next().captured(0).trimmed();
        if (!out.contains(m))
            out << m;
    }
    return out;
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

namespace
{
bool mapsCoordsInRange(double lat, double lon)
{
    return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0;
}

/// Thử các dạng chứa tọa độ trong MỘT chuỗi, theo thứ tự ưu tiên (xem QRPayload::parseGoogleMapsCoordinates).
bool matchMapsCoords(const QString& s, double* lat, double* lon)
{
    static const QRegularExpression kPatterns[] = {
        // Điểm GHIM của ĐỊA ĐIỂM có mã địa điểm (!1s0x<hex>:0x<hex>) - đúng điểm mà link rút gọn của địa điểm đó
        // dẫn tới. Cần ưu tiên hơn "cặp !3d!4d đầu tiên": link LỘ TRÌNH chỉ đường (data=!4m12...) liệt kê điểm
        // XUẤT PHÁT trước (không có mã địa điểm, vd điểm người dùng gõ bằng độ-phút-giây) rồi mới tới địa điểm.
        QRegularExpression(R"(!1s0x[0-9a-fA-F]+:0x[0-9a-fA-F]+!8m2!3d(-?\d+(?:\.\d+)?)!4d(-?\d+(?:\.\d+)?))"),
        // Điểm GHIM thật của địa điểm trong phần data=... - chính xác hơn tâm khung nhìn "@".
        QRegularExpression(R"(!3d(-?\d+(?:\.\d+)?)!4d(-?\d+(?:\.\d+)?))"),
        // Tâm khung nhìn: ".../maps/@21.028511,105.804817,15z" (link copy từ thanh địa chỉ).
        QRegularExpression(R"(@(-?\d+(?:\.\d+)?),(-?\d+(?:\.\d+)?))"),
        // Tham số truy vấn: link "chia sẻ vị trí" hoặc chính link nút "Mở bản đồ" tạo ra.
        QRegularExpression(R"([?&](?:q|query|ll|center)=(-?\d+(?:\.\d+)?),\s*(-?\d+(?:\.\d+)?))"),
    };
    for (const QRegularExpression& re : kPatterns)
    {
        QRegularExpressionMatchIterator it = re.globalMatch(s);
        while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const double la = m.captured(1).toDouble();
            const double lo = m.captured(2).toDouble();
            if (!mapsCoordsInRange(la, lo))
                continue; // khớp mẫu regex nhưng ra ngoài phạm vi hợp lệ - không phải tọa độ thật, bỏ qua
            *lat = la;
            *lon = lo;
            return true;
        }
    }
    return false;
}
/// Chuẩn hóa ký tự độ/phút/giây Unicode về ASCII để các mẫu bên dưới chỉ phải xử lý một dạng.
QString normalizeCoordText(QString s)
{
    for (QChar c : {QChar(0x2032), QChar(0x2019), QChar(0x2018), QChar(0x00B4), QChar(0x02B9), QChar(0x02BC)})
        s.replace(c, QLatin1Char('\''));
    for (QChar c : {QChar(0x2033), QChar(0x201D), QChar(0x201C), QChar(0x02BA)})
        s.replace(c, QLatin1Char('"'));
    for (QChar c : {QChar(0x00BA), QChar(0x02DA), QChar(0x2218)})
        s.replace(c, QChar(0x00B0));
    s.replace(QStringLiteral("''"), QStringLiteral("\"")); // hai dấu nháy đơn = giây
    return s;
}

/// Độ/phút/giây -> độ thập phân; false nếu phút/giây ngoài [0, 60).
bool dmsToDecimal(double d, double m, double s, double* out)
{
    if (m < 0.0 || m >= 60.0 || s < 0.0 || s >= 60.0)
        return false;
    *out = d + m / 60.0 + s / 3600.0;
    return true;
}

/// Tọa độ VIẾT TAY trong một chuỗi (xem QRPayload::parseLocationText).
bool matchCoordinateText(const QString& raw, double* lat, double* lon)
{
    const QString s = normalizeCoordText(raw);
    constexpr auto kI = QRegularExpression::CaseInsensitiveOption;
    // Giá trị số / chữ hướng dùng chung. (?![A-Za-z]) sau chữ hướng để "N" không ăn vào đầu một từ khác.
    static const QRegularExpression kDmsAfter(
        R"((\d{1,3})\s*°\s*(\d{1,2})\s*'\s*(\d{1,2}(?:\.\d+)?)\s*"?\s*([NS])(?![A-Za-z])[\s,;]*)"
        R"((\d{1,3})\s*°\s*(\d{1,2})\s*'\s*(\d{1,2}(?:\.\d+)?)\s*"?\s*([EW])(?![A-Za-z]))", kI);
    static const QRegularExpression kDmsBefore(
        R"((?<![A-Za-z])([NS])\s*(\d{1,3})\s*°\s*(\d{1,2})\s*'\s*(\d{1,2}(?:\.\d+)?)\s*"?[\s,;]*)"
        R"(([EW])\s*(\d{1,3})\s*°\s*(\d{1,2})\s*'\s*(\d{1,2}(?:\.\d+)?)\s*"?)", kI);
    static const QRegularExpression kDmAfter(
        R"((\d{1,3})\s*°\s*(\d{1,2}(?:\.\d+)?)\s*'?\s*([NS])(?![A-Za-z])[\s,;]*)"
        R"((\d{1,3})\s*°\s*(\d{1,2}(?:\.\d+)?)\s*'?\s*([EW])(?![A-Za-z]))", kI);
    static const QRegularExpression kDdAfter(
        R"((\d{1,3}(?:\.\d+)?)\s*°?\s*([NS])(?![A-Za-z])[\s,;]*(\d{1,3}(?:\.\d+)?)\s*°?\s*([EW])(?![A-Za-z]))", kI);
    static const QRegularExpression kDdBefore(
        R"((?<![A-Za-z])([NS])\s*(\d{1,3}(?:\.\d+)?)\s*°?[\s,;]*([EW])\s*(\d{1,3}(?:\.\d+)?)\s*°?(?![\d.]))", kI);
    static const QRegularExpression kPlainPair(
        R"(^\s*(?:geo:)?\s*\(?\s*(-?\d{1,3}(?:\.\d+)?)\s*[,;]\s*(-?\d{1,3}(?:\.\d+)?)\s*\)?\s*$)", kI);
    static const QRegularExpression kPlainPairSpace(
        R"(^\s*\(?\s*(-?\d{1,3}\.\d+)\s+(-?\d{1,3}\.\d+)\s*\)?\s*$)");

    auto finish = [&](double la, QChar hLat, double lo, QChar hLon) {
        if (hLat.toUpper() == QLatin1Char('S'))
            la = -la;
        if (hLon.toUpper() == QLatin1Char('W'))
            lo = -lo;
        if (!mapsCoordsInRange(la, lo))
            return false;
        *lat = la;
        *lon = lo;
        return true;
    };
    auto d = [](const QRegularExpressionMatch& m, int i) { return m.captured(i).toDouble(); };
    auto h = [](const QRegularExpressionMatch& m, int i) { return m.captured(i).at(0); };

    QRegularExpressionMatch m;
    double la = 0, lo = 0;
    if ((m = kDmsAfter.match(s)).hasMatch() && dmsToDecimal(d(m, 1), d(m, 2), d(m, 3), &la) &&
        dmsToDecimal(d(m, 5), d(m, 6), d(m, 7), &lo))
        return finish(la, h(m, 4), lo, h(m, 8));
    if ((m = kDmsBefore.match(s)).hasMatch() && dmsToDecimal(d(m, 2), d(m, 3), d(m, 4), &la) &&
        dmsToDecimal(d(m, 6), d(m, 7), d(m, 8), &lo))
        return finish(la, h(m, 1), lo, h(m, 5));
    if ((m = kDmAfter.match(s)).hasMatch() && dmsToDecimal(d(m, 1), d(m, 2), 0.0, &la) &&
        dmsToDecimal(d(m, 4), d(m, 5), 0.0, &lo))
        return finish(la, h(m, 3), lo, h(m, 6));
    if ((m = kDdAfter.match(s)).hasMatch())
        return finish(d(m, 1), h(m, 2), d(m, 3), h(m, 4));
    if ((m = kDdBefore.match(s)).hasMatch())
        return finish(d(m, 2), h(m, 1), d(m, 4), h(m, 3));
    if ((m = kPlainPair.match(s)).hasMatch())
        return finish(d(m, 1), QLatin1Char('N'), d(m, 2), QLatin1Char('E'));
    if ((m = kPlainPairSpace.match(s)).hasMatch())
        return finish(d(m, 1), QLatin1Char('N'), d(m, 2), QLatin1Char('E'));
    return false;
}
} // namespace

bool QRPayload::parseLocationText(const QString& text, double* lat, double* lon)
{
    if (!lat || !lon)
        return false;
    if (parseGoogleMapsCoordinates(text, lat, lon))
        return true;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;
    QStringList candidates{trimmed};
    // Đường dẫn Google Maps mã hóa phần trăm: /place/20%C2%B059'31.1%22N+105%C2%B050'33.8%22E (dấu + = khoảng trắng).
    const QString decoded = QUrl::fromPercentEncoding(trimmed.toUtf8());
    if (decoded != trimmed)
        candidates << decoded;
    QString spaced = decoded;
    spaced.replace(QLatin1Char('+'), QLatin1Char(' '));
    if (spaced != decoded)
        candidates << spaced;
    for (const QString& c : candidates)
        if (matchCoordinateText(c, lat, lon))
            return true;
    return false;
}

QList<QPointF> QRPayload::googleMapsPins(const QString& text)
{
    static const QRegularExpression kPin(R"(!3d(-?\d+(?:\.\d+)?)!4d(-?\d+(?:\.\d+)?))");
    QList<QPointF> pins;
    const QString decoded = QUrl::fromPercentEncoding(text.toUtf8());
    for (const QString& s : {text, decoded})
    {
        QRegularExpressionMatchIterator it = kPin.globalMatch(s);
        while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            const double la = m.captured(1).toDouble();
            const double lo = m.captured(2).toDouble();
            if (!mapsCoordsInRange(la, lo))
                continue;
            const bool dup = std::any_of(pins.begin(), pins.end(), [&](const QPointF& p) {
                return std::abs(p.x() - la) < 1e-9 && std::abs(p.y() - lo) < 1e-9;
            });
            if (!dup)
                pins.append(QPointF(la, lo));
        }
    }
    return pins;
}

bool QRPayload::parseGoogleMapsCoordinates(const QString& text, double* lat, double* lon)
{
    if (!lat || !lon)
        return false;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false;

    QStringList candidates{trimmed};
    // Bản đã giải mã phần trăm: "q=21.0%2C105.8" -> "q=21.0,105.8".
    const QString decoded = QUrl::fromPercentEncoding(trimmed.toUtf8());
    if (decoded != trimmed)
        candidates << decoded;
    // Trang đồng ý cookie của Google (consent.google.com/m?continue=<link thật>) bọc link thật ở đây.
    const QUrl url(trimmed, QUrl::TolerantMode);
    if (url.isValid())
    {
        const QString cont = QUrlQuery(url).queryItemValue("continue", QUrl::FullyDecoded);
        if (!cont.isEmpty() && cont != trimmed)
            candidates << cont;
    }
    for (const QString& c : candidates)
        if (matchMapsCoords(c, lat, lon))
            return true;
    return false;
}

QUrl QRPayload::normalizeLinkUrl(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty() || t.contains(QRegularExpression(R"(\s)")))
        return QUrl();
    if (!t.contains("://"))
        t = "https://" + t;
    QUrl u(t, QUrl::TolerantMode);
    if (!u.isValid() || u.host().isEmpty())
        return QUrl();
    if (u.scheme() != "https" && u.scheme() != "http")
        return QUrl();
    u.setScheme("https"); // luôn dùng https - không gửi yêu cầu chuyển hướng qua kênh không mã hóa
    return u;
}

bool QRPayload::isGoogleMapsShortLink(const QString& text)
{
    const QUrl u = normalizeLinkUrl(text);
    if (u.isEmpty())
        return false;
    const QString host = u.host().toLower();
    const QString path = u.path();
    if (host == "maps.app.goo.gl")
        return path.length() > 1;
    if (host == "goo.gl")
        return path.startsWith("/maps");
    if (host == "g.co")
        return path.startsWith("/kgs");
    return false;
}

bool QRPayload::isAllowedMapsRedirectUrl(const QUrl& url)
{
    if (!url.isValid() || url.scheme() != "https")
        return false;
    // google.<tld> chỉ nhận đúng các dạng TLD thật của Google (com, co.xx, com.xx, xx 2 chữ cái) - KHÔNG nhận
    // "google.evil.co" (nhãn "evil" 4 chữ cái không khớp) hay "evilgoogle.com" (phải là nhãn trọn vẹn).
    static const QRegularExpression kHost(
        R"(^(?:[a-z0-9-]+\.)*(?:google\.(?:com(?:\.[a-z]{2})?|co\.[a-z]{2}|[a-z]{2})|goo\.gl|g\.co)$)");
    return kHost.match(url.host().toLower()).hasMatch();
}

WifiInfo QRPayload::parseWifi(const QString& content)
{
    WifiInfo info;
    const QString text = content.trimmed();
    if (!text.startsWith("WIFI:", Qt::CaseInsensitive))
        return info;

    const QStringList parts = splitUnescaped(text.mid(5), ';');
    bool sawType = false;
    for (const QString& part : parts)
    {
        if (part.size() < 2 || part[1] != ':')
            continue;
        const QString value = unescapeWifi(part.mid(2));
        switch (part[0].toUpper().unicode())
        {
            case 'S': info.ssid = value; break;
            // Trường P lặp lại (mã tự dựng/lỗi): giữ giá trị KHÁC RỖNG - cùng lý do đã áp dụng ở parse().
            case 'P': if (!value.isEmpty()) info.password = value; break;
            case 'T': info.security = value; sawType = true; break;
            case 'H': info.hidden = value.compare("true", Qt::CaseInsensitive) == 0; break;
        }
    }
    // Thiếu hẳn trường T: (nhiều ứng dụng tạo mã bỏ qua): trước đây giữ mặc định "WPA" của WifiInfo kể cả
    // khi không có mật khẩu - mã "WIFI:S:Cafe;;" của mạng MỞ bị hiểu là WPA2 với mật khẩu rỗng và kết nối
    // luôn thất bại. Suy theo mật khẩu: có mật khẩu -> WPA, không có -> mạng mở.
    if (!sawType)
        info.security = info.password.isEmpty() ? QStringLiteral("nopass") : QStringLiteral("WPA");
    else if (info.security.isEmpty())
        info.security = "nopass";
    return info;
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
        const WifiInfo w = parseWifi(text);
        p.fields << qMakePair(QString("Tên mạng (SSID)"), w.ssid)
                 << qMakePair(QString("Bảo mật"), w.security)
                 << qMakePair(QString("Mật khẩu"), w.password)
                 << qMakePair(QString("Mạng ẩn"), w.hidden ? QString("Có") : QString("Không"));
        if (!w.password.isEmpty())
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
            // Google Maps (yêu cầu người dùng) - cú pháp URL chính thức của Google cho việc mở một tọa độ,
            // hoạt động trên cả trình duyệt desktop lẫn tự mở app Google Maps trên di động nếu có cài.
            p.actionUrl = QString("https://www.google.com/maps/search/?api=1&query=%1,%2").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
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

    // Văn bản tự do (không khớp bất kỳ loại cụ thể nào ở trên) - vẫn tìm thêm email/SĐT RẢI RÁC bên trong
    // (khác hẳn looksLikeEmail/looksLikePhone chỉ khớp khi CẢ chuỗi là một email/SĐT trần) để người dùng
    // không phải tự đọc dò trong đoạn văn bản dài.
    const QStringList embeddedEmails = findEmbeddedEmails(text);
    const QStringList embeddedPhones = findEmbeddedPhones(text);
    if (!embeddedEmails.isEmpty())
        p.fields << qMakePair(QString("📧 Email phát hiện trong văn bản"), embeddedEmails.join(", "));
    if (!embeddedPhones.isEmpty())
        p.fields << qMakePair(QString("📞 SĐT phát hiện trong văn bản"), embeddedPhones.join(", "));

    p.fields << qMakePair(QString("Nội dung"), content);
    return p;
}
