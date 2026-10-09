#include "DuplicateFilesTab.h"

#include "DiskCleanupWindow.h"
#include "DiskUiStyle.h"
#include "engine/FsSafety.h"
#include "engine/RecycleBinOps.h"

#include <QDir>
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

namespace
{
constexpr int kLastWriteRole = Qt::UserRole + 1;
} // namespace

DuplicateFilesTab::DuplicateFilesTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_finder = new DuplicateFinder(this);
    connect(m_finder, &DuplicateFinder::progressTick, this, &DuplicateFilesTab::onProgressTick);
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
    // QTreeView mặc định "stretchLastSection=true" - tự kéo giãn cột CUỐI (ở đây là "Kích thước") bất
    // kể resize mode đã đặt, đè lên cột 0 đã Stretch và để lại khoảng trắng lớn bên phải vì chữ căn
    // trái. Tắt đi để cột 1 chỉ rộng vừa đủ nội dung, nằm sát mép phải thật sự.
    m_tree->header()->setStretchLastSection(false);
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

bool DuplicateFilesTab::isScanningNow() const
{
    return m_finder && m_finder->isRunning();
}

void DuplicateFilesTab::stopScanIfRunning()
{
    if (m_finder && m_finder->isRunning())
        m_finder->requestStop();
}

void DuplicateFilesTab::onScanClicked()
{
    if (m_rootPath.isEmpty())
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
    // Ưu tiên thấp: hash SHA-256 nội dung tệp trên cả thư mục lớn là CPU/I-O nặng - nhường CPU cho
    // luồng giao diện để cửa sổ không bao giờ bị Windows báo "Không phản hồi" dù quét lâu.
    m_finder->startScan(QThread::LowPriority);
}

void DuplicateFilesTab::onStopClicked()
{
    m_finder->requestStop();
    m_statusLabel->setText("Đang dừng...");
}

void DuplicateFilesTab::onProgressTick(qint64 filesScanned, qint64 filesHashed, QString currentPath)
{
    // Rút gọn đường dẫn (giữ đầu + cuối) - mục đích chính là CHO THẤY ứng dụng vẫn đang chạy.
    const QString elided = m_statusLabel->fontMetrics().elidedText(currentPath, Qt::ElideMiddle, 480);
    if (filesHashed > 0)
        m_statusLabel->setText(QString("⏳ Đang so sánh nội dung... %1 tệp đã kiểm tra - %2").arg(filesHashed).arg(elided));
    else
        m_statusLabel->setText(QString("⏳ Đang liệt kê tệp... %1 tệp đã quét - %2").arg(filesScanned).arg(elided));
}

void DuplicateFilesTab::addGroupToTree(const DuplicateGroup& group)
{
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
        child->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        child->setData(0, Qt::UserRole, group.paths[i]);         // đường dẫn thật, không lẫn hậu tố hiển thị
        child->setData(1, Qt::UserRole, group.sizeEachBytes);    // kích thước thô (byte), không lẫn chuỗi đã định dạng
        // Thời điểm sửa cuối LÚC QUÉT - để kiểm lại ngay trước khi xóa rằng tệp chưa bị sửa từ đó tới giờ.
        child->setData(0, kLastWriteRole, group.lastWriteTimes.value(i, 0));
        child->setFlags(child->flags() | Qt::ItemIsUserCheckable);
        child->setCheckState(0, keepThisOne ? Qt::Unchecked : Qt::Checked);
    }
    // Thu gọn mặc định (không setExpanded(true)): kết quả có thể tới hàng trăm nhóm (vd thư mục cache
    // trình duyệt có nhiều tệp trùng kích thước khối cố định) - mở sẵn hết sẽ làm bảng rất dài và tốn
    // công dựng giao diện không cần thiết; người dùng tự mở nhóm mình quan tâm.
}

void DuplicateFilesTab::onScanFinished(QList<DuplicateGroup> groups, qint64 wastedBytes, int totalGroupsFound)
{
    m_scanBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    m_progressBar->setVisible(false);

    // Dựng cả cây trong MỘT lần, luồng giao diện tắt tạm việc vẽ lại/tính layout cho tới khi xong -
    // đúng mẫu đã đo thực tế là không gây "Không phản hồi" (xem LargeFileScanner::scanFinished).
    m_updatingTree = true;
    m_tree->setUpdatesEnabled(false);
    for (const auto& group : groups)
        addGroupToTree(group);
    m_tree->setUpdatesEnabled(true);
    m_updatingTree = false;

    if (totalGroupsFound == 0)
    {
        m_statusLabel->setText("✓ Quét xong: không tìm thấy tệp trùng lặp nào.");
    }
    else
    {
        QString text = QString("✓ Quét xong: %1 nhóm trùng lặp, có thể giải phóng %2.")
                           .arg(totalGroupsFound)
                           .arg(DiskUi::formatBytes(wastedBytes));
        if (groups.size() < totalGroupsFound)
            text += QString(" (chỉ hiển thị %1 nhóm lãng phí nhiều nhất)").arg(groups.size());
        m_statusLabel->setText(text);
    }
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

    // BẮT BUỘC giữ lại ít nhất 1 bản mỗi nhóm: nếu lần tích này khiến MỌI bản trong nhóm đều được chọn
    // để xóa thì hoàn lại ngay. Trước đây hộp xác nhận chỉ NÓI "mỗi nhóm vẫn giữ lại ít nhất 1 bản" mà
    // không có gì kiểm tra - tích hết cả nhóm là xóa sạch mọi bản của tệp đó.
    if (item->checkState(0) == Qt::Checked)
    {
        QTreeWidgetItem* group = item->parent();
        bool anyKept = false;
        for (int j = 0; j < group->childCount(); ++j)
            anyKept = anyKept || group->child(j)->checkState(0) != Qt::Checked;
        if (!anyKept)
        {
            m_updatingTree = true;
            item->setCheckState(0, Qt::Unchecked);
            m_updatingTree = false;
            m_statusLabel->setText("⚠ Mỗi nhóm phải giữ lại ít nhất 1 bản - không thể chọn xóa toàn bộ các bản của cùng một tệp.");
        }
    }
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
    // Không xóa trong lúc một tab khác đang đổi kích thước phân vùng/dọn dẹp/quét trên cùng ổ đĩa - xem
    // DiskCleanupWindow::isAnyOtherTabBusy() (trước đây chỉ các nút Quét mới kiểm tra điều này).
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

    // Kết quả "trùng lặp" chỉ đúng tại thời điểm quét. Ngay trước khi xóa, kiểm lại TỪNG NHÓM trên đĩa:
    //  - phải còn ít nhất 1 bản KHÔNG chọn xóa vẫn tồn tại và chưa bị sửa (bản sẽ giữ lại) - nếu bản giữ
    //    lại đã mất/đã đổi nội dung thì các bản đang chọn xóa có thể là bản cuối cùng: bỏ qua cả nhóm;
    //  - từng bản chọn xóa phải còn đúng kích thước + thời điểm sửa như lúc quét, nếu không thì nó không
    //    còn chắc là bản sao nữa: giữ lại;
    //  - bản không vào Thùng rác được (quá lớn, ổ không có Thùng rác) cũng giữ lại - Shell sẽ hủy hẳn nó.
    struct Selection
    {
        QStringList paths;
        qint64 total{0};
        int skippedGroups{0};
        int skippedChanged{0};
        int skippedNotRecyclable{0};
    };
    auto collectSelection = [this]() {
        Selection sel;
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
        {
            QTreeWidgetItem* group = m_tree->topLevelItem(i);
            QList<QTreeWidgetItem*> checked;
            bool keeperIntact = false;
            for (int j = 0; j < group->childCount(); ++j)
            {
                QTreeWidgetItem* child = group->child(j);
                if (child->checkState(0) == Qt::Checked)
                {
                    checked << child;
                    continue;
                }
                keeperIntact = keeperIntact ||
                               DuplicateFinder::fileUnchanged(child->data(0, Qt::UserRole).toString(),
                                                              child->data(1, Qt::UserRole).toLongLong(),
                                                              child->data(0, kLastWriteRole).toLongLong());
            }
            if (checked.isEmpty())
                continue;
            if (!keeperIntact)
            {
                ++sel.skippedGroups;
                continue;
            }
            for (QTreeWidgetItem* child : checked)
            {
                const QString path = child->data(0, Qt::UserRole).toString();
                const qint64 size = child->data(1, Qt::UserRole).toLongLong();
                if (!DuplicateFinder::fileUnchanged(path, size, child->data(0, kLastWriteRole).toLongLong()))
                {
                    ++sel.skippedChanged;
                    continue;
                }
                if (!RecycleBinOps::notRecyclableReason(path, size).isEmpty())
                {
                    ++sel.skippedNotRecyclable;
                    continue;
                }
                sel.paths << path;
                sel.total += size;
            }
        }
        return sel;
    };
    const Selection selection = collectSelection();
    QStringList paths = selection.paths;
    const qint64 total = selection.total;
    const int skippedGroups = selection.skippedGroups;
    const int skippedChanged = selection.skippedChanged;
    const int skippedNotRecyclable = selection.skippedNotRecyclable;

    QStringList skippedNotes;
    if (skippedGroups > 0)
        skippedNotes << QString("%1 nhóm bị bỏ qua vì bản sẽ giữ lại không còn nguyên vẹn trên đĩa").arg(skippedGroups);
    if (skippedChanged > 0)
        skippedNotes << QString("%1 tệp đã thay đổi hoặc không còn kể từ lúc quét").arg(skippedChanged);
    if (skippedNotRecyclable > 0)
        skippedNotes << QString("%1 tệp không thể đưa vào Thùng rác để khôi phục").arg(skippedNotRecyclable);
    const QString skippedText =
        skippedNotes.isEmpty() ? QString() : ("\n\nĐược GIỮ NGUYÊN, không xóa: " + skippedNotes.join("; ") + ". Hãy quét lại để có kết quả mới.");

    if (paths.isEmpty())
    {
        if (!skippedText.isEmpty())
            QMessageBox::warning(this, "Xóa tệp trùng lặp", "Không có tệp nào đủ điều kiện để xóa." + skippedText);
        return;
    }

    if (QMessageBox::question(this, "Xóa tệp trùng lặp",
                              QString("Chuyển %1 tệp (%2) vào Thùng rác?\n\nMỗi nhóm vẫn giữ lại ít nhất 1 bản.")
                                      .arg(paths.size())
                                      .arg(DiskUi::formatBytes(total)) +
                                  skippedText,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    // Hộp xác nhận chạy vòng lặp sự kiện riêng và có thể mở rất lâu - kiểm lại TOÀN BỘ một lần nữa ngay
    // sát lúc xóa, chỉ xóa những tệp vừa được xác nhận VÀ vẫn còn đủ điều kiện (bản giữ lại còn nguyên,
    // bản xóa chưa bị sửa). Tệp không còn đủ điều kiện được giữ nguyên.
    const QStringList stillEligible = collectSelection().paths;
    const int confirmedCount = paths.size();
    QStringList recheckedPaths;
    for (const QString& path : paths)
        if (stillEligible.contains(path))
            recheckedPaths << path;
    paths = recheckedPaths;
    if (paths.isEmpty())
    {
        QMessageBox::warning(this, "Xóa tệp trùng lặp",
            "Các tệp đã thay đổi trong lúc chờ xác nhận - không xóa gì. Hãy quét lại để có kết quả mới.");
        return;
    }

    QString error;
    const bool ok = RecycleBinOps::moveToRecycleBin(paths, &error);
    int deleted = 0;
    for (const QString& path : paths)
        if (!FsSafety::existsNoFollow(path))
            ++deleted;

    if (!ok || deleted < confirmedCount)
        QMessageBox::critical(this, "Lỗi",
            QString("Chỉ chuyển được %1/%2 tệp vào Thùng rác.\n%3").arg(deleted).arg(confirmedCount).arg(error));

    m_statusLabel->setText(QString("✓ Đã chuyển %1 tệp vào Thùng rác.").arg(deleted));
    // Quét lại để làm mới danh sách (các mục đã xóa không còn nữa) - kể cả khi lỗi một phần, để bảng
    // không còn hiện các tệp đã bị xóa.
    onScanClicked();
}
