#include "UpdateChecker.h"
#include "core/Version.h"

#include "core/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace
{
// Trễ vài giây sau khi UpdateChecker được tạo (MainWindow khởi tạo nó ngay lúc mở) - để request mạng
// không cạnh tranh với việc dựng giao diện/nạp dữ liệu các tool lúc ứng dụng vừa mở.
constexpr int kStartupCheckDelayMs = 4000;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);

    // Theo đúng yêu cầu: chỉ hỏi MỘT LẦN mỗi lần mở ứng dụng (không lặp lại định kỳ trong lúc đang chạy).
    QTimer::singleShot(kStartupCheckDelayMs, this, &UpdateChecker::checkNow);
}

void UpdateChecker::checkNow()
{
    const QUrl url(QString("https://api.github.com/repos/%1/releases/latest").arg(APP_GITHUB_REPO));
    QNetworkRequest req(url);
    // API GitHub từ chối request không có User-Agent hợp lệ - bắt buộc phải đặt.
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("OneForAll/%1").arg(APP_VERSION));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setTransferTimeout(15000); // không để một kết nối treo giữ QNetworkReply sống suốt phiên chạy

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
}

void UpdateChecker::onReplyFinished(QNetworkReply* reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        const QString err = reply->errorString();
        Logger::instance().warning("Update", "Kiểm tra cập nhật thất bại: " + err);
        emit checkFailed(err);
        return;
    }

    const UpdateInfo info = UpdateCheckerInternal::parseLatestRelease(reply->readAll());
    if (info.version.isEmpty())
    {
        Logger::instance().warning("Update", "Không đọc được thông tin bản phát hành từ GitHub (JSON không đúng cấu trúc mong đợi hoặc chưa có bản phát hành nào đính kèm trình cài đặt).");
        emit checkFailed("Không đọc được thông tin phiên bản từ GitHub.");
        return;
    }

    if (UpdateCheckerInternal::compareVersions(info.version, APP_VERSION) > 0)
    {
        Logger::instance().info("Update", "Có bản cập nhật mới: v" + info.version + " (đang chạy v" + APP_VERSION + ")");
        emit updateAvailable(info);
    }
    else
    {
        emit upToDate();
    }
}

namespace UpdateCheckerInternal
{

namespace
{
// Lấy dãy CHỮ SỐ Ở ĐẦU chuỗi, bỏ qua phần còn lại - khác QString::toInt() (yêu cầu CẢ chuỗi phải là số,
// trả về 0 nếu không) - để xử lý đúng tag có hậu tố kiểu semver (vd "v1.16.5-hotfix" -> phần patch
// "5-hotfix" vẫn đọc ra đúng 5, không bị coi là "không parse được" rồi rơi về 0 một cách sai lệch, có
// thể khiến so sánh phiên bản sai và bỏ lỡ bản cập nhật thật). Quy trình phát hành của dự án (mục 5c
// PROJECT_OVERVIEW.md) chỉ tạo tag "vX.Y.Z" thuần, nhưng hàm này được gọi trên dữ liệu từ GitHub API -
// không coi là đáng tin cậy tuyệt đối, xử lý khoan dung hơn cho chắc.
int toIntOrZero(const QString& s)
{
    int i = 0;
    while (i < s.size() && s[i].isDigit())
        ++i;
    if (i == 0)
        return 0;
    bool ok = false;
    const int v = s.left(i).toInt(&ok);
    return ok ? v : 0;
}

QString stripLeadingV(QString s)
{
    s = s.trimmed();
    if (s.startsWith('v', Qt::CaseInsensitive))
        s.remove(0, 1);
    return s;
}
} // namespace

int compareVersions(const QString& aRaw, const QString& bRaw)
{
    const QStringList a = stripLeadingV(aRaw).split('.');
    const QStringList b = stripLeadingV(bRaw).split('.');
    for (int i = 0; i < 3; ++i)
    {
        const int av = i < a.size() ? toIntOrZero(a[i]) : 0;
        const int bv = i < b.size() ? toIntOrZero(b[i]) : 0;
        if (av != bv)
            return av < bv ? -1 : 1;
    }
    return 0;
}

UpdateInfo parseLatestRelease(const QByteArray& json)
{
    UpdateInfo info;

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return info;

    const QJsonObject root = doc.object();
    const QString tag = root.value("tag_name").toString();
    if (tag.isEmpty())
        return info;

    // "digest" (GitHub thêm từ 2025) có dạng "sha256:<hex>" - trả hex thường, rỗng nếu thiếu/khác thuật toán.
    const auto sha256Of = [](const QJsonObject& asset) -> QString {
        const QString digest = asset.value("digest").toString().trimmed();
        if (!digest.startsWith(QStringLiteral("sha256:"), Qt::CaseInsensitive))
            return QString();
        const QString hex = digest.mid(7).trimmed().toLower();
        static const QRegularExpression hex64(QStringLiteral("^[0-9a-f]{64}$"));
        return hex64.match(hex).hasMatch() ? hex : QString();
    };

    QString downloadUrl, exeSha, msiUrl, msiSha;
    qint64 size = 0, msiSize = 0;
    const QJsonArray assets = root.value("assets").toArray();
    for (const QJsonValue& v : assets)
    {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value("name").toString();
        const QString url = asset.value("browser_download_url").toString();
        // So KHỚP ĐÚNG TUYỆT ĐỐI tên file (biết trước chính xác - OutputBaseFilename=OneForAll_Setup
        // trong installer/OneForAll.iss luôn sinh ra đúng tên này) - KHÔNG dùng endsWith("Setup.exe")
        // lỏng lẻo như trước (có thể khớp nhầm "MyApp_Setup.exe" của một asset khác lỡ đính kèm cùng
        // release). Lấy asset ĐẦU TIÊN khớp cho mỗi loại.
        if (downloadUrl.isEmpty() && name.compare(QStringLiteral("OneForAll_Setup.exe"), Qt::CaseInsensitive) == 0)
        {
            downloadUrl = url;
            size = static_cast<qint64>(asset.value("size").toDouble());
            exeSha = sha256Of(asset);
        }
        else if (msiUrl.isEmpty() && name.compare(QStringLiteral("OneForAll_Setup.msi"), Qt::CaseInsensitive) == 0)
        {
            msiUrl = url;
            msiSize = static_cast<qint64>(asset.value("size").toDouble());
            msiSha = sha256Of(asset);
        }
    }
    if (downloadUrl.isEmpty())
        return info; // bản phát hành không đính kèm trình cài đặt - không có gì để tự cập nhật

    info.version = stripLeadingV(tag);
    info.downloadUrl = downloadUrl;
    info.downloadSize = size;
    info.sha256 = exeSha;
    info.msiUrl = msiUrl;
    info.msiSize = msiSize;
    info.msiSha256 = msiSha;
    info.releaseNotes = root.value("body").toString();
    return info;
}

bool isTrustedDownloadUrl(const QString& url)
{
    const QUrl u(url, QUrl::StrictMode);
    return u.isValid() && u.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0 &&
           u.host().compare(QStringLiteral("github.com"), Qt::CaseInsensitive) == 0 && u.userInfo().isEmpty();
}

} // namespace UpdateCheckerInternal
