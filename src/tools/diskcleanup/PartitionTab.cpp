#include "PartitionTab.h"

#include "DiskCleanupWindow.h"
#include "DiskMapWidget.h"
#include "DiskUiStyle.h"
#include "engine/PartitionResizer.h"
#include "core/ToolManager.h"

#include <QApplication>
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

    updateElevationBanner();
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
    // Luồng nền đang hỏi Windows (chỉ đọc, tối đa ~20 giây) cũng phải thoát hẳn trước khi bị hủy.
    if (m_queryThread)
        m_queryThread->wait();
}

bool PartitionTab::isResizingNow() const
{
    return m_resizer && m_resizer->isRunning();
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

    auto* dangerBanner = new QLabel(
        "⚠ Đổi kích thước phân vùng là thao tác đĩa THẬT. Hãy sao lưu dữ liệu quan trọng trước khi "
        "tiếp tục, và không tắt máy/rút nguồn trong lúc đang xử lý.",
        this);
    dangerBanner->setWordWrap(true);
    dangerBanner->setStyleSheet(DiskUi::bannerStyle("danger"));
    root->addWidget(dangerBanner);

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
    connect(m_diskMap, &DiskMapWidget::previewSizeChanged, this, &PartitionTab::onMapPreviewChanged);
    root->addWidget(m_diskMap);

    auto* mapHint = new QLabel(
        "💡 Bấm một khối trên sơ đồ (hoặc dòng trong bảng) để chọn phân vùng, bấm \"Tra kích thước có thể đổi tới...\", "
        "rồi KÉO tay nắm ở mép phải khối: sang TRÁI = thu nhỏ, sang PHẢI = mở rộng.",
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
    resizeRow->addStretch();
    root->addLayout(resizeRow);
    connect(m_newSizeSpin, &QDoubleSpinBox::valueChanged, this, &PartitionTab::onNewSizeChanged);

    m_changeLabel = new QLabel(this);
    m_changeLabel->setWordWrap(true);
    m_changeLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_changeLabel->setObjectName("partitionChangeLabel");
    m_changeLabel->setVisible(false);
    root->addWidget(m_changeLabel);

    m_edgeNoteLabel = new QLabel(
        "ℹ Windows chỉ đổi kích thước bằng MÉP PHẢI: mép TRÁI (vị trí bắt đầu) của phân vùng luôn đứng yên. Muốn lấy "
        "chỗ trống nằm BÊN TRÁI một phân vùng, hoặc dồn dung lượng từ ổ này sang ổ nằm TRƯỚC nó, cần công cụ di "
        "chuyển phân vùng (vd GParted, MiniTool) - ứng dụng này không tự làm vì rủi ro mất dữ liệu rất cao.",
        this);
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
    const double value = m_newSizeSpin->value();
    if (PartitionManager::internal::isSameSizeAtInputPrecision(value, p.sizeBytes))
        return p.sizeBytes;
    return PartitionManager::internal::clampResizeBytes(value, m_supportedRange);
}

void PartitionTab::applyDirectionBounds(bool resetToCurrent)
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok)
        return;
    const auto bounds = DiskLayout::boundsForDirection(p->sizeBytes, m_supportedRange, m_direction);
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

void PartitionTab::onNewSizeChanged(double)
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok || m_queriedDisk < 0)
        return;
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
