#include "PartitionTab.h"

#include "DiskCleanupWindow.h"
#include "DiskMapWidget.h"
#include "DiskUiStyle.h"
#include "engine/PartitionMover.h"
#include "engine/PartitionResizer.h"
#include "core/ToolManager.h"
#include "engine/PartitionRelocator.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
constexpr double GB = 1024.0 * 1024.0 * 1024.0;

/// Chuỗi người dùng phải gõ lại để xác nhận - tên ổ đĩa nếu có ("C"), nếu không thì định danh theo
/// số đĩa/số phân vùng (phân vùng EFI/Recovery thường không có tên ổ đĩa).
QString confirmToken(const PartitionManager::PartitionInfo& p)
{
    if (!p.driveLetter.isEmpty())
        return p.driveLetter;
    return QString("DISK%1-PART%2").arg(p.diskNumber).arg(p.partitionNumber);
}
} // namespace

PartitionTab::PartitionTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_resizer = new PartitionResizer(this);
    connect(m_resizer, &PartitionResizer::resizeFinished, this, &PartitionTab::onResizeFinished);

    m_plan = new PartitionMover::PlanWorker(this);
    connect(m_plan, &PartitionMover::PlanWorker::progress, this, &PartitionTab::onPlanProgress, Qt::QueuedConnection);
    connect(m_plan, &PartitionMover::PlanWorker::planFinished, this, &PartitionTab::onPlanFinished, Qt::QueuedConnection);
    m_resumeWorker = new PartitionMover::MoveWorker(this);
    connect(m_resumeWorker, &PartitionMover::MoveWorker::progress, this,
            [this](qint64 d, qint64 t, QString phase) { onPlanProgress(0, 1, d, t, phase); }, Qt::QueuedConnection);
    connect(m_resumeWorker, &PartitionMover::MoveWorker::moveFinished, this,
            [this](bool ok, bool resumable, QString message) { onPlanFinished(ok, resumable, message, ok ? -1 : 0); }, Qt::QueuedConnection);

    updateElevationBanner();
    checkPendingJournal();
    reloadPartitions();
}

PartitionTab::~PartitionTab()
{
    // KHÔNG hủy giữa chừng (an toàn đĩa quan trọng hơn đóng cửa sổ nhanh) - nhưng CŨNG không được để
    // QThread bị hủy đối tượng trong lúc vẫn đang thực sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu
    // Qt). Co giãn lớn có thể mất VÀI PHÚT - 5 giây không đủ, nên chờ KHÔNG GIỚI HẠN thời gian ở đây.
    // Bình thường không tới mức này: DiskCleanupWindow::closeEvent() đã CHẶN đóng cửa sổ hẳn trong lúc
    // đang đổi kích thước, đây chỉ là lưới an toàn cuối cùng (vd nếu widget bị hủy theo đường khác).
    if (m_resizer && m_resizer->isRunning())
        m_resizer->wait();
    // Tương tự cho việc dời/chép dữ liệu: KHÔNG hủy giữa chừng nhưng cũng không để QThread bị hủy khi còn chạy. (Cửa sổ đã chặn đóng khi đang chạy.)
    if (m_plan && m_plan->isRunning())
        m_plan->wait();
    if (m_resumeWorker && m_resumeWorker->isRunning())
        m_resumeWorker->wait();
    // Luồng nền đang hỏi Windows (chỉ đọc, tối đa ~20 giây) cũng phải thoát hẳn trước khi bị hủy.
    if (m_queryThread)
        m_queryThread->wait();
}

bool PartitionTab::isResizingNow() const
{
    return (m_resizer && m_resizer->isRunning()) || (m_plan && m_plan->isRunning()) || (m_resumeWorker && m_resumeWorker->isRunning()) || m_planActive;
}

void PartitionTab::buildUi()
{
    // Nội dung tab dài hơn (sơ đồ đĩa + hàng hướng + mô tả thay đổi): đặt trong vùng cuộn để cửa sổ nhỏ (tối
    // thiểu 760x480) không cắt mất ô xác nhận/nút "Đổi kích thước" - Qt sẽ không co các widget dưới kích thước tối thiểu.
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    scroll->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget();
    content->setObjectName("partitionTabContent");
    content->setStyleSheet("#partitionTabContent { background: transparent; }");
    scroll->setWidget(content);
    outer->addWidget(scroll);

    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_elevationBanner = new QLabel(this);
    m_elevationBanner->setWordWrap(true);
    m_elevationBanner->setVisible(false);
    root->addWidget(m_elevationBanner);

    m_relaunchBtn = new QPushButton("🛡 Chạy lại với quyền Quản trị...", this);
    m_relaunchBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_relaunchBtn->setCursor(Qt::PointingHandCursor);
    m_relaunchBtn->setVisible(false);
    connect(m_relaunchBtn, &QPushButton::clicked, this, &PartitionTab::onRelaunchElevatedClicked);
    root->addWidget(m_relaunchBtn);

    // Banner "có lần dời dở dang" (từ nhật ký phục hồi) - hiện đầu tab, kèm Tiếp tục / Hủy bỏ.
    m_pendingBanner = new QLabel(this);
    m_pendingBanner->setObjectName("partitionPendingBanner");
    m_pendingBanner->setWordWrap(true);
    m_pendingBanner->setStyleSheet(DiskUi::bannerStyle("danger"));
    m_pendingBanner->setVisible(false);
    root->addWidget(m_pendingBanner);
    auto* pendingRow = new QHBoxLayout();
    m_resumeBtn = new QPushButton("▶ Tiếp tục lần dời dở dang", this);
    m_resumeBtn->setObjectName("partitionResumeButton");
    m_resumeBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_resumeBtn->setCursor(Qt::PointingHandCursor);
    m_resumeBtn->setVisible(false);
    connect(m_resumeBtn, &QPushButton::clicked, this, &PartitionTab::onResumeClicked);
    pendingRow->addWidget(m_resumeBtn);
    m_abandonBtn = new QPushButton("Hủy bỏ lần dời này", this);
    m_abandonBtn->setObjectName("partitionAbandonButton");
    m_abandonBtn->setStyleSheet(DiskUi::buttonStyle());
    m_abandonBtn->setCursor(Qt::PointingHandCursor);
    m_abandonBtn->setVisible(false);
    connect(m_abandonBtn, &QPushButton::clicked, this, &PartitionTab::onAbandonClicked);
    pendingRow->addWidget(m_abandonBtn);
    pendingRow->addStretch();
    root->addLayout(pendingRow);

    auto* dangerBanner = new QLabel(
        "⚠ Đổi kích thước phân vùng là thao tác đĩa THẬT. Hãy sao lưu dữ liệu quan trọng trước khi "
        "tiếp tục, và không tắt máy/rút nguồn trong lúc đang xử lý.",
        this);
    dangerBanner->setWordWrap(true);
    dangerBanner->setStyleSheet(DiskUi::bannerStyle("danger"));
    root->addWidget(dangerBanner);

    // Dời phân vùng/kéo mép trái là tính năng MỚI ghi thẳng xuống đĩa: mặc định TẮT, người dùng phải chủ động tích (không lưu giữa các lần mở).
    m_experimentalCheck = new QCheckBox("🧪 Bật dời phân vùng / kéo mép trái (THỬ NGHIỆM - ghi thẳng xuống đĩa, chưa kiểm chứng trên mọi loại đĩa; tôi hiểu có rủi ro mất dữ liệu và đã sao lưu)", this);
    m_experimentalCheck->setObjectName("partitionMoveExperimentalCheck");
    m_experimentalCheck->setStyleSheet("color: #9a6700; font-weight: 600; font-size: 12px;");
    m_experimentalCheck->setCursor(Qt::PointingHandCursor);
    connect(m_experimentalCheck, &QCheckBox::toggled, this, [this](bool) {
        updateMoveSupport();
        updateChangePreview();
        m_resizeBtn->setEnabled(!m_loading && !isResizingNow() && resizeRequestBlockReason().isEmpty());
    });
    root->addWidget(m_experimentalCheck);

    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Làm mới", this);
    m_refreshBtn->setStyleSheet(DiskUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &PartitionTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);
    topRow->addStretch();
    root->addLayout(topRow);

    // Sơ đồ đĩa kiểu Disk Management: bấm khối để chọn; sau khi tra kích thước thì kéo tay nắm ở mép phải khối.
    m_diskMap = new DiskMapWidget(this);
    m_diskMap->setObjectName("partitionDiskMap");
    connect(m_diskMap, &DiskMapWidget::partitionClicked, this, &PartitionTab::onMapPartitionClicked);
    connect(m_diskMap, &DiskMapWidget::previewGeometryChanged, this, &PartitionTab::onMapGeometryChanged);
    root->addWidget(m_diskMap);

    auto* mapHint = new QLabel(
        "💡 Bấm một khối trên sơ đồ (hoặc dòng trong bảng) để chọn phân vùng, bấm \"Tra kích thước có thể đổi tới...\", "
        "rồi KÉO tay nắm ở mép phải khối: sang TRÁI = thu nhỏ, sang PHẢI = mở rộng. Với phân vùng dữ liệu NTFS còn kéo được MÉP TRÁI "
        "và kéo CẢ KHỐI để dời (kể cả sang vùng chưa phân bổ khác để đổi thứ tự) - ứng dụng sẽ chép dữ liệu sang vị trí mới.",
        this);
    mapHint->setWordWrap(true);
    mapHint->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(mapHint);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);


    // ---- Khu vực đổi kích thước (ẩn cho tới khi chọn 1 phân vùng) ----
    m_detailLabel = new QLabel(this);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_detailLabel->setVisible(false);
    root->addWidget(m_detailLabel);

    m_queryBtn = new QPushButton("Tra kích thước có thể đổi tới...", this);
    m_queryBtn->setStyleSheet(DiskUi::buttonStyle());
    m_queryBtn->setCursor(Qt::PointingHandCursor);
    m_queryBtn->setVisible(false);
    m_queryBtn->setObjectName("partitionQueryButton"); // objectName: để bộ test UI tìm đúng widget
    connect(m_queryBtn, &QPushButton::clicked, this, &PartitionTab::onQuerySupportedSizeClicked);
    root->addWidget(m_queryBtn);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(DiskUi::bannerStyle("warn"));
    m_warningLabel->setVisible(false);
    root->addWidget(m_warningLabel);

    // ---- Hàng "Hướng thay đổi": KHÓA hướng để không kéo/nhập nhầm (chỉ hiện sau khi tra được khoảng cho phép) ----
    m_directionRow = new QWidget(this);
    m_directionRow->setVisible(false);
    auto* dirLayout = new QHBoxLayout(m_directionRow);
    dirLayout->setContentsMargins(0, 0, 0, 0);
    auto* dirLabel = new QLabel("Hướng thay đổi:", m_directionRow);
    dirLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    dirLayout->addWidget(dirLabel);
    const QString toggleStyle =
        DiskUi::buttonStyle() +
        "QPushButton:checked { background-color: #0969da; color: #ffffff; border-color: #0969da; font-weight: bold; }"
        "QPushButton:checked:hover { background-color: #0854b0; color: #ffffff; }";
    auto makeDirButton = [&](const QString& text, const QString& name, const QString& tip, DiskLayout::ResizeDirection dir) {
        auto* b = new QPushButton(text, m_directionRow);
        b->setCheckable(true);
        b->setStyleSheet(toggleStyle);
        b->setCursor(Qt::PointingHandCursor);
        b->setObjectName(name);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, dir]() { onDirectionButtonClicked(dir); });
        dirLayout->addWidget(b);
        return b;
    };
    m_dirShrinkBtn = makeDirButton("◀ Chỉ thu nhỏ", "partitionDirShrink",
                                   "Chỉ cho phép THU NHỎ: kéo mép phải sang TRÁI. Phần giải phóng nằm bên phải phân vùng.",
                                   DiskLayout::ResizeDirection::ShrinkOnly);
    m_dirFreeBtn = makeDirButton("◀ ▶ Cả hai hướng", "partitionDirFree",
                                 "Cho phép cả thu nhỏ (kéo trái) lẫn mở rộng (kéo phải).", DiskLayout::ResizeDirection::Free);
    m_dirGrowBtn = makeDirButton("Chỉ mở rộng ▶", "partitionDirGrow",
                                 "Chỉ cho phép MỞ RỘNG: kéo mép phải sang PHẢI, lấy từ vùng chưa phân bổ liền kề bên phải.",
                                 DiskLayout::ResizeDirection::GrowOnly);
    m_dirFreeBtn->setChecked(true);
    dirLayout->addStretch();
    root->addWidget(m_directionRow);

    auto* resizeRow = new QHBoxLayout();
    m_sizeLabel = new QLabel("Kích thước mới:", this);
    m_sizeLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_newSizeSpin = new QDoubleSpinBox(this);
    m_newSizeSpin->setStyleSheet(DiskUi::inputStyle());
    m_newSizeSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_newSizeSpin->setSuffix(" GB");
    m_newSizeSpin->setDecimals(2);
    m_newSizeSpin->setVisible(false);
    m_newSizeSpin->setObjectName("partitionNewSizeSpin");
    m_sizeLabel->setVisible(false);
    resizeRow->addWidget(m_sizeLabel);
    resizeRow->addWidget(m_newSizeSpin);
    m_startLabel = new QLabel("Vị trí bắt đầu mới:", this);
    m_startLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_startLabel->setVisible(false);
    m_startSpin = new QDoubleSpinBox(this);
    m_startSpin->setStyleSheet(DiskUi::inputStyle());
    m_startSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_startSpin->setSuffix(" GB");
    m_startSpin->setDecimals(2);
    m_startSpin->setRange(0.0, 1.0e6);
    m_startSpin->setVisible(false);
    m_startSpin->setObjectName("partitionNewStartSpin");
    m_startSpin->setToolTip("Vị trí bắt đầu của phân vùng tính từ đầu đĩa (GB). Đổi giá trị này (hoặc kéo mép trái/khối trên sơ đồ) để dời phân vùng.");
    resizeRow->addWidget(m_startLabel);
    resizeRow->addWidget(m_startSpin);
    resizeRow->addStretch();
    root->addLayout(resizeRow);
    connect(m_newSizeSpin, &QDoubleSpinBox::valueChanged, this, &PartitionTab::onNewSizeChanged);
    connect(m_startSpin, &QDoubleSpinBox::valueChanged, this, &PartitionTab::onNewStartChanged);

    m_changeLabel = new QLabel(this);
    m_changeLabel->setWordWrap(true);
    m_changeLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_changeLabel->setObjectName("partitionChangeLabel");
    m_changeLabel->setVisible(false);
    root->addWidget(m_changeLabel);

    m_edgeNoteLabel = new QLabel(this);
    m_edgeNoteLabel->setWordWrap(true);
    m_edgeNoteLabel->setStyleSheet(DiskUi::bannerStyle("info"));
    m_edgeNoteLabel->setObjectName("partitionEdgeNote");
    m_edgeNoteLabel->setVisible(false);
    root->addWidget(m_edgeNoteLabel);

    m_confirmHintLabel = new QLabel(this);
    m_confirmHintLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_confirmHintLabel->setVisible(false);
    root->addWidget(m_confirmHintLabel);

    auto* confirmRow = new QHBoxLayout();
    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setStyleSheet(DiskUi::inputStyle());
    m_confirmEdit->setVisible(false);
    m_confirmEdit->setObjectName("partitionConfirmEdit");
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &PartitionTab::onConfirmTextChanged);
    confirmRow->addWidget(m_confirmEdit);

    m_resizeBtn = new QPushButton("Đổi kích thước", this);
    m_resizeBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_resizeBtn->setCursor(Qt::PointingHandCursor);
    m_resizeBtn->setEnabled(false);
    m_resizeBtn->setVisible(false);
    m_resizeBtn->setObjectName("partitionResizeButton");
    connect(m_resizeBtn, &QPushButton::clicked, this, &PartitionTab::onResizeClicked);
    confirmRow->addWidget(m_resizeBtn);
    root->addLayout(confirmRow);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #cf222e; border-radius: 3px; }");
    root->addWidget(m_progressBar);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setObjectName("partitionProgressLabel");
    m_progressLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_progressLabel->setWordWrap(true);
    m_progressLabel->setTextFormat(Qt::PlainText);
    m_progressLabel->setVisible(false);
    root->addWidget(m_progressLabel);
    m_pauseBtn = new QPushButton("⏸ Tạm dừng (chạy tiếp được)", this);
    m_pauseBtn->setObjectName("partitionPauseButton");
    m_pauseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_pauseBtn->setCursor(Qt::PointingHandCursor);
    m_pauseBtn->setVisible(false);
    connect(m_pauseBtn, &QPushButton::clicked, this, &PartitionTab::onPauseClicked);
    root->addWidget(m_pauseBtn);

    // Bảng chỉ là danh sách phụ (sơ đồ ở trên đã chọn được phân vùng): đặt CUỐI để ô điều khiển đổi kích thước nằm sát
    // sơ đồ, không phải cuộn xuống dưới bảng mới thấy.
    m_table = new QTableWidget(0, 7, this);
    m_table->setMinimumHeight(130); // vùng cuộn lo phần còn lại, bảng không bị ép về 0 dòng khi cửa sổ thấp
    m_table->setHorizontalHeaderLabels({"Ổ đĩa", "Loại", "Hệ thống tệp", "Nhãn", "Dung lượng", "Còn trống", "Ghi chú"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DiskUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &PartitionTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);
}

void PartitionTab::updateElevationBanner()
{
    const bool elevated = PartitionManager::isElevated();
    m_elevationBanner->setVisible(!elevated);
    m_relaunchBtn->setVisible(!elevated);
    if (!elevated)
    {
        m_elevationBanner->setText(
            "🔒 Đang chạy KHÔNG có quyền Administrator - có thể xem danh sách phân vùng, nhưng cần "
            "quyền Administrator để tra kích thước cho phép và đổi kích thước thật.");
        m_elevationBanner->setStyleSheet(DiskUi::bannerStyle("warn"));
    }
}

void PartitionTab::onRelaunchElevatedClicked()
{
    // qApp->quit() ở cuối hàm này KHÔNG tự gọi closeEvent() của các cửa sổ tool KHÁC đang mở (xem
    // MainWindow.cpp) - nếu CHÍNH tab này (hoặc một cửa sổ Disk Cleanup khác, dù hiện tại chỉ có một
    // cửa sổ duy nhất mỗi loại) đang đổi kích thước phân vùng thật, buộc thoát ngay bây giờ sẽ
    // TerminateProcess giữa chừng một thao tác không an toàn để hủy - CHẶN hẳn trước khi relaunch.
    QString busyReason;
    if (ToolManager::instance().anyToolWindowBusy(&busyReason))
    {
        QMessageBox::warning(this, "Không thể chạy lại lúc này",
            "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason + ") - chạy lại ứng "
            "dụng lúc này có thể làm hỏng dữ liệu. Vui lòng đợi thao tác đó hoàn tất rồi thử lại.");
        return;
    }

    QString error;
    if (!PartitionManager::relaunchElevated(&error))
    {
        QMessageBox::warning(this, "Chạy lại với quyền Quản trị", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    // Khởi chạy bản sao mới (có quyền Administrator) thành công - đóng bản hiện tại (không có quyền).
    qApp->quit();
}

void PartitionTab::onRefreshClicked()
{
    updateElevationBanner();
    reloadPartitions();
}

bool PartitionTab::isLoading() const
{
    return m_loading;
}

void PartitionTab::setLoading(bool loading, const QString& statusText)
{
    m_loading = loading;
    // Khóa bảng + các nút trong lúc đang hỏi Windows: không cho đổi dòng chọn/bấm tra lần nữa khi kết quả
    // của lần hỏi trước chưa về (kết quả sẽ được gắn cho phân vùng đang chọn lúc BẮT ĐẦU hỏi).
    m_refreshBtn->setEnabled(!loading);
    m_table->setEnabled(!loading);
    m_diskMap->setEnabled(!loading);
    m_queryBtn->setEnabled(!loading);
    if (loading && !statusText.isEmpty())
        m_statusLabel->setText(statusText);
}

void PartitionTab::runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done)
{
    // Get-Partition/Get-PartitionSupportedSize qua PowerShell mất 1-3 giây (lần đầu nạp module Storage
    // có thể lâu hơn nhiều, tối đa 20 giây theo PowerShellRunner) - trước đây chạy thẳng trên luồng giao
    // diện, làm cả cửa sổ Disk Cleanup đứng hình ngay lúc mở và mỗi lần bấm Làm mới/Tra kích thước.
    setLoading(true, statusText);
    QThread* thread = QThread::create(std::move(work));
    thread->setParent(this);
    m_queryThread = thread;
    connect(thread, &QThread::finished, this, [this, thread, done = std::move(done)]() {
        if (m_queryThread == thread)
            m_queryThread = nullptr;
        thread->deleteLater();
        setLoading(false);
        done();
    });
    thread->start();
}

void PartitionTab::reloadPartitions()
{
    if (m_loading)
        return;

    m_table->clearSelection();
    resetResizeState();
    showSelectionControls(false);

    auto partitions = std::make_shared<QList<PartitionManager::PartitionInfo>>();
    auto disks = std::make_shared<QList<PartitionManager::DiskInfo>>();
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang đọc danh sách phân vùng...",
        [partitions, disks, error]() {
            *partitions = PartitionManager::listPartitions(error.get());
            // Get-Disk chỉ để vẽ sơ đồ (dung lượng/kiểu đĩa): lỗi ở đây KHÔNG được báo như lỗi đọc phân vùng - sơ
            // đồ tự rơi về dựng từ riêng các phân vùng.
            QString diskError;
            *disks = PartitionManager::listDisks(&diskError);
        },
        [this, partitions, disks, error]() { populatePartitions(*partitions, *disks, *error); });
}

void PartitionTab::populatePartitions(const QList<PartitionManager::PartitionInfo>& partitions,
                                      const QList<PartitionManager::DiskInfo>& disks, const QString& error)
{
    m_table->clearSelection();
    resetResizeState();
    showSelectionControls(false);
    m_partitions = partitions;
    m_disks = disks;
    m_diskMap->setData(m_disks, m_partitions);

    m_table->setRowCount(m_partitions.size());
    for (int i = 0; i < m_partitions.size(); ++i)
    {
        const auto& p = m_partitions[i];
        auto setItem = [&](int col, const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            m_table->setItem(i, col, item);
        };
        setItem(0, p.driveLetter.isEmpty() ? "—" : (p.driveLetter + ":"));
        setItem(1, p.type);
        setItem(2, p.hasFileSystem() ? p.fileSystem : "—");
        setItem(3, p.label);
        setItem(4, DiskUi::formatBytes(p.sizeBytes));
        setItem(5, p.freeBytes >= 0 ? DiskUi::formatBytes(p.freeBytes) : "—");

        QStringList notes;
        if (p.isBoot) notes << "Khởi động";
        if (p.isSystem) notes << "Hệ thống (EFI)";
        if (p.isActive) notes << "Đang hoạt động";
        setItem(6, notes.join(", "));
    }

    if (!error.isEmpty())
        m_statusLabel->setText("⚠ " + error);
    else if (!m_statusAfterReload.isEmpty())
        m_statusLabel->setText(m_statusAfterReload); // giữ kết quả lần đổi kích thước vừa xong trên màn hình
    else
        m_statusLabel->setText(QString("Tìm thấy %1 phân vùng.").arg(m_partitions.size()));
    m_statusAfterReload.clear();
}

void PartitionTab::setDataForTest(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions)
{
    populatePartitions(partitions, disks, QString());
}

const PartitionManager::PartitionInfo* PartitionTab::selectedPartition() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_partitions.size())
        return nullptr;
    return &m_partitions[row];
}

void PartitionTab::showSelectionControls(bool visible)
{
    m_detailLabel->setVisible(visible);
    m_queryBtn->setVisible(visible);
}

void PartitionTab::resetResizeState()
{
    // Xóa SẠCH mọi thứ thuộc về lần tra trước: khoảng kích thước, phân vùng đã tra, ô nhập, ô xác nhận,
    // nút "Đổi kích thước". Gọi ở MỌI lần đổi dòng chọn/nạp lại danh sách - trước đây chỉ làm khi bỏ
    // chọn hẳn, nên chọn ổ C: -> tra -> gõ "C" (nút bật) -> bấm sang dòng ổ D: vẫn để nguyên nút đang
    // bật cùng khoảng kích thước của C:, và bấm nút sẽ đổi kích thước ổ D: theo số liệu của ổ C:.
    m_supportedRange = PartitionManager::SupportedSizeRange{};
    m_queriedDisk = -1;
    m_queriedPartition = -1;
    m_queriedSizeBytes = -1;

    m_diskMap->clearResizeContext();
    m_diskMap->setMoveEnabled(false);
    m_queriedStartBytes = -1;
    m_startLabel->setVisible(false);
    m_startSpin->setVisible(false);
    m_warningLabel->setVisible(false);
    m_directionRow->setVisible(false);
    m_changeLabel->setVisible(false);
    m_edgeNoteLabel->setVisible(false);
    m_newSizeSpin->setVisible(false);
    m_sizeLabel->setVisible(false);
    m_confirmHintLabel->setVisible(false);
    m_confirmEdit->setVisible(false);
    m_confirmEdit->clear();
    m_confirmEdit->setPlaceholderText(QString());
    m_resizeBtn->setVisible(false);
    m_resizeBtn->setEnabled(false);
}

void PartitionTab::onRowSelectionChanged()
{
    resetResizeState();
    const auto* p = selectedPartition();
    showSelectionControls(p != nullptr);
    m_diskMap->setSelection(p ? p->diskNumber : -1, p ? p->partitionNumber : -1);
    if (!p)
        return;

    m_detailLabel->setText(QString("Đã chọn: %1 - %2 - %3 - Dung lượng hiện tại %4")
                               .arg(p->driveLetter.isEmpty() ? confirmToken(*p) : (p->driveLetter + ":"))
                               .arg(p->type)
                               .arg(p->hasFileSystem() ? p->fileSystem : "(không có hệ thống tệp)")
                               .arg(DiskUi::formatBytes(p->sizeBytes)));
}

void PartitionTab::onQuerySupportedSizeClicked()
{
    const auto* p = selectedPartition();
    if (!p || m_loading)
        return;

    resetResizeState();
    // Chép ra giá trị: con trỏ vào m_partitions không được giữ qua lúc chờ luồng nền.
    const int disk = p->diskNumber;
    const int partition = p->partitionNumber;
    const qint64 sizeBytes = p->sizeBytes;

    auto range = std::make_shared<PartitionManager::SupportedSizeRange>();
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang hỏi Windows khoảng kích thước cho phép...",
        [range, error, disk, partition]() { *range = PartitionManager::querySupportedSize(disk, partition, error.get()); },
        [this, range, error, disk, partition, sizeBytes]() {
            m_statusLabel->setText(QString("Tìm thấy %1 phân vùng.").arg(m_partitions.size()));
            applySupportedRange(disk, partition, sizeBytes, *range, *error);
        });
}

void PartitionTab::applySupportedRange(int disk, int partition, qint64 sizeBytes,
                                       const PartitionManager::SupportedSizeRange& range, const QString& error)
{
    resetResizeState();

    // Kết quả chỉ có nghĩa cho ĐÚNG phân vùng đã hỏi - nếu dòng đang chọn không còn là nó thì bỏ.
    const auto* p = selectedPartition();
    if (!p || p->diskNumber != disk || p->partitionNumber != partition || p->sizeBytes != sizeBytes)
        return;

    if (!range.ok)
    {
        m_warningLabel->setText("⚠ Không tra được kích thước cho phép" + (error.isEmpty() ? "" : (": " + error)) +
                                (PartitionManager::isElevated() ? "" : " (thường cần quyền Administrator)."));
        m_warningLabel->setVisible(true);
        return;
    }

    m_supportedRange = range;
    m_queriedDisk = disk;
    m_queriedPartition = partition;
    m_queriedSizeBytes = sizeBytes;
    m_queriedStartBytes = p->offsetBytes;

    QString warn = QString("Windows cho phép đổi kích thước phân vùng này trong khoảng %1 - %2.")
                       .arg(DiskUi::formatBytes(m_supportedRange.minBytes), DiskUi::formatBytes(m_supportedRange.maxBytes));
    if (p->isBoot || p->isSystem)
        warn += " ⚠ Đây là phân vùng khởi động/hệ thống - cực kỳ cẩn trọng khi đổi kích thước.";
    m_warningLabel->setText(warn);
    m_warningLabel->setVisible(true);

    // Bật tay nắm kéo trên sơ đồ cho đúng phân vùng này rồi áp hướng đã khóa lên khoảng giá trị của ô nhập.
    m_diskMap->setResizeContext(disk, partition, m_supportedRange, m_direction);
    applyDirectionBounds(true);
    m_directionRow->setVisible(true);
    m_newSizeSpin->setVisible(true);
    m_sizeLabel->setVisible(true);
    m_changeLabel->setVisible(true);
    m_edgeNoteLabel->setVisible(true);
    updateMoveSupport();
    updateChangePreview();

    m_confirmHintLabel->setText(QString("Gõ \"%1\" vào ô dưới để xác nhận:").arg(confirmToken(*p)));
    m_confirmHintLabel->setVisible(true);
    m_confirmEdit->clear();
    m_confirmEdit->setPlaceholderText(confirmToken(*p));
    m_confirmEdit->setVisible(true);
    m_resizeBtn->setVisible(true);
    m_resizeBtn->setEnabled(false);
}

void PartitionTab::applySupportedRangeForTest(const PartitionManager::SupportedSizeRange& range)
{
    if (const auto* p = selectedPartition())
        applySupportedRange(p->diskNumber, p->partitionNumber, p->sizeBytes, range, QString());
}

QString PartitionTab::selectedDisplayName() const
{
    const auto* p = selectedPartition();
    if (!p)
        return {};
    return p->driveLetter.isEmpty() ? confirmToken(*p) : (p->driveLetter + ":");
}

qint64 PartitionTab::effectiveNewSizeBytes(const PartitionManager::PartitionInfo& p) const
{
    // Ô nhập làm tròn 2 chữ số thập phân GB: giá trị CHƯA được sửa phải được coi đúng là kích thước hiện tại (không
    // phải một lần đổi vài MB do làm tròn) - cùng quy tắc isSameSizeAtInputPrecision mà nút "Đổi kích thước" dùng.
    if (m_diskMap->isMoveEnabled() && m_diskMap->isResizeActive() && p.offsetBytes >= 0 && m_diskMap->previewStart() != p.offsetBytes)
        return m_diskMap->previewSize(); // đã dời mép trái: kích thước CHÍNH XÁC (byte) lấy từ sơ đồ, không qua ô nhập làm tròn/kẹp theo mức Windows
    const double value = m_newSizeSpin->value();
    if (PartitionManager::internal::isSameSizeAtInputPrecision(value, p.sizeBytes))
        return p.sizeBytes;
    return PartitionManager::internal::clampResizeBytes(value, m_supportedRange);
}

DiskLayout::Geometry PartitionTab::plannedGeometry() const
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok || m_queriedDisk < 0)
        return {-1, -1};
    if (m_diskMap->isMoveEnabled() && m_diskMap->isResizeActive() && p->offsetBytes >= 0 && m_diskMap->previewStart() != p->offsetBytes)
        return {m_diskMap->previewStart(), m_diskMap->previewSize()};
    return {p->offsetBytes, effectiveNewSizeBytes(*p)};
}

DiskLayout::ChangePlan PartitionTab::currentPlan() const
{
    DiskLayout::ChangePlan none;
    const auto* p = selectedPartition();
    const auto g = plannedGeometry();
    if (!p || g.size < 0 || p->offsetBytes < 0)
    {
        none.blockReason = "Chưa chọn/tra phân vùng.";
        return none;
    }
    const auto rows = DiskLayout::build(m_disks, m_partitions);
    int index = -1;
    for (int i = 0; i < m_partitions.size(); ++i)
        if (m_partitions[i].diskNumber == p->diskNumber && m_partitions[i].partitionNumber == p->partitionNumber)
            index = i;
    for (const auto& row : rows)
        if (row.disk.number == p->diskNumber)
            return DiskLayout::planChange(row.segments, index, g.start, g.size, m_supportedRange.minBytes, row.disk.sizeBytes);
    none.blockReason = "Không dựng được sơ đồ đĩa.";
    return none;
}

QString PartitionTab::moveSupportReason() const
{
    const auto* p = selectedPartition();
    if (!p)
        return "chưa chọn phân vùng";
    if (m_moveForcedForTest)
        return {};
    if (!m_experimentalCheck || !m_experimentalCheck->isChecked())
        return "tính năng đang ở chế độ THỬ NGHIỆM - tích ô \"Bật dời phân vùng / kéo mép trái (THỬ NGHIỆM)\" ở đầu tab để dùng";
    if (!PartitionManager::isElevated())
        return "cần chạy ứng dụng với quyền Administrator";
    if (m_pendingJournal)
        return "đang có một lần dời dở dang - hãy tiếp tục hoặc hủy bỏ nó trước";
    return PartitionMover::notMovableReason(*p);
}

bool PartitionTab::needsRelocation() const
{
    const auto* p = selectedPartition();
    return p && !PartitionMover::runningFromDriveReason(p->driveLetter).isEmpty();
}

bool PartitionTab::startRelocated(const PartitionRelocator::Plan& plan)
{
    // Thoát ứng dụng sau khi chạy bản sao: chặn nếu tool khác đang làm việc không an toàn để buộc dừng (cùng lớp chặn với "chạy lại với quyền Quản trị").
    QString busyReason;
    if (ToolManager::instance().anyToolWindowBusy(&busyReason))
    {
        QMessageBox::warning(this, "Không thể chạy lại lúc này",
                             "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason + ") - hãy đợi xong rồi thử lại.");
        return false;
    }
    const QString runtimeDir = PartitionRelocator::newRuntimeDir();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_statusLabel->setText("⏳ Đang chép bộ chạy tối thiểu sang thư mục tạm...");
    QApplication::processEvents();
    qint64 bytes = 0;
    QString error;
    const bool copied = PartitionRelocator::copyRuntime(QCoreApplication::applicationDirPath(), runtimeDir, &error, &bytes);
    QApplication::restoreOverrideCursor();
    if (!copied)
    {
        QMessageBox::critical(this, "Không chép được bộ chạy tạm", error + "\n\nChưa có gì bị thay đổi.");
        return false;
    }
    const QString runnerExe = runtimeDir + "/" + QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    if (!PartitionRelocator::launchElevatedRunner(runnerExe, runtimeDir, plan, &error))
    {
        QDir(runtimeDir).removeRecursively();
        QMessageBox::warning(this, "Không khởi chạy được", error + "\n\nChưa có gì bị thay đổi.");
        return false;
    }
    m_statusLabel->setText("Đã khởi chạy bản sao tạm - ứng dụng này sẽ đóng để nhả ổ...");
    QApplication::processEvents();
    qApp->quit();
    return true;
}

void PartitionTab::updateMoveSupport()
{
    const auto* p = selectedPartition();
    const QString why = moveSupportReason();
    const bool ok = p && why.isEmpty() && m_supportedRange.ok && m_queriedDisk >= 0 && p->offsetBytes >= 0;
    m_diskMap->setMoveEnabled(ok, why);
    if (!ok && p && m_diskMap->isResizeActive() && m_diskMap->previewStart() != m_diskMap->currentStart())
    {
        m_diskMap->setPreviewGeometry(m_diskMap->currentStart(), m_diskMap->currentSize()); // tắt dời giữa lúc đã kéo: về hình học hiện tại
        QSignalBlocker blocker(m_newSizeSpin);
        m_newSizeSpin->setValue(static_cast<double>(m_diskMap->previewSize()) / GB);
    }
    m_startLabel->setVisible(ok);
    m_startSpin->setVisible(ok);
    if (ok)
    {
        QSignalBlocker blocker(m_startSpin);
        m_startSpin->setValue(static_cast<double>(m_diskMap->previewStart()) / GB);
        m_edgeNoteLabel->setText(
            "ℹ Phân vùng này DỜI được: kéo MÉP TRÁI (sang trái = mở rộng về bên trái, sang phải = thu nhỏ từ bên trái) hoặc kéo CẢ KHỐI trên sơ đồ "
            "(kể cả sang một vùng chưa phân bổ khác để đổi thứ tự). Windows không làm được việc này nên ứng dụng tự CHÉP dữ liệu sang vị trí mới - "
            "chậm (phụ thuộc tốc độ đĩa), ổ bị khóa trong lúc chép, có nhật ký để chạy tiếp nếu mất điện. HÃY SAO LƯU trước." +
            QString(needsRelocation() ? "\n⚙ Ứng dụng đang chạy từ chính ổ này: khi bấm Áp dụng, ứng dụng sẽ TỰ chép mình sang thư mục tạm và khởi động lại ở đó (một lần UAC) rồi đóng, "
                                        "để ổ tháo được - không cần tự chép sang ổ khác. Cửa sổ Explorer/Terminal/VS Code đang mở ở ổ này vẫn phải đóng trước."
                                      : QString()));
    }
    else
    {
        m_edgeNoteLabel->setText("ℹ Windows chỉ đổi kích thước bằng MÉP PHẢI. Không kéo mép trái/dời được phân vùng này: " + why + ".");
    }
}

void PartitionTab::applyDirectionBounds(bool resetToCurrent)
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok)
        return;
    auto bounds = DiskLayout::boundsForDirection(p->sizeBytes, m_supportedRange, m_direction);
    if (m_diskMap->isMoveEnabled() && m_direction != DiskLayout::ResizeDirection::ShrinkOnly)
    {
        // Dời được: kích thước tối đa là cả khe (chỗ cũ + vùng trống liền kề hai bên), không chỉ mức Windows báo cho vị trí hiện tại.
        const DiskLayout::Span sp = m_diskMap->targetSpan();
        bounds.maxBytes = std::max(bounds.maxBytes, sp.hi - sp.lo);
    }
    {
        // Đổi khoảng/giá trị ô nhập ở đây là cập nhật theo HƯỚNG, không phải người dùng gõ: chặn tín hiệu rồi tự đồng bộ bên dưới.
        QSignalBlocker blocker(m_newSizeSpin);
        m_newSizeSpin->setRange(bounds.minBytes / GB, bounds.maxBytes / GB);
        const double base = resetToCurrent ? p->sizeBytes / GB : m_newSizeSpin->value();
        m_newSizeSpin->setValue(qBound(m_newSizeSpin->minimum(), base, m_newSizeSpin->maximum()));
    }
    m_dirShrinkBtn->setChecked(m_direction == DiskLayout::ResizeDirection::ShrinkOnly);
    m_dirFreeBtn->setChecked(m_direction == DiskLayout::ResizeDirection::Free);
    m_dirGrowBtn->setChecked(m_direction == DiskLayout::ResizeDirection::GrowOnly);
    m_diskMap->setDirection(m_direction);
    m_diskMap->setPreviewSize(effectiveNewSizeBytes(*p));
}

void PartitionTab::updateChangePreview()
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok || m_queriedDisk < 0)
    {
        m_changeLabel->clear();
        return;
    }
    const auto g = plannedGeometry();
    if (m_diskMap->isMoveEnabled() && p->offsetBytes >= 0 && g.start != p->offsetBytes)
    {
        const auto plan = currentPlan();
        QString planText = DiskLayout::describePlan(selectedDisplayName(), plan);
        const QString dirReason = DiskLayout::directionBlockReason(m_newSizeSpin->value(), p->sizeBytes, m_direction);
        if (!dirReason.isEmpty())
            planText += "\n⛔ " + dirReason;
        m_changeLabel->setText(planText);
        return;
    }
    QString text = DiskLayout::describeResize(selectedDisplayName(), p->sizeBytes, effectiveNewSizeBytes(*p),
                                              m_diskMap->freeBytesAfterResizeTarget(), m_diskMap->freeBytesBeforeResizeTarget());
    const QString directionReason = DiskLayout::directionBlockReason(m_newSizeSpin->value(), p->sizeBytes, m_direction);
    if (!directionReason.isEmpty())
        text += "\n⛔ " + directionReason;
    m_changeLabel->setText(text);
}

void PartitionTab::onMapPartitionClicked(int diskNumber, int partitionNumber)
{
    if (m_loading || isResizingNow())
        return;
    for (int i = 0; i < m_partitions.size(); ++i)
    {
        if (m_partitions[i].diskNumber == diskNumber && m_partitions[i].partitionNumber == partitionNumber)
        {
            m_table->selectRow(i); // đã chọn đúng dòng này thì không có tín hiệu -> giữ nguyên kết quả tra kích thước
            return;
        }
    }
}

void PartitionTab::onMapPreviewChanged(qint64 bytes)
{
    // Người dùng kéo tay nắm/bấm phím trên sơ đồ -> đưa vào ô nhập (ô làm tròn 0,01 GB rồi tự đồng bộ ngược sang sơ đồ).
    m_newSizeSpin->setValue(static_cast<double>(bytes) / GB);
}

void PartitionTab::onMapGeometryChanged(qint64 startBytes, qint64 sizeBytes)
{
    const auto* p = selectedPartition();
    if (!p || p->offsetBytes < 0 || startBytes == p->offsetBytes)
    {
        // Chưa dời mép trái: đúng đường cũ (chỉ kích thước) - ô nhập làm tròn rồi tự đồng bộ ngược sang sơ đồ.
        if (p && m_startSpin->isVisible())
        {
            QSignalBlocker blocker(m_startSpin);
            m_startSpin->setValue(static_cast<double>(startBytes) / GB);
        }
        onMapPreviewChanged(sizeBytes);
        return;
    }
    // Đã dời: giữ NGUYÊN số byte của sơ đồ (không cho ô nhập làm tròn/kẹp rồi ghi ngược).
    {
        QSignalBlocker b1(m_newSizeSpin);
        QSignalBlocker b2(m_startSpin);
        m_newSizeSpin->setValue(static_cast<double>(sizeBytes) / GB);
        m_startSpin->setValue(static_cast<double>(startBytes) / GB);
    }
    updateChangePreview();
    m_resizeBtn->setEnabled(!m_loading && !isResizingNow() && resizeRequestBlockReason().isEmpty());
}

void PartitionTab::onNewStartChanged(double startGb)
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok || m_queriedDisk < 0 || p->offsetBytes < 0 || !m_diskMap->isMoveEnabled())
        return;
    constexpr qint64 kMiBBytes = 1024LL * 1024;
    qint64 start = static_cast<qint64>(std::llround(startGb * GB / static_cast<double>(kMiBBytes))) * kMiBBytes;
    if (PartitionManager::internal::isSameSizeAtInputPrecision(startGb, p->offsetBytes))
        start = p->offsetBytes; // chưa sửa gì ở độ chính xác của ô nhập
    // Giữ nguyên KÍCH THƯỚC xem trước hiện tại (ô này chỉ dời); sơ đồ tự kiểm tra hình học (không chồng, đủ chỗ...) và bỏ nếu sai.
    m_diskMap->setPreviewGeometry(start, m_diskMap->previewSize());
    {
        QSignalBlocker blocker(m_startSpin);
        m_startSpin->setValue(static_cast<double>(m_diskMap->previewStart()) / GB); // đưa về giá trị sơ đồ thật sự chấp nhận
    }
    updateChangePreview();
    m_resizeBtn->setEnabled(!m_loading && !isResizingNow() && resizeRequestBlockReason().isEmpty());
}

void PartitionTab::onNewSizeChanged(double sizeGb)
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok || m_queriedDisk < 0)
        return;
    if (m_diskMap->isMoveEnabled() && p->offsetBytes >= 0 && m_diskMap->previewStart() != p->offsetBytes)
    {
        // Đã dời mép trái: đổi kích thước giữ NGUYÊN vị trí bắt đầu mới; sơ đồ kiểm tra hình học và bỏ nếu không hợp lệ.
        constexpr qint64 kMiBBytes = 1024LL * 1024;
        const qint64 bytes = static_cast<qint64>(std::llround(sizeGb * GB / static_cast<double>(kMiBBytes))) * kMiBBytes;
        m_diskMap->setPreviewGeometry(m_diskMap->previewStart(), bytes);
        QSignalBlocker blocker(m_newSizeSpin);
        m_newSizeSpin->setValue(static_cast<double>(m_diskMap->previewSize()) / GB);
    }
    else
        m_diskMap->setPreviewSize(effectiveNewSizeBytes(*p));
    updateChangePreview();
    // Nút "Đổi kích thước" phải đánh giá lại: hướng đã khóa là một điều kiện chặn (xem resizeRequestBlockReason).
    m_resizeBtn->setEnabled(!m_loading && !isResizingNow() && resizeRequestBlockReason().isEmpty());
}

void PartitionTab::onDirectionButtonClicked(DiskLayout::ResizeDirection direction)
{
    m_direction = direction;
    const auto* p = selectedPartition();
    if (p && m_supportedRange.ok && m_queriedDisk >= 0)
    {
        applyDirectionBounds(false);
        updateChangePreview();
        m_resizeBtn->setEnabled(!m_loading && !isResizingNow() && resizeRequestBlockReason().isEmpty());
    }
    else
    {
        // Chưa tra kích thước: chỉ ghi nhớ lựa chọn (và giữ trạng thái chọn của nút nhất quán).
        m_dirShrinkBtn->setChecked(direction == DiskLayout::ResizeDirection::ShrinkOnly);
        m_dirFreeBtn->setChecked(direction == DiskLayout::ResizeDirection::Free);
        m_dirGrowBtn->setChecked(direction == DiskLayout::ResizeDirection::GrowOnly);
    }
}

QString PartitionTab::resizeRequestBlockReason() const
{
    const auto* p = selectedPartition();
    if (!p)
        return "Chưa chọn phân vùng nào.";
    if (!m_supportedRange.ok || m_queriedDisk < 0)
        return "Chưa tra kích thước cho phép của phân vùng đang chọn.";
    // Phân vùng đang chọn PHẢI là đúng phân vùng đã tra (cùng số đĩa, số phân vùng, kích thước) - khoảng
    // kích thước, giá trị trong ô nhập và chuỗi đã gõ xác nhận đều thuộc về phân vùng đã tra.
    if (p->diskNumber != m_queriedDisk || p->partitionNumber != m_queriedPartition || p->sizeBytes != m_queriedSizeBytes)
        return "Phân vùng đang chọn không phải phân vùng đã tra kích thước - hãy tra lại cho phân vùng này.";
    if (m_confirmEdit->text() != confirmToken(*p))
        return QString("Chưa gõ đúng \"%1\" để xác nhận.").arg(confirmToken(*p));
    if (m_pendingJournal)
        return "Có một lần dời dở dang (xem banner đầu tab) - tiếp tục hoặc hủy bỏ nó trước khi làm việc khác.";
    if (m_diskMap->isMoveEnabled() && p->offsetBytes >= 0 && m_diskMap->previewStart() != p->offsetBytes)
    {
        if (p->offsetBytes != m_queriedStartBytes)
            return "Vị trí phân vùng đã đổi kể từ lúc tra - hãy tra lại.";
        if (const QString why = moveSupportReason(); !why.isEmpty())
            return "Không dời được phân vùng này: " + why + ".";
        const auto plan = currentPlan();
        if (!plan.blockReason.isEmpty())
            return plan.blockReason;
    }
    // Hướng đã khóa (chỉ thu nhỏ/chỉ mở rộng) là lớp chặn cuối cho trường hợp kích thước nhập trái hướng.
    return DiskLayout::directionBlockReason(m_newSizeSpin->value(), p->sizeBytes, m_direction);
}

void PartitionTab::onConfirmTextChanged(const QString&)
{
    m_resizeBtn->setEnabled(!m_loading && resizeRequestBlockReason().isEmpty());
}

void PartitionTab::onResizeClicked()
{
    if (m_loading || isResizingNow())
        return;

    const QString blockReason = resizeRequestBlockReason();
    if (!blockReason.isEmpty())
    {
        QMessageBox::warning(this, "Chưa thể đổi kích thước", blockReason);
        const auto* stale = selectedPartition();
        if (stale && (stale->diskNumber != m_queriedDisk || stale->partitionNumber != m_queriedPartition))
            resetResizeState();
        else
            m_resizeBtn->setEnabled(false);
        return;
    }
    const auto* p = selectedPartition();

    if (!PartitionManager::isElevated())
    {
        QMessageBox::warning(this, "Cần quyền Administrator",
            "Đổi kích thước phân vùng cần quyền Administrator - hãy bấm \"Chạy lại với quyền Quản trị\" rồi thử lại.");
        return;
    }

    // Tránh đổi kích thước trong lúc một tab KHÁC (Tìm tệp lớn/Tìm tệp trùng lặp/Dọn dẹp) đang đọc/ghi
    // trên cùng ổ đĩa - Resize-Partition cần di chuyển dữ liệu hệ thống tệp, I/O đồng thời từ chính ứng
    // dụng này có thể làm chậm hoặc khiến lệnh resize thất bại giữa chừng (xem DiskCleanupWindow::
    // isAnyOtherTabBusy()).
    if (auto* win = qobject_cast<DiskCleanupWindow*>(window()))
    {
        if (win->isAnyOtherTabBusy(this))
        {
            QMessageBox::warning(this, "Đang có thao tác khác",
                "Một tab khác trong Disk Cleanup đang quét/dọn dẹp - vui lòng đợi xong để tránh xung đột "
                "trên cùng ổ đĩa trong lúc đổi kích thước, rồi thử lại.");
            return;
        }
    }

    // ---- Dời / đổi mép trái: kế hoạch nhiều bước (Windows lo mép phải, ứng dụng chép dữ liệu) ----
    if (m_diskMap->isMoveEnabled() && p->offsetBytes >= 0 && m_diskMap->previewStart() != p->offsetBytes)
    {
        const auto plan = currentPlan();
        if (!plan.blockReason.isEmpty() || !plan.needsMove())
        {
            QMessageBox::warning(this, "Chưa thể dời", plan.blockReason.isEmpty() ? QString("Không có gì để dời.") : plan.blockReason);
            return;
        }
        const QString moveName = p->driveLetter.isEmpty() ? confirmToken(*p) : (p->driveLetter + ":");
        const double copyGb = static_cast<double>(plan.copyBytes()) / GB;
        const auto answer = QMessageBox::warning(
            this, "Xác nhận dời phân vùng",
            QString("Sắp DỜI/đổi mép trái ổ %1 (đĩa %2, phân vùng %3).\n\n%4\n\n"
                    "• Ứng dụng sẽ CHÉP %5 GB dữ liệu của ổ này sang vị trí mới - có thể mất vài phút tới hàng chục phút.\n"
                    "• Trong lúc chép, ổ %1 bị KHÓA và THÁO (các chương trình đang mở tệp trên ổ sẽ báo lỗi) - hãy đóng chúng trước. Nếu không khóa được, thao tác bị từ chối trước khi ghi gì.\n"
                    "%6"
                    "• KHÔNG tắt máy/rút nguồn/rút cáp. Nếu lỡ mất điện: mở lại ứng dụng, tab này sẽ hiện nút \"Tiếp tục\" theo nhật ký phục hồi; "
                    "TRONG LÚC DANG DỞ KHÔNG định dạng hay dùng ổ này.\n"
                    "• Đây là thao tác ghi thẳng xuống đĩa. Hãy SAO LƯU dữ liệu quan trọng trước. Chưa được kiểm chứng trên mọi loại đĩa.\n\n"
                    "Tiếp tục?")
                .arg(moveName)
                .arg(p->diskNumber)
                .arg(p->partitionNumber)
                .arg(DiskLayout::describePlan(moveName, plan))
                .arg(QString::number(copyGb, 'f', 2))
                .arg(needsRelocation() ? QString("• Ứng dụng đang chạy từ chính ổ %1: nên sẽ TỰ chép mình sang thư mục tạm, khởi động lại ở đó với quyền Administrator (một lần UAC) và ĐÓNG cửa sổ này "
                                                 "để nhả ổ; việc dời chạy trong cửa sổ mới, xong thì có nút \"Mở lại One for ALL\".\n").arg(moveName)
                                       : QString()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
        p = selectedPartition();
        if (!p || !resizeRequestBlockReason().isEmpty() || m_loading || isResizingNow())
            return;
        if (needsRelocation())
        {
            PartitionRelocator::Plan rp;
            rp.disk = p->diskNumber;
            rp.partition = p->partitionNumber;
            rp.letter = p->driveLetter;
            rp.steps = plan.steps;
            rp.originalExe = QCoreApplication::applicationFilePath();
            rp.description = QString("Dời/đổi mép trái ổ %1 (đĩa %2, phân vùng %3)\n%4").arg(moveName).arg(p->diskNumber).arg(p->partitionNumber).arg(DiskLayout::describePlan(moveName, plan));
            startRelocated(rp);
            return;
        }
        startPlan(plan, moveName, p->diskNumber, p->partitionNumber, p->driveLetter);
        return;
    }

    // Kẹp theo BYTE vào đúng khoảng Windows trả về (ô nhập làm tròn 2 chữ số thập phân GB).
    const qint64 newSizeBytes = PartitionManager::internal::clampResizeBytes(m_newSizeSpin->value(), m_supportedRange);
    // So cả theo độ chính xác của ô nhập: giá trị CHƯA được sửa (kích thước hiện tại đã làm tròn 2 chữ
    // số) đổi ra byte lệch vài MB - không được biến thành một lệnh Resize-Partition thật.
    if (newSizeBytes == p->sizeBytes ||
        PartitionManager::internal::isSameSizeAtInputPrecision(m_newSizeSpin->value(), p->sizeBytes))
    {
        QMessageBox::information(this, "Đổi kích thước", "Kích thước mới bằng kích thước hiện tại - không có gì để đổi.");
        return;
    }
    const QString name = p->driveLetter.isEmpty() ? confirmToken(*p) : (p->driveLetter + ":");
    // Nói rõ thu nhỏ hay mở rộng, vùng giải phóng/lấy thêm nằm phía nào - để người dùng xác nhận ĐÚNG điều sắp xảy ra.
    const QString change = DiskLayout::describeResize(name, p->sizeBytes, newSizeBytes, m_diskMap->freeBytesAfterResizeTarget(),
                                                      m_diskMap->freeBytesBeforeResizeTarget());

    const auto answer = QMessageBox::warning(
        this, "Xác nhận đổi kích thước phân vùng",
        QString("Sắp đổi kích thước ổ %1 (đĩa %4, phân vùng %5) từ %2 thành %3.\n\n"
                "%6\n"
                "Hướng đã chọn: %7.\n\n"
                "Đây là thao tác đĩa THẬT, không có \"hoàn tác\" dễ dàng nếu có sự cố giữa chừng. "
                "Hãy chắc chắn bạn đã sao lưu dữ liệu quan trọng.\n\n"
                "Tiếp tục?")
            .arg(name, DiskUi::formatBytes(p->sizeBytes), DiskUi::formatBytes(newSizeBytes))
            .arg(p->diskNumber)
            .arg(p->partitionNumber)
            .arg(change, DiskLayout::directionName(m_direction)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    // Hộp thoại vừa rồi chạy vòng lặp sự kiện riêng - kiểm tra lại lần nữa rằng không có gì đổi trong lúc đó.
    p = selectedPartition();
    if (!resizeRequestBlockReason().isEmpty() || !p || m_loading || isResizingNow())
        return;

    m_refreshBtn->setEnabled(false);
    m_table->setEnabled(false);
    m_diskMap->setEnabled(false);
    m_directionRow->setEnabled(false);
    m_queryBtn->setEnabled(false);
    m_resizeBtn->setEnabled(false);
    m_confirmEdit->setEnabled(false);
    m_newSizeSpin->setEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang đổi kích thước - KHÔNG tắt máy/rút nguồn...");

    // PartitionResizer tự đọc lại danh sách phân vùng + khoảng cho phép từ Windows ngay trước khi chạy
    // và dừng nếu có sai khác so với m_queriedSizeBytes (xem PartitionResizer.h).
    m_resizer->setTarget(p->diskNumber, p->partitionNumber, newSizeBytes, m_queriedSizeBytes);
    m_resizer->start();
}

void PartitionTab::onResizeFinished(bool success, QString error)
{
    m_refreshBtn->setEnabled(true);
    m_table->setEnabled(true);
    m_diskMap->setEnabled(true);
    m_directionRow->setEnabled(true);
    m_queryBtn->setEnabled(true);
    m_confirmEdit->setEnabled(true);
    m_newSizeSpin->setEnabled(true);
    m_progressBar->setVisible(false);

    if (success)
    {
        m_statusAfterReload = "✓ Đã đổi kích thước thành công.";
        QMessageBox::information(this, "Hoàn tất", "Đã đổi kích thước phân vùng thành công.");
    }
    else
    {
        m_statusAfterReload = "⚠ Đổi kích thước thất bại: " + error;
        QMessageBox::critical(this, "Lỗi", "Không đổi được kích thước:\n" + error);
    }
    reloadPartitions(); // làm mới danh sách + ẩn khu vực đổi kích thước, dù thành công hay thất bại
}

// ------------------------------------------------------------------------------------------------ dời phân vùng

void PartitionTab::setOperationUi(bool running)
{
    m_refreshBtn->setEnabled(!running);
    m_table->setEnabled(!running);
    m_diskMap->setEnabled(!running);
    m_directionRow->setEnabled(!running);
    m_queryBtn->setEnabled(!running);
    m_confirmEdit->setEnabled(!running);
    m_newSizeSpin->setEnabled(!running);
    m_startSpin->setEnabled(!running);
    if (running)
        m_resizeBtn->setEnabled(false);
    m_resumeBtn->setEnabled(!running);
    m_abandonBtn->setEnabled(!running);
    m_progressBar->setVisible(running);
    m_pauseBtn->setVisible(running);
    m_pauseBtn->setEnabled(running);
    m_progressLabel->setVisible(running || !m_progressLabel->text().isEmpty());
}

void PartitionTab::startPlan(const DiskLayout::ChangePlan& plan, const QString& name, int disk, int partition, const QString& letter)
{
    m_planName = name;
    m_planLetter = letter;
    m_planActive = true;
    m_copyTimer.invalidate();
    m_copyBaseBytes = 0;
    m_progressBar->setRange(0, 0);
    m_progressLabel->clear();
    setOperationUi(true);
    m_pauseBtn->setVisible(plan.needsMove());
    m_statusLabel->setText("⏳ Đang đổi vị trí/kích thước - KHÔNG tắt máy/rút nguồn/rút cáp...");
    m_plan->setPlan(disk, partition, plan.steps, letter);
    m_plan->start();
}

void PartitionTab::onPlanProgress(int stepIndex, int stepCount, qint64 doneBytes, qint64 totalBytes, QString phase)
{
    if (totalBytes <= 0)
    {
        m_progressBar->setRange(0, 0);
        m_progressLabel->setText(phase);
        m_copyTimer.invalidate();
        return;
    }
    if (!m_copyTimer.isValid())
    {
        m_copyTimer.start();
        m_copyBaseBytes = doneBytes;
    }
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(static_cast<int>(1000.0 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes)));
    QString text = QString("Bước %1/%2 · %3 · %4 / %5 (%6%)")
                       .arg(stepIndex + 1)
                       .arg(stepCount)
                       .arg(phase, DiskUi::formatBytes(doneBytes), DiskUi::formatBytes(totalBytes))
                       .arg(QString::number(100.0 * static_cast<double>(doneBytes) / static_cast<double>(totalBytes), 'f', 1));
    const qint64 ms = m_copyTimer.elapsed();
    if (ms > 3000 && doneBytes > m_copyBaseBytes)
    {
        const double rate = static_cast<double>(doneBytes - m_copyBaseBytes) / (static_cast<double>(ms) / 1000.0);
        const double left = static_cast<double>(totalBytes - doneBytes) / rate;
        text += QString(" · %1/giây · còn khoảng %2 phút").arg(DiskUi::formatBytes(static_cast<qint64>(rate)), QString::number(std::max(1.0, left / 60.0), 'f', 0));
    }
    m_progressLabel->setText(text);
}

void PartitionTab::onPlanFinished(bool ok, bool resumable, QString message, int failedStep)
{
    m_planActive = false;
    setOperationUi(false);
    m_progressBar->setVisible(false);
    m_pauseBtn->setVisible(false);
    m_progressLabel->setText(message);
    m_progressLabel->setVisible(true);
    checkPendingJournal();
    if (ok)
    {
        m_statusAfterReload = "✓ " + message;
        QMessageBox::information(this, "Hoàn tất", message);
    }
    else
    {
        m_statusAfterReload = QString("⚠ %1%2").arg(failedStep >= 0 ? QString("(bước %1) ").arg(failedStep + 1) : (failedStep == -2 ? QString("(kiểm tra trước) ") : QString()), message.left(300));
        if (resumable)
            QMessageBox::warning(this, "Đã dừng giữa chừng", message);
        else
            QMessageBox::critical(this, "Lỗi", message);
    }
    reloadPartitions();
}

void PartitionTab::onPauseClicked()
{
    m_pauseBtn->setEnabled(false);
    m_progressLabel->setText("Đang dừng ở ranh giới an toàn tiếp theo...");
    if (m_plan)
        m_plan->requestPause();
    if (m_resumeWorker)
        m_resumeWorker->requestPause();
}

void PartitionTab::checkPendingJournal()
{
    PartitionMove::JournalState j;
    QString err;
    const bool have = PartitionMover::loadJournal(&j, &err);
    m_pendingJournal = have || !err.isEmpty();
    m_pendingBanner->setVisible(m_pendingJournal);
    m_resumeBtn->setVisible(have);
    m_abandonBtn->setVisible(have && PartitionMover::canAbandon(j));
    if (have)
    {
        m_pendingBanner->setText("⚠ CÓ MỘT LẦN DỜI PHÂN VÙNG DỞ DANG (có thể do mất điện/đóng giữa chừng): " + PartitionMover::describeJournal(j) +
                                 "\n KHÔNG định dạng hay dùng ổ này cho tới khi bấm \"Tiếp tục\" (cần quyền Administrator) để hoàn tất"
                                 + (PartitionMover::canAbandon(j) ? ", hoặc \"Hủy bỏ\" (an toàn: dữ liệu gốc còn nguyên)." : ". Dữ liệu gốc đã bị ghi đè một phần nên CHỈ có thể tiếp tục."));
    }
    else if (!err.isEmpty())
    {
        m_pendingBanner->setText("⚠ Tệp nhật ký dời phân vùng bị hỏng nên không dùng được: " + err + "\n(" + QDir::toNativeSeparators(PartitionMover::journalPath()) +
                                 ") - KHÔNG xóa/sửa tệp này; nhờ người có kinh nghiệm xem xét trước khi dùng ổ liên quan.");
    }
}

void PartitionTab::onResumeClicked()
{
    PartitionMove::JournalState j;
    QString err;
    if (!PartitionMover::loadJournal(&j, &err))
    {
        QMessageBox::warning(this, "Tiếp tục", "Không đọc được nhật ký: " + err);
        checkPendingJournal();
        return;
    }
    if (!PartitionManager::isElevated())
    {
        QMessageBox::warning(this, "Cần quyền Administrator", "Hãy bấm \"Chạy lại với quyền Quản trị\" rồi thử lại.");
        return;
    }
    if (isResizingNow() || m_loading)
        return;
    const auto answer = QMessageBox::question(this, "Tiếp tục lần dời dở dang",
                                              QString("%1\n\nTiếp tục chép từ chỗ đã dừng? KHÔNG tắt máy/rút nguồn trong lúc chạy.").arg(PartitionMover::describeJournal(j)),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    QString letter;
    for (const auto& pp : m_partitions)
        if (pp.diskNumber == j.diskNumber && pp.partitionNumber == j.partitionNumber)
            letter = pp.driveLetter;
    if (!PartitionMover::runningFromDriveReason(letter).isEmpty())
    {
        PartitionRelocator::Plan rp;
        rp.resume = true;
        rp.disk = j.diskNumber;
        rp.partition = j.partitionNumber;
        rp.letter = letter;
        rp.originalExe = QCoreApplication::applicationFilePath();
        rp.description = "Chạy tiếp lần dời dở dang\n" + PartitionMover::describeJournal(j);
        startRelocated(rp);
        return;
    }
    m_planActive = true;
    m_copyTimer.invalidate();
    m_progressBar->setRange(0, 0);
    m_progressLabel->clear();
    setOperationUi(true);
    m_statusLabel->setText("⏳ Đang chạy tiếp lần dời - KHÔNG tắt máy/rút nguồn/rút cáp...");
    m_resumeWorker->setResume(j, letter);
    m_resumeWorker->start();
}

void PartitionTab::onAbandonClicked()
{
    PartitionMove::JournalState j;
    if (!PartitionMover::loadJournal(&j) || !PartitionMover::canAbandon(j))
    {
        QMessageBox::warning(this, "Hủy bỏ", "Lần dời này không hủy bỏ an toàn được (dữ liệu gốc đã bị ghi đè một phần) - chỉ có thể \"Tiếp tục\".");
        checkPendingJournal();
        return;
    }
    const auto answer = QMessageBox::question(this, "Hủy bỏ lần dời",
                                              QString("%1\n\nHủy bỏ: xóa nhật ký, ổ giữ nguyên ở vị trí cũ (dữ liệu gốc còn nguyên). Phần đã chép sang vị trí mới chỉ là bản nháp và sẽ bị bỏ.\n\nHủy bỏ?")
                                                  .arg(PartitionMover::describeJournal(j)),
                                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;
    PartitionMover::discardJournal();
    checkPendingJournal();
    m_statusLabel->setText("Đã hủy bỏ lần dời dở dang.");
}
