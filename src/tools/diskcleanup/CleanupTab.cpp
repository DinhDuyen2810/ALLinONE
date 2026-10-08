#include "CleanupTab.h"

#include "DiskCleanupWindow.h"
#include "DiskUiStyle.h"
#include "engine/CategoryRegistry.h"
#include "engine/CleanupExecutor.h"
#include "engine/DiskSpaceInfo.h"

#include <QCheckBox>
#include <QColor>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

CleanupTab::CleanupTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    reloadDriveOverview();

    m_scanner = new CleanupScanner(this);
    connect(m_scanner, &CleanupScanner::categoryStarted, this, &CleanupTab::onCategoryStarted);
    connect(m_scanner, &CleanupScanner::itemFound, this, &CleanupTab::onItemFound);
    connect(m_scanner, &CleanupScanner::categoryFinished, this, &CleanupTab::onCategoryFinished);
    connect(m_scanner, &CleanupScanner::scanFinished, this, &CleanupTab::onScanFinished);
    connect(m_scanner, &CleanupScanner::scanStopped, this, &CleanupTab::onScanStopped);

    m_executor = new CleanupExecutor(this);
    connect(m_executor, &CleanupExecutor::executionFinished, this, &CleanupTab::onExecutionFinished);
}

CleanupTab::~CleanupTab()
{
    if (m_scanner && m_scanner->isRunning())
    {
        m_scanner->requestStop();
        m_scanner->wait(3000);
    }
    // CleanupExecutor KHÔNG hỗ trợ hủy giữa chừng (một lệnh SHFileOperationW xử lý CẢ LÔ, không có móc
    // nào để dừng sớm) - xóa hàng nghìn tệp thật (vd cache trình duyệt) có thể mất LÂU HƠN 5 giây, nên
    // wait(5000) không đủ và sẽ khiến QThread bị hủy đối tượng trong lúc vẫn đang thực sự chạy (hành vi
    // KHÔNG XÁC ĐỊNH theo tài liệu Qt). Bình thường không tới đây: DiskCleanupWindow::closeEvent() đã
    // CHẶN đóng cửa sổ hẳn trong lúc đang dọn dẹp - đây chỉ là lưới an toàn cuối cùng.
    if (m_executor && m_executor->isRunning())
        m_executor->wait();
}

bool CleanupTab::isCleaningNow() const
{
    return m_executor && m_executor->isRunning();
}

bool CleanupTab::isBusy() const
{
    return m_scanning || isCleaningNow();
}

void CleanupTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* overviewLabel = new QLabel("DUNG LƯỢNG Ổ ĐĨA", this);
    overviewLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px;");
    root->addWidget(overviewLabel);

    m_driveOverview = new QWidget(this);
    m_driveOverviewLayout = new QHBoxLayout(m_driveOverview);
    m_driveOverviewLayout->setContentsMargins(0, 0, 0, 0);
    m_driveOverviewLayout->setSpacing(10);
    root->addWidget(m_driveOverview);

    auto* catLabel = new QLabel("HẠNG MỤC DỌN DẸP", this);
    catLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; margin-top: 6px;");
    root->addWidget(catLabel);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"", "Hạng mục", "Mức độ", "Số mục", "Dung lượng"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DiskUi::tableStyle());
    connect(m_table, &QTableWidget::cellChanged, this, &CleanupTab::onTableCellChanged);
    root->addWidget(m_table, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #0969da; border-radius: 3px; }");
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Bấm \"Quét\" để tìm tệp có thể dọn dẹp.", this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &CleanupTab::onScanClicked);
    bottomRow->addWidget(m_scanBtn);

    m_cleanBtn = new QPushButton("🧹 Dọn dẹp mục đã chọn", this);
    m_cleanBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_cleanBtn->setCursor(Qt::PointingHandCursor);
    m_cleanBtn->setEnabled(false);
    connect(m_cleanBtn, &QPushButton::clicked, this, &CleanupTab::onCleanClicked);
    bottomRow->addWidget(m_cleanBtn);

    m_permanentCheck = new QCheckBox("Xóa vĩnh viễn (bỏ qua Thùng rác)", this);
    m_permanentCheck->setStyleSheet("color: #57606a; font-size: 11px;");
    bottomRow->addWidget(m_permanentCheck);

    bottomRow->addStretch();
    m_summaryLabel = new QLabel("Đã chọn: 0 B", this);
    m_summaryLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 13px;");
    bottomRow->addWidget(m_summaryLabel);
    root->addLayout(bottomRow);
}

void CleanupTab::reloadDriveOverview()
{
    QLayoutItem* child;
    while ((child = m_driveOverviewLayout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
    {
        auto* card = new QWidget(m_driveOverview);
        card->setStyleSheet("background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px;");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 10, 14, 10);
        layout->setSpacing(4);

        auto* nameLabel = new QLabel(QString("%1 (%2)").arg(d.displayName, d.rootPath), card);
        nameLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px; border: none; background: transparent;");
        layout->addWidget(nameLabel);

        auto* bar = new QProgressBar(card);
        bar->setRange(0, 100);
        bar->setValue(static_cast<int>(d.usedRatio() * 100));
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        const QString barColor = d.usedRatio() > 0.9 ? "#cf222e" : (d.usedRatio() > 0.75 ? "#9a6700" : "#1f883d");
        bar->setStyleSheet(QString("QProgressBar { background-color: #eaeef2; border-radius: 4px; } "
                                   "QProgressBar::chunk { background-color: %1; border-radius: 4px; }")
                               .arg(barColor));
        layout->addWidget(bar);

        auto* detailLabel = new QLabel(QString("Còn trống %1 / Tổng %2")
                                           .arg(DiskUi::formatBytes(d.freeBytes), DiskUi::formatBytes(d.totalBytes)),
                                       card);
        detailLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
        layout->addWidget(detailLabel);

        m_driveOverviewLayout->addWidget(card);
    }
    m_driveOverviewLayout->addStretch();
}

void CleanupTab::onScanClicked()
{
    if (m_scanning)
        return;

    // Tránh quét chồng chéo trên cùng ổ đĩa với một tab khác đang đổi kích thước/dọn dẹp/quét - xem
    // DiskCleanupWindow::isAnyOtherTabBusy().
    if (auto* win = qobject_cast<DiskCleanupWindow*>(window()))
    {
        if (win->isAnyOtherTabBusy(this))
        {
            QMessageBox::warning(this, "Đang có thao tác khác",
                "Một tab khác trong Disk Cleanup đang quét/dọn dẹp/đổi kích thước phân vùng - vui lòng "
                "đợi xong để tránh xung đột trên cùng ổ đĩa, rồi thử lại.");
            return;
        }
    }

    const QList<CleanupCategory> all = CategoryRegistry::buildCategories(CleanupEnvironment::current());
    m_categories.clear();
    for (const CleanupCategory& c : all)
        if (!c.rootPaths.isEmpty())
            m_categories.push_back(c);

    m_itemsByCategory.clear();
    m_rowByCategory.clear();
    m_updatingTable = true;
    m_table->setRowCount(m_categories.size());
    for (int i = 0; i < m_categories.size(); ++i)
    {
        const CleanupCategory& c = m_categories[i];
        m_rowByCategory[c.id] = i;

        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        checkItem->setCheckState(c.risk == CleanupRisk::Safe ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, checkItem);

        auto* nameItem = new QTableWidgetItem(c.name);
        nameItem->setToolTip(c.description);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, nameItem);

        auto* riskItem = new QTableWidgetItem(CategoryRegistry::riskName(c.risk));
        riskItem->setForeground(QColor(DiskUi::riskColor(CategoryRegistry::riskName(c.risk))));
        riskItem->setFlags(riskItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, riskItem);

        auto* countItem = new QTableWidgetItem("—");
        countItem->setTextAlignment(Qt::AlignCenter);
        countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 3, countItem);

        auto* sizeItem = new QTableWidgetItem("Đang quét...");
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 4, sizeItem);
    }
    m_updatingTable = false;

    if (m_categories.isEmpty())
    {
        m_statusLabel->setText("Không tìm thấy hạng mục nào khả dụng trên máy này.");
        return;
    }

    m_scanning = true;
    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang quét...");

    m_scanner->setCategories(m_categories);
    // Ưu tiên thấp: nhường CPU cho luồng giao diện, tránh bị Windows báo "Không phản hồi" khi gặp
    // hạng mục lớn (vd Windows.old hàng chục GB).
    m_scanner->start(QThread::LowPriority);
}

void CleanupTab::onCategoryStarted(QString name)
{
    m_statusLabel->setText("⏳ Đang quét: " + name);
}

void CleanupTab::onItemFound(CleanupItem item)
{
    m_itemsByCategory[item.categoryId].push_back(item);
}

void CleanupTab::onCategoryFinished(CleanupCategoryId id, qint64 bytes, int count)
{
    const int row = m_rowByCategory.value(id, -1);
    if (row < 0)
        return;

    m_updatingTable = true;
    m_table->item(row, 3)->setText(QString::number(count));
    m_table->item(row, 4)->setText(DiskUi::formatBytes(bytes));
    m_updatingTable = false;
}

void CleanupTab::onScanFinished(qint64 totalBytes, int totalItems)
{
    m_scanning = false;
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(QString("✓ Quét xong: %1 mục, tổng %2 có thể dọn dẹp.")
                               .arg(totalItems)
                               .arg(DiskUi::formatBytes(totalBytes)));
    updateSelectedSummary();
}

void CleanupTab::onScanStopped()
{
    m_scanning = false;
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Đã dừng quét.");
    updateSelectedSummary();
}

void CleanupTab::onTableCellChanged(int row, int column)
{
    if (m_updatingTable || column != 0)
        return;
    updateSelectedSummary();
}

qint64 CleanupTab::selectedTotalBytes() const
{
    qint64 total = 0;
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        QTableWidgetItem* check = m_table->item(row, 0);
        if (!check || check->checkState() != Qt::Checked)
            continue;
        if (row >= m_categories.size())
            continue;
        const CleanupCategoryId id = m_categories[row].id;
        for (const CleanupItem& item : m_itemsByCategory.value(id))
            total += item.sizeBytes;
    }
    return total;
}

void CleanupTab::updateSelectedSummary()
{
    const qint64 total = selectedTotalBytes();
    m_summaryLabel->setText("Đã chọn: " + DiskUi::formatBytes(total));
    m_cleanBtn->setEnabled(!m_scanning && total > 0);
}

void CleanupTab::onCleanClicked()
{
    // Tránh xóa chồng chéo trên cùng ổ đĩa với một tab khác đang đổi kích thước/dọn dẹp/quét - xem
    // DiskCleanupWindow::isAnyOtherTabBusy().
    if (auto* win = qobject_cast<DiskCleanupWindow*>(window()))
    {
        if (win->isAnyOtherTabBusy(this))
        {
            QMessageBox::warning(this, "Đang có thao tác khác",
                "Một tab khác trong Disk Cleanup đang quét/dọn dẹp/đổi kích thước phân vùng - vui lòng "
                "đợi xong để tránh xung đột trên cùng ổ đĩa, rồi thử lại.");
            return;
        }
    }

    QStringList paths;
    QList<qint64> sizes;
    QStringList categoryNames;
    qint64 total = 0;
    bool includesHighRisk = false;

    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        QTableWidgetItem* check = m_table->item(row, 0);
        if (!check || check->checkState() != Qt::Checked || row >= m_categories.size())
            continue;

        const CleanupCategory& cat = m_categories[row];
        const QList<CleanupItem> items = m_itemsByCategory.value(cat.id);
        if (items.isEmpty())
            continue;

        categoryNames << cat.name;
        if (cat.risk == CleanupRisk::High)
            includesHighRisk = true;
        for (const CleanupItem& item : items)
        {
            paths << item.path;
            sizes << item.sizeBytes;
            total += item.sizeBytes;
        }
    }

    if (paths.isEmpty())
    {
        QMessageBox::information(this, "Dọn dẹp", "Chưa có hạng mục nào được chọn.");
        return;
    }

    const bool permanent = m_permanentCheck->isChecked();
    QString message = QString("Sẽ xóa %1 mục (%2) thuộc %3 hạng mục:\n\n  • %4\n\n")
                          .arg(paths.size())
                          .arg(DiskUi::formatBytes(total))
                          .arg(categoryNames.size())
                          .arg(categoryNames.join("\n  • "));
    message += permanent ? "⚠ XÓA VĨNH VIỄN - KHÔNG qua Thùng rác, không thể khôi phục."
                         : "Các mục sẽ được chuyển vào Thùng rác và có thể khôi phục lại nếu cần.";
    if (includesHighRisk)
        message += "\n\n⚠ Có hạng mục RỦI RO CAO (ví dụ Windows.old) - hãy chắc chắn trước khi tiếp tục.";

    if (QMessageBox::question(this, "Xác nhận dọn dẹp", message, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    m_cleanBtn->setEnabled(false);
    m_scanBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang dọn dẹp...");

    m_executor->setItems(paths, sizes);
    m_executor->setPermanentDelete(permanent);
    m_executor->start(QThread::LowPriority);
}

void CleanupTab::onExecutionFinished(bool success, QString error, qint64 freedBytes, int deletedCount)
{
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    reloadDriveOverview();

    if (success)
    {
        m_statusLabel->setText(QString("✓ Đã dọn dẹp %1 mục, giải phóng %2.")
                                   .arg(deletedCount)
                                   .arg(DiskUi::formatBytes(freedBytes)));
        QString info = QString("Đã giải phóng %1 (%2 mục).").arg(DiskUi::formatBytes(freedBytes)).arg(deletedCount);
        // 'error' ở đây là GHI CHÚ, không phải lỗi nghiêm trọng - vài tệp tạm tự mất giữa lúc quét và
        // lúc xóa (hoặc đang được chương trình khác dùng) là chuyện bình thường, không đáng báo đỏ.
        if (!error.isEmpty())
            info += "\n\nLưu ý: " + error;
        QMessageBox::information(this, "Hoàn tất", info);
        // Xóa xong, quét lại để làm mới danh sách (các mục đã xóa không còn nữa)
        onScanClicked();
    }
    else
    {
        m_statusLabel->setText("⚠ Dọn dẹp thất bại: " + error);
        QMessageBox::critical(this, "Lỗi", "Không dọn dẹp được:\n" + error);
        updateSelectedSummary();
    }
}
