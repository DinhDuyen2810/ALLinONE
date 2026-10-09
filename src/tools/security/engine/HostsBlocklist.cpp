#include "HostsBlocklist.h"

#include "core/WinElevation.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>
#include <QUrl>

namespace HostsBlocklist
{

namespace
{
const QRegularExpression& whitespaceRx()
{
    static const QRegularExpression rx("\\s+");
    return rx;
}

/// Các tên đứng sau "0.0.0.0" trên một dòng (bỏ phần ghi chú sau '#') - rỗng nếu dòng không phải dạng đó.
/// Nhận cả nhiều tên trên một dòng ("0.0.0.0 a.com b.com"): bản trước cho phép nhập như vậy rồi chỉ đọc
/// lại tên đầu, khiến tên thứ hai biến mất ở lần ghi kế tiếp.
QStringList blockedNamesOnLine(const QString& rawLine)
{
    QString line = rawLine;
    const int hash = line.indexOf('#');
    if (hash >= 0)
        line.truncate(hash);
    QStringList parts = line.split(whitespaceRx(), Qt::SkipEmptyParts);
    if (parts.size() < 2 || parts.first() != "0.0.0.0")
        return {};
    parts.removeFirst();
    for (QString& p : parts)
        p = p.toLower();
    return parts;
}

/// Đúng NGUYÊN dạng công cụ này tự ghi: "0.0.0.0 <một tên miền hợp lệ đã chuẩn hóa>", không gì khác.
bool isOwnFormatLine(const QString& rawLine, QString* domain)
{
    const QStringList parts = rawLine.trimmed().split(whitespaceRx(), Qt::SkipEmptyParts);
    if (parts.size() != 2 || parts[0] != "0.0.0.0")
        return false;
    const QString d = parts[1].toLower();
    if (internal::normalizeDomain(d) != d)
        return false;
    if (domain)
        *domain = d;
    return true;
}

struct SplitResult
{
    QStringList kept;    ///< Mọi dòng KHÔNG thuộc khối của công cụ - giữ nguyên văn (kể cả '\r' cuối dòng nếu có)
    QStringList domains; ///< Tên miền đang nằm trong khối (chữ thường, không trùng)
    /// Chỉ số trong 'kept' của dòng CUỐI tệp khi nó không kết thúc bằng '\n' (tệp không có xuống dòng cuối);
    /// -1 nếu dòng đó không được giữ. Mọi dòng khác trong 'kept' đều từng có '\n' đứng sau.
    int unterminatedKept{-1};
    bool hasMarker{false}; ///< Có ít nhất một dòng marker START/END - tệp ĐÃ từng được công cụ ghi
};

SplitResult splitManagedBlock(const QString& content)
{
    using internal::kMarkerEnd;
    using internal::kMarkerStart;

    const QStringList lines = content.split('\n');
    SplitResult r;
    auto addDomain = [&r](const QString& d) {
        if (!r.domains.contains(d))
            r.domains << d;
    };

    for (int i = 0; i < lines.size(); ++i)
    {
        const QString trimmed = lines[i].trimmed();
        if (trimmed == kMarkerEnd)
        {
            r.hasMarker = true;
            continue; // marker END lạc (không có START đi trước) - bỏ riêng dòng marker
        }
        if (trimmed != kMarkerStart)
        {
            if (i == lines.size() - 1)
                r.unterminatedKept = r.kept.size();
            r.kept << lines[i];
            continue;
        }
        r.hasMarker = true;

        // Tìm marker END của khối này (dừng nếu gặp một START khác trước - khối trước coi như hỏng).
        int end = -1;
        for (int j = i + 1; j < lines.size(); ++j)
        {
            const QString t = lines[j].trimmed();
            if (t == kMarkerEnd) { end = j; break; }
            if (t == kMarkerStart) break;
        }

        if (end >= 0)
        {
            for (int k = i + 1; k < end; ++k)
            {
                const QStringList names = blockedNamesOnLine(lines[k]);
                if (!names.isEmpty())
                    for (const QString& n : names) addDomain(n);
                else if (!lines[k].trimmed().isEmpty())
                    r.kept << lines[k]; // dòng lạ người dùng tự thêm vào giữa khối - giữ lại, không xóa
            }
            i = end;
        }
        else
        {
            // Khối HỎNG (thiếu END) - xem ghi chú ở buildUpdatedHostsContent trong HostsBlocklist.h.
            int k = i + 1;
            QString d;
            while (k < lines.size() && isOwnFormatLine(lines[k], &d))
            {
                addDomain(d);
                ++k;
            }
            i = k - 1;
        }
    }
    return r;
}

/// Nội dung hosts đã giải mã + cách mã hóa lại để ghi ra ĐÚNG như cũ.
struct HostsText
{
    QString text;     ///< NGUYÊN VĂN, kể cả kiểu xuống dòng của từng dòng ("\r\n" hay "\n")
    bool utf8{true};  ///< false: tệp không phải UTF-8 hợp lệ - đọc/ghi theo Latin-1 để giữ nguyên từng byte
    bool bom{false};
    bool crlf{true};  ///< Kiểu xuống dòng CHIẾM ĐA SỐ trong tệp - chỉ dùng cho các dòng do công cụ tự ghi
    bool existed{false}; ///< Tệp có trên đĩa lúc đọc
};

bool readHosts(const QString& path, HostsText* out, QString* error)
{
    *out = {};
    QFile f(path);
    if (!f.exists())
        return true; // chưa có hosts file - coi như rỗng
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error) *error = "Không đọc được hosts file (" + f.errorString() + ").";
        return false;
    }
    out->existed = true;
    QByteArray bytes = f.readAll();
    f.close();

    static const QByteArray kBom = QByteArray::fromHex("efbbbf");
    if (bytes.startsWith(kBom))
    {
        out->bom = true;
        bytes.remove(0, kBom.size());
    }

    // Hosts file thường là ASCII/UTF-8, nhưng có máy lưu ghi chú theo codepage ANSI. Giải mã UTF-8 rồi
    // ghi lại sẽ biến các byte đó thành U+FFFD - đọc theo Latin-1 (1 byte <-> 1 ký tự, khứ hồi nguyên
    // vẹn) trong trường hợp này. Mọi thứ công cụ tự ghi thêm đều là ASCII nên an toàn với cả hai cách.
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString text = decoder(bytes);
    if (decoder.hasError())
    {
        text = QString::fromLatin1(bytes);
        out->utf8 = false;
    }
    // Tệp hosts do công cụ khác ghi (WSL, Docker, trình quản lý hosts...) có thể xuống dòng kiểu Unix, hoặc
    // LẪN LỘN hai kiểu. Văn bản được giữ NGUYÊN (không đổi "\r\n" thành "\n" rồi đổi ngược lại khi ghi như
    // trước - cách đó đưa cả tệp lẫn lộn về "\r\n", tức sửa những dòng không phải của mình): '\r' cuối dòng
    // đi theo từng dòng qua splitManagedBlock. Chỉ các dòng công cụ TỰ GHI mới theo kiểu chiếm đa số
    // (bằng nhau, hoặc tệp chưa có dòng nào: "\r\n" - chuẩn của hosts file trên Windows).
    const qsizetype crlfCount = text.count(QLatin1String("\r\n"));
    const qsizetype lfOnlyCount = text.count(QLatin1Char('\n')) - crlfCount;
    out->crlf = crlfCount >= lfOnlyCount;
    out->text = text;
    return true;
}

/// 'backupFirst': chép nguyên bản tệp hiện có ra tệp sao lưu trước khi ghi (nơi gọi quyết định - xem rewriteFile).
bool writeHosts(const QString& path, const HostsText& hosts, bool backupFirst, QString* error)
{
    QByteArray bytes = hosts.utf8 ? hosts.text.toUtf8() : hosts.text.toLatin1();
    if (hosts.bom)
        bytes.prepend(QByteArray::fromHex("efbbbf"));

    // Sao lưu MỘT lần trước lần ghi đầu tiên. Không chặn việc ghi nếu không tạo được bản sao lưu (thư mục
    // etc bị khóa tạo tệp mới trong khi hosts vẫn ghi được) - khi đó QSaveFile bên dưới cũng phải ghi
    // trực tiếp; đây là trường hợp hiếm, đã ghi nhận.
    const QString backup = internal::backupFilePath(path);
    if (backupFirst && QFile::exists(path) && !QFile::exists(backup))
        QFile::copy(path, backup);

    QSaveFile f(path);
    f.setDirectWriteFallback(true); // thư mục etc có thể không cho tạo tệp tạm cạnh hosts
    if (!f.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = WinElevation::isElevated()
                ? "Không ghi được hosts file (" + f.errorString() + ")."
                : "Không ghi được hosts file - cần quyền Administrator (hosts file là tệp hệ thống được bảo vệ).";
        return false;
    }
    if (f.write(bytes) != bytes.size())
    {
        if (error) *error = "Ghi hosts file thất bại (" + f.errorString() + ") - hosts file cũ được giữ nguyên.";
        f.cancelWriting();
        return false;
    }
    if (!f.commit())
    {
        if (error) *error = "Không hoàn tất được việc ghi hosts file (" + f.errorString() + ").";
        return false;
    }
    return true;
}
} // namespace

namespace internal
{

QString hostsFilePath()
{
    QString systemRoot = QProcessEnvironment::systemEnvironment().value("SystemRoot");
    if (systemRoot.isEmpty())
        systemRoot = "C:/Windows";
    return systemRoot + "/System32/drivers/etc/hosts";
}

QString backupFilePath(const QString& hostsPath)
{
    return hostsPath + ".oneforall.bak";
}

QString normalizeDomain(const QString& input, QString* error)
{
    auto fail = [error](const QString& message) {
        if (error) *error = message;
        return QString();
    };

    QString s = input.trimmed().toLower();
    if (s.isEmpty())
        return fail("Tên miền trống.");
    for (const QChar c : s)
    {
        if (c.isSpace() || c.category() == QChar::Other_Control)
            return fail("Mỗi lần chỉ nhập MỘT tên miền, không chứa khoảng trắng.");
    }

    // Người dùng hay dán nguyên URL - lấy đúng phần tên máy chủ.
    static const QRegularExpression schemeRx("^[a-z][a-z0-9+.\\-]*://");
    s.remove(schemeRx);
    static const QRegularExpression pathRx("[/?#\\\\]");
    const int cut = s.indexOf(pathRx);
    if (cut >= 0)
        s.truncate(cut);
    const int at = s.lastIndexOf('@');
    if (at >= 0)
        s = s.mid(at + 1);
    static const QRegularExpression portRx(":\\d{1,5}$");
    s.remove(portRx);
    while (s.endsWith('.'))
        s.chop(1);
    if (s.isEmpty())
        return fail("Không tìm thấy tên miền trong nội dung vừa nhập.");

    bool ascii = true;
    for (const QChar c : s)
        if (c.unicode() > 0x7F) { ascii = false; break; }
    if (!ascii)
    {
        // Tên miền có dấu (IDN): hosts file so khớp theo dạng punycode mà trình duyệt thật sự phân giải.
        s = QString::fromLatin1(QUrl::toAce(s));
        if (s.isEmpty())
            return fail("Tên miền không hợp lệ.");
    }

    if (s.size() > 253)
        return fail("Tên miền quá dài (tối đa 253 ký tự).");

    static const QRegularExpression labelRx("^[a-z0-9_]([a-z0-9_\\-]{0,61}[a-z0-9_])?$");
    static const QRegularExpression digitsRx("^[0-9]+$");
    bool allNumeric = true;
    for (const QString& label : s.split('.'))
    {
        if (!labelRx.match(label).hasMatch())
            return fail("Tên miền không hợp lệ - chỉ gồm chữ cái, chữ số, dấu gạch ngang và dấu chấm "
                        "(vd: vi-du-doc-hai.com).");
        if (!digitsRx.match(label).hasMatch())
            allNumeric = false;
    }
    if (allNumeric)
        return fail("Đây là địa chỉ IP, không phải tên miền - hosts file chỉ chặn được theo TÊN MIỀN.");
    if (s == "localhost" || s.endsWith(".localhost"))
        return fail("Không chặn \"localhost\" - nhiều phần mềm trên máy cần tên này để hoạt động.");
    return s;
}

QStringList parseManagedDomains(const QString& hostsContent)
{
    return splitManagedBlock(hostsContent).domains;
}

QString buildUpdatedHostsContent(const QString& existingContent, const QStringList& domains, const QString& eol)
{
    // Loại bỏ khối cũ (nếu có) - giữ nguyên MỌI dòng khác người dùng/chương trình khác đã có.
    const SplitResult split = splitManagedBlock(existingContent);
    QStringList kept = split.kept;

    // Bỏ các dòng trắng thừa ở cuối trước khi nối thêm khối mới, tránh nội dung phình to dần qua mỗi lần lưu.
    while (!kept.isEmpty() && kept.last().trimmed().isEmpty())
        kept.removeLast();

    // Mỗi dòng được giữ mang theo '\r' của chính nó (nếu có) nên nối bằng '\n' là trả lại ĐÚNG kiểu xuống
    // dòng của từng dòng. Riêng dòng cuối: giữ kiểu cũ của nó; nếu nó vốn KHÔNG có xuống dòng (cuối tệp) thì
    // phần xuống dòng thêm vào là của công cụ -> theo 'eol'. Dòng trắng ngăn cách cũng là của công cụ.
    QString result = kept.join('\n');
    if (!result.isEmpty())
    {
        const bool lastHadNoNewline = split.unterminatedKept == kept.size() - 1;
        result += (lastHadNoNewline && !result.endsWith(QLatin1Char('\r'))) ? eol : QStringLiteral("\n");
        result += eol;
    }

    // Mỗi tên miền MỘT dòng; bỏ qua mục rỗng/chứa khoảng trắng/ghi chú - không để một mục hỏng chèn thêm
    // dòng hay tên khác vào hosts file.
    QStringList lines;
    for (const QString& raw : domains)
    {
        const QString d = raw.trimmed().toLower();
        if (d.isEmpty() || d.contains(whitespaceRx()) || d.contains('#') || lines.contains(d))
            continue;
        lines << d;
    }

    if (!lines.isEmpty())
    {
        result += kMarkerStart + eol;
        for (const QString& d : lines)
            result += QStringLiteral("0.0.0.0 %1").arg(d) + eol;
        result += kMarkerEnd + eol;
    }
    return result;
}

QStringList listBlockedDomainsInFile(const QString& hostsPath, QString* error)
{
    HostsText hosts;
    if (!readHosts(hostsPath, &hosts, error))
        return {};
    return parseManagedDomains(hosts.text);
}

namespace
{
/// Đọc MỘT lần, đổi danh sách bằng `mutate`, ghi lại nếu nội dung thật sự thay đổi - dùng chung cho add/remove.
template <typename Mutate>
bool rewriteFile(const QString& hostsPath, Mutate mutate, QString* error)
{
    HostsText hosts;
    if (!readHosts(hostsPath, &hosts, error))
        return false;

    const QStringList current = parseManagedDomains(hosts.text);
    QStringList domains = current;
    mutate(domains);
    // Danh sách không đổi (thêm tên đã có/gỡ tên không có) thì không ghi gì: so cả nội dung dựng lại như bên
    // dưới là chưa đủ - bước dựng lại chuẩn hóa dòng trắng cuối tệp, nên gỡ một tên KHÔNG có trong danh sách
    // trên một hosts file chưa từng có khối của công cụ vẫn ghi đè tệp (và tạo luôn bản sao lưu).
    if (domains == current)
        return true;

    const QString updated =
        buildUpdatedHostsContent(hosts.text, domains, hosts.crlf ? QStringLiteral("\r\n") : QStringLiteral("\n"));
    if (updated == hosts.text)
        return true; // không có gì đổi - không đụng vào hosts file

    // Bản sao lưu phải là nội dung có TRƯỚC khi công cụ đụng tới. Tệp đã mang marker của công cụ thì không
    // còn là nội dung đó: khi hosts CHƯA tồn tại ở lần ghi đầu (không có gì để sao lưu), lần ghi thứ hai từng
    // "sao lưu" chính tệp do công cụ vừa tạo - một bản .bak chỉ chứa khối chặn, khôi phục nó là vô nghĩa.
    // Tệp RỖNG (0 byte - vd tệp do công cụ tạo, sau khi gỡ hết tên miền) cũng không có gì để sao lưu.
    const bool emptyFile = hosts.text.isEmpty() && !hosts.bom;
    const bool backupFirst = hosts.existed && !emptyFile && !splitManagedBlock(hosts.text).hasMarker;
    hosts.text = updated;
    return writeHosts(hostsPath, hosts, backupFirst, error);
}
} // namespace

bool addDomainToFile(const QString& hostsPath, const QString& domain, QString* error)
{
    const QString d = normalizeDomain(domain, error);
    if (d.isEmpty())
        return false;
    return rewriteFile(hostsPath, [&d](QStringList& domains) {
        if (!domains.contains(d))
            domains << d;
    }, error);
}

bool removeDomainFromFile(const QString& hostsPath, const QString& domain, QString* error)
{
    // So theo đúng chuỗi đang hiển thị (không ép chuẩn hóa) - để gỡ được cả mục cũ không hợp lệ do bản
    // trước ghi vào (vd "http://x.com/a").
    const QString raw = domain.trimmed().toLower();
    const QString normalized = normalizeDomain(domain);
    return rewriteFile(hostsPath, [&](QStringList& domains) {
        if (domains.removeAll(raw) == 0 && !normalized.isEmpty())
            domains.removeAll(normalized);
    }, error);
}

} // namespace internal

QStringList listBlockedDomains(QString* error)
{
    return internal::listBlockedDomainsInFile(internal::hostsFilePath(), error);
}

bool addDomain(const QString& domain, QString* error)
{
    return internal::addDomainToFile(internal::hostsFilePath(), domain, error);
}

bool removeDomain(const QString& domain, QString* error)
{
    return internal::removeDomainFromFile(internal::hostsFilePath(), domain, error);
}

} // namespace HostsBlocklist
