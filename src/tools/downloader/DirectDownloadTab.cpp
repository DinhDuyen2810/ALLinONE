#include "DirectDownloadTab.h"

#include "DownloaderUiStyle.h"
#include "engine/FileDownloader.h"
#include "model/DownloadItem.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

DirectDownloadTab::DirectDownloadTab(FileDownloader* downloader, QWidget* parent)
    : QWidget(parent)
    , m_downloader(downloader)
{
    m_saveFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    buildUi();

    connect(m_downloader, &FileDownloader::itemUpdated, this, &DirectDownloadTab::onItemUpdated);

    connect(qApp->clipboard(), &QClipboard::dataChanged, this, &DirectDownloadTab::onClipboardChanged);
}

void DirectDownloadTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* introLabel = new QLabel(
        "Dán URL TRỰC TIẾP tới một tệp (vd kết thúc bằng .jpg/.png/.mp4/.pdf/.zip...) - hoặc bất kỳ "
        "URL nào server trả về nội dung tải được. Nếu bạn chỉ có URL một TRANG web (không phải link tệp "
        "trực tiếp), hãy dùng tab \"Quét trang web\" để tìm các tệp media/tài liệu trong trang đó.",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    auto* urlRow = new QHBoxLayout();
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setStyleSheet(DownloaderUi::inputStyle());
    m_urlEdit->setPlaceholderText("https://...");
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &DirectDownloadTab::onAddClicked);
    urlRow->addWidget(m_urlEdit, 1);
    m_addBtn = new QPushButton("+ Thêm vào hàng đợi", this);
    m_addBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_addBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addBtn, &QPushButton::clicked, this, &DirectDownloadTab::onAddClicked);
    urlRow->addWidget(m_addBtn);
    root->addLayout(urlRow);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu:", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(m_saveFolder, this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_folderLabel->setWordWrap(true); // đường dẫn thư mục có thể rất dài - tránh bị cắt cụt không thấy hết
    m_folderLabel->setToolTip(m_saveFolder);
    folderRow->addWidget(m_folderLabel, 1);
    m_chooseFolderBtn = new QPushButton("Đổi...", this);
    m_chooseFolderBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_chooseFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_chooseFolderBtn, &QPushButton::clicked, this, &DirectDownloadTab::onChooseFolderClicked);
    folderRow->addWidget(m_chooseFolderBtn);
    root->addLayout(folderRow);

    m_clipboardCheck = new QCheckBox("Tự động điền link vào ô trên khi sao chép (không tự thêm vào hàng đợi)", this);
    m_clipboardCheck->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_clipboardCheck);

    auto* queueHeaderRow = new QHBoxLayout();
    auto* queueLabel = new QLabel("Hàng đợi tải xuống:", this);
    queueLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    queueHeaderRow->addWidget(queueLabel);
    queueHeaderRow->addStretch();
    m_startAllBtn = new QPushButton("▶ Bắt đầu tất cả", this);
    m_startAllBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_startAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_startAllBtn, &QPushButton::clicked, this, &DirectDownloadTab::onStartAllClicked);
    queueHeaderRow->addWidget(m_startAllBtn);
    root->addLayout(queueHeaderRow);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"Tên tệp", "Kích thước", "Tiến độ", "Tốc độ", "Trạng thái"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &DirectDownloadTab::onTableContextMenu);
    root->addWidget(m_table, 1);
}

QString DirectDownloadTab::suggestedDestPath(const QString& url) const
{
    const QUrl u(url);
    QString name = u.fileName();
    if (name.isEmpty() || !name.contains('.'))
        name = QString("tai-ve-%1").arg(QDateTime::currentMSecsSinceEpoch());

    QString destPath = m_saveFolder + "/" + name;
    // Tránh ghi đè tệp đã có cùng tên - thêm hậu tố (1), (2)... giống Windows/trình duyệt vẫn làm.
    if (QFile::exists(destPath))
    {
        const int dot = name.lastIndexOf('.');
        const QString base = dot >= 0 ? name.left(dot) : name;
        const QString ext = dot >= 0 ? name.mid(dot) : QString();
        int counter = 1;
        do {
            destPath = QString("%1/%2 (%3)%4").arg(m_saveFolder, base).arg(counter++).arg(ext);
        } while (QFile::exists(destPath));
    }
    return destPath;
}

void DirectDownloadTab::onAddClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    enqueueUrl(url);
    m_urlEdit->clear();
}

void DirectDownloadTab::enqueueUrl(const QString& url)
{
    const QString destPath = suggestedDestPath(url);
    const int id = m_downloader->enqueue(url, destPath);
    addRowForId(id);
    m_downloader->start(id);
}

void DirectDownloadTab::addRowForId(int id)
{
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_idToRow[id] = row;

    for (int col = 0; col < 5; ++col)
    {
        if (col == 2) continue; // cột tiến độ dùng QProgressBar riêng, không phải QTableWidgetItem
        auto* item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, col, item);
    }

    auto* progressBar = new QProgressBar(this);
    progressBar->setRange(0, 100);
    progressBar->setTextVisible(true);
    m_table->setCellWidget(row, 2, progressBar);

    refreshRow(id);
}

void DirectDownloadTab::refreshRow(int id)
{
    if (!m_idToRow.contains(id))
        return;
    const int row = m_idToRow[id];
    const DownloadItem* it = m_downloader->item(id);
    if (!it)
        return;

    m_table->item(row, 0)->setText(it->fileName());
    m_table->item(row, 0)->setToolTip(it->url);
    m_table->item(row, 1)->setText(it->totalBytes > 0 ? DownloaderUi::formatBytes(it->totalBytes) : "?");

    auto* progressBar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, 2));
    if (progressBar)
    {
        if (it->totalBytes > 0)
        {
            progressBar->setRange(0, 100);
            progressBar->setValue(static_cast<int>(100.0 * it->receivedBytes / it->totalBytes));
        }
        else
        {
            progressBar->setRange(0, 0); // chưa biết tổng dung lượng - hiện dạng "đang chạy" không xác định
        }
    }

    m_table->item(row, 3)->setText(it->status == DownloadItem::Status::Downloading
                                        ? DownloaderUi::formatSpeed(it->speedBytesPerSec)
                                        : "-");
    m_table->item(row, 4)->setText(DownloadItem::statusLabel(it->status) +
                                    (it->status == DownloadItem::Status::Failed && !it->error.isEmpty()
                                         ? (" (" + it->error + ")")
                                         : ""));
}

void DirectDownloadTab::onItemUpdated(int id)
{
    refreshRow(id);
}

void DirectDownloadTab::onChooseFolderClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục lưu", m_saveFolder);
    if (dir.isEmpty())
        return;
    m_saveFolder = dir;
    m_folderLabel->setText(m_saveFolder);
    m_folderLabel->setToolTip(m_saveFolder);
}

void DirectDownloadTab::onStartAllClicked()
{
    m_downloader->startAllQueued();
}

void DirectDownloadTab::onTableContextMenu(const QPoint& pos)
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.isEmpty())
        return;
    const int row = rows.first().row();

    int targetId = -1;
    for (auto it = m_idToRow.constBegin(); it != m_idToRow.constEnd(); ++it)
        if (it.value() == row) { targetId = it.key(); break; }
    if (targetId < 0)
        return;
    const DownloadItem* item = m_downloader->item(targetId);
    if (!item)
        return;

    QMenu menu(this);
    QAction* startAct = menu.addAction("▶ Bắt đầu/Tiếp tục");
    QAction* pauseAct = menu.addAction("⏸ Tạm dừng");
    QAction* cancelAct = menu.addAction("✕ Hủy");
    startAct->setEnabled(item->status != DownloadItem::Status::Downloading &&
                         item->status != DownloadItem::Status::Completed);
    pauseAct->setEnabled(item->status == DownloadItem::Status::Downloading);
    cancelAct->setEnabled(item->status != DownloadItem::Status::Completed);

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == startAct) m_downloader->start(targetId);
    else if (chosen == pauseAct) m_downloader->pause(targetId);
    else if (chosen == cancelAct) m_downloader->cancel(targetId);
}

void DirectDownloadTab::onClipboardChanged()
{
    if (!m_clipboardCheck->isChecked())
        return;
    const QString text = qApp->clipboard()->text().trimmed();
    if (text.isEmpty() || text == m_lastClipboardText)
        return;
    m_lastClipboardText = text;

    static const QRegularExpression urlRx(R"(^https?://\S+$)", QRegularExpression::CaseInsensitiveOption);
    if (urlRx.match(text).hasMatch())
        m_urlEdit->setText(text); // chỉ ĐIỀN SẴN vào ô nhập - không tự thêm vào hàng đợi, người dùng vẫn phải tự bấm
}
