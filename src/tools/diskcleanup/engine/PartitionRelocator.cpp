#include "PartitionRelocator.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

namespace PartitionRelocator
{
namespace
{
const QString kDirPrefix = QStringLiteral("OneForAll_diskmove_");

bool excludedName(const QString& name)
{
    static const QSet<QString> skip = {QStringLiteral("yt-dlp"),        QStringLiteral("scrcpy"),         QStringLiteral("wintun"),
                                       QStringLiteral("profiles"),      QStringLiteral("opengl32sw.dll"), QStringLiteral("d3dcompiler_47.dll")};
    return skip.contains(name.toLower());
}

bool copyDirRecursive(const QString& src, const QString& dst, QString* error, qint64* bytes)
{
    if (!QDir().mkpath(dst))
    {
        if (error)
            *error = QStringLiteral("Không tạo được thư mục %1").arg(QDir::toNativeSeparators(dst));
        return false;
    }
    const QDir sd(src);
    for (const QFileInfo& fi : sd.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
    {
        const QString target = dst + "/" + fi.fileName();
        if (fi.isDir())
        {
            if (!copyDirRecursive(fi.absoluteFilePath(), target, error, bytes))
                return false;
            continue;
        }
        if (!QFile::copy(fi.absoluteFilePath(), target))
        {
            if (error)
                *error = QStringLiteral("Không chép được %1").arg(QDir::toNativeSeparators(fi.absoluteFilePath()));
            return false;
        }
        if (bytes)
            *bytes += fi.size();
    }
    return true;
}
} // namespace

QByteArray toJson(const Plan& p)
{
    QJsonObject o;
    o["v"] = 1;
    o["resume"] = p.resume;
    o["disk"] = p.disk;
    o["partition"] = p.partition;
    o["letter"] = p.letter;
    o["originalExe"] = p.originalExe;
    o["description"] = p.description;
    QJsonArray steps;
    for (const auto& s : p.steps)
    {
        QJsonObject so;
        so["kind"] = static_cast<int>(s.kind);
        so["start"] = QString::number(s.after.start);
        so["size"] = QString::number(s.after.size);
        steps.append(so);
    }
    o["steps"] = steps;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

bool fromJson(const QByteArray& json, Plan* out, QString* error)
{
    auto fail = [&](const QString& why) {
        if (error)
            *error = why;
        return false;
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("Kế hoạch không phải JSON hợp lệ."));
    const QJsonObject o = doc.object();
    if (o.value("v").toInt() != 1)
        return fail(QStringLiteral("Phiên bản kế hoạch không được hỗ trợ."));
    Plan p;
    p.resume = o.value("resume").toBool(false);
    p.disk = o.value("disk").toInt(-1);
    p.partition = o.value("partition").toInt(-1);
    p.letter = o.value("letter").toString();
    p.originalExe = o.value("originalExe").toString();
    p.description = o.value("description").toString();
    if (p.disk < 0 || p.disk > 1024 || p.partition < 0 || p.partition > 4096)
        return fail(QStringLiteral("Số đĩa/phân vùng không hợp lệ."));
    if (!p.letter.isEmpty() && (p.letter.size() != 1 || !p.letter[0].isLetter()))
        return fail(QStringLiteral("Chữ ổ không hợp lệ."));
    const QJsonArray steps = o.value("steps").toArray();
    if (steps.size() > 4)
        return fail(QStringLiteral("Quá nhiều bước."));
    for (const QJsonValue& v : steps)
    {
        const QJsonObject so = v.toObject();
        const int kind = so.value("kind").toInt(-1);
        bool okStart = false, okSize = false;
        const qint64 start = so.value("start").toString().toLongLong(&okStart);
        const qint64 size = so.value("size").toString().toLongLong(&okSize);
        if (kind < 0 || kind > 2 || !okStart || !okSize || start < 0 || size <= 0)
            return fail(QStringLiteral("Bước trong kế hoạch không hợp lệ."));
        p.steps.append(DiskLayout::PlanStep{static_cast<DiskLayout::StepKind>(kind), DiskLayout::Geometry{start, size}});
    }
    if (!p.resume && p.steps.isEmpty())
        return fail(QStringLiteral("Kế hoạch không có bước nào."));
    *out = p;
    return true;
}

QString encodeArgument(const Plan& plan)
{
    return QString::fromLatin1(toJson(plan).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool decodeArgument(const QString& argument, Plan* out, QString* error)
{
    const QByteArray raw = QByteArray::fromBase64(argument.toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
    if (raw.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Tham số kế hoạch không giải mã được.");
        return false;
    }
    return fromJson(raw, out, error);
}

bool copyRuntime(const QString& srcDir, const QString& dstDir, QString* error, qint64* bytesCopied)
{
    qint64 bytes = 0;
    const QDir sd(srcDir);
    if (!sd.exists())
    {
        if (error)
            *error = QStringLiteral("Không thấy thư mục ứng dụng %1").arg(QDir::toNativeSeparators(srcDir));
        return false;
    }
    if (!QDir().mkpath(dstDir))
    {
        if (error)
            *error = QStringLiteral("Không tạo được thư mục tạm %1").arg(QDir::toNativeSeparators(dstDir));
        return false;
    }
    bool ok = true;
    for (const QFileInfo& fi : sd.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
    {
        if (excludedName(fi.fileName()))
            continue;
        const QString target = dstDir + "/" + fi.fileName();
        if (fi.isDir())
            ok = copyDirRecursive(fi.absoluteFilePath(), target, error, &bytes);
        else
        {
            ok = QFile::copy(fi.absoluteFilePath(), target);
            if (ok)
                bytes += fi.size();
            else if (error)
                *error = QStringLiteral("Không chép được %1").arg(QDir::toNativeSeparators(fi.absoluteFilePath()));
        }
        if (!ok)
            break;
    }
    if (!ok)
    {
        QDir(dstDir).removeRecursively();
        return false;
    }
    if (bytesCopied)
        *bytesCopied = bytes;
    return true;
}

QString newRuntimeDir()
{
    return QDir::cleanPath(QDir::tempPath() + "/" + kDirPrefix + QDateTime::currentDateTime().toString("yyyyMMddhhmmss") + "_" +
                           QString::number(QRandomGenerator::global()->bounded(100000, 999999)));
}

bool isRuntimeDir(const QString& path)
{
    const QFileInfo fi(path);
    const QString parent = QDir::cleanPath(fi.absolutePath());
    const QString temp = QDir::cleanPath(QDir(QDir::tempPath()).absolutePath());
    return !path.isEmpty() && parent.compare(temp, Qt::CaseInsensitive) == 0 && fi.fileName().startsWith(kDirPrefix) && fi.fileName().size() > kDirPrefix.size();
}

void cleanupStaleRuntimes()
{
    const QDir temp(QDir::tempPath());
    const QString mine = QDir::cleanPath(QCoreApplication::applicationDirPath());
    for (const QFileInfo& fi : temp.entryInfoList({kDirPrefix + "*"}, QDir::Dirs | QDir::NoDotAndDotDot))
    {
        if (!isRuntimeDir(fi.absoluteFilePath()) || QDir::cleanPath(fi.absoluteFilePath()).compare(mine, Qt::CaseInsensitive) == 0)
            continue;
        if (fi.lastModified().secsTo(QDateTime::currentDateTime()) < 600)
            continue; // mới tạo: có thể đang được dùng
        QDir(fi.absoluteFilePath()).removeRecursively(); // đang được dùng (tệp bị khóa) thì xóa không hết - bỏ qua, lần sau thử lại
    }
}

bool launchElevatedRunner(const QString& runnerExe, const QString& runtimeDir, const Plan& plan, QString* error)
{
    const std::wstring exe = QDir::toNativeSeparators(runnerExe).toStdWString();
    const std::wstring dir = QDir::toNativeSeparators(runtimeDir).toStdWString();
    const QString args = QStringLiteral("--run-partition-plan --plan-b64 %1 --wait-pid %2 --cleanup-dir \"%3\"")
                             .arg(encodeArgument(plan))
                             .arg(GetCurrentProcessId())
                             .arg(QDir::toNativeSeparators(runtimeDir));
    const std::wstring params = args.toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas"; // UAC chuẩn của Windows - người dùng tự xác nhận/từ chối
    info.lpFile = exe.c_str();
    info.lpParameters = params.c_str();
    info.lpDirectory = dir.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
    {
        const DWORD err = GetLastError();
        if (error)
            *error = err == ERROR_CANCELLED ? QStringLiteral("Bạn đã từ chối cấp quyền Administrator (hộp thoại UAC) nên không bắt đầu được.")
                                            : QStringLiteral("Không khởi chạy được bản sao tạm của ứng dụng (mã lỗi %1).").arg(err);
        return false;
    }
    if (info.hProcess)
        CloseHandle(info.hProcess);
    return true;
}

bool processHasExited(quint32 pid, int timeoutMs)
{
    if (pid == 0)
        return true;
    const HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return true; // không mở được = không còn (hoặc không có quyền - coi như đã thoát)
    const DWORD r = WaitForSingleObject(h, static_cast<DWORD>(std::max(0, timeoutMs)));
    CloseHandle(h);
    return r == WAIT_OBJECT_0;
}
} // namespace PartitionRelocator
