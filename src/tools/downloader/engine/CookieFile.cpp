#include "CookieFile.h"

#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QTimeZone>

namespace
{
constexpr qint64 kMaxCookieFileBytes = 5LL * 1024 * 1024;
} // namespace

namespace CookieFile
{
QList<QNetworkCookie> parse(const QByteArray& text, const QDateTime& now)
{
    QList<QNetworkCookie> out;
    const QList<QByteArray> lines = text.split('\n');
    for (QByteArray line : lines)
    {
        if (line.endsWith('\r'))
            line.chop(1);
        bool httpOnly = false;
        if (line.startsWith("#HttpOnly_"))
        {
            httpOnly = true;
            line = line.mid(10);
        }
        else if (line.isEmpty() || line.startsWith('#'))
        {
            continue;
        }
        const QList<QByteArray> f = line.split('\t');
        if (f.size() < 7)
            continue;
        QByteArray domain = f[0].trimmed();
        const bool includeSub = f[1].trimmed().toUpper() == "TRUE";
        const QByteArray path = f[2].trimmed();
        const bool secure = f[3].trimmed().toUpper() == "TRUE";
        const qint64 expiry = f[4].trimmed().toLongLong();
        const QByteArray name = f[5];
        const QByteArray value = f[6];
        if (domain.isEmpty() || name.isEmpty())
            continue;
        // Miền gồm cả miền con thì phải có dấu chấm đầu; miền chỉ riêng máy chủ thì không có.
        if (includeSub && !domain.startsWith('.'))
            domain.prepend('.');
        if (!includeSub && domain.startsWith('.'))
            domain.remove(0, 1);

        QNetworkCookie c(name, value);
        c.setDomain(QString::fromLatin1(domain));
        c.setPath(path.isEmpty() ? QStringLiteral("/") : QString::fromLatin1(path));
        c.setSecure(secure);
        c.setHttpOnly(httpOnly);
        if (expiry > 0)
        {
            const QDateTime exp = QDateTime::fromSecsSinceEpoch(expiry, QTimeZone::UTC);
            if (exp <= now)
                continue; // đã hết hạn
            c.setExpirationDate(exp);
        }
        out << c;
    }
    return out;
}

QList<QNetworkCookie> load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = "Không mở được tệp cookie: " + f.errorString();
        return {};
    }
    if (f.size() > kMaxCookieFileBytes)
    {
        if (error)
            *error = "Tệp cookie quá lớn (trên 5 MB).";
        return {};
    }
    return parse(f.readAll());
}

int applyTo(QNetworkAccessManager* nam, const QString& path)
{
    if (!nam)
        return 0;
    QNetworkCookieJar* old = nam->cookieJar();
    auto* jar = new QNetworkCookieJar(nam);
    int count = 0;
    if (!path.isEmpty())
        for (const QNetworkCookie& c : load(path))
            if (jar->insertCookie(c))
                ++count;
    nam->setCookieJar(jar);
    if (old && old != jar && old->parent() == nam)
        old->deleteLater(); // bình cookie cũ do chính nam sở hữu
    return count;
}

QString stamp(const QString& path)
{
    if (path.isEmpty())
        return {};
    const QFileInfo fi(path);
    return fi.exists() ? path + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch()) + "|" + QString::number(fi.size())
                       : path + "|missing";
}
} // namespace CookieFile
