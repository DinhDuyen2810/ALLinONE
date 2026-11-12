#include "Authenticator.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

AuthenticatorStore& AuthenticatorStore::instance()
{
    static AuthenticatorStore s;
    return s;
}

AuthenticatorStore::AuthenticatorStore()
    : m_path(AppPaths::profileFile("authenticator.json"))
{
    load();
}

AuthenticatorStore::AuthenticatorStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
{
    load();
}

bool AuthenticatorStore::readFile(QList<OtpAccount>* out)
{
    out->clear();
    QFile f(m_path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly))
    {
        m_error = "Không mở được tệp " + m_path;
        return false;
    }
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &pe);
    if (!doc.isObject())
    {
        // Tệp hỏng: giữ lại bản hỏng (.bak) để còn cứu tay, rồi coi như kho rỗng.
        if (!raw.trimmed().isEmpty())
        {
            QString bak = m_path + ".bak";
            if (QFile::exists(bak))
                bak = m_path + "." + QString::number(QDateTime::currentSecsSinceEpoch()) + ".bak";
            QFile::rename(m_path, bak);
            Logger::instance().warning("Authenticator", "Tệp kho hỏng, đã đổi tên thành " + bak);
        }
        return true;
    }
    int unreadable = 0;
    for (const QJsonValue& v : doc.object().value("accounts").toArray())
    {
        const QJsonObject o = v.toObject();
        OtpAccount a;
        a.id = o.value("id").toString();
        a.issuer = o.value("issuer").toString();
        a.name = o.value("name").toString();
        a.type = o.value("type").toString() == "hotp" ? OtpAccount::Type::Hotp : OtpAccount::Type::Totp;
        a.algorithm = o.value("algorithm").toString("SHA1");
        a.digits = o.value("digits").toInt(6);
        a.period = o.value("period").toInt(30);
        a.counter = static_cast<qint64>(o.value("counter").toDouble(0));
        a.secret = Otp::unprotect(QByteArray::fromBase64(o.value("secret").toString().toLatin1()));
        if (a.id.isEmpty() || a.secret.isEmpty())
        {
            ++unreadable; // khóa không mở được (tệp chép từ tài khoản/máy khác): bỏ qua (sẽ mất khỏi tệp ở lần ghi kế tiếp)
            continue;
        }
        out->push_back(a);
    }
    if (unreadable > 0)
        m_error = QString("%1 tài khoản không đọc được (khóa được mã hóa cho một tài khoản Windows khác).").arg(unreadable);
    return true;
}

bool AuthenticatorStore::load()
{
    m_error.clear();
    QList<OtpAccount> list;
    if (!readFile(&list))
        return false;
    m_accounts = list;
    emit changed();
    return true;
}

bool AuthenticatorStore::save()
{
    QJsonArray arr;
    for (const OtpAccount& a : m_accounts)
    {
        const QByteArray cipher = Otp::protect(a.secret);
        if (cipher.isEmpty())
        {
            m_error = "Không mã hóa được khóa bí mật bằng DPAPI của Windows - không lưu.";
            return false;
        }
        QJsonObject o;
        o["id"] = a.id;
        o["issuer"] = a.issuer;
        o["name"] = a.name;
        o["type"] = a.type == OtpAccount::Type::Hotp ? "hotp" : "totp";
        o["algorithm"] = a.algorithm;
        o["digits"] = a.digits;
        o["period"] = a.period;
        o["counter"] = static_cast<double>(a.counter);
        o["secret"] = QString::fromLatin1(cipher.toBase64());
        arr.push_back(o);
    }
    QJsonObject root;
    root["version"] = 1;
    root["accounts"] = arr;
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
    {
        m_error = "Không ghi được " + m_path;
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!f.commit())
    {
        m_error = "Ghi tệp thất bại: " + m_path;
        return false;
    }
    return true;
}

bool AuthenticatorStore::add(OtpAccount account)
{
    m_error.clear();
    // Đọc lại TRƯỚC khi sửa: bản ứng dụng khác có thể đã thêm/xóa tài khoản từ lần đọc gần nhất.
    QList<OtpAccount> disk;
    if (!readFile(&disk))
        return false;
    m_accounts = disk;
    for (const OtpAccount& e : m_accounts)
        if (e.sameAccount(account))
        {
            m_error = "Tài khoản này đã có trong danh sách.";
            return false;
        }
    account.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_accounts.push_back(account);
    if (!save())
    {
        m_accounts.removeLast();
        return false;
    }
    emit changed();
    return true;
}

bool AuthenticatorStore::update(const OtpAccount& account)
{
    m_error.clear();
    QList<OtpAccount> disk;
    if (!readFile(&disk))
        return false;
    m_accounts = disk;
    for (OtpAccount& e : m_accounts)
        if (e.id == account.id)
        {
            e = account;
            if (!save())
                return false;
            emit changed();
            return true;
        }
    m_error = "Không tìm thấy tài khoản.";
    return false;
}

bool AuthenticatorStore::remove(const QString& id)
{
    m_error.clear();
    QList<OtpAccount> disk;
    if (!readFile(&disk))
        return false;
    m_accounts = disk;
    for (int i = 0; i < m_accounts.size(); ++i)
        if (m_accounts[i].id == id)
        {
            m_accounts.removeAt(i);
            if (!save())
                return false;
            emit changed();
            return true;
        }
    return false;
}

bool AuthenticatorStore::advanceCounter(const QString& id)
{
    for (const OtpAccount& e : m_accounts)
        if (e.id == id)
        {
            OtpAccount a = e;
            ++a.counter;
            return update(a);
        }
    return false;
}
