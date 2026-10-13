#include "HeadlessBrowser.h"

#include "FileDownloader.h"
#include "core/WinProcessTree.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <vector>

namespace
{
constexpr qint64 kMaxNetLogBytes = 60LL * 1024 * 1024; // net-log thường 5-20 MB; trần để không ăn hết RAM
constexpr qint64 kMaxDomBytes = 30LL * 1024 * 1024;
} // namespace

namespace HeadlessBrowserInternal
{
QStringList candidatePaths()
{
    QStringList out;
    for (const QString& envName : {"PROGRAMFILES(X86)", "PROGRAMFILES", "LOCALAPPDATA"})
    {
        const QString base = qEnvironmentVariable(envName.toUtf8().constData());
        if (base.isEmpty())
            continue;
        out << base + "/Microsoft/Edge/Application/msedge.exe";
    }
    for (const QString& envName : {"PROGRAMFILES", "PROGRAMFILES(X86)", "LOCALAPPDATA"})
    {
        const QString base = qEnvironmentVariable(envName.toUtf8().constData());
        if (base.isEmpty())
            continue;
        out << base + "/Google/Chrome/Application/chrome.exe";
    }
    return out;
}

QStringList buildArguments(const QString& url, const QString& profileDir, const QString& netLogPath, bool legacyHeadless)
{
    return {
        legacyHeadless ? "--headless" : "--headless=new",
        "--disable-gpu",
        "--no-first-run",
        "--no-default-browser-check",
        "--disable-extensions",
        "--disable-popup-blocking",
        "--mute-audio",
        // Trình phát video thường chỉ tải luồng khi bắt đầu phát - cho phép tự phát để thấy được yêu cầu .m3u8/.mp4.
        "--autoplay-policy=no-user-gesture-required",
        "--user-data-dir=" + profileDir,
        "--log-net-log=" + netLogPath,
        "--net-log-capture-mode=Default",
        "--virtual-time-budget=15000", // cho trang ~15 giây (thời gian ảo) để chạy script và gọi mạng
        "--window-size=1280,900",
        "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
        "Chrome/126.0.0.0 Safari/537.36",
        "--dump-dom",
        url,
    };
}
} // namespace HeadlessBrowserInternal

HeadlessBrowser::HeadlessBrowser(QObject* parent)
    : QObject(parent)
{
}

HeadlessBrowser::~HeadlessBrowser()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        disconnect(m_process, nullptr, this, nullptr);
        killTree();
        m_process->waitForFinished(3000);
    }
}

QString HeadlessBrowser::findBrowserExecutable()
{
    for (const QString& p : HeadlessBrowserInternal::candidatePaths())
        if (QFileInfo::exists(p))
            return p;
    return {};
}

bool HeadlessBrowser::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void HeadlessBrowser::emitOnce(bool ok, const QString& error)
{
    if (m_emitted)
        return;
    m_emitted = true;
    if (m_timeout)
        m_timeout->stop();

    QString dom = QString::fromUtf8(m_stdout);
    QByteArray netLog;
    if (m_tmp && m_tmp->isValid())
    {
        QFile f(m_tmp->filePath("net.json"));
        if (f.open(QIODevice::ReadOnly))
            netLog = f.read(kMaxNetLogBytes);
    }
    m_stdout.clear();
    emit finished(ok, dom, netLog, error);
    m_tmp.reset(); // xóa hồ sơ tạm sau khi đã đọc xong (Edge có thể còn giữ khóa tệp vài trăm ms - xóa gắng hết sức)
}

void HeadlessBrowser::killTree()
{
    if (!m_process)
        return;
    const std::vector<qint64> descendants = m_pid > 0 ? WinProcessTree::findDescendants(m_pid) : std::vector<qint64>{};
    m_process->kill();
    WinProcessTree::terminateProcessList(descendants); // Edge sinh renderer/GPU/network service - dọn cả cây
}

void HeadlessBrowser::start(const QString& url, int timeoutMs, bool legacyHeadless)
{
    if (isRunning())
        return;
    m_emitted = false;
    m_canceled = false;
    m_timedOut = false;
    m_stdout.clear();
    m_stderrTail.clear();

    const QString exe = findBrowserExecutable();
    if (exe.isEmpty())
    {
        QTimer::singleShot(0, this, [this]() {
            emitOnce(false, "Không tìm thấy Microsoft Edge hoặc Google Chrome trên máy để quét sâu trang này.");
        });
        return;
    }
    if (!FileDownloaderInternal::isHttpUrl(url))
    {
        QTimer::singleShot(0, this, [this]() { emitOnce(false, "Địa chỉ không hợp lệ."); });
        return;
    }

    m_tmp = std::make_unique<QTemporaryDir>();
    if (!m_tmp->isValid())
    {
        QTimer::singleShot(0, this, [this]() { emitOnce(false, "Không tạo được thư mục tạm cho trình duyệt."); });
        return;
    }

    if (m_process)
        m_process->deleteLater();
    m_process = new QProcess(this);
    m_process->setProgram(exe);
    m_process->setArguments(HeadlessBrowserInternal::buildArguments(url, m_tmp->filePath("profile"), m_tmp->filePath("net.json"), legacyHeadless));
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    QProcess* process = m_process;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        const QByteArray chunk = process->readAllStandardOutput();
        if (m_stdout.size() + chunk.size() <= kMaxDomBytes)
            m_stdout += chunk;
    });
    // Edge ghi rất nhiều cảnh báo ra stderr - PHẢI đọc bỏ để pipe không đầy làm treo tiến trình; chỉ giữ phần cuối để
    // chẩn đoán khi trình duyệt không trả về trang.
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        m_stderrTail += process->readAllStandardError();
        if (m_stderrTail.size() > 1200)
            m_stderrTail = m_stderrTail.right(1200);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emitOnce(false, "Không khởi chạy được trình duyệt (" + process->errorString() + ").");
    });
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_canceled)
        {
            emitOnce(false, "Đã hủy.");
            return;
        }
        if (m_timedOut)
        {
            emitOnce(false, "Trình duyệt ẩn chạy quá thời gian cho phép.");
            return;
        }
        if (status != QProcess::NormalExit)
        {
            emitOnce(false, "Trình duyệt ẩn bị dừng đột ngột.");
            return;
        }
        // Có DOM là đủ dùng (mã thoát khác 0 khi vài tài nguyên lỗi vẫn thường có DOM).
        if (m_stdout.isEmpty())
        {
            // Dòng cuối có chữ của stderr thường nói rõ lý do (chính sách, thiếu quyền, đường dẫn...).
            QString detail;
            const QStringList lines = QString::fromUtf8(m_stderrTail).split('\n', Qt::SkipEmptyParts);
            for (int i = lines.size() - 1; i >= 0 && detail.isEmpty(); --i)
                detail = lines[i].trimmed();
            QString msg = QString("Trình duyệt ẩn không trả về nội dung trang (mã %1)").arg(exitCode);
            if (!detail.isEmpty())
                msg += ": " + detail.left(160);
            emitOnce(false, msg + ".");
            return;
        }
        emitOnce(true, QString());
    });

    if (!m_timeout)
    {
        m_timeout = new QTimer(this);
        m_timeout->setSingleShot(true);
        connect(m_timeout, &QTimer::timeout, this, [this]() {
            if (!isRunning())
                return;
            m_timedOut = true;
            killTree();
        });
    }
    m_timeout->start(timeoutMs);

    process->start();
    m_pid = process->processId();
}

void HeadlessBrowser::cancel()
{
    if (!isRunning())
        return;
    m_canceled = true;
    killTree();
}
