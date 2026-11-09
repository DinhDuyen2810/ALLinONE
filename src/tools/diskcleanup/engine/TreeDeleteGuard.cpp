#include "TreeDeleteGuard.h"

#include "FsSafety.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>
#include <QStringList>

namespace TreeDeleteGuard
{
namespace
{
QString norm(const QString& p)
{
    QString n = QDir::cleanPath(QDir::fromNativeSeparators(p));
    while (n.size() > 3 && n.endsWith(QLatin1Char('/')))
        n.chop(1);
    return n;
}

/// a == b hoặc a nằm bên trong b (so không phân biệt hoa thường, theo ranh giới thư mục).
bool isSameOrInside(const QString& a, const QString& b)
{
    if (a.compare(b, Qt::CaseInsensitive) == 0)
        return true;
    const QString prefix = b.endsWith(QLatin1Char('/')) ? b : b + QLatin1Char('/');
    return a.startsWith(prefix, Qt::CaseInsensitive);
}

/// Thư mục chứa chương trình đang chạy (bao gồm cả khi người dùng chọn xóa chính nó hoặc cha của nó).
QString appDir()
{
    const QCoreApplication* app = QCoreApplication::instance();
    return app ? norm(QCoreApplication::applicationDirPath()) : QString();
}

/// Mục đặc biệt ở GỐC ổ đĩa mà hệ thống cần - tên thư mục/tệp cấp 1 ngay sau "X:/".
bool isRootSpecial(const QString& firstComponent)
{
    static const QSet<QString> names = {
        QStringLiteral("system volume information"), QStringLiteral("$recycle.bin"),
        QStringLiteral("pagefile.sys"),              QStringLiteral("hiberfil.sys"),
        QStringLiteral("swapfile.sys"),              QStringLiteral("recovery"),
        QStringLiteral("boot"),                      QStringLiteral("efi"),
        QStringLiteral("bootmgr"),                   QStringLiteral("bootnxt"),
        QStringLiteral("$winreagent"),               QStringLiteral("$sysreset"),
        QStringLiteral("$mft"),                      QStringLiteral("$logfile"),
        QStringLiteral("$bitmap"),                   QStringLiteral("$extend"),
    };
    return names.contains(firstComponent.toLower());
}
} // namespace

Verdict check(const QString& rawPath, const QString& scanRoot, bool isDir)
{
    Verdict v;
    const QString path = norm(rawPath);
    const QString root = norm(scanRoot);
    if (path.isEmpty())
    {
        v.blockReason = QStringLiteral("đường dẫn rỗng");
        return v;
    }

    // Windows Shell có thể hiểu đường dẫn này thành một đối tượng KHÁC (dấu chấm cuối, ký tự đại diện, gốc ổ...).
    const QString shellReason = FsSafety::unsafeShellPathReason(path);
    if (!shellReason.isEmpty())
    {
        v.blockReason = shellReason;
        return v;
    }

    if (!root.isEmpty() && isSameOrInside(root, path)) // chính gốc đang xem hoặc cha của nó
    {
        v.blockReason = QStringLiteral("đây là thư mục gốc của cây đang xem (hoặc chứa nó)");
        return v;
    }

    const QString winDir = norm(FsSafety::windowsDirectory());
    if (!winDir.isEmpty() && isSameOrInside(path, winDir))
    {
        v.blockReason = QStringLiteral("nằm trong thư mục Windows - xóa tay có thể làm hỏng hệ thống (hãy dùng tab \"Dọn dẹp theo hạng mục\")");
        return v;
    }
    if (!winDir.isEmpty() && isSameOrInside(winDir, path)) // cha của thư mục Windows
    {
        v.blockReason = QStringLiteral("chứa thư mục Windows");
        return v;
    }

    const QString app = appDir();
    if (!app.isEmpty() && (isSameOrInside(app, path) || isSameOrInside(path, app)))
    {
        v.blockReason = QStringLiteral("là thư mục chứa chính ứng dụng One for ALL đang chạy");
        return v;
    }

    // Mục đặc biệt ở gốc ổ: "X:/<tên>" hoặc bên trong nó.
    if (path.size() >= 3 && path[1] == QLatin1Char(':') && path[2] == QLatin1Char('/'))
    {
        const QString rest = path.mid(3);
        const QString first = rest.section(QLatin1Char('/'), 0, 0);
        if (!first.isEmpty() && isRootSpecial(first))
        {
            v.blockReason = QStringLiteral("là tệp/thư mục hệ thống ở gốc ổ đĩa (%1)").arg(first);
            return v;
        }
    }

    // Thư mục: không được trùng hay CHỨA hồ sơ người dùng/Program Files/ProgramData... (xóa nội dung bên trong thì được).
    if (isDir)
    {
        const QString protectedReason = FsSafety::unsafeCleanupRootReason(path, FsSafety::systemProtectedDirs());
        if (!protectedReason.isEmpty())
        {
            v.blockReason = protectedReason;
            return v;
        }
    }

    // Cảnh báo: bên trong thư mục chương trình đã cài.
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432", "ProgramData"})
    {
        const QString dir = norm(qEnvironmentVariable(var));
        if (!dir.isEmpty() && isSameOrInside(path, dir))
        {
            v.warning = QStringLiteral("nằm trong %1 - xóa tay có thể làm hỏng chương trình đã cài (nên gỡ bằng Cài đặt > Ứng dụng)")
                            .arg(QDir::toNativeSeparators(dir));
            break;
        }
    }
    return v;
}
} // namespace TreeDeleteGuard
