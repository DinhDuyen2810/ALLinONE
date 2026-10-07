#include "PageScanTab.h"

#include "DownloaderUiStyle.h"
#include "engine/FileDownloader.h"
#include "engine/PageMediaScanner.h"

#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

PageScanTab::PageScanTab(FileDownloader* downloader, QWidget* parent)
    : QWidget(parent)
    , m_downloader(downloader)
{
    m_saveFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    buildUi();

    m_scanner = new PageMediaScanner(this);
    connect(m_scanner, &PageMediaScanner::result, this, &PageScanTab::onScanResult);
    connect(m_scanner, &PageMediaScanner::errorOccurred, this, &PageScanTab::onScanError);
}

void PageScanTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* introLabel = new QLabel(
        "Dán URL một TRANG web để tìm ảnh/video/âm thanh/tài liệu có trong trang đó. Đây là phân tích "
        "mã nguồn HTML TĨNH (không chạy JavaScript) - các trang hiện đại tải nội dung động/lazy-load có "
        "thể không được tìm thấy đầy đủ; nếu vậy, hãy bấm chuột phải vào ảnh/video trên trình duyệt > "
        "\"Sao chép địa chỉ liên kết\" rồi dán thẳng vào tab \"Tải trực tiếp\".",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    auto* urlRow = new QHBoxLayout();
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setStyleSheet(DownloaderUi::inputStyle());
    m_urlEdit->setPlaceholderText("https://vi-du.com/trang-co-anh-video");
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &PageScanTab::onScanClicked);
    urlRow->addWidget(m_urlEdit, 1);
    m_scanBtn = new QPushButton("🔍 Quét trang", this);
    m_scanBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &PageScanTab::onScanClicked);
    urlRow->addWidget(m_scanBtn);
    root->addLayout(urlRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu:", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(m_saveFolder, this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    folderRow->addWidget(m_folderLabel, 1);
    m_chooseFolderBtn = new QPushButton("Đổi...", this);
    m_chooseFolderBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_chooseFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_chooseFolderBtn, &QPushButton::clicked, this, &PageScanTab::onChooseFolderClicked);
    folderRow->addWidget(m_chooseFolderBtn);
    root->addLayout(folderRow);

    auto* actionRow = new QHBoxLayout();
    m_selectAllBtn = new QPushButton("Chọn tất cả", this);
    m_selectAllBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_selectAllBtn->setCursor(Qt::PointingHandCursor);
    connect(m_selectAllBtn, &QPushButton::clicked, this, &PageScanTab::onSelectAllClicked);
    actionRow->addWidget(m_selectAllBtn);
    m_selectNoneBtn = new QPushButton("Bỏ chọn tất cả", this);
    m_selectNoneBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_selectNoneBtn->setCursor(Qt::PointingHandCursor);
    connect(m_selectNoneBtn, &QPushButton::clicked, this, &PageScanTab::onSelectNoneClicked);
    actionRow->addWidget(m_selectNoneBtn);
    actionRow->addStretch();
    m_downloadSelectedBtn = new QPushButton("⬇ Tải các mục đã chọn", this);
    m_downloadSelectedBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_downloadSelectedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadSelectedBtn, &QPushButton::clicked, this, &PageScanTab::onDownloadSelectedClicked);
    actionRow->addWidget(m_downloadSelectedBtn);
    root->addLayout(actionRow);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"", "Loại", "Tên tệp / URL"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    root->addWidget(m_table, 1);
}

void PageScanTab::onScanClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    m_statusLabel->setText("⏳ Đang quét...");
    m_table->setRowCount(0);
    m_links.clear();
    m_scanBtn->setEnabled(false);
    m_scanner->scan(url);
}

void PageScanTab::onScanResult(QList<MediaLink> links)
{
    m_scanBtn->setEnabled(true);
    m_links = links;

    if (links.isEmpty())
    {
        m_statusLabel->setText(
            "Không tìm thấy ảnh/video/âm thanh/tài liệu nào trong mã nguồn HTML tĩnh của trang này - có "
            "thể nội dung được tải động bằng JavaScript (xem ghi chú ở trên).");
        return;
    }
    m_statusLabel->setText(QString("Tìm thấy %1 liên kết.").arg(links.size()));

    m_table->setRowCount(links.size());
    for (int i = 0; i < links.size(); ++i)
    {
        const auto& link = links[i];
        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        checkItem->setCheckState(Qt::Unchecked);
        m_table->setItem(i, 0, checkItem);

        auto* typeItem = new QTableWidgetItem(MediaLink::typeLabel(link.type));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 1, typeItem);

        auto* nameItem = new QTableWidgetItem(link.fileName());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(link.url);
        m_table->setItem(i, 2, nameItem);
    }
}

void PageScanTab::onScanError(QString message)
{
    m_scanBtn->setEnabled(true);
    m_statusLabel->setText("⚠ " + message);
}

void PageScanTab::onSelectAllClicked()
{
    for (int i = 0; i < m_table->rowCount(); ++i)
        m_table->item(i, 0)->setCheckState(Qt::Checked);
}

void PageScanTab::onSelectNoneClicked()
{
    for (int i = 0; i < m_table->rowCount(); ++i)
        m_table->item(i, 0)->setCheckState(Qt::Unchecked);
}

void PageScanTab::onChooseFolderClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục lưu", m_saveFolder);
    if (dir.isEmpty())
        return;
    m_saveFolder = dir;
    m_folderLabel->setText(m_saveFolder);
}

QString PageScanTab::suggestedDestPath(const MediaLink& link) const
{
    QString name = link.fileName();
    QString destPath = m_saveFolder + "/" + name;
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

void PageScanTab::onDownloadSelectedClicked()
{
    int count = 0;
    for (int i = 0; i < m_table->rowCount(); ++i)
    {
        if (m_table->item(i, 0)->checkState() != Qt::Checked)
            continue;
        const auto& link = m_links[i];
        const int id = m_downloader->enqueue(link.url, suggestedDestPath(link));
        m_downloader->start(id);
        ++count;
    }

    if (count == 0)
    {
        QMessageBox::information(this, "Tải các mục đã chọn", "Chưa chọn mục nào - tick vào ô ở cột đầu trước.");
        return;
    }
    QMessageBox::information(this, "Đã thêm vào hàng đợi",
                              QString("Đã thêm %1 mục vào hàng đợi tải - xem tab \"Tải trực tiếp\" để theo "
                                      "dõi tiến độ.")
                                  .arg(count));
}
