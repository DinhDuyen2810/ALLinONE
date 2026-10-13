#include "PageScanTab.h"

#include "DownloaderSettings.h"
#include "DownloaderUiStyle.h"
#include "engine/DownloadQueue.h"
#include "engine/FileDownloader.h"
#include "engine/PageMediaScanner.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
enum Col { kCheck = 0, kType, kName, kOpen, kColCount };
}

PageScanTab::PageScanTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_queue(queue)
    , m_settings(settings)
{
    buildUi();

    m_scanner = new PageMediaScanner(this);
    connect(m_scanner, &PageMediaScanner::result, this, &PageScanTab::onScanResult);
    connect(m_scanner, &PageMediaScanner::errorOccurred, this, &PageScanTab::onScanError);
    connect(m_settings, &DownloaderSettings::changed, this, &PageScanTab::refreshFolderLabel);
}

void PageScanTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* introLabel = new QLabel(
        "Dán URL một TRANG web để tìm ảnh/video/âm thanh/tài liệu có trong trang đó (phân tích mã nguồn HTML TĨNH, không "
        "chạy JavaScript). Chọn các mục rồi \"Thêm vào hàng đợi\" - việc tải thực hiện ở tab \"Tải trực tiếp\". Trang tải "
        "nội dung động hoặc chặn chuột phải/sao chép link: bấm \"Quét sâu\" để bộ phân giải đầy đủ (yt-dlp, quét sâu mã "
        "nguồn, trình duyệt ẩn) xử lý.",
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
    m_deepBtn = new QPushButton("🔬 Quét sâu", this);
    m_deepBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_deepBtn->setCursor(Qt::PointingHandCursor);
    m_deepBtn->setToolTip("Đưa chính trang này vào hàng đợi để bộ phân giải đầy đủ (yt-dlp, quét sâu, trình duyệt ẩn) tìm tệp");
    connect(m_deepBtn, &QPushButton::clicked, this, &PageScanTab::onDeepScanClicked);
    urlRow->addWidget(m_deepBtn);
    root->addLayout(urlRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu (đổi ở tab Cài đặt):", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_folderLabel->setWordWrap(true); // đường dẫn thư mục có thể rất dài - tránh bị cắt cụt không thấy hết
    folderRow->addWidget(m_folderLabel, 1);
    root->addLayout(folderRow);
    refreshFolderLabel();

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
    m_addSelectedBtn = new QPushButton("+ Thêm các mục đã chọn vào hàng đợi", this);
    m_addSelectedBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_addSelectedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addSelectedBtn, &QPushButton::clicked, this, &PageScanTab::onAddSelectedClicked);
    actionRow->addWidget(m_addSelectedBtn);
    root->addLayout(actionRow);

    m_table = new QTableWidget(0, kColCount, this);
    m_table->setHorizontalHeaderLabels({"", "Loại", "Tên tệp / URL", ""});
    m_table->horizontalHeader()->setSectionResizeMode(kCheck, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kType, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kOpen, QHeaderView::Fixed);
    m_table->setColumnWidth(kOpen, 40);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    root->addWidget(m_table, 1);
}

void PageScanTab::refreshFolderLabel()
{
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
}

void PageScanTab::onScanClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    if (!FileDownloaderInternal::isHttpUrl(url))
    {
        m_statusLabel->setText("⚠ Địa chỉ không hợp lệ - chỉ hỗ trợ trang bắt đầu bằng http:// hoặc https://.");
        return;
    }
    m_statusLabel->setText("⏳ Đang quét...");
    m_table->setRowCount(0);
    m_links.clear();
    m_pageUrl = url;
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
            "Không tìm thấy ảnh/video/âm thanh/tài liệu nào trong mã nguồn HTML tĩnh của trang này - nội dung có thể được "
            "tải động bằng JavaScript hoặc ẩn sau trình phát. Thử bấm \"Quét sâu\".");
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
        m_table->setItem(i, kCheck, checkItem);

        auto* typeItem = new QTableWidgetItem(MediaLink::typeLabel(link.type));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, kType, typeItem);

        auto* nameItem = new QTableWidgetItem(link.fileName());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(link.url);
        m_table->setItem(i, kName, nameItem);

        auto* openBtn = new QPushButton("🌐", m_table);
        openBtn->setToolTip("Mở link này trên trình duyệt");
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setStyleSheet(DownloaderUi::buttonStyle() + "QPushButton { padding: 2px 6px; }");
        const QString linkUrl = link.url;
        connect(openBtn, &QPushButton::clicked, this, [linkUrl] {
            const QUrl u(linkUrl);
            if (u.isValid() && (u.scheme() == "http" || u.scheme() == "https"))
                QDesktopServices::openUrl(u);
        });
        m_table->setCellWidget(i, kOpen, openBtn);
    }
}

void PageScanTab::onScanError(QString message)
{
    m_scanBtn->setEnabled(true);
    m_statusLabel->setText("⚠ " + message + " - thử \"Quét sâu\" để dùng trình duyệt ẩn.");
}

void PageScanTab::onSelectAllClicked()
{
    for (int i = 0; i < m_table->rowCount(); ++i)
        m_table->item(i, kCheck)->setCheckState(Qt::Checked);
}

void PageScanTab::onSelectNoneClicked()
{
    for (int i = 0; i < m_table->rowCount(); ++i)
        m_table->item(i, kCheck)->setCheckState(Qt::Unchecked);
}

void PageScanTab::onAddSelectedClicked()
{
    int added = 0;
    for (int i = 0; i < m_table->rowCount() && i < m_links.size(); ++i)
    {
        if (m_table->item(i, kCheck)->checkState() != Qt::Checked)
            continue;
        const MediaLink& link = m_links[i];
        ResolvedMedia media;
        media.kind = ResolvedMedia::Kind::HttpFile;
        media.url = link.url;
        // Liên kết lấy từ một trang web bất kỳ - tên tệp là dữ liệu KHÔNG tin cậy; hàng đợi làm sạch lại lúc chọn đường
        // dẫn đích nhưng ta cũng làm sạch ngay từ đây.
        media.fileName = FileDownloaderInternal::sanitizeFileName(FileDownloaderInternal::fileNameFromUrl(link.url));
        media.title = media.fileName;
        media.mediaType = MediaLink::typeLabel(link.type);
        media.referer = m_pageUrl;
        media.note = "quét trang";
        if (m_queue->addResolved(media, m_pageUrl) >= 0)
            ++added;
    }

    if (added == 0)
    {
        QMessageBox::information(this, "Thêm vào hàng đợi",
                                 "Chưa chọn mục nào (hoặc các mục đã có sẵn trong hàng đợi) - tick vào ô ở cột đầu trước.");
        return;
    }
    QMessageBox::information(this, "Đã thêm vào hàng đợi",
                             QString("Đã thêm %1 mục vào hàng đợi (chưa tải). Sang tab \"Tải trực tiếp\" để chọn và tải.")
                                 .arg(added));
}

void PageScanTab::onDeepScanClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    if (!FileDownloaderInternal::isHttpUrl(url))
    {
        m_statusLabel->setText("⚠ Địa chỉ không hợp lệ - chỉ hỗ trợ trang bắt đầu bằng http:// hoặc https://.");
        return;
    }
    if (m_queue->addSource(url) >= 0)
        m_statusLabel->setText("✓ Đã đưa trang vào hàng đợi - xem tab \"Tải trực tiếp\": trang sẽ được kiểm tra và hiện OK/ERR "
                               "cùng các tệp tìm được (quét sâu có thể mất tới vài chục giây).");
    else
        m_statusLabel->setText("Trang này đã có trong hàng đợi (hoặc địa chỉ không hợp lệ) - xem tab \"Tải trực tiếp\".");
}
