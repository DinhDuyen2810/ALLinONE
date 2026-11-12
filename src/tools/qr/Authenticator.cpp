#include "Authenticator.h"

#include <QMessageAuthenticationCode>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

QString OtpAccount::displayName() const
{
    if (!issuer.isEmpty() && !name.isEmpty())
        return issuer + " (" + name + ")";
    return !issuer.isEmpty() ? issuer : name;
}

bool OtpAccount::sameAccount(const OtpAccount& o) const
{
    return secret == o.secret && issuer == o.issuer && name == o.name && type == o.type;
}

// ---------------------------------------------------------------- Base32

QByteArray Otp::base32Decode(const QString& text, bool* ok)
{
    QByteArray out;
    quint32 buffer = 0;
    int bits = 0;
    bool good = true;
    int symbols = 0;
    for (const QChar qc : text)
    {
        const ushort c = qc.toUpper().unicode();
        if (c == ' ' || c == '-' || c == '\t' || c == '\r' || c == '\n' || c == '=')
            continue;
        int v;
        if (c >= 'A' && c <= 'Z')
            v = c - 'A';
        else if (c >= '2' && c <= '7')
            v = c - '2' + 26;
        else
        {
            good = false;
            break;
        }
        ++symbols;
        buffer = (buffer << 5) | static_cast<quint32>(v);
        bits += 5;
        if (bits >= 8)
        {
            bits -= 8;
            out.append(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    if (symbols == 0)
        good = false;
    if (ok)
        *ok = good;
    return good ? out : QByteArray();
}

QString Otp::base32Encode(const QByteArray& data)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    QString out;
    quint32 buffer = 0;
    int bits = 0;
    for (const char b : data)
    {
        buffer = (buffer << 8) | static_cast<quint8>(b);
        bits += 8;
        while (bits >= 5)
        {
            bits -= 5;
            out.append(QLatin1Char(alphabet[(buffer >> bits) & 0x1F]));
        }
    }
    if (bits > 0)
        out.append(QLatin1Char(alphabet[(buffer << (5 - bits)) & 0x1F]));
    return out;
}

// ---------------------------------------------------------------- otpauth://

static bool algorithmOk(const QString& a)
{
    return a == "SHA1" || a == "SHA256" || a == "SHA512";
}

bool Otp::isOtpUri(const QString& text)
{
    const QString t = text.trimmed();
    return t.startsWith("otpauth://", Qt::CaseInsensitive) || t.startsWith("otpauth-migration://", Qt::CaseInsensitive);
}

bool Otp::parseUri(const QString& uriText, OtpAccount* out, QString* error)
{
    auto fail = [&](const QString& msg) {
        if (error)
            *error = msg;
        return false;
    };
    const QString uri = uriText.trimmed();
    if (!uri.startsWith("otpauth://", Qt::CaseInsensitive))
        return fail("Không phải liên kết otpauth://");
    const QUrl url(uri, QUrl::TolerantMode);
    if (!url.isValid())
        return fail("Liên kết otpauth không hợp lệ");

    OtpAccount a;
    const QString kind = url.host().toLower();
    if (kind == "totp")
        a.type = OtpAccount::Type::Totp;
    else if (kind == "hotp")
        a.type = OtpAccount::Type::Hotp;
    else
        return fail("Loại OTP không hỗ trợ: " + kind);

    const QUrlQuery q(url);
    bool ok = false;
    a.secret = base32Decode(q.queryItemValue("secret", QUrl::FullyDecoded), &ok);
    if (!ok || a.secret.isEmpty())
        return fail("Thiếu hoặc sai khóa bí mật (secret, base32)");

    QString label = url.path(QUrl::FullyDecoded);
    if (label.startsWith('/'))
        label.remove(0, 1);
    QString issuerFromLabel;
    const int colon = label.indexOf(':');
    if (colon >= 0)
    {
        issuerFromLabel = label.left(colon).trimmed();
        label = label.mid(colon + 1).trimmed();
    }
    a.name = label.trimmed();
    const QString issuerParam = q.queryItemValue("issuer", QUrl::FullyDecoded).trimmed();
    a.issuer = !issuerParam.isEmpty() ? issuerParam : issuerFromLabel;

    const QString alg = q.queryItemValue("algorithm", QUrl::FullyDecoded).trimmed().toUpper();
    if (!alg.isEmpty())
    {
        a.algorithm = alg == "SHA-1" ? "SHA1" : alg == "SHA-256" ? "SHA256" : alg == "SHA-512" ? "SHA512" : alg;
        if (!algorithmOk(a.algorithm))
            return fail("Thuật toán không hỗ trợ: " + alg);
    }
    const QString digits = q.queryItemValue("digits", QUrl::FullyDecoded).trimmed();
    if (!digits.isEmpty())
    {
        a.digits = digits.toInt(&ok);
        if (!ok || a.digits < 4 || a.digits > 10)
            return fail("Số chữ số không hợp lệ: " + digits);
    }
    const QString period = q.queryItemValue("period", QUrl::FullyDecoded).trimmed();
    if (!period.isEmpty())
    {
        a.period = period.toInt(&ok);
        if (!ok || a.period < 1 || a.period > 3600)
            return fail("Chu kỳ không hợp lệ: " + period);
    }
    if (a.type == OtpAccount::Type::Hotp)
    {
        const QString counter = q.queryItemValue("counter", QUrl::FullyDecoded).trimmed();
        a.counter = counter.isEmpty() ? 0 : counter.toLongLong(&ok);
        if (!counter.isEmpty() && (!ok || a.counter < 0))
            return fail("Bộ đếm HOTP không hợp lệ: " + counter);
    }
    *out = a;
    return true;
}

QString Otp::buildUri(const OtpAccount& a)
{
    QString label = a.issuer.isEmpty() ? a.name : a.issuer + ":" + a.name;
    QUrl url;
    url.setScheme("otpauth");
    url.setHost(a.type == OtpAccount::Type::Totp ? "totp" : "hotp");
    url.setPath("/" + label);
    QUrlQuery q;
    q.addQueryItem("secret", base32Encode(a.secret));
    if (!a.issuer.isEmpty())
        q.addQueryItem("issuer", a.issuer);
    if (a.algorithm != "SHA1")
        q.addQueryItem("algorithm", a.algorithm);
    if (a.digits != 6)
        q.addQueryItem("digits", QString::number(a.digits));
    if (a.type == OtpAccount::Type::Totp && a.period != 30)
        q.addQueryItem("period", QString::number(a.period));
    if (a.type == OtpAccount::Type::Hotp)
        q.addQueryItem("counter", QString::number(a.counter));
    url.setQuery(q);
    return QString::fromUtf8(url.toEncoded());
}

QString Otp::maskUri(const QString& uri)
{
    QString out = uri;
    static const QRegularExpression re("(secret|data)=[^&\\s]*", QRegularExpression::CaseInsensitiveOption);
    out.replace(re, "\\1=••••••••");
    return out;
}

// ---------------------------------------------------------------- Google Authenticator (migration)

namespace
{
struct PbReader
{
    const QByteArray& d;
    int pos{0};
    bool bad{false};
    explicit PbReader(const QByteArray& data) : d(data) {}
    bool atEnd() const { return pos >= d.size(); }
    quint64 varint()
    {
        quint64 v = 0;
        int shift = 0;
        while (pos < d.size())
        {
            const quint8 b = static_cast<quint8>(d[pos++]);
            v |= static_cast<quint64>(b & 0x7F) << shift;
            if (!(b & 0x80))
                return v;
            shift += 7;
            if (shift > 63)
                break;
        }
        bad = true;
        return 0;
    }
    QByteArray bytes()
    {
        const quint64 n = varint();
        if (bad || n > static_cast<quint64>(d.size() - pos))
        {
            bad = true;
            return {};
        }
        const QByteArray r = d.mid(pos, static_cast<int>(n));
        pos += static_cast<int>(n);
        return r;
    }
    void skip(int wire)
    {
        switch (wire)
        {
        case 0: varint(); break;
        case 1: pos += 8; break;
        case 2: bytes(); break;
        case 5: pos += 4; break;
        default: bad = true;
        }
        if (pos > d.size())
            bad = true;
    }
};
} // namespace

QList<OtpAccount> Otp::parseMigration(const QString& uriText, QString* error)
{
    QList<OtpAccount> accounts;
    auto fail = [&](const QString& msg) {
        if (error)
            *error = msg;
        return QList<OtpAccount>();
    };
    const QString uri = uriText.trimmed();
    if (!uri.startsWith("otpauth-migration://", Qt::CaseInsensitive))
        return fail("Không phải liên kết otpauth-migration://");
    const QUrl url(uri, QUrl::TolerantMode);
    QString b64 = QUrlQuery(url).queryItemValue("data", QUrl::FullyDecoded);
    b64.replace(' ', '+'); // '+' không được mã hóa phần trăm bị đọc thành khoảng trắng
    if (b64.isEmpty())
        return fail("Thiếu dữ liệu (data=) trong liên kết xuất của Google Authenticator");
    QByteArray raw = QByteArray::fromBase64(b64.toLatin1(), QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
    if (raw.isEmpty())
        raw = QByteArray::fromBase64(b64.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (raw.isEmpty())
        return fail("Dữ liệu xuất không phải base64 hợp lệ");

    int skipped = 0;
    PbReader top(raw);
    while (!top.atEnd() && !top.bad)
    {
        const quint64 tag = top.varint();
        const int field = static_cast<int>(tag >> 3), wire = static_cast<int>(tag & 7);
        if (field != 1 || wire != 2)
        {
            top.skip(wire);
            continue;
        }
        const QByteArray msg = top.bytes();
        if (top.bad)
            break;
        PbReader r(msg);
        OtpAccount a;
        int algo = 0, digitsEnum = 0, typeEnum = 0;
        while (!r.atEnd() && !r.bad)
        {
            const quint64 t = r.varint();
            const int f = static_cast<int>(t >> 3), w = static_cast<int>(t & 7);
            if (f == 1 && w == 2) a.secret = r.bytes();
            else if (f == 2 && w == 2) a.name = QString::fromUtf8(r.bytes());
            else if (f == 3 && w == 2) a.issuer = QString::fromUtf8(r.bytes());
            else if (f == 4 && w == 0) algo = static_cast<int>(r.varint());
            else if (f == 5 && w == 0) digitsEnum = static_cast<int>(r.varint());
            else if (f == 6 && w == 0) typeEnum = static_cast<int>(r.varint());
            else if (f == 7 && w == 0) a.counter = static_cast<qint64>(r.varint());
            else r.skip(w);
        }
        if (r.bad || a.secret.isEmpty())
        {
            ++skipped;
            continue;
        }
        switch (algo)
        {
        case 0:
        case 1: a.algorithm = "SHA1"; break;
        case 2: a.algorithm = "SHA256"; break;
        case 3: a.algorithm = "SHA512"; break;
        default: ++skipped; continue; // MD5 (4) và giá trị lạ
        }
        a.digits = digitsEnum == 2 ? 8 : 6;
        a.type = typeEnum == 1 ? OtpAccount::Type::Hotp : OtpAccount::Type::Totp;
        a.period = 30;
        a.name = a.name.trimmed();
        a.issuer = a.issuer.trimmed();
        const int colon = a.name.indexOf(':');
        if (a.issuer.isEmpty() && colon > 0)
        {
            a.issuer = a.name.left(colon).trimmed();
            a.name = a.name.mid(colon + 1).trimmed();
        }
        else if (!a.issuer.isEmpty() && colon > 0 && a.name.left(colon).trimmed() == a.issuer)
            a.name = a.name.mid(colon + 1).trimmed(); // Google Authenticator lưu nhãn dạng "Issuer:account" kể cả khi đã có trường issuer
        accounts << a;
    }
    if (top.bad && accounts.isEmpty())
        return fail("Dữ liệu xuất của Google Authenticator bị hỏng");
    if (error && skipped > 0)
        *error = QString("Bỏ qua %1 tài khoản dùng thuật toán không hỗ trợ hoặc dữ liệu hỏng").arg(skipped);
    return accounts;
}

// ---------------------------------------------------------------- HOTP / TOTP

QString Otp::hotp(const OtpAccount& a, qint64 counter)
{
    QCryptographicHash::Algorithm alg = QCryptographicHash::Sha1;
    if (a.algorithm == "SHA256")
        alg = QCryptographicHash::Sha256;
    else if (a.algorithm == "SHA512")
        alg = QCryptographicHash::Sha512;

    QByteArray msg(8, 0);
    for (int i = 7; i >= 0; --i)
    {
        msg[i] = static_cast<char>(counter & 0xFF);
        counter >>= 8;
    }
    const QByteArray mac = QMessageAuthenticationCode::hash(msg, a.secret, alg);
    const int offset = mac.at(mac.size() - 1) & 0x0F;
    const quint32 bin = ((static_cast<quint8>(mac[offset]) & 0x7F) << 24) | (static_cast<quint8>(mac[offset + 1]) << 16)
                      | (static_cast<quint8>(mac[offset + 2]) << 8) | static_cast<quint8>(mac[offset + 3]);
    quint64 mod = 1;
    for (int i = 0; i < a.digits; ++i)
        mod *= 10;
    return QString::number(static_cast<quint64>(bin) % mod).rightJustified(a.digits, '0');
}

QString Otp::code(const OtpAccount& a, qint64 unixSeconds)
{
    if (a.type == OtpAccount::Type::Hotp)
        return hotp(a, a.counter);
    return hotp(a, unixSeconds / std::max(1, a.period));
}

int Otp::remainingSeconds(const OtpAccount& a, qint64 unixSeconds)
{
    if (a.type == OtpAccount::Type::Hotp)
        return 0;
    const int p = std::max(1, a.period);
    return p - static_cast<int>(unixSeconds % p);
}

// ---------------------------------------------------------------- DPAPI

static const char kEntropy[] = "OneForAll Authenticator v1";

QByteArray Otp::protect(const QByteArray& plain)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.constData()))};
    DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{0, nullptr};
    if (!CryptProtectData(&in, L"OneForAll Authenticator", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    const QByteArray r(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return r;
#else
    Q_UNUSED(plain);
    return {};
#endif
}

QByteArray Otp::unprotect(const QByteArray& cipher)
{
#ifdef Q_OS_WIN
    DATA_BLOB in{static_cast<DWORD>(cipher.size()), reinterpret_cast<BYTE*>(const_cast<char*>(cipher.constData()))};
    DATA_BLOB ent{static_cast<DWORD>(sizeof(kEntropy) - 1), reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
    DATA_BLOB out{0, nullptr};
    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};
    const QByteArray r(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return r;
#else
    Q_UNUSED(cipher);
    return {};
#endif
}
