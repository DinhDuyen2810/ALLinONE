#include "DriveViewerCapture.h"

#include "CdpClient.h"
#include "FileDownloader.h"
#include "HeadlessBrowser.h"
#include "ImagePdf.h"
#include "core/WinProcessTree.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>
#include <map>
#include <vector>

namespace
{
constexpr qint64 kMaxTotalJpegBytes = 1024LL * 1024 * 1024; // trần RAM cho toàn bộ ảnh trang trước khi ghi PDF
constexpr int kStepTimeoutMs = 60000;

/// Một phiên trình duyệt: khởi chạy Edge/Chrome ẩn với hồ sơ tạm; hủy -> dọn CẢ CÂY tiến trình rồi xóa hồ sơ.
class BrowserSession
{
public:
    ~BrowserSession() { stop(); }

    bool start(const QString& exe, const QStringList& args, QString* error)
    {
        m_process.setProgram(exe);
        m_process.setArguments(args);
        // Edge ghi rất nhiều cảnh báo ra stderr - đổ vào NUL để pipe không bao giờ đầy làm treo tiến trình.
        m_process.setStandardOutputFile(QProcess::nullDevice());
        m_process.setStandardErrorFile(QProcess::nullDevice());
        m_process.start();
        if (!m_process.waitForStarted(15000))
        {
            *error = "Không khởi chạy được trình duyệt (" + m_process.errorString() + ").";
            return false;
        }
        m_pid = m_process.processId();
        return true;
    }

    bool alive() const { return m_process.state() != QProcess::NotRunning; }

    void stop()
    {
        if (m_process.state() != QProcess::NotRunning)
        {
            // Quét hậu duệ TRƯỚC khi dừng tiến trình cha (xem WinProcessTree) - Edge sinh renderer/GPU/network service.
            const std::vector<qint64> descendants = m_pid > 0 ? WinProcessTree::findDescendants(m_pid) : std::vector<qint64>{};
            m_process.kill();
            WinProcessTree::terminateProcessList(descendants);
            m_process.waitForFinished(5000);
        }
    }

    QString profilePath() const { return m_profile.path(); }
    bool profileValid() const { return m_profile.isValid(); }

    /// Xóa hồ sơ tạm SAU khi tiến trình đã tắt; Edge còn giữ khóa tệp một lúc nên thử lại.
    void removeProfile()
    {
        stop();
        for (int i = 0; i < 10 && QDir(m_profile.path()).exists(); ++i)
        {
            if (QDir(m_profile.path()).removeRecursively())
                break;
            QThread::msleep(300);
        }
    }

private:
    QTemporaryDir m_profile;
    QProcess m_process;
    qint64 m_pid{0};
};

quint16 freeLocalPort()
{
    QTcpServer s;
    return s.listen(QHostAddress::LocalHost, 0) ? s.serverPort() : 0;
}
} // namespace

// ------------------------------------------------------------------------------------------------ Thuần
namespace DriveViewerCaptureInternal
{
bool isAllowedUrl(const QString& url, bool allowAny, QString* fileId)
{
    if (fileId)
        fileId->clear();
    if (!FileDownloaderInternal::isHttpUrl(url))
        return false;
    const QUrl u(url);
    static const QRegularExpression rx(QStringLiteral("^/file/d/([A-Za-z0-9_-]{10,})/view/?$"));
    const auto m = rx.match(u.path());
    const bool isDrive = u.scheme() == QStringLiteral("https") && u.host().toLower() == QStringLiteral("drive.google.com") && m.hasMatch();
    if (isDrive && fileId)
        *fileId = m.captured(1);
    return isDrive || allowAny;
}

QStringList buildArguments(quint16 port, const QString& profileDir, const QString& url, bool showWindow, double scale)
{
    QStringList a = {
        QString("--remote-debugging-port=%1").arg(port),
        "--remote-allow-origins=*",
        "--user-data-dir=" + QDir::toNativeSeparators(profileDir),
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-extensions",
        "--window-size=1400,1000",
        QString("--force-device-scale-factor=%1").arg(QString::number(scale, 'f', 1)),
    };
    if (!showWindow)
        a << "--headless=new";
    a << url;
    return a;
}

QString injectedScript()
{
    // Lõi "lấy trang": nhận trang qua thẻ h2 "Trang n / N" (không dựa vào tên class bị làm rối), gom ảnh blob: của khung trang
    // chứa nó, vẽ lên canvas ở kích thước tự nhiên rồi xuất JPEG. Chụp NGAY khi ảnh nạp xong vì viewer có thể gỡ ảnh của trang
    // đã cuộn qua.
    return QString::fromUtf8(R"JS((function () {
  if (window.__dx) return;
  const sleep = ms => new Promise(r => setTimeout(r, ms));
  const rx = /(\d+)\s*(?:\/|of)\s*(\d+)/i;
  const done = new Set();
  let sc = null, total = 0;
  const heads = () => [...document.querySelectorAll('h2')].filter(h => rx.test(h.textContent));
  const scroller = () => {
    if (sc && sc.isConnected) return sc;
    const first = heads()[0];
    if (!first) return null;
    let e = first.parentElement;
    while (e && !(e.scrollHeight > e.clientHeight + 5 && /auto|scroll/.test(getComputedStyle(e).overflowY))) e = e.parentElement;
    return (sc = e || document.scrollingElement);
  };
  window.__dx = {
    status() {
      const hs = heads();
      if (hs.length) total = +rx.exec(hs[0].textContent)[2];
      return { found: hs.length, total, got: done.size, title: document.title, url: location.href };
    },
    top() { const s = scroller(); if (s) s.scrollTop = 0; },
    async step() {
      const pages = [];
      for (const h of heads()) {
        const m = rx.exec(h.textContent), n = +m[1];
        total = +m[2];
        if (done.has(n)) continue;
        const box = h.closest('[style*="padding-bottom"]');
        const img = box && box.querySelector('img[src^="blob:"]');
        if (!img || !img.complete || !img.naturalWidth) continue;
        const c = document.createElement('canvas');
        c.width = img.naturalWidth; c.height = img.naturalHeight;
        const g = c.getContext('2d');
        g.fillStyle = '#fff'; g.fillRect(0, 0, c.width, c.height);
        g.drawImage(img, 0, 0);
        pages.push({ n, w: c.width, h: c.height, b64: c.toDataURL('image/jpeg', 0.92).split(',')[1] });
        done.add(n);
      }
      const s = scroller();
      let atBottom = true;
      if (s) { const before = s.scrollTop; s.scrollTop += s.clientHeight * 0.6; await sleep(500); atBottom = s.scrollTop === before; }
      return { pages, atBottom, got: done.size, total };
    }
  };
}))JS");
}
} // namespace DriveViewerCaptureInternal

// ------------------------------------------------------------------------------------------------ Luồng
DriveViewerCapture::DriveViewerCapture(QObject* parent)
    : QThread(parent)
{
}

DriveViewerCapture::~DriveViewerCapture()
{
    requestCancel();
    wait(15000);
}

void DriveViewerCapture::setParams(const Params& params)
{
    m_params = params;
    m_cancel = false;
    m_canceledByUser = false;
}

void DriveViewerCapture::run()
{
    QString path, message, error;
    const bool ok = runCapture(&path, &message, &error);
    if (!ok && m_cancel)
        m_canceledByUser = true;
    if (ok)
        emit captureFinished(true, path, message);
    else
        emit captureFinished(false, QString(), m_cancel ? QStringLiteral("Đã hủy.") : error);
}

bool DriveViewerCapture::runCapture(QString* outPath, QString* warning, QString* error)
{
    using Status = CdpClient::Status;
    auto cancelled = [this]() { return m_cancel.load(); };
    auto failWith = [&](const QString& text) {
        *error = text;
        return false;
    };

    if (m_params.outputPath.isEmpty())
        return failWith("Thiếu đường dẫn lưu PDF.");
    if (!DriveViewerCaptureInternal::isAllowedUrl(m_params.url, m_params.allowAnyHttpUrl))
        return failWith("Địa chỉ không phải trang xem Google Drive (https://drive.google.com/file/d/<ID>/view).");
    const QString exe = HeadlessBrowser::findBrowserExecutable();
    if (exe.isEmpty())
        return failWith("Không tìm thấy Microsoft Edge hoặc Google Chrome trên máy.");
    const quint16 port = freeLocalPort();
    if (port == 0)
        return failWith("Không xin được cổng cục bộ cho trình duyệt.");

    BrowserSession session;
    if (!session.profileValid())
        return failWith("Không tạo được thư mục tạm cho trình duyệt.");
    struct Cleanup
    {
        BrowserSession& s;
        ~Cleanup() { s.removeProfile(); }
    } cleanup{session};

    QString startError;
    if (!session.start(exe, DriveViewerCaptureInternal::buildArguments(port, session.profilePath(), m_params.url,
                                                                       m_params.showWindow, m_params.scale), &startError))
        return failWith(startError);

    QElapsedTimer clock;
    clock.start();
    const qint64 limit = m_params.timeoutMs;
    auto timedOut = [&]() { return clock.elapsed() > limit; };

    CdpClient cdp(&m_cancel);

    // 1. Chờ DevTools lên rồi nối vào tab của trang xem.
    QString wsPath, lastProblem = "chưa có phản hồi";
    while (wsPath.isEmpty())
    {
        if (cancelled())
            return failWith("Đã hủy.");
        if (timedOut() || !session.alive())
            return failWith((session.alive() ? QString("Trình duyệt không khởi động kịp") : QString("Trình duyệt bị dừng đột ngột khi khởi động")) +
                            " (" + lastProblem + ").");
        QByteArray body;
        const Status listStatus = cdp.httpGetLocal(port, "/json/list", 2000, &body);
        if (listStatus != Status::Ok)
            lastProblem = cdp.error();
        else
        {
            lastProblem = "chưa có tab trang nào";
            for (const QJsonValue& v : QJsonDocument::fromJson(body).array())
            {
                const QJsonObject t = v.toObject();
                if (t.value("type").toString() == "page" && t.value("url").toString().startsWith("http"))
                {
                    wsPath = QUrl(t.value("webSocketDebuggerUrl").toString()).path();
                    break;
                }
            }
        }
        if (wsPath.isEmpty())
            QThread::msleep(300);
    }
    if (cdp.connectTo(port, wsPath, 15000) != Status::Ok)
        return failWith(cancelled() ? QStringLiteral("Đã hủy.") : "Không điều khiển được trình duyệt: " + cdp.error());

    // 2. Chờ viewer vẽ danh sách trang.
    const QString inject = DriveViewerCaptureInternal::injectedScript() + "(); __dx.status()";
    QJsonObject status;
    for (;;)
    {
        if (cancelled())
            return failWith("Đã hủy.");
        if (timedOut())
            return failWith("Hết thời gian chờ trang xem của Google Drive. Thường do: link chưa mở \"Bất kỳ ai có đường liên kết\", "
                            "tệp không phải dạng tài liệu nhiều trang, hoặc Drive đã đổi giao diện.");
        QJsonValue value;
        const Status st = cdp.evaluate(inject, 15000, &value);
        if (st == Status::Canceled)
            return failWith("Đã hủy.");
        if (st == Status::Disconnected || !session.alive())
            return failWith("Trình duyệt bị đóng giữa chừng.");
        if (st == Status::Ok) // Failed = đang điều hướng (ngữ cảnh bị hủy) -> thử lại
        {
            status = value.toObject();
            if (status.value("url").toString().contains("accounts.google.com"))
                return failWith("Link yêu cầu đăng nhập - chỉ chụp được tệp mở công khai (\"Bất kỳ ai có đường liên kết\").");
            if (status.value("found").toInt() > 0)
                break;
        }
        QThread::msleep(1000);
    }
    int total = status.value("total").toInt();
    emit pageProgress(0, total);

    // 3. Cuộn và chụp - tối đa 3 lượt để bù trang nạp chậm; trang đã chụp không chụp lại.
    std::map<int, ImagePdf::Page> pages;
    qint64 jpegBytes = 0;
    for (int pass = 1; pass <= 3 && !(total > 0 && int(pages.size()) >= total); ++pass)
    {
        if (cdp.evaluate("__dx.top()", kStepTimeoutMs) == Status::Canceled)
            return failWith("Đã hủy.");
        QThread::msleep(800);
        for (int bottomHits = 0; bottomHits < 2 && !(total > 0 && int(pages.size()) >= total);)
        {
            if (cancelled())
                return failWith("Đã hủy.");
            if (timedOut())
                return failWith("Quá thời gian cho phép khi đang chụp trang.");
            QJsonValue value;
            const Status st = cdp.evaluate("__dx.step()", kStepTimeoutMs, &value);
            if (st == Status::Canceled)
                return failWith("Đã hủy.");
            if (st != Status::Ok)
            {
                if (st == Status::Disconnected || !session.alive())
                    return failWith("Trình duyệt bị đóng giữa chừng.");
                if (st == Status::TimedOut)
                    return failWith("Trình duyệt không phản hồi khi chụp trang.");
                QThread::msleep(500); // lỗi script thoáng qua (trang đang vẽ lại) - thử lại
                continue;
            }
            const QJsonObject r = value.toObject();
            for (const QJsonValue& pv : r.value("pages").toArray())
            {
                const QJsonObject p = pv.toObject();
                ImagePdf::Page pg;
                pg.jpeg = QByteArray::fromBase64(p.value("b64").toString().toLatin1());
                pg.width = p.value("w").toInt();
                pg.height = p.value("h").toInt();
                jpegBytes += pg.jpeg.size();
                if (jpegBytes > kMaxTotalJpegBytes)
                    return failWith("Tài liệu quá lớn để ghép PDF trong bộ nhớ (trên 1 GB ảnh).");
                pages[p.value("n").toInt()] = std::move(pg);
            }
            if (r.value("total").toInt() > 0)
                total = r.value("total").toInt();
            bottomHits = r.value("atBottom").toBool() ? bottomHits + 1 : 0;
            emit pageProgress(int(pages.size()), total);
        }
    }
    if (pages.empty())
        return failWith("Không lấy được trang nào (trang xem không có ảnh nạp xong - tệp có thể không phải tài liệu nhiều trang).");

    // 4. Ghép PDF và ghi nguyên tử.
    QStringList missing;
    for (int i = 1; i <= total; ++i)
        if (!pages.count(i))
            missing << QString::number(i);
    const QByteArray pdf = ImagePdf::build(pages);
    if (pdf.isEmpty())
        return failWith("Không ghép được PDF (ảnh trang không hợp lệ).");
    QDir().mkpath(QFileInfo(m_params.outputPath).absolutePath());
    QSaveFile file(m_params.outputPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(pdf) != pdf.size() || !file.commit())
        return failWith("Không ghi được tệp PDF: " + file.errorString());

    *outPath = m_params.outputPath;
    if (!missing.isEmpty())
        *warning = QString("PDF còn THIẾU %1/%2 trang (số %3) - tải lại để thử bù.").arg(missing.size()).arg(total)
                       .arg(missing.mid(0, 12).join(", ") + (missing.size() > 12 ? ", ..." : ""));
    return true;
}
