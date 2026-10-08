#include "UpdateChecker.h"
#include "core/Version.h"

#include "core/Logger.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
int toIntOrZero(const QString& s)
{
    bool ok = false;
    const int v = s.toInt(&ok);
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

    QString downloadUrl;
    qint64 size = 0;
    const QJsonArray assets = root.value("assets").toArray();
    for (const QJsonValue& v : assets)
    {
        const QJsonObject asset = v.toObject();
        const QString name = asset.value("name").toString();
        if (name.endsWith("Setup.exe", Qt::CaseInsensitive))
        {
            downloadUrl = asset.value("browser_download_url").toString();
            size = static_cast<qint64>(asset.value("size").toDouble());
            break;
        }
    }
    if (downloadUrl.isEmpty())
        return info; // bản phát hành không đính kèm trình cài đặt - không có gì để tự cập nhật

    info.version = stripLeadingV(tag);
    info.downloadUrl = downloadUrl;
    info.downloadSize = size;
    info.releaseNotes = root.value("body").toString();
    return info;
}

} // namespace UpdateCheckerInternal
