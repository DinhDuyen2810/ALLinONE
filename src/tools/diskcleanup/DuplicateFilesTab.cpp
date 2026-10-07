#include "DuplicateFilesTab.h"

#include "DiskUiStyle.h"
#include "engine/RecycleBinOps.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

DuplicateFilesTab::DuplicateFilesTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_finder = new DuplicateFinder(this);
    connect(m_finder, &DuplicateFinder::progressTick, this, &DuplicateFilesTab::onProgressTick);
    connect(m_finder, &DuplicateFinder::groupFound, this, &DuplicateFilesTab::onGroupFound);
    connect(m_finder, &DuplicateFinder::scanFinished, this, &DuplicateFilesTab::onScanFinished);
    connect(m_finder, &DuplicateFinder::scanStopped, this, &DuplicateFilesTab::onScanStopped);
}

DuplicateFilesTab::~DuplicateFilesTab()
{
    if (m_finder && m_finder->isRunning())
    {
        m_finder->requestStop();
        m_finder->wait(3000);
    }
}

void DuplicateFilesTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* topRow = new QHBoxLayout();
    m_browseBtn = new QPushButton("Chọn thư mục...", this);
    m_browseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &DuplicateFilesTab::onBrowseClicked);
    topRow->addWidget(m_browseBtn);

    auto* sizeLabel = new QLabel("Bỏ qua tệp nhỏ hơn:", this);
    sizeLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    topRow->addWidget(sizeLabel);

    m_minSizeSpin = new QSpinBox(this);
    m_minSizeSpin->setStyleSheet(DiskUi::inputStyle());
    m_minSizeSpin->setRange(1, 1048576); // 1 KB .. 1 GB
    m_minSizeSpin->setValue(100);
    m_minSizeSpin->setSuffix(" KB");
    m_minSizeSpin->setSingleStep(50);
    topRow->addWidget(m_minSizeSpin);
    topRow->addStretch();
    root->addLayout(topRow);

    m_pathLabel = new QLabel("Chưa chọn thư mục. Hãy chọn một thư mục để quét tìm tệp trùng lặp.", this);
    m_pathLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_pathLabel->setWordWrap(true);
    root->addWidget(m_pathLabel);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({"Nhóm trùng lặp / Đường dẫn", "Kích thước"});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->setStyleSheet(
        "QTreeWidget { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; }"
        "QTreeWidget::item { padding: 4px; }"
        "QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; padding: 6px; border-bottom: 1px solid #d0d7de; }");
    connect(m_tree, &QTreeWidget::itemChanged, this, &DuplicateFilesTab::onItemChanged);
    root->addWidget(m_tree, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #0969da; border-radius: 3px; }");
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Chọn thư mục rồi bấm \"Quét\".", this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setEnabled(false);
    connect(m_scanBtn, &QPushButton::clicked, this, &DuplicateFilesTab::onScanClicked);
    bottomRow->addWidget(m_scanBtn);

    m_stopBtn = new QPushButton("Dừng", this);
    m_stopBtn->setStyleSheet(DiskUi::buttonStyle());
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &DuplicateFilesTab::onStopClicked);
    bottomRow->addWidget(m_stopBtn);

    bottomRow->addStretch();

    m_deleteBtn = new QPushButton("🗑 Xóa các bản đã chọn", this);
    m_deleteBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &DuplicateFilesTab::onDeleteSelectedClicked);
    bottomRow->addWidget(m_deleteBtn);

    bottomRow->addStretch();
    m_summaryLabel = new QLabel("Đã chọn: 0 B", this);
    m_summaryLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 13px;");
    bottomRow->addWidget(m_summaryLabel);
    root->addLayout(bottomRow);
}

void DuplicateFilesTab::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục để quét tìm tệp trùng lặp",
                                                           m_rootPath.isEmpty() ? QString() : m_rootPath);
    if (dir.isEmpty())
        return;
    m_rootPath = dir;
    m_pathLabel->setText("Sẽ quét: " + m_rootPath);
    m_scanBtn->setEnabled(true);
}

void DuplicateFilesTab::onScanClicked()
{
    if (m_rootPath.isEmpty())
        return;

    m_updatingTree = true;
    m_tree->clear();
    m_updatingTree = false;
    m_deleteBtn->setEnabled(false);
    updateSelectedSummary();

    m_scanBtn->setVisible(false);
    m_stopBtn->setVisible(true);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang quét " + m_rootPath + "...");

    m_finder->setRootPath(m_rootPath);
    m_finder->setMinSizeBytes(static_cast<qint64>(m_minSizeSpin->value()) * 1024);
    m_finder->start();
}

void DuplicateFilesTab::onStopClicked()
{
    m_finder->requestStop();
    m_statusLabel->setText("Đang dừng...");
}

void DuplicateFilesTab::onProgressTick(qint64 filesScanned, qint64 filesHashed)
{
    if (filesHashed > 0)
        m_statusLabel->setText(QString("⏳ Đang so sánh nội dung... %1 tệp đã kiểm tra").arg(filesHashed));
    else
        m_statusLabel->setText(QString("⏳ Đang liệt kê tệp... %1 tệp đã quét").arg(filesScanned));
}

void DuplicateFilesTab::onGroupFound(DuplicateGroup group)
{
    addGroupToTree(group);
}

void DuplicateFilesTab::addGroupToTree(const DuplicateGroup& group)
{
    m_updatingTree = true;

    auto* groupItem = new QTreeWidgetItem(m_tree);
    groupItem->setText(0, QString("📁 %1 bản giống hệt nhau - mỗi bản %2 - lãng phí %3")
                             .arg(group.paths.size())
                             .arg(DiskUi::formatBytes(group.sizeEachBytes))
                             .arg(DiskUi::formatBytes(group.wastedBytes())));
    groupItem->setFirstColumnSpanned(true);
    QFont f = groupItem->font(0);
    f.setBold(true);
    groupItem->setFont(0, f);

    for (int i = 0; i < group.paths.size(); ++i)
    {
        auto* child = new QTreeWidgetItem(groupItem);
        const bool keepThisOne = (i == 0); // mặc định giữ lại bản đầu tiên, tick sẵn các bản còn lại để xóa
        child->setText(0, group.paths[i] + (keepThisOne ? "   (sẽ giữ lại)" : ""));
        child->setText(1, DiskUi::formatBytes(group.sizeEachBytes));
        child->setData(0, Qt::UserRole, group.paths[i]);         // đường dẫn thật, không lẫn hậu tố hiển thị
        child->setData(1, Qt::UserRole, group.sizeEachBytes);    // kích thước thô (byte), không lẫn chuỗi đã định dạng
        child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
        child->setCheckState(0, keepThisOne ? Qt::Unchecked : Qt::Checked);
    }
    groupItem->setExpanded(true);

    m_updatingTree = false;
}

void DuplicateFilesTab::onScanFinished(int groupCount, qint64 wastedBytes)
{
    m_scanBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(groupCount > 0
                               ? QString("✓ Quét xong: %1 nhóm trùng lặp, có thể giải phóng %2.")
                                     .arg(groupCount)
                                     .arg(DiskUi::formatBytes(wastedBytes))
                               : "✓ Quét xong: không tìm thấy tệp trùng lặp nào.");
    updateSelectedSummary();
}

void DuplicateFilesTab::onScanStopped()
{
    m_scanBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Đã dừng quét.");
    updateSelectedSummary();
}

void DuplicateFilesTab::onItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_updatingTree || column != 0 || item->parent() == nullptr)
        return; // chỉ quan tâm tích/bỏ tích ở các dòng con (từng tệp), không phải dòng nhóm
    updateSelectedSummary();
}

void DuplicateFilesTab::updateSelectedSummary()
{
    // Kích thước lấy từ UserRole (byte thô, xem addGroupToTree()) - không parse ngược chuỗi đã định dạng.
    qint64 total = 0;
    int count = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j)
        {
            QTreeWidgetItem* child = group->child(j);
            if (child->checkState(0) == Qt::Checked)
            {
                total += child->data(1, Qt::UserRole).toLongLong();
                ++count;
            }
        }
    }

    m_summaryLabel->setText(QString("Đã chọn: %1 (%2 tệp)").arg(DiskUi::formatBytes(total)).arg(count));
    m_deleteBtn->setEnabled(count > 0);
}

void DuplicateFilesTab::onDeleteSelectedClicked()
{
    QStringList paths;
    qint64 total = 0;
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* group = m_tree->topLevelItem(i);
        for (int j = 0; j < group->childCount(); ++j)
        {
            QTreeWidgetItem* child = group->child(j);
            if (child->checkState(0) == Qt::Checked)
            {
                paths << child->data(0, Qt::UserRole).toString();
                total += child->data(1, Qt::UserRole).toLongLong();
            }
        }
    }
    if (paths.isEmpty())
        return;

    if (QMessageBox::question(this, "Xóa tệp trùng lặp",
                              QString("Chuyển %1 tệp (%2) vào Thùng rác?\n\nMỗi nhóm vẫn giữ lại ít nhất 1 bản.")
                                  .arg(paths.size())
                                  .arg(DiskUi::formatBytes(total)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    if (!RecycleBinOps::moveToRecycleBin(paths, &error))
    {
        QMessageBox::critical(this, "Lỗi", "Không xóa được:\n" + error);
        return;
    }

    m_statusLabel->setText(QString("✓ Đã chuyển %1 tệp vào Thùng rác.").arg(paths.size()));
    // Quét lại để làm mới danh sách (các mục đã xóa không còn nữa)
    onScanClicked();
}
