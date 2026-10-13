#include "DownloaderSettings.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>

DownloaderSettings::DownloaderSettings(QObject* parent)
    : QObject(parent)
    , m_filePath(AppPaths::profileFile("downloader_settings.json"))
{
    m_folder = defaultDownloadFolder();
    load();
}

DownloaderSettings::DownloaderSettings(const QString& filePath, QObject* parent)
    : QObject(parent)
    , m_filePath(filePath)
{
    m_folder = defaultDownloadFolder();
    load();
}

QString DownloaderSettings::defaultDownloadFolder()
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return d.isEmpty() ? QDir::homePath() : d;
}

QStringList DownloaderSettings::supportedCookieBrowsers()
{
    return {"firefox", "edge", "chrome", "brave", "opera", "vivaldi", "chromium"};
}

void DownloaderSettings::setDownloadFolder(const QString& folder)
{
    const QString f = QDir::cleanPath(folder.trimmed());
    const QString next = f.isEmpty() || f == "." ? defaultDownloadFolder() : f;
    if (next == m_folder)
        return;
    m_folder = next;
    save();
    emit changed();
}

void DownloaderSettings::setMaxConcurrent(int n)
{
    n = qBound(1, n, 8);
    if (n == m_maxConcurrent)
        return;
    m_maxConcurrent = n;
    save();
    emit changed();
}

void DownloaderSettings::setProfileMaxItems(int n)
{
    n = qBound(1, n, 1000);
    if (n == m_profileMaxItems)
        return;
    m_profileMaxItems = n;
    save();
    emit changed();
}

void DownloaderSettings::setUseBrowserFallback(bool on)
{
    if (on == m_useBrowser)
        return;
    m_useBrowser = on;
    save();
    emit changed();
}

void DownloaderSettings::setCookies(CookieMode mode, const QString& browser, const QString& file)
{
    const QString b = supportedCookieBrowsers().contains(browser) ? browser : QStringLiteral("firefox");
    const QString f = file.trimmed();
    if (mode == m_cookieMode && b == m_cookieBrowser && f == m_cookieFile)
        return;
    m_cookieMode = mode;
    m_cookieBrowser = b;
    m_cookieFile = f;
    save();
    emit changed();
}

QStringList DownloaderSettings::ytDlpExtraArguments() const
{
    switch (m_cookieMode)
    {
        case CookieMode::Browser:
            return {"--cookies-from-browser", m_cookieBrowser};
        case CookieMode::File:
            if (!m_cookieFile.isEmpty() && QFileInfo::exists(m_cookieFile))
                return {"--cookies", m_cookieFile};
            return {};
        case CookieMode::None:
            break;
    }
    return {};
}

QString DownloaderSettings::httpCookieFile() const
{
    return (m_cookieMode == CookieMode::File && !m_cookieFile.isEmpty() && QFileInfo::exists(m_cookieFile)) ? m_cookieFile : QString();
}

QJsonObject DownloaderSettings::toJson() const
{
    QJsonObject o;
    o["version"] = 1;
    o["downloadFolder"] = m_folder;
    o["maxConcurrent"] = m_maxConcurrent;
    o["profileMaxItems"] = m_profileMaxItems;
    o["useBrowserFallback"] = m_useBrowser;
    o["cookieMode"] = m_cookieMode == CookieMode::Browser ? "browser" : m_cookieMode == CookieMode::File ? "file" : "none";
    o["cookieBrowser"] = m_cookieBrowser;
    o["cookieFile"] = m_cookieFile;
    return o;
}

void DownloaderSettings::fromJson(const QJsonObject& o)
{
    // Giá trị lạ/thiếu -> giữ mặc định; luôn kẹp vào phạm vi hợp lệ (tệp có thể bị sửa tay hoặc hỏng).
    const QString folder = o.value("downloadFolder").toString().trimmed();
    m_folder = folder.isEmpty() ? defaultDownloadFolder() : QDir::cleanPath(folder);
    m_maxConcurrent = qBound(1, o.value("maxConcurrent").toInt(3), 8);
    m_profileMaxItems = qBound(1, o.value("profileMaxItems").toInt(100), 1000);
    m_useBrowser = o.value("useBrowserFallback").toBool(true);
    const QString mode = o.value("cookieMode").toString();
    m_cookieMode = mode == "browser" ? CookieMode::Browser : mode == "file" ? CookieMode::File : CookieMode::None;
    const QString b = o.value("cookieBrowser").toString("firefox");
    m_cookieBrowser = supportedCookieBrowsers().contains(b) ? b : QStringLiteral("firefox");
    m_cookieFile = o.value("cookieFile").toString().trimmed();
}

void DownloaderSettings::load()
{
    QFile f(m_filePath);
    if (!f.open(QIODevice::ReadOnly))
        return; // chưa có tệp - dùng mặc định
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isObject())
        fromJson(doc.object());
}

void DownloaderSettings::save() const
{
    QSaveFile f(m_filePath);
    if (!f.open(QIODevice::WriteOnly))
    {
        Logger::instance().warning("Downloader", "Không lưu được cài đặt: " + m_filePath);
        return;
    }
    f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit())
        Logger::instance().warning("Downloader", "Không ghi được cài đặt: " + m_filePath);
}
