#include "PartitionRunWindow.h"

#include "DiskUiStyle.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "engine/PartitionMover.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

PartitionRunWindow::PartitionRunWindow(const PartitionRelocator::Plan& plan, quint32 waitPid, const QString& cleanupDir, QWidget* parent)
    : QWidget(parent)
    , m_plan(plan)
    , m_waitPid(waitPid)
    , m_cleanupDir(cleanupDir)
{
    setWindowTitle("One for ALL - Dời phân vùng (chạy từ bản sao tạm)");
    resize(640, 360);
    setStyleSheet("QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(10);

    auto* head = new QLabel(
        "Ứng dụng đã tự chép mình sang thư mục tạm và chạy lại ở đây (quyền Administrator) để ổ đang chứa ứng dụng tháo được. "
        "Ứng dụng gốc đã đóng. KHÔNG tắt máy/rút nguồn/rút cáp trong lúc dời.",
        this);
    head->setWordWrap(true);
    head->setStyleSheet(DiskUi::bannerStyle("warn"));
    root->addWidget(head);

    m_planLabel = new QLabel(m_plan.description.isEmpty() ? QStringLiteral("(không có mô tả)") : m_plan.description, this);
    m_planLabel->setObjectName("runPlanLabel");
    m_planLabel->setWordWrap(true);
    m_planLabel->setTextFormat(Qt::PlainText);
    m_planLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_planLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("runStatusLabel");
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setStyleSheet("font-weight: 600;");
    root->addWidget(m_statusLabel);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(8);
    m_bar->setVisible(false);
    m_bar->setStyleSheet(DiskUi::thinBarStyle("#cf222e"));
    root->addWidget(m_bar);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName("runProgressLabel");
    m_progressLabel->setWordWrap(true);
    m_progressLabel->setTextFormat(Qt::PlainText);
    root->addWidget(m_progressLabel);
    root->addStretch();

    auto* row = new QHBoxLayout();
    m_startBtn = new QPushButton("▶ Bắt đầu dời", this);
    m_startBtn->setObjectName("runStartButton");
    m_startBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setEnabled(false);
    connect(m_startBtn, &QPushButton::clicked, this, &PartitionRunWindow::start);
    row->addWidget(m_startBtn);
    m_pauseBtn = new QPushButton("⏸ Tạm dừng (chạy tiếp được)", this);
    m_pauseBtn->setObjectName("runPauseButton");
    m_pauseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setVisible(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &PartitionRunWindow::onPause);
    row->addWidget(m_pauseBtn);
    row->addStretch();
    m_reopenBtn = new QPushButton("Mở lại One for ALL", this);
    m_reopenBtn->setObjectName("runReopenButton");
    m_reopenBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_reopenBtn->setCursor(Qt::PointingHandCursor);
    m_reopenBtn->setVisible(false);
    connect(m_reopenBtn, &QPushButton::clicked, this, &PartitionRunWindow::onReopen);
    row->addWidget(m_reopenBtn);
    m_closeBtn = new QPushButton("Đóng", this);
    m_closeBtn->setObjectName("runCloseButton");
    m_closeBtn->setStyleSheet(DiskUi::buttonStyle());
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_closeBtn, &QPushButton::clicked, this, &QWidget::close);
    row->addWidget(m_closeBtn);
    root->addLayout(row);

    m_planWorker = new PartitionMover::PlanWorker(this);
    connect(m_planWorker, &PartitionMover::PlanWorker::progress, this, &PartitionRunWindow::onProgress, Qt::QueuedConnection);
    connect(m_planWorker, &PartitionMover::PlanWorker::planFinished, this, &PartitionRunWindow::onFinished, Qt::QueuedConnection);
    m_resumeWorker = new PartitionMover::MoveWorker(this);
    connect(m_resumeWorker, &PartitionMover::MoveWorker::progress, this,
            [this](qint64 d, qint64 t, QString phase) { onProgress(0, 1, d, t, phase); }, Qt::QueuedConnection);
    connect(m_resumeWorker, &PartitionMover::MoveWorker::moveFinished, this,
            [this](bool ok, bool resumable, QString message) { onFinished(ok, resumable, message, ok ? -1 : 0); }, Qt::QueuedConnection);

    // Chờ ứng dụng cũ (đang giữ ổ) thoát hẳn rồi mới cho bắt đầu - nếu không việc khóa ổ sẽ thất bại.
    m_waited.start();
    m_poll = new QTimer(this);
    connect(m_poll, &QTimer::timeout, this, &PartitionRunWindow::onPoll);
    m_poll->start(250);
    onPoll();
}

PartitionRunWindow::~PartitionRunWindow()
{
    if (m_planWorker && m_planWorker->isRunning())
        m_planWorker->wait();
    if (m_resumeWorker && m_resumeWorker->isRunning())
        m_resumeWorker->wait();
}

bool PartitionRunWindow::isRunning() const
{
    return m_running;
}

bool PartitionRunWindow::startEnabled() const
{
    return m_startBtn->isEnabled();
}

QString PartitionRunWindow::startButtonText() const
{
    return m_startBtn->text();
}

void PartitionRunWindow::onPoll()
{
    if (m_running || !m_poll)
        return;
    m_parentGone = PartitionRelocator::processHasExited(m_waitPid, 0);
    if (m_parentGone)
    {
        m_poll->stop();
        m_statusLabel->setText("Sẵn sàng. Bấm \"Bắt đầu dời\" để tiếp tục (ứng dụng gốc đã đóng).");
        m_startBtn->setEnabled(true);
    }
    else if (m_waited.elapsed() > 60000)
    {
        m_poll->stop();
        m_statusLabel->setText("Ứng dụng gốc chưa đóng sau 60 giây - có thể không khóa được ổ. Hãy đóng nó rồi bấm \"Bắt đầu dời\" (việc khóa ổ sẽ báo rõ nếu vẫn bị giữ).");
        m_startBtn->setEnabled(true);
    }
    else
        m_statusLabel->setText("Đang chờ ứng dụng gốc đóng hẳn để nhả ổ...");
}

void PartitionRunWindow::start()
{
    if (m_running || !m_startBtn->isEnabled())
        return;
    m_running = true;
    m_startBtn->setEnabled(false);
    m_pauseBtn->setVisible(true);
    m_pauseBtn->setEnabled(true);
    m_closeBtn->setEnabled(false);
    m_bar->setVisible(true);
    m_bar->setRange(0, 0);
    m_statusLabel->setText("⏳ Đang dời - KHÔNG tắt máy/rút nguồn/rút cáp...");
    m_copyTimer.invalidate();
    Logger::instance().info("PartitionRun", QStringLiteral("Bắt đầu: đĩa %1 phân vùng %2 resume=%3").arg(m_plan.disk).arg(m_plan.partition).arg(m_plan.resume));
    if (m_plan.resume)
    {
        PartitionMove::JournalState j;
        QString err;
        if (!PartitionMover::loadJournal(&j, &err))
        {
            onFinished(false, false, QStringLiteral("Không đọc được nhật ký để chạy tiếp: ") + (err.isEmpty() ? QStringLiteral("không có nhật ký.") : err), 0);
            return;
        }
        m_resumeWorker->setResume(j, m_plan.letter);
        m_resumeWorker->start();
        return;
    }
    m_planWorker->setPlan(m_plan.disk, m_plan.partition, m_plan.steps, m_plan.letter);
    m_planWorker->start();
}

void PartitionRunWindow::onProgress(int stepIndex, int stepCount, qint64 doneBytes, qint64 totalBytes, QString phase)
{
    if (totalBytes <= 0)
    {
        m_bar->setRange(0, 0);
        m_progressLabel->setText(phase);
        m_copyTimer.invalidate();
        return;
    }
    if (!m_copyTimer.isValid())
    {
        m_copyTimer.start();
        m_copyBase = doneBytes;
    }
    m_bar->setRange(0, 1000);
    m_bar->setValue(static_cast<int>(1000.0 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes)));
    QString text = QString("Bước %1/%2 · %3 · %4 / %5 (%6%)")
                       .arg(stepIndex + 1)
                       .arg(stepCount)
                       .arg(phase, DiskUi::formatBytes(doneBytes), DiskUi::formatBytes(totalBytes))
                       .arg(QString::number(100.0 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes), 'f', 1));
    const qint64 ms = m_copyTimer.elapsed();
    if (ms > 3000 && doneBytes > m_copyBase)
    {
        const double rate = static_cast<double>(doneBytes - m_copyBase) / (static_cast<double>(ms) / 1000.0);
        text += QString(" · %1/giây · còn khoảng %2 phút")
                    .arg(DiskUi::formatBytes(static_cast<qint64>(rate)), QString::number(std::max(1.0, static_cast<double>(totalBytes - doneBytes) / rate / 60.0), 'f', 0));
    }
    m_progressLabel->setText(text);
}

void PartitionRunWindow::onPause()
{
    m_pauseBtn->setEnabled(false);
    m_progressLabel->setText("Đang dừng ở ranh giới an toàn tiếp theo...");
    m_planWorker->requestPause();
    m_resumeWorker->requestPause();
}

void PartitionRunWindow::onFinished(bool ok, bool resumable, QString message, int failedStep)
{
    m_running = false;
    m_bar->setVisible(false);
    m_pauseBtn->setVisible(false);
    m_closeBtn->setEnabled(true);
    m_reopenBtn->setVisible(true);
    m_statusLabel->setText(ok ? QStringLiteral("✓ Hoàn tất")
                              : failedStep == -2 ? QStringLiteral("⚠ Chưa bắt đầu - kiểm tra trước không đạt (chưa có gì bị thay đổi)")
                                                 : QString("⚠ Dừng%1").arg(failedStep >= 0 ? QString(" ở bước %1").arg(failedStep + 1) : QString()));
    m_progressLabel->setText(message);
    m_startBtn->setEnabled(false); // chỉ bật lại ở hai trường hợp bên dưới (tiếp tục / thử lại)
    if (!ok && resumable)
    {
        m_startBtn->setText("▶ Tiếp tục");
        m_plan.resume = true; // lần bấm sau chạy tiếp theo nhật ký
        m_startBtn->setEnabled(true);
    }
    else if (!ok && (failedStep == -2 || failedStep == 0))
    {
        // Chưa đổi gì (kiểm tra trước không đạt, hoặc bước ĐẦU TIÊN lỗi trước khi ghi): đóng các chương trình đang giữ ổ rồi thử lại NGAY, khỏi mở lại ứng dụng.
        m_startBtn->setText("🔄 Thử lại (sau khi đã đóng các chương trình giữ ổ)");
        m_startBtn->setEnabled(true);
    }
    if (m_quiet)
        return;
    if (ok)
        QMessageBox::information(this, "Hoàn tất", message);
    else
        QMessageBox::warning(this, resumable ? "Đã dừng giữa chừng" : "Không dời được", message);
}

void PartitionRunWindow::onReopen()
{
    // Mở bản gốc bằng QUYỀN THƯỜNG qua Explorer (tiến trình này đang là Administrator - chạy thẳng sẽ cho bản gốc quyền cao không cần thiết).
    const QString exe = m_plan.originalExe;
    if (exe.isEmpty() || !QFileInfo::exists(exe))
    {
        QMessageBox::warning(this, "Mở lại", "Không thấy ứng dụng gốc tại:\n" + QDir::toNativeSeparators(exe) + "\n(ổ có thể chưa gắn lại - thử lại sau vài giây).");
        return;
    }
    QProcess::startDetached(AppPaths::systemExecutable("explorer.exe"), {QDir::toNativeSeparators(exe)});
}

void PartitionRunWindow::scheduleCleanup()
{
    // Xóa thư mục bản sao sau khi tiến trình này thoát (tệp exe đang chạy không xóa được khi còn sống). CHỈ xóa thư mục đúng mẫu trong %TEMP%.
    if (m_cleanupDir.isEmpty() || !PartitionRelocator::isRuntimeDir(m_cleanupDir))
        return;
    const QString cmd = AppPaths::systemExecutable("cmd.exe");
    QProcess::startDetached(cmd, {"/c", QString("ping -n 4 127.0.0.1 >nul & rmdir /s /q \"%1\"").arg(QDir::toNativeSeparators(m_cleanupDir))});
}

void PartitionRunWindow::closeEvent(QCloseEvent* event)
{
    if (m_running)
    {
        QMessageBox::warning(this, "Đang dời", "Không thể đóng cửa sổ trong lúc đang dời phân vùng - hãy bấm \"Tạm dừng\" hoặc đợi xong.");
        event->ignore();
        return;
    }
    scheduleCleanup();
    event->accept();
}
