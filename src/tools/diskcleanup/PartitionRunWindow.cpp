#include "PartitionRunWindow.h"

#include "DiskUiStyle.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "engine/PartitionMover.h"

#include <QCloseEvent>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

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

    // ---- Chương trình/dịch vụ đang giữ ổ: hiện khi khóa ổ thất bại hoặc khi bấm nút kiểm tra ----
    m_holdersPanel = new QFrame(this);
    m_holdersPanel->setObjectName("runHoldersPanel");
    m_holdersPanel->setStyleSheet("#runHoldersPanel { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 8px; }");
    m_holdersPanel->setVisible(false);
    auto* hp = new QVBoxLayout(m_holdersPanel);
    hp->setContentsMargins(10, 8, 10, 8);
    hp->setSpacing(6);
    m_holdersLabel = new QLabel(m_holdersPanel);
    m_holdersLabel->setObjectName("runHoldersLabel");
    m_holdersLabel->setWordWrap(true);
    m_holdersLabel->setTextFormat(Qt::PlainText);
    m_holdersLabel->setStyleSheet("background: transparent; border: none;");
    hp->addWidget(m_holdersLabel);
    m_holdersTable = new QTableWidget(0, 4, m_holdersPanel);
    m_holdersTable->setObjectName("runHoldersTable");
    m_holdersTable->setHorizontalHeaderLabels({"Chương trình / dịch vụ", "PID", "Đang giữ", ""});
    m_holdersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_holdersTable->horizontalHeader()->resizeSection(0, 260);
    m_holdersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_holdersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_holdersTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_holdersTable->horizontalHeader()->resizeSection(3, 120);
    m_holdersTable->verticalHeader()->setVisible(false);
    m_holdersTable->verticalHeader()->setDefaultSectionSize(38);
    m_holdersTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_holdersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_holdersTable->setWordWrap(false);
    m_holdersTable->setTextElideMode(Qt::ElideRight); // phần đầu (cách gỡ / tệp đầu tiên) luôn hiện; đủ nội dung ở chú thích khi rê chuột
    m_holdersTable->setStyleSheet(DiskUi::tableStyle());
    hp->addWidget(m_holdersTable, 1);
    auto* hrow = new QHBoxLayout();
    m_rescanBtn = new QPushButton("🔍 Quét lại", m_holdersPanel);
    m_rescanBtn->setObjectName("runHoldersRescan");
    m_rescanBtn->setStyleSheet(DiskUi::buttonStyle());
    m_rescanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rescanBtn, &QPushButton::clicked, this, &PartitionRunWindow::scanHolders);
    hrow->addWidget(m_rescanBtn);
    hrow->addStretch();
    m_forceBtn = new QPushButton("⚠ Buộc tháo ổ rồi thử lại", m_holdersPanel);
    m_forceBtn->setObjectName("runForceDismountButton");
    m_forceBtn->setStyleSheet(DiskUi::buttonStyle());
    m_forceBtn->setCursor(Qt::PointingHandCursor);
    m_forceBtn->setToolTip("Giống \"chkdsk /x\": tháo cưỡng bức ổ - mọi chương trình còn mở tệp trên ổ mất quyền truy cập tệp đó - rồi khóa ổ ngay. "
                           "Dùng khi đã dừng hết những gì dừng được mà vẫn không khóa được.");
    m_forceBtn->setVisible(false);
    connect(m_forceBtn, &QPushButton::clicked, this, &PartitionRunWindow::onForceDismount);
    hrow->addWidget(m_forceBtn);
    m_stopAllBtn = new QPushButton("⛔ Dừng tất cả", m_holdersPanel);
    m_stopAllBtn->setObjectName("runHoldersStopAll");
    m_stopAllBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_stopAllBtn->setCursor(Qt::PointingHandCursor);
    m_stopAllBtn->setEnabled(false);
    connect(m_stopAllBtn, &QPushButton::clicked, this, &PartitionRunWindow::onStopAllHolders);
    hrow->addWidget(m_stopAllBtn);
    hp->addLayout(hrow);
    root->addWidget(m_holdersPanel, 1);
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
    m_holdersBtn = new QPushButton(m_plan.letter.isEmpty() ? QStringLiteral("🔍 Chương trình đang giữ ổ")
                                                            : QStringLiteral("🔍 Chương trình đang giữ ổ %1:").arg(m_plan.letter.left(1).toUpper()),
                                   this);
    m_holdersBtn->setObjectName("runHoldersButton");
    m_holdersBtn->setStyleSheet(DiskUi::buttonStyle());
    m_holdersBtn->setCursor(Qt::PointingHandCursor);
    m_holdersBtn->setToolTip("Liệt kê chương trình/dịch vụ đang mở tệp trên ổ này (làm ổ không khóa được) - có nút Dừng từng cái và Dừng tất cả.");
    m_holdersBtn->setVisible(!m_plan.letter.isEmpty());
    connect(m_holdersBtn, &QPushButton::clicked, this, &PartitionRunWindow::scanHolders);
    row->addWidget(m_holdersBtn);
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
    m_holdersStop = true;
    if (m_holdersThread)
        m_holdersThread->wait();
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
    if (m_holdersBusy)
        return; // đang dừng/quét chương trình giữ ổ - đợi xong
    m_running = true;
    m_startBtn->setEnabled(false);
    m_holdersBtn->setEnabled(false);
    m_rescanBtn->setEnabled(false);
    m_stopAllBtn->setEnabled(false);
    m_forceBtn->setEnabled(false);
    for (QPushButton* b : m_holdersTable->findChildren<QPushButton*>())
        b->setEnabled(false);
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
    m_holdersBtn->setEnabled(true);
    m_forceBtn->setEnabled(true);
    // Khóa ổ thất bại (chưa đổi gì): cho phép "Buộc tháo ổ" - trừ khi lượt này ĐÃ buộc tháo mà vẫn không được.
    const bool lockFailed = !ok && (failedStep == -2 || failedStep == 0) && message.contains(QStringLiteral("Không khóa được"));
    m_forceBtn->setVisible(lockFailed && !PartitionMover::forceDismountAllowed());
    if (lockFailed)
        m_holdersPanel->setVisible(true);
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
        // Thường là ổ không khóa được: tự liệt kê ngay ai đang giữ ổ, kèm nút Dừng (không tự dừng gì).
        if (!m_quiet)
            scanHolders();
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

// ======================= Chương trình/dịch vụ đang giữ ổ (v1.33.0) =======================

void PartitionRunWindow::onForceDismount()
{
    if (m_running || m_holdersBusy || !m_startBtn->isEnabled())
        return;
    const QString letter = m_plan.letter.isEmpty() ? QStringLiteral("?") : m_plan.letter.left(1).toUpper();
    if (!confirm(QStringLiteral("Buộc tháo ổ"),
                 QStringLiteral("Buộc tháo ổ %1: rồi thử lại (giống \"chkdsk /x\")?\n\n"
                                "Mọi chương trình còn đang mở tệp trên %1: sẽ MẤT quyền truy cập tệp đó ngay lập tức - dữ liệu chưa lưu của chúng có thể "
                                "mất, phần mềm có thể báo lỗi (phần mềm bảo mật thường tự mở lại sau). Ổ được gắn lại bình thường sau khi dời xong.\n\n"
                                "Chỉ dùng khi đã dừng hết những gì dừng được mà vẫn không khóa được. KHÔNG dùng nếu đang chạy máy ảo/WSL/ổ đĩa ảo từ "
                                "ổ %1: (tắt chúng trước).")
                     .arg(letter)))
        return;
    Logger::instance().warning("PartitionRun", QStringLiteral("Người dùng chọn BUỘC THÁO ổ %1: rồi thử lại").arg(letter));
    PartitionMover::setForceDismountAllowed(true);
    m_forceBtn->setVisible(false);
    start();
}

bool PartitionRunWindow::confirm(const QString& title, const QString& text)
{
    if (m_quiet)
        return true; // test UI: không hiện hộp thoại
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void PartitionRunWindow::runInBackground(std::function<void()> work, std::function<void()> done)
{
    QThread* t = QThread::create(std::move(work));
    m_holdersThread = t;
    connect(t, &QThread::finished, this, [this, t, done]() {
        if (m_holdersThread == t)
            m_holdersThread = nullptr;
        t->deleteLater();
        done();
    });
    t->start();
}

void PartitionRunWindow::setHoldersBusy(bool busy, const QString& text)
{
    m_holdersBusy = busy;
    m_holdersBtn->setEnabled(!busy && !m_running);
    m_rescanBtn->setEnabled(!busy && !m_running);
    if (busy)
        m_stopAllBtn->setEnabled(false);
    for (QPushButton* b : m_holdersTable->findChildren<QPushButton*>())
        if (busy)
            b->setEnabled(false);
    if (!text.isEmpty())
        m_holdersLabel->setText(text);
}

void PartitionRunWindow::scanHolders()
{
    if (m_holdersBusy || m_running || m_plan.letter.isEmpty())
        return;
    m_holdersPanel->setVisible(true);
    if (height() < 600)
        resize(std::max(width(), 820), 620);
    const QString letter = m_plan.letter.left(1).toUpper();
    setHoldersBusy(true, QStringLiteral("⏳ Đang kiểm KỸ ổ %1: - chương trình, dịch vụ, nhân Windows, WSL/máy ảo, ổ ảo... (khoảng 10-60 giây)").arg(letter));
    m_holdersStop = false;
    auto result = std::make_shared<VolumeHolders::Report>();
    // Tiến độ từ luồng quét -> nhãn (luồng giao diện).
    auto progress = [this, letter](int n) {
        QMetaObject::invokeMethod(this, [this, letter, n]() {
            if (m_holdersBusy)
                m_holdersLabel->setText(QStringLiteral("⏳ Đang kiểm KỸ ổ %1: - đã hỏi %2 tệp/thư mục có thể đang bị giữ...").arg(letter).arg(n));
        }, Qt::QueuedConnection);
    };
    runInBackground([this, result, letter, progress]() { *result = VolumeHolders::scan(letter, &m_holdersStop, true, progress); },
                    [this, result]() {
                        setHoldersBusy(false, QString());
                        showHolders(*result);
                    });
}

void PartitionRunWindow::setHoldersForTest(const VolumeHolders::Report& report)
{
    m_holdersPanel->setVisible(true);
    showHolders(report);
}

void PartitionRunWindow::showHolders(const VolumeHolders::Report& report)
{
    m_holders = report;
    const QString letter = m_plan.letter.isEmpty() ? QStringLiteral("?") : m_plan.letter.left(1).toUpper();
    m_holdersTable->setRowCount(0);
    int stoppable = 0;
    for (int i = 0; i < report.holders.size(); ++i)
    {
        const VolumeHolders::Holder& h = report.holders[i];
        m_holdersTable->insertRow(i);
        QString name = h.name;
        if (!h.serviceLabels.isEmpty())
            name += QStringLiteral(" — dịch vụ: ") + h.serviceLabels.join(", ");
        auto* nameItem = new QTableWidgetItem(name);
        QString tip = h.name + (h.imagePath.isEmpty() ? QString() : "\n" + QDir::toNativeSeparators(h.imagePath));
        if (!h.serviceLabels.isEmpty())
            tip += "\nDịch vụ: " + h.serviceLabels.join(", ");
        if (!h.samplePaths.isEmpty())
            tip += "\n\nĐang mở:\n  " + h.samplePaths.join("\n  ");
        nameItem->setToolTip(tip);
        m_holdersTable->setItem(i, 0, nameItem);
        auto* pidItem = new QTableWidgetItem(h.pid == 0 ? QStringLiteral("—") : QString::number(h.pid));
        pidItem->setTextAlignment(Qt::AlignCenter);
        m_holdersTable->setItem(i, 1, pidItem);
        auto* holdItem = new QTableWidgetItem(h.holdingSummary());
        if (h.kind == VolumeHolders::HolderKind::System)
            holdItem->setForeground(QColor("#9a6700"));
        holdItem->setToolTip(tip);
        m_holdersTable->setItem(i, 2, holdItem);

        // Nút nằm trong một khung có lề để không bị cắt theo chiều cao dòng; kiểu gọn (kiểu nút đỏ chung có padding lớn hơn dòng bảng).
        auto* cell = new QWidget(m_holdersTable);
        cell->setStyleSheet("background: transparent;");
        auto* cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(6, 0, 6, 0);
        auto* btn = new QPushButton(cell);
        btn->setFixedHeight(26);
        cellLayout->addWidget(btn);
        btn->setStyleSheet("QPushButton { background-color: #ffffff; color: #57606a; border: 1px solid #d0d7de; border-radius: 6px; padding: 0px 8px; "
                           "font-size: 12px; }");
        btn->setObjectName(QStringLiteral("runHolderStop_%1").arg(i));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setText(h.stopLabel());
        if (!h.stoppable())
        {
            btn->setEnabled(false);
            btn->setToolTip(h.self ? QStringLiteral("Chính ứng dụng này.")
                                   : h.hint.isEmpty() ? QStringLiteral("Thành phần hệ thống của Windows - không được dừng từ đây.")
                                                      : QStringLiteral("Không dừng được từ đây - cách gỡ: ") + h.hint);
        }
        else
        {
            if (h.kind == VolumeHolders::HolderKind::Wsl)
                btn->setToolTip(QStringLiteral("wsl --shutdown: tắt máy ảo WSL (dừng mọi bản Linux đang chạy)"));
            else if (h.kind == VolumeHolders::HolderKind::VirtualDisk)
                btn->setToolTip(QStringLiteral("Tháo ổ đĩa ảo %1").arg(h.diskImagePath));
            else
                btn->setToolTip(h.serviceNames.isEmpty() ? QStringLiteral("Buộc dừng %1 (PID %2)").arg(h.name).arg(h.pid)
                                                         : QStringLiteral("Dừng dịch vụ: %1").arg(h.serviceLabels.join(", ")));
            btn->setStyleSheet("QPushButton { background-color: #cf222e; color: white; border: none; border-radius: 6px; padding: 0px 8px; "
                               "font-size: 12px; font-weight: bold; }"
                               "QPushButton:hover { background-color: #a40e26; }"
                               "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }");
            btn->setEnabled(!m_running);
            ++stoppable;
            connect(btn, &QPushButton::clicked, this, [this, i]() { onStopHolder(i); });
        }
        m_holdersTable->setCellWidget(i, 3, cell);
    }
    m_stopAllBtn->setEnabled(stoppable > 0 && !m_running);
    // Không còn gì để dừng: nút "Buộc tháo ổ" là đường duy nhất còn lại - làm nổi bật.
    m_forceBtn->setStyleSheet(stoppable == 0 && report.error.isEmpty() ? DiskUi::dangerButtonStyle() : DiskUi::buttonStyle());

    QStringList lines;
    bool anyStoppable = false;
    for (const VolumeHolders::Holder& h : report.holders)
        anyStoppable = anyStoppable || h.stoppable();
    if (!report.error.isEmpty())
        lines << "⚠ " + report.error;
    else if (report.holders.isEmpty())
        lines << QStringLiteral("Đã kiểm kỹ mà KHÔNG thấy chương trình/dịch vụ/thành phần Windows nào đang mở tệp trên ổ %1: - thứ chặn khóa ổ đang ở "
                                "mức DRIVER/tiến trình được bảo vệ (phần mềm diệt virus như Defender, EDR, ổ đĩa ảo/WSL2...) mà không ứng dụng nào nhìn "
                                "thấy được. Cách nhanh nhất: bấm \"⚠ Buộc tháo ổ rồi thử lại\" (như chkdsk /x). Nếu vẫn không được: khởi động lại máy "
                                "rồi dời NGAY, chưa mở WSL/Docker/VS Code/phần mềm đồng bộ.")
                     .arg(letter);
    else if (!anyStoppable)
        lines << QStringLiteral("Ổ %1: đang bị giữ bởi thành phần của Windows (không dừng được từ đây) - xem cột \"Đang giữ\" để biết cách gỡ, rồi bấm "
                                "\"Thử lại\".")
                     .arg(letter);
    else
        lines << QStringLiteral("%1 chương trình/dịch vụ đang giữ ổ %2: - bấm \"Dừng\" từng cái (hoặc \"Dừng tất cả\") rồi bấm \"Thử lại\". "
                                "Dừng chương trình thường = buộc thoát (mất dữ liệu chưa lưu); dịch vụ được dừng đúng cách và chạy lại khi khởi "
                                "động lại máy. Phần mềm đồng bộ (OneDrive/Google Drive/Nextcloud...) sẽ tự chạy lại sau khi khởi động lại.")
                     .arg(report.holders.size())
                     .arg(letter);
    if (report.pagefileOnDrive)
        lines << QStringLiteral("⚠ Ổ %1: có tệp hoán trang (pagefile/swapfile) - Windows không bao giờ nhả ổ này khi đang chạy. Tắt bộ nhớ ảo trên ổ %1: "
                                "(Hệ thống > Cài đặt hệ thống nâng cao > Hiệu năng > Nâng cao > Bộ nhớ ảo) rồi khởi động lại máy.")
                     .arg(letter);
    if (!report.handleScanComplete)
        lines << QStringLiteral("Lưu ý: chưa quét hết được mọi tệp đang mở (một số truy vấn bị treo) - danh sách có thể thiếu.");
    for (const QString& note : report.notes)
        lines << note;
    if (!report.filters.isEmpty())
    {
        QStringList hints;
        for (const QString& f : report.filters)
            if (const QString h = VolumeHolders::filterHint(f); !h.isEmpty())
                hints << "• " + h;
        lines << QStringLiteral("Driver lọc đang gắn vào ổ %1: %2").arg(letter, report.filters.join(", "));
        if (!hints.isEmpty() && (report.holders.isEmpty() || !anyStoppable))
            lines << hints.join("\n");
    }
    if (report.deepScanDone)
        lines << QStringLiteral("Đã kiểm kỹ: liệt kê %1 tệp/thư mục, hỏi \"ai đang mở\" %2 tệp có khả năng bị giữ%3.")
                     .arg(report.entriesListed)
                     .arg(report.filesChecked)
                     .arg(report.deepLimited ? QStringLiteral(" (chạm giới hạn - có thể còn sót)") : QString());
    if (!report.unreadable.isEmpty() && report.holders.isEmpty())
        lines << QStringLiteral("Không đọc được %1 tiến trình được bảo vệ (vd phần mềm diệt virus: %2) - nếu sau khi dừng hết vẫn không khóa được ổ, "
                                "hãy tạm tắt bảo vệ thời gian thực/phần mềm đó.")
                     .arg(report.unreadable.size())
                     .arg(report.unreadable.mid(0, 4).join(", ") + (report.unreadable.size() > 4 ? ", ..." : ""));
    if (!report.elevated && report.error.isEmpty())
        lines << QStringLiteral("(Đang chạy không có quyền Administrator - chỉ thấy chương trình của tài khoản này.)");
    m_holdersLabel->setText(lines.join("\n"));
}

void PartitionRunWindow::onStopHolder(int row)
{
    if (m_holdersBusy || m_running || row < 0 || row >= m_holders.holders.size())
        return;
    const VolumeHolders::Holder h = m_holders.holders[row];
    if (!h.stoppable())
        return;
    QString text;
    if (h.kind == VolumeHolders::HolderKind::Wsl)
        text = QStringLiteral("Tắt WSL (wsl --shutdown)?\n\nMọi bản Linux WSL đang chạy (và Docker Desktop nếu dùng WSL) sẽ dừng NGAY - chương trình đang chạy "
                              "trong đó mất dữ liệu chưa lưu. Mở lại WSL sau khi dời xong.");
    else if (h.kind == VolumeHolders::HolderKind::VirtualDisk)
        text = QStringLiteral("Tháo ổ đĩa ảo %1?\n\nỔ ảo này đang được gắn từ một tệp trên ổ cần dời. Nếu nó là ổ của một máy ảo đang chạy, hãy tắt máy "
                              "ảo trước.")
                   .arg(h.diskImagePath);
    else if (!h.serviceNames.isEmpty())
        text = QStringLiteral("Dừng dịch vụ: %1 (PID %2)?\n\nDịch vụ được dừng đúng cách qua Windows (kèm các dịch vụ phụ thuộc đang chạy) và sẽ chạy "
                              "lại khi khởi động lại máy, hoặc bật lại trong services.msc.")
                   .arg(h.serviceLabels.join(", "))
                   .arg(h.pid);
    else if (h.name.compare(QLatin1String("explorer.exe"), Qt::CaseInsensitive) == 0)
        text = QStringLiteral("Buộc dừng Windows Explorer (PID %1)?\n\nThanh tác vụ và các cửa sổ thư mục sẽ biến mất vài giây rồi Windows tự mở lại "
                              "Explorer.")
                   .arg(h.pid);
    else
        text = QStringLiteral("Buộc dừng %1 (PID %2)?\n\nDữ liệu CHƯA LƯU của chương trình này sẽ MẤT. Nên tự đóng chương trình đó nếu được.")
                   .arg(h.name)
                   .arg(h.pid);
    if (!confirm(QStringLiteral("Dừng chương trình giữ ổ"), text))
        return;
    setHoldersBusy(true, QStringLiteral("⏳ Đang dừng %1...").arg(h.name));
    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    Logger::instance().info("PartitionRun", QStringLiteral("Dừng chương trình giữ ổ: %1 PID %2 dịch vụ [%3]").arg(h.name).arg(h.pid).arg(h.serviceNames.join(",")));
    runInBackground([h, error, ok]() { *ok = VolumeHolders::stop(h, error.get()); },
                    [this, h, error, ok]() {
                        setHoldersBusy(false, QString());
                        if (!*ok)
                        {
                            Logger::instance().warning("PartitionRun", QStringLiteral("Không dừng được %1: %2").arg(h.name, *error));
                            if (!m_quiet)
                                QMessageBox::warning(this, QStringLiteral("Không dừng được"), *error);
                        }
                        scanHolders(); // xem lại còn ai giữ ổ
                    });
}

void PartitionRunWindow::onStopAllHolders()
{
    if (m_holdersBusy || m_running)
        return;
    QList<VolumeHolders::Holder> targets;
    QStringList names;
    for (const VolumeHolders::Holder& h : m_holders.holders)
        if (h.stoppable())
        {
            targets << h;
            names << QStringLiteral("%1 (PID %2)%3")
                         .arg(h.name)
                         .arg(h.pid)
                         .arg(h.serviceLabels.isEmpty() ? QString() : QStringLiteral(" - dịch vụ: ") + h.serviceLabels.join(", "));
        }
    if (targets.isEmpty())
        return;
    if (!confirm(QStringLiteral("Dừng tất cả"),
                 QStringLiteral("Dừng %1 chương trình/dịch vụ đang giữ ổ?\n\n  • %2\n\nChương trình thường bị BUỘC thoát (mất dữ liệu chưa lưu); dịch vụ "
                                "được dừng đúng cách. Tiến trình hệ thống không bị đụng tới.")
                     .arg(targets.size())
                     .arg(names.join("\n  • "))))
        return;
    setHoldersBusy(true, QStringLiteral("⏳ Đang dừng %1 chương trình/dịch vụ...").arg(targets.size()));
    auto errors = std::make_shared<QStringList>();
    Logger::instance().info("PartitionRun", QStringLiteral("Dừng tất cả chương trình giữ ổ: %1").arg(names.join("; ")));
    runInBackground(
        [targets, errors]() {
            for (const VolumeHolders::Holder& h : targets)
            {
                QString e;
                if (!VolumeHolders::stop(h, &e))
                    *errors << e;
            }
        },
        [this, errors]() {
            setHoldersBusy(false, QString());
            if (!errors->isEmpty())
            {
                Logger::instance().warning("PartitionRun", QStringLiteral("Dừng tất cả: %1 lỗi").arg(errors->size()));
                if (!m_quiet)
                    QMessageBox::warning(this, QStringLiteral("Có mục không dừng được"), errors->join("\n"));
            }
            scanHolders();
        });
}
