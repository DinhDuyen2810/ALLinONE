#include "LargeFilesTab.h"

#include "DiskCleanupWindow.h"
#include "DiskUiStyle.h"
#include "engine/DiskSpaceInfo.h"
#include "engine/FsSafety.h"
#include "engine/RecycleBinOps.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

LargeFilesTab::LargeFilesTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_scanner = new LargeFileScanner(this);
    connect(m_scanner, &LargeFileScanner::progressTick, this, &LargeFilesTab::onProgressTick);
    connect(m_scanner, &LargeFileScanner::scanFinished, this, &LargeFilesTab::onScanFinished);
    connect(m_scanner, &LargeFileScanner::scanStopped, this, &LargeFilesTab::onScanStopped);
}

LargeFilesTab::~LargeFilesTab()
{
    if (m_scanner && m_scanner->isRunning())
    {
        m_scanner->requestStop();
        m_scanner->wait(3000);
    }
}

void LargeFilesTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* topRow = new QHBoxLayout();
    m_driveCombo = new QComboBox(this);
    m_driveCombo->setStyleSheet(DiskUi::inputStyle());
    m_driveCombo->setMinimumWidth(160);
    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
        m_driveCombo->addItem(QString("%1 (%2)").arg(d.displayName, d.rootPath), d.rootPath);
    connect(m_driveCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_customRoot.clear();
        m_pathLabel->setText("Sẽ quét: " + selectedPath());
    });
    topRow->addWidget(m_driveCombo);

    m_browseBtn = new QPushButton("Chọn thư mục khác...", this);
    m_browseBtn->setStyleSheet(DiskUi::buttonStyle());
    m_browseBtn->setCursor(Qt::PointingHandCursor);
    connect(m_browseBtn, &QPushButton::clicked, this, &LargeFilesTab::onBrowseClicked);
    topRow->addWidget(m_browseBtn);

    auto* sizeLabel = new QLabel("Lớn hơn:", this);
    sizeLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    topRow->addWidget(sizeLabel);

    m_minSizeSpin = new QSpinBox(this);
    m_minSizeSpin->setStyleSheet(DiskUi::inputStyle());
    m_minSizeSpin->setRange(1, 102400);
    m_minSizeSpin->setValue(100);
    m_minSizeSpin->setSuffix(" MB");
    m_minSizeSpin->setSingleStep(50);
    topRow->addWidget(m_minSizeSpin);
    topRow->addStretch();
    root->addLayout(topRow);

    m_pathLabel = new QLabel("Sẽ quét: " + selectedPath(), this);
    m_pathLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_pathLabel->setWordWrap(true);
    root->addWidget(m_pathLabel);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"Đường dẫn", "Kích thước", "Sửa đổi lần cuối"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DiskUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &LargeFilesTab::onTableSelectionChanged);
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

    m_statusLabel = new QLabel("Chọn phạm vi và bấm \"Quét\".", this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &LargeFilesTab::onScanClicked);
    bottomRow->addWidget(m_scanBtn);

    m_stopBtn = new QPushButton("Dừng", this);
    m_stopBtn->setStyleSheet(DiskUi::buttonStyle());
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &LargeFilesTab::onStopClicked);
    bottomRow->addWidget(m_stopBtn);

    bottomRow->addStretch();

    m_openFolderBtn = new QPushButton("📂 Mở thư mục chứa", this);
    m_openFolderBtn->setStyleSheet(DiskUi::buttonStyle());
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    m_openFolderBtn->setEnabled(false);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &LargeFilesTab::onOpenFolderClicked);
    bottomRow->addWidget(m_openFolderBtn);

    m_deleteBtn = new QPushButton("🗑 Xóa vào Thùng rác", this);
    m_deleteBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setEnabled(false);
    connect(m_deleteBtn, &QPushButton::clicked, this, &LargeFilesTab::onDeleteSelectedClicked);
    bottomRow->addWidget(m_deleteBtn);

    root->addLayout(bottomRow);
}

QString LargeFilesTab::selectedPath() const
{
    if (!m_customRoot.isEmpty())
        return m_customRoot;
    return m_driveCombo->currentData().toString();
}

void LargeFilesTab::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục để quét", selectedPath());
    if (dir.isEmpty())
        return;
    m_customRoot = dir;
    m_pathLabel->setText("Sẽ quét: " + m_customRoot);
}

bool LargeFilesTab::isScanningNow() const
{
    return m_scanner && m_scanner->isRunning();
}

void LargeFilesTab::stopScanIfRunning()
{
    if (m_scanner && m_scanner->isRunning())
        m_scanner->requestStop();
}

void LargeFilesTab::onScanClicked()
{
    const QString root = selectedPath();
    if (root.isEmpty())
    {
        QMessageBox::warning(this, "Tìm tệp lớn", "Không có thư mục nào để quét.");
        return;
    }

    // Tránh quét chồng chéo trên cùng ổ đĩa với một tab khác đang đổi kích thước/dọn dẹp/quét (vd tab
    // Quản lý phân vùng đang shrink chính ổ đang quét đây) - xem DiskCleanupWindow::isAnyOtherTabBusy().
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

    m_table->setRowCount(0);
    m_results.clear();
    m_openFolderBtn->setEnabled(false);
    m_deleteBtn->setEnabled(false);

    m_scanBtn->setVisible(false);
    m_stopBtn->setVisible(true);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang quét " + root + "...");

    m_scanner->setRootPath(root);
    m_scanner->setMinSizeBytes(static_cast<qint64>(m_minSizeSpin->value()) * 1024 * 1024);
    m_scanner->setMaxResults(300);
    // Ưu tiên thấp: quét cả ổ đĩa (hàng trăm nghìn tệp) là CPU/I-O nặng - nhường CPU cho luồng giao
    // diện để cửa sổ không bao giờ bị Windows báo "Không phản hồi" dù quét lâu.
    m_scanner->startScan(QThread::LowPriority);
}

void LargeFilesTab::onStopClicked()
{
    m_scanner->requestStop();
    m_statusLabel->setText("Đang dừng...");
}

void LargeFilesTab::onProgressTick(qint64 filesScanned, QString currentPath)
{
    // Rút gọn đường dẫn (giữ đầu + cuối) để không tràn dòng - mục đích chính là CHO THẤY ứng dụng vẫn
    // đang chạy (không bị treo), không cần hiển thị đường dẫn đầy đủ.
    const QString elided = m_statusLabel->fontMetrics().elidedText(currentPath, Qt::ElideMiddle, 480);
    m_statusLabel->setText(QString("⏳ Đang quét... %1 tệp đã kiểm tra - %2").arg(filesScanned).arg(elided));
}

void LargeFilesTab::onScanFinished(QList<LargeFileEntry> results)
{
    m_scanBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(QString("✓ Quét xong: tìm thấy %1 tệp lớn.").arg(results.size()));
    populateTable(results);
}

void LargeFilesTab::onScanStopped()
{
    m_scanBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Đã dừng quét.");
}

void LargeFilesTab::populateTable(const QList<LargeFileEntry>& results)
{
    m_results = results;
    m_table->setRowCount(results.size());
    for (int i = 0; i < results.size(); ++i)
    {
        const LargeFileEntry& e = results[i];
        auto* pathItem = new QTableWidgetItem(e.path);
        pathItem->setFlags(pathItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, pathItem);

        auto* sizeItem = new QTableWidgetItem(DiskUi::formatBytes(e.sizeBytes));
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeItem->setFlags(sizeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, sizeItem);

        auto* dateItem = new QTableWidgetItem(e.lastModified.toString("yyyy-MM-dd HH:mm"));
        dateItem->setFlags(dateItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 2, dateItem);
    }
}

void LargeFilesTab::onTableSelectionChanged()
{
    const bool hasSelection = !m_table->selectionModel()->selectedRows().isEmpty();
    m_deleteBtn->setEnabled(hasSelection);
    m_openFolderBtn->setEnabled(m_table->selectionModel()->selectedRows().size() == 1);
}

void LargeFilesTab::onOpenFolderClicked()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.size() != 1 || rows.first().row() >= m_results.size())
        return;
    const QString path = m_results[rows.first().row()].path;
    // Đường dẫn tuyệt đối tới explorer.exe trong thư mục Windows - không để Windows tự tìm "explorer.exe"
    // theo thư mục ứng dụng/thư mục làm việc/PATH (một tệp trùng tên đặt cạnh exe sẽ được chạy thay,
    // đặc biệt nguy hiểm khi ứng dụng đang chạy với quyền Administrator).
    const QString winDir = FsSafety::windowsDirectory();
    if (winDir.isEmpty())
        return;
    QProcess::startDetached(QDir::toNativeSeparators(winDir + "/explorer.exe"), {"/select,", QDir::toNativeSeparators(path)});
}

void LargeFilesTab::onDeleteSelectedClicked()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;

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

    // Lọc TRƯỚC các tệp không thể vào Thùng rác (lớn hơn dung lượng Thùng rác của ổ, Thùng rác bị tắt,
    // ổ mạng/ổ tháo rời): Shell sẽ XÓA VĨNH VIỄN chúng nếu cứ đưa vào - đúng loại tệp mà tab "tệp lớn"
    // hay gặp nhất, trong khi nút bấm ghi rõ "Xóa vào Thùng rác".
    QStringList paths;
    QStringList notRecyclable;
    qint64 total = 0;
    for (const QModelIndex& idx : rows)
    {
        if (idx.row() >= m_results.size())
            continue;
        const LargeFileEntry& entry = m_results[idx.row()];
        const QString reason = RecycleBinOps::notRecyclableReason(entry.path, entry.sizeBytes);
        if (!reason.isEmpty())
        {
            notRecyclable << QDir::toNativeSeparators(entry.path) + "\n      (" + reason + ")";
            continue;
        }
        paths << entry.path;
        total += entry.sizeBytes;
    }

    if (!notRecyclable.isEmpty())
    {
        QMessageBox::warning(this, "Không thể đưa vào Thùng rác",
            QString("%1 tệp KHÔNG thể đưa vào Thùng rác để khôi phục được nên sẽ được GIỮ NGUYÊN:\n\n  • %2\n\n"
                    "Nếu thật sự muốn xóa hẳn các tệp này, hãy dùng \"Mở thư mục chứa\" rồi xóa trong Explorer.")
                .arg(notRecyclable.size())
                .arg(notRecyclable.mid(0, 8).join("\n  • ") + (notRecyclable.size() > 8 ? "\n  • ..." : "")));
    }
    if (paths.isEmpty())
        return;

    if (QMessageBox::question(this, "Xóa tệp",
                              QString("Chuyển %1 tệp (%2) vào Thùng rác?").arg(paths.size()).arg(DiskUi::formatBytes(total)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString error;
    const bool ok = RecycleBinOps::moveToRecycleBin(paths, &error);

    // Dù thành công hay lỗi giữa chừng: gỡ khỏi bảng đúng những tệp THẬT SỰ không còn trên đĩa (kiểm tra
    // không đi theo liên kết) - trước đây khi có lỗi, hàm thoát sớm và để lại các dòng của tệp đã bị xóa.
    int removed = 0;
    for (int row = m_results.size() - 1; row >= 0; --row)
    {
        if (paths.contains(m_results[row].path) && !FsSafety::existsNoFollow(m_results[row].path))
        {
            m_table->removeRow(row);
            m_results.removeAt(row);
            ++removed;
        }
    }

    if (!ok || removed < paths.size())
    {
        m_statusLabel->setText(QString("⚠ Đã chuyển %1/%2 tệp vào Thùng rác.").arg(removed).arg(paths.size()));
        QMessageBox::critical(this, "Lỗi",
            QString("Chỉ chuyển được %1/%2 tệp vào Thùng rác.\n%3").arg(removed).arg(paths.size()).arg(error));
        return;
    }
    m_statusLabel->setText(QString("✓ Đã chuyển %1 tệp vào Thùng rác.").arg(paths.size()));
}

