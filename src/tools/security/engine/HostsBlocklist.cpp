#include "HostsBlocklist.h"

#include "core/WinElevation.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QTextStream>

namespace HostsBlocklist
{

namespace internal
{

QString hostsFilePath()
{
    QString systemRoot = QProcessEnvironment::systemEnvironment().value("SystemRoot");
    if (systemRoot.isEmpty())
        systemRoot = "C:/Windows";
    return systemRoot + "/System32/drivers/etc/hosts";
}

QStringList parseManagedDomains(const QString& hostsContent)
{
    QStringList result;
    const QStringList lines = hostsContent.split('\n');
    bool inBlock = false;
    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line == kMarkerStart)
        {
            inBlock = true;
            continue;
        }
        if (line == kMarkerEnd)
        {
            inBlock = false;
            continue;
        }
        if (!inBlock || line.isEmpty() || line.startsWith('#'))
            continue;
        // Mỗi dòng do OneForAll ghi có dạng "0.0.0.0 <domain>"
        const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.size() >= 2)
            result << parts[1];
    }
    return result;
}

QString buildUpdatedHostsContent(const QString& existingContent, const QStringList& domains)
{
    QStringList lines = existingContent.split('\n');

    // Tìm và loại bỏ khối cũ (nếu có) - giữ nguyên MỌI dòng khác người dùng/chương trình khác đã có.
    QStringList kept;
    bool inBlock = false;
    for (const QString& line : lines)
    {
        const QString trimmed = line.trimmed();
        if (trimmed == kMarkerStart)
        {
            inBlock = true;
            continue;
        }
        if (trimmed == kMarkerEnd)
        {
            inBlock = false;
            continue;
        }
        if (inBlock)
            continue;
        kept << line;
    }

    // Bỏ các dòng trắng thừa ở cuối trước khi nối thêm khối mới, tránh nội dung phình to dần qua mỗi lần lưu.
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty())
        kept.removeLast();

    QString result = kept.join('\n');
    if (!result.isEmpty())
        result += "\n\n";

    if (!domains.isEmpty())
    {
        result += kMarkerStart + "\n";
        for (const QString& d : domains)
            result += QStringLiteral("0.0.0.0 %1\n").arg(d.trimmed().toLower());
        result += kMarkerEnd + "\n";
    }
    return result;
}

} // namespace internal

QStringList listBlockedDomains(QString* error)
{
    QFile f(internal::hostsFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error) *error = "Không đọc được hosts file.";
        return {};
    }
    const QString content = QString::fromUtf8(f.readAll());
    return internal::parseManagedDomains(content);
}

namespace
{
/// Đọc nội dung hosts file hiện tại, thêm/bớt domain, rồi ghi đè lại - dùng chung cho add/remove.
/// Cần quyền Administrator vì hosts file là tệp hệ thống được bảo vệ.
bool rewriteWithDomains(const QStringList& newDomains, QString* error)
{
    const QString path = internal::hostsFilePath();
    QFile f(path);
    QString existingContent;
    if (f.exists())
    {
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (error) *error = "Không đọc được hosts file - cần quyền Administrator.";
            return false;
        }
        existingContent = QString::fromUtf8(f.readAll());
        f.close();
    }

    const QString updated = internal::buildUpdatedHostsContent(existingContent, newDomains);

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (error)
            *error = WinElevation::isElevated()
                ? "Không ghi được hosts file."
                : "Không ghi được hosts file - cần quyền Administrator (hosts file là tệp hệ thống được bảo vệ).";
        return false;
    }
    QTextStream out(&f);
    out << updated;
    return true;
}
} // namespace

bool addDomain(const QString& domain, QString* error)
{
    const QString d = domain.trimmed().toLower();
    if (d.isEmpty())
    {
        if (error) *error = "Tên miền trống.";
        return false;
    }
    QStringList domains = listBlockedDomains();
    if (!domains.contains(d))
        domains << d;
    return rewriteWithDomains(domains, error);
}

bool removeDomain(const QString& domain, QString* error)
{
    const QString d = domain.trimmed().toLower();
    QStringList domains = listBlockedDomains();
    domains.removeAll(d);
    return rewriteWithDomains(domains, error);
}

} // namespace HostsBlocklist
