#include "PartitionTab.h"

#include "DiskUiStyle.h"
#include "engine/PartitionResizer.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

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
}

bool PartitionTab::isResizingNow() const
{
    return m_resizer && m_resizer->isRunning();
}

void PartitionTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
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

    m_table = new QTableWidget(0, 7, this);
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
    connect(m_queryBtn, &QPushButton::clicked, this, &PartitionTab::onQuerySupportedSizeClicked);
    root->addWidget(m_queryBtn);

    m_warningLabel = new QLabel(this);
    m_warningLabel->setWordWrap(true);
    m_warningLabel->setStyleSheet(DiskUi::bannerStyle("warn"));
    m_warningLabel->setVisible(false);
    root->addWidget(m_warningLabel);

    auto* resizeRow = new QHBoxLayout();
    m_sizeLabel = new QLabel("Kích thước mới:", this);
    m_sizeLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_newSizeSpin = new QDoubleSpinBox(this);
    m_newSizeSpin->setStyleSheet(DiskUi::inputStyle());
    m_newSizeSpin->setSuffix(" GB");
    m_newSizeSpin->setDecimals(2);
    m_newSizeSpin->setVisible(false);
    m_sizeLabel->setVisible(false);
    resizeRow->addWidget(m_sizeLabel);
    resizeRow->addWidget(m_newSizeSpin);
    resizeRow->addStretch();
    root->addLayout(resizeRow);

    m_confirmHintLabel = new QLabel(this);
    m_confirmHintLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_confirmHintLabel->setVisible(false);
    root->addWidget(m_confirmHintLabel);

    auto* confirmRow = new QHBoxLayout();
    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setStyleSheet(DiskUi::inputStyle());
    m_confirmEdit->setVisible(false);
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &PartitionTab::onConfirmTextChanged);
    confirmRow->addWidget(m_confirmEdit);

    m_resizeBtn = new QPushButton("Đổi kích thước", this);
    m_resizeBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_resizeBtn->setCursor(Qt::PointingHandCursor);
    m_resizeBtn->setEnabled(false);
    m_resizeBtn->setVisible(false);
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

void PartitionTab::reloadPartitions()
{
    showResizeControls(false);
    QString error;
    m_partitions = PartitionManager::listPartitions(&error);

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

    m_statusLabel->setText(error.isEmpty() ? QString("Tìm thấy %1 phân vùng.").arg(m_partitions.size())
                                           : ("⚠ " + error));
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

void PartitionTab::showResizeControls(bool visible)
{
    m_detailLabel->setVisible(visible);
    m_queryBtn->setVisible(visible);
    if (!visible)
    {
        m_warningLabel->setVisible(false);
        m_newSizeSpin->setVisible(false);
        m_sizeLabel->setVisible(false);
        m_confirmHintLabel->setVisible(false);
        m_confirmEdit->setVisible(false);
        m_confirmEdit->clear();
        m_resizeBtn->setVisible(false);
        m_resizeBtn->setEnabled(false);
        m_supportedRange = PartitionManager::SupportedSizeRange{};
    }
}

void PartitionTab::onRowSelectionChanged()
{
    const auto* p = selectedPartition();
    showResizeControls(p != nullptr);
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
    if (!p)
        return;

    QString error;
    m_supportedRange = PartitionManager::querySupportedSize(p->diskNumber, p->partitionNumber, &error);
    if (!m_supportedRange.ok)
    {
        m_warningLabel->setText("⚠ Không tra được kích thước cho phép" + (error.isEmpty() ? "" : (": " + error)) +
                                (PartitionManager::isElevated() ? "" : " (thường cần quyền Administrator)."));
        m_warningLabel->setVisible(true);
        m_newSizeSpin->setVisible(false);
        m_sizeLabel->setVisible(false);
        m_confirmHintLabel->setVisible(false);
        m_confirmEdit->setVisible(false);
        m_resizeBtn->setVisible(false);
        return;
    }

    QString warn = QString("Windows cho phép đổi kích thước phân vùng này trong khoảng %1 - %2.")
                       .arg(DiskUi::formatBytes(m_supportedRange.minBytes), DiskUi::formatBytes(m_supportedRange.maxBytes));
    if (p->isBoot || p->isSystem)
        warn += " ⚠ Đây là phân vùng khởi động/hệ thống - cực kỳ cẩn trọng khi đổi kích thước.";
    m_warningLabel->setText(warn);
    m_warningLabel->setVisible(true);

    m_newSizeSpin->setRange(m_supportedRange.minBytes / GB, m_supportedRange.maxBytes / GB);
    m_newSizeSpin->setValue(qBound<double>(m_newSizeSpin->minimum(), p->sizeBytes / GB, m_newSizeSpin->maximum()));
    m_newSizeSpin->setVisible(true);
    m_sizeLabel->setVisible(true);

    m_confirmHintLabel->setText(QString("Gõ \"%1\" vào ô dưới để xác nhận:").arg(confirmToken(*p)));
    m_confirmHintLabel->setVisible(true);
    m_confirmEdit->clear();
    m_confirmEdit->setPlaceholderText(confirmToken(*p));
    m_confirmEdit->setVisible(true);
    m_resizeBtn->setVisible(true);
    m_resizeBtn->setEnabled(false);
}

void PartitionTab::onConfirmTextChanged(const QString& text)
{
    const auto* p = selectedPartition();
    m_resizeBtn->setEnabled(p && m_supportedRange.ok && text == confirmToken(*p));
}

void PartitionTab::onResizeClicked()
{
    const auto* p = selectedPartition();
    if (!p || !m_supportedRange.ok)
        return;

    const qint64 newSizeBytes = static_cast<qint64>(m_newSizeSpin->value() * GB);
    const QString name = p->driveLetter.isEmpty() ? confirmToken(*p) : (p->driveLetter + ":");

    const auto answer = QMessageBox::warning(
        this, "Xác nhận đổi kích thước phân vùng",
        QString("Sắp đổi kích thước ổ %1 từ %2 thành %3.\n\n"
                "Đây là thao tác đĩa THẬT, không có \"hoàn tác\" dễ dàng nếu có sự cố giữa chừng. "
                "Hãy chắc chắn bạn đã sao lưu dữ liệu quan trọng.\n\n"
                "Tiếp tục?")
            .arg(name, DiskUi::formatBytes(p->sizeBytes), DiskUi::formatBytes(newSizeBytes)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    m_refreshBtn->setEnabled(false);
    m_table->setEnabled(false);
    m_queryBtn->setEnabled(false);
    m_resizeBtn->setEnabled(false);
    m_confirmEdit->setEnabled(false);
    m_newSizeSpin->setEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang đổi kích thước - KHÔNG tắt máy/rút nguồn...");

    m_resizer->setTarget(p->diskNumber, p->partitionNumber, newSizeBytes);
    m_resizer->start();
}

void PartitionTab::onResizeFinished(bool success, QString error)
{
    m_refreshBtn->setEnabled(true);
    m_table->setEnabled(true);
    m_queryBtn->setEnabled(true);
    m_confirmEdit->setEnabled(true);
    m_newSizeSpin->setEnabled(true);
    m_progressBar->setVisible(false);

    if (success)
    {
        m_statusLabel->setText("✓ Đã đổi kích thước thành công.");
        QMessageBox::information(this, "Hoàn tất", "Đã đổi kích thước phân vùng thành công.");
    }
    else
    {
        m_statusLabel->setText("⚠ Đổi kích thước thất bại: " + error);
        QMessageBox::critical(this, "Lỗi", "Không đổi được kích thước:\n" + error);
    }
    reloadPartitions(); // làm mới danh sách + ẩn khu vực đổi kích thước, dù thành công hay thất bại
}
