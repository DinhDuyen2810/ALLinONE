#include "FolderTreeTab.h"

#include "DiskCleanupWindow.h"
#include "DiskUiStyle.h"
#include "FolderTreeModel.h"
#include "engine/DiskSpaceInfo.h"
#include "engine/FsSafety.h"
#include "engine/PartitionManager.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

namespace
{
/// Cột "% của thư mục cha": thanh ngang + số %, màu đậm dần theo tỉ lệ (xanh < 20% < vàng < 50% <= đỏ).
class ShareBarDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString text = opt.text;
        opt.text.clear();
        const QWidget* widget = opt.widget;
        QStyle* style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget); // nền chọn/di chuột

        const bool selected = opt.state & QStyle::State_Selected;
        const double share = qBound(0.0, index.data(FolderTreeModel::ShareRole).toDouble(), 1.0);
        const QRect cell = opt.rect.adjusted(6, 0, -6, 0);
        const int textWidth = 46;
        QRect bar(cell.left(), cell.center().y() - 4, qMax(10, cell.width() - textWidth - 6), 8);

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? QColor(255, 255, 255, 80) : QColor("#e6ebf1"));
        painter->drawRoundedRect(bar, 4, 4);
        // Dòng gốc luôn 100% - trung tính (xanh), chỉ các dòng con mới đổi màu theo mức chiếm của thư mục cha.
        const bool isRoot = !index.parent().isValid();
        QColor fillColor = isRoot ? QColor("#0969da") : share >= 0.5 ? QColor("#cf222e") : share >= 0.2 ? QColor("#bf8700") : QColor("#0969da");
        if (selected)
            fillColor = QColor("#ffffff");
        QRect fill = bar;
        fill.setWidth(share > 0.0 ? qMax(3, static_cast<int>(bar.width() * share)) : 0);
        painter->setBrush(fillColor);
        painter->drawRoundedRect(fill, 4, 4);
        painter->setPen(selected ? QColor("#ffffff") : QColor("#1f2328"));
        painter->drawText(QRect(bar.right() + 6, opt.rect.top(), textWidth, opt.rect.height()), Qt::AlignRight | Qt::AlignVCenter, text);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(qMax(s.height(), 26));
        return s;
    }
};

QString duration(qint64 ms)
{
    const qint64 sec = ms / 1000;
    if (ms < 1000)
        return QStringLiteral("%1 mili giây").arg(ms);
    if (sec < 60)
        return QStringLiteral("%1 giây").arg(QLocale().toString(ms / 1000.0, 'f', 1));
    return QStringLiteral("%1 phút %2 giây").arg(sec / 60).arg(sec % 60);
}

QString count(qint64 n)
{
    return QLocale().toString(n);
}
} // namespace

FolderTreeTab::FolderTreeTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_scanner = new FolderTreeScanner(this);
    connect(m_scanner, &FolderTreeScanner::progressTick, this, &FolderTreeTab::onProgressTick);
    connect(m_scanner, &FolderTreeScanner::scanFinished, this, &FolderTreeTab::onScanFinished);
    connect(m_scanner, &FolderTreeScanner::scanStopped, this, &FolderTreeTab::onScanStopped);
}

FolderTreeTab::~FolderTreeTab()
{
    if (m_scanner && m_scanner->isRunning())
    {
        m_scanner->requestStop();
        m_scanner->wait(3000);
    }
}

void FolderTreeTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* topRow = new QHBoxLayout();
    m_driveCombo = new QComboBox(this);
    m_driveCombo->setObjectName("treeDriveCombo");
    m_driveCombo->setStyleSheet(DiskUi::inputStyle());
    m_driveCombo->setMinimumWidth(260);
    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
        m_driveCombo->addItem(QString("%1  -  %2").arg(d.displayName, d.rootPath), d.rootPath);
    connect(m_driveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_customRoot.clear();
        updateDriveInfo();
    });
    topRow->addWidget(m_driveCombo);

    m_browseBtn = new QPushButton("Chọn thư mục khác...", this);
    m_browseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &FolderTreeTab::onBrowseClicked);
    topRow->addWidget(m_browseBtn);
    topRow->addStretch();

    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setObjectName("treeScanButton");
    m_scanBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &FolderTreeTab::onScanClicked);
    topRow->addWidget(m_scanBtn);

    m_stopBtn = new QPushButton("Dừng", this);
    m_stopBtn->setObjectName("treeStopButton");
    m_stopBtn->setStyleSheet(DiskUi::buttonStyle());
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &FolderTreeTab::onStopClicked);
    topRow->addWidget(m_stopBtn);
    root->addLayout(topRow);

    m_driveLabel = new QLabel(this);
    m_driveLabel->setStyleSheet("color: #1f2328; font-size: 12px; font-weight: 600;");
    root->addWidget(m_driveLabel);
    m_driveBar = new QProgressBar(this);
    m_driveBar->setRange(0, 1000);
    m_driveBar->setTextVisible(false);
    m_driveBar->setFixedHeight(8);
    root->addWidget(m_driveBar);

    m_pathLabel = new QLabel(this);
    m_pathLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_pathLabel->setWordWrap(true);
    root->addWidget(m_pathLabel);

    m_model = new FolderTreeModel(this);
    m_view = new QTreeView(this);
    m_view->setObjectName("folderTreeView");
    m_view->setModel(m_model);
    m_view->setStyleSheet(DiskUi::viewStyle());
    m_view->setItemDelegateForColumn(FolderTreeModel::ShareColumn, new ShareBarDelegate(m_view));
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setAnimated(false);
    m_view->setIndentation(18);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setSortingEnabled(false); // luôn giảm dần theo dung lượng - đó là mục đích của tab này
    QHeaderView* header = m_view->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(FolderTreeModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(FolderTreeModel::SizeColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(FolderTreeModel::ShareColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(FolderTreeModel::FilesColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(FolderTreeModel::FoldersColumn, QHeaderView::Fixed);
    header->resizeSection(FolderTreeModel::SizeColumn, 110);
    header->resizeSection(FolderTreeModel::ShareColumn, 170);
    header->resizeSection(FolderTreeModel::FilesColumn, 90);
    header->resizeSection(FolderTreeModel::FoldersColumn, 90);
    header->setSectionsClickable(false);
    connect(m_view, &QTreeView::customContextMenuRequested, this, &FolderTreeTab::onContextMenu);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() { onSelectionChanged(); });
    root->addWidget(m_view, 1);

    m_busyBar = new QProgressBar(this);
    m_busyBar->setRange(0, 0);
    m_busyBar->setTextVisible(false);
    m_busyBar->setFixedHeight(6);
    m_busyBar->setVisible(false);
    m_busyBar->setStyleSheet(DiskUi::thinBarStyle());
    root->addWidget(m_busyBar);

    m_statusLabel = new QLabel("Chọn ổ đĩa hoặc thư mục rồi bấm \"Quét\" - quét xong sẽ hiện cây thư mục sắp theo dung lượng.", this);
    m_statusLabel->setObjectName("treeStatusLabel");
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_statusLabel->setTextFormat(Qt::PlainText); // chứa đường dẫn (dữ liệu bên ngoài)
    root->addWidget(m_statusLabel);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("treeSummaryLabel");
    m_summaryLabel->setStyleSheet(DiskUi::bannerStyle("info"));
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setTextFormat(Qt::PlainText);
    m_summaryLabel->setVisible(false);
    root->addWidget(m_summaryLabel);

    auto* bottomRow = new QHBoxLayout();
    m_expandBtn = new QPushButton("Mở rộng 1 cấp", this);
    m_expandBtn->setStyleSheet(DiskUi::buttonStyle());
    m_expandBtn->setCursor(Qt::PointingHandCursor);
    m_expandBtn->setEnabled(false);
    connect(m_expandBtn, &QPushButton::clicked, this, [this]() {
        // Mở rộng thêm một cấp dưới mọi nhánh đang mở.
        const QModelIndex top = m_model->index(0, 0);
        if (!top.isValid())
            return;
        std::vector<QModelIndex> pending{top};
        while (!pending.empty())
        {
            const QModelIndex idx = pending.back();
            pending.pop_back();
            if (!m_view->isExpanded(idx))
            {
                if (m_model->hasChildren(idx))
                    m_view->expand(idx);
                continue; // chỉ mở thêm MỘT cấp mới
            }
            for (int r = 0; r < m_model->rowCount(idx); ++r)
                pending.push_back(m_model->index(r, 0, idx));
        }
    });
    bottomRow->addWidget(m_expandBtn);

    m_collapseBtn = new QPushButton("Thu gọn", this);
    m_collapseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_collapseBtn->setCursor(Qt::PointingHandCursor);
    m_collapseBtn->setEnabled(false);
    connect(m_collapseBtn, &QPushButton::clicked, this, [this]() {
        m_view->collapseAll();
        const QModelIndex top = m_model->index(0, 0);
        if (top.isValid())
            m_view->expand(top);
    });
    bottomRow->addWidget(m_collapseBtn);

    m_heaviestBtn = new QPushButton("🔥 Mở nhánh nặng nhất", this);
    m_heaviestBtn->setObjectName("treeHeaviestButton");
    m_heaviestBtn->setStyleSheet(DiskUi::buttonStyle());
    m_heaviestBtn->setCursor(Qt::PointingHandCursor);
    m_heaviestBtn->setEnabled(false);
    connect(m_heaviestBtn, &QPushButton::clicked, this, &FolderTreeTab::onExpandHeaviestClicked);
    bottomRow->addWidget(m_heaviestBtn);
    bottomRow->addStretch();

    m_copyBtn = new QPushButton("📋 Sao chép đường dẫn", this);
    m_copyBtn->setStyleSheet(DiskUi::buttonStyle());
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setEnabled(false);
    connect(m_copyBtn, &QPushButton::clicked, this, &FolderTreeTab::onCopyPathClicked);
    bottomRow->addWidget(m_copyBtn);

    m_explorerBtn = new QPushButton("📂 Mở trong Explorer", this);
    m_explorerBtn->setStyleSheet(DiskUi::buttonStyle());
    m_explorerBtn->setCursor(Qt::PointingHandCursor);
    m_explorerBtn->setEnabled(false);
    connect(m_explorerBtn, &QPushButton::clicked, this, &FolderTreeTab::onOpenInExplorerClicked);
    bottomRow->addWidget(m_explorerBtn);
    root->addLayout(bottomRow);

    updateDriveInfo();
}

QString FolderTreeTab::selectedPath() const
{
    if (!m_customRoot.isEmpty())
        return m_customRoot;
    return m_driveCombo->currentData().toString();
}

void FolderTreeTab::setScanRoot(const QString& path)
{
    m_customRoot = QDir::cleanPath(path);
    updateDriveInfo();
}

void FolderTreeTab::updateDriveInfo()
{
    const QString path = selectedPath();
    m_pathLabel->setText(path.isEmpty() ? QString() : "Sẽ quét: " + QDir::toNativeSeparators(path));
    // Thông tin dung lượng ổ chứa thư mục sẽ quét (ổ không đọc được thì ẩn).
    const DriveSpaceInfo* found = nullptr;
    const auto drives = DiskSpaceInfo::listDrives();
    for (const DriveSpaceInfo& d : drives)
        if (!path.isEmpty() && path.startsWith(d.rootPath, Qt::CaseInsensitive))
            found = &d;
    if (!found)
    {
        m_driveLabel->clear();
        m_driveBar->setVisible(false);
        return;
    }
    m_driveBar->setVisible(true);
    m_driveBar->setValue(static_cast<int>(found->usedRatio() * 1000));
    m_driveBar->setStyleSheet(DiskUi::thinBarStyle(DiskUi::usageColor(found->usedRatio() * 100.0)));
    m_driveLabel->setText(QString("💽 Ổ %1 (%2): đã dùng %3 / %4 (%5%) - còn trống %6")
                              .arg(found->rootPath, found->fileSystem, DiskUi::formatBytes(found->usedBytes()),
                                   DiskUi::formatBytes(found->totalBytes), QString::number(found->usedRatio() * 100.0, 'f', 0),
                                   DiskUi::formatBytes(found->freeBytes)));
}

void FolderTreeTab::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục để quét", selectedPath());
    if (dir.isEmpty())
        return;
    setScanRoot(dir);
}

bool FolderTreeTab::isScanningNow() const
{
    return m_scanner && m_scanner->isRunning();
}

void FolderTreeTab::stopScanIfRunning()
{
    if (m_scanner && m_scanner->isRunning())
        m_scanner->requestStop();
}

void FolderTreeTab::setScanningUi(bool scanning)
{
    m_scanBtn->setVisible(!scanning);
    m_stopBtn->setVisible(scanning);
    m_busyBar->setVisible(scanning);
    m_driveCombo->setEnabled(!scanning);
    m_browseBtn->setEnabled(!scanning);
}

void FolderTreeTab::onScanClicked()
{
    const QString root = selectedPath();
    if (root.isEmpty())
    {
        QMessageBox::warning(this, "Cây thư mục", "Không có ổ đĩa/thư mục nào để quét.");
        return;
    }
    if (!QFileInfo(root).isDir())
    {
        QMessageBox::warning(this, "Cây thư mục", "Thư mục này không tồn tại hoặc không đọc được:\n" + QDir::toNativeSeparators(root));
        return;
    }

    // Tránh quét chồng chéo với tab khác đang đổi kích thước/dọn dẹp/quét trên cùng ổ - xem DiskCleanupWindow::isAnyOtherTabBusy().
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
    // Lượt quét trước đã báo xong nhưng luồng có thể chưa thoát hẳn - xem LargeFilesTab::onScanClicked().
    if (m_scanner->isRunning() && !m_scanner->wait(3000))
    {
        m_statusLabel->setText("⚠ Lượt quét trước chưa kết thúc - vui lòng thử lại sau giây lát.");
        return;
    }

    m_model->clear(); // giải phóng cây cũ trước khi quét lại
    m_summaryLabel->setVisible(false);
    m_expandBtn->setEnabled(false);
    m_collapseBtn->setEnabled(false);
    m_heaviestBtn->setEnabled(false);
    onSelectionChanged();
    setScanningUi(true);
    m_scannedRoot = root;
    m_statusLabel->setText("⏳ Đang quét " + QDir::toNativeSeparators(root) + "...");

    m_scanner->setRootPath(root);
    // Ưu tiên thấp: quét cả ổ là I/O + CPU nặng - nhường cho luồng giao diện để cửa sổ không bị "Không phản hồi".
    m_scanner->startScan(QThread::LowPriority);
}

void FolderTreeTab::onStopClicked()
{
    m_scanner->requestStop();
    m_statusLabel->setText("Đang dừng...");
}

void FolderTreeTab::onProgressTick(qint64 files, qint64 folders, qint64 bytes, QString currentFolder)
{
    const QString elided = m_statusLabel->fontMetrics().elidedText(QDir::toNativeSeparators(currentFolder), Qt::ElideMiddle, 420);
    m_statusLabel->setText(QString("⏳ Đang quét... %1 tệp, %2 thư mục, %3 - %4")
                               .arg(count(files), count(folders), DiskUi::formatBytes(bytes), elided));
}

QString FolderTreeTab::buildSummary(const FolderTree& tree) const
{
    const FolderTreeStats& s = tree.stats;
    QString text = QString("✓ Quét xong trong %1: %2 tệp, %3 thư mục. Chiếm trên đĩa %4 (kích thước tệp cộng lại: %5).")
                       .arg(duration(s.elapsedMs), count(s.files), count(s.folders), DiskUi::formatBytes(s.bytes),
                            DiskUi::formatBytes(s.logicalBytes));

    // Quét cả ổ: so với số Windows báo "đã dùng" để người dùng biết phần chênh là gì (không giả vờ khớp tuyệt đối).
    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
    {
        if (QDir::cleanPath(tree.rootPath).compare(QDir::cleanPath(d.rootPath), Qt::CaseInsensitive) != 0)
            continue;
        const qint64 diff = d.usedBytes() - s.bytes;
        text += QString("\nWindows báo ổ %1 đã dùng %2; quét thấy %3").arg(d.rootPath, DiskUi::formatBytes(d.usedBytes()), DiskUi::formatBytes(s.bytes));
        if (diff > 0)
            text += QString(", chênh %1 - thường là tệp hệ thống NTFS ẩn ($MFT, $LogFile...), điểm khôi phục/ảnh bóng (System Volume Information), "
                            "thư mục không đọc được và tệp đang bị khóa.").arg(DiskUi::formatBytes(diff));
        else if (diff < 0)
            text += QString(", nhiều hơn %1 - thường do liên kết cứng (vd WinSxS) bị đếm nhiều lần.").arg(DiskUi::formatBytes(-diff));
        else
            text += ".";
        break;
    }
    if (s.unreadableFolders > 0)
    {
        text += QString("\n%1 thư mục không đọc được (hiện màu đỏ trong cây).").arg(count(s.unreadableFolders));
        if (!PartitionManager::isElevated())
            text += " Chạy ứng dụng với quyền Administrator để quét cả các thư mục hệ thống.";
    }
    if (s.estimatedFiles > 0)
        text += QString("\nỔ này không báo được dung lượng cấp phát thật nên %1 tệp được ƯỚC TÍNH (làm tròn theo cụm %2).")
                    .arg(count(s.estimatedFiles), DiskUi::formatBytes(s.clusterBytes));
    if (s.cloudFiles > 0)
        text += QString("\n%1 tệp OneDrive chỉ có trên đám mây (chưa tải về) không chiếm chỗ trên đĩa nên tính 0 byte.").arg(count(s.cloudFiles));
    if (s.skippedLinks > 0)
        text += QString("\n%1 junction/symlink bị bỏ qua (không đi vào, không cộng) để khỏi đếm trùng.").arg(count(s.skippedLinks));
    return text;
}

void FolderTreeTab::onScanFinished(std::shared_ptr<FolderTree> tree)
{
    setScanningUi(false);
    if (!tree || !tree->root)
    {
        m_statusLabel->setText("Không có kết quả quét.");
        return;
    }
    m_model->setTree(tree);
    const QModelIndex top = m_model->index(0, 0);
    if (top.isValid())
    {
        m_view->expand(top);
        m_view->setCurrentIndex(top);
    }
    if (tree->root->unreadable && tree->stats.files == 0)
        m_statusLabel->setText("⚠ Không đọc được thư mục này: " + QDir::toNativeSeparators(tree->rootPath));
    else
        m_statusLabel->setText(QString("✓ Đã quét %1 - %2 chiếm trên đĩa.").arg(QDir::toNativeSeparators(tree->rootPath), DiskUi::formatBytes(tree->stats.bytes)));
    m_summaryLabel->setText(buildSummary(*tree));
    m_summaryLabel->setVisible(true);
    m_expandBtn->setEnabled(true);
    m_collapseBtn->setEnabled(true);
    m_heaviestBtn->setEnabled(true);
    onSelectionChanged();
}

void FolderTreeTab::onScanStopped()
{
    setScanningUi(false);
    m_statusLabel->setText("Đã dừng quét.");
}

void FolderTreeTab::onSelectionChanged()
{
    const QModelIndex cur = m_view->currentIndex();
    const bool has = cur.isValid() && !m_model->pathAt(cur).isEmpty();
    m_explorerBtn->setEnabled(has);
    m_copyBtn->setEnabled(has);
}

void FolderTreeTab::onExpandHeaviestClicked()
{
    // Đi xuống theo dòng NẶNG NHẤT ở mỗi cấp (dòng đầu tiên vì cây luôn giảm dần), dừng khi gặp tệp/dòng gộp hoặc khi
    // nhánh đó chỉ còn chiếm < 15% thư mục cha (không còn "nặng đáng kể" nữa).
    QModelIndex cur = m_model->index(0, 0);
    if (!cur.isValid())
        return;
    m_view->expand(cur);
    for (int depth = 0; depth < 12; ++depth)
    {
        const QModelIndex first = m_model->index(0, 0, cur);
        if (!first.isValid() || m_model->kindAt(first) != FolderTreeModel::Kind::Folder)
            break;
        if (first.data(FolderTreeModel::ShareRole).toDouble() < 0.15)
            break;
        cur = first;
        if (m_model->hasChildren(cur))
            m_view->expand(cur);
    }
    m_view->setCurrentIndex(cur);
    m_view->scrollTo(cur, QAbstractItemView::PositionAtCenter);
}

void FolderTreeTab::onOpenInExplorerClicked()
{
    const QModelIndex cur = m_view->currentIndex();
    const QString path = m_model->pathAt(cur);
    if (path.isEmpty())
        return;
    if (!QFileInfo::exists(path))
    {
        m_statusLabel->setText("⚠ Không còn tồn tại: " + QDir::toNativeSeparators(path));
        return;
    }
    // Đường dẫn tuyệt đối tới explorer.exe - xem LargeFilesTab::onOpenFolderClicked().
    const QString winDir = FsSafety::windowsDirectory();
    if (winDir.isEmpty())
        return;
    const bool isFile = m_model->kindAt(cur) == FolderTreeModel::Kind::File;
    const QString explorer = QDir::toNativeSeparators(winDir + "/explorer.exe");
    if (isFile)
        QProcess::startDetached(explorer, {"/select,", QDir::toNativeSeparators(path)});
    else
        QProcess::startDetached(explorer, {QDir::toNativeSeparators(path)});
}

void FolderTreeTab::onCopyPathClicked()
{
    const QString path = m_model->pathAt(m_view->currentIndex());
    if (!path.isEmpty())
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
}

void FolderTreeTab::onContextMenu(const QPoint& pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid())
        return;
    m_view->setCurrentIndex(idx);
    QMenu menu(this);
    QAction* open = menu.addAction("📂 Mở trong Explorer");
    QAction* copy = menu.addAction("📋 Sao chép đường dẫn");
    QAction* size = menu.addAction("Sao chép dung lượng");
    QAction* chosen = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (chosen == open)
        onOpenInExplorerClicked();
    else if (chosen == copy)
        onCopyPathClicked();
    else if (chosen == size)
        QApplication::clipboard()->setText(DiskUi::formatBytes(m_model->bytesAt(idx)));
}
