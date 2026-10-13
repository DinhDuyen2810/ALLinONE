#include "PageScanTab.h"

#include "DownloaderSettings.h"
#include "DownloaderUiStyle.h"
#include "engine/DownloadQueue.h"
#include "engine/FileDownloader.h"
#include "engine/LinkResolver.h"
#include "engine/LinkResolverCore.h"
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

QString typeLabelOf(const ResolvedMedia& m)
{
    return m.mediaType.isEmpty() ? QStringLiteral("Khác") : m.mediaType;
}

QString nameOf(const ResolvedMedia& m)
{
    if (m.kind == ResolvedMedia::Kind::YtDlp && !m.title.isEmpty())
        return m.title;
    if (!m.fileName.isEmpty())
        return m.fileName;
    if (!m.title.isEmpty())
        return m.title;
    return FileDownloaderInternal::fileNameFromUrl(m.url);
}
} // namespace

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

PageScanTab::~PageScanTab()
{
    cancelScan();
}

void PageScanTab::cancelScan()
{
    if (m_resolver && m_resolver->isRunning())
    {
        m_stopped = true;
        m_resolver->cancel();
    }
}

void PageScanTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* introLabel = new QLabel(
        "Dán link MỘT TRANG, MỘT HỒ SƠ (Facebook, TikTok, Instagram, YouTube...) hoặc MỘT BÀI ĐĂNG để liệt kê mọi ảnh/video/âm "
        "thanh/tài liệu tìm được. Ứng dụng quét mã nguồn trang trước; nếu chưa thấy video/tài liệu, hoặc là hồ sơ/bài đăng, sẽ tự "
        "quét sâu (yt-dlp, trình duyệt ẩn - trang chặn chuột phải/sao chép link vẫn đọc được; hồ sơ thì quét TỪNG BÀI ĐĂNG). "
        "Tick các mục rồi \"Thêm vào hàng đợi\" hoặc \"Tải ngay\".",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    auto* urlRow = new QHBoxLayout();
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setStyleSheet(DownloaderUi::inputStyle());
    m_urlEdit->setPlaceholderText("https://vi-du.com/trang-hoac-ho-so");
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
    m_deepBtn->setToolTip("Luôn quét sâu (yt-dlp, quét mã nguồn, trình duyệt ẩn) kể cả khi quét thường đã thấy tệp");
    connect(m_deepBtn, &QPushButton::clicked, this, &PageScanTab::onDeepClicked);
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
    m_addSelectedBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_addSelectedBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addSelectedBtn, &QPushButton::clicked, this, &PageScanTab::onAddSelectedClicked);
    actionRow->addWidget(m_addSelectedBtn);
    m_downloadNowBtn = new QPushButton("⬇ Tải ngay các mục đã chọn", this);
    m_downloadNowBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_downloadNowBtn->setCursor(Qt::PointingHandCursor);
    connect(m_downloadNowBtn, &QPushButton::clicked, this, &PageScanTab::onDownloadNowClicked);
    actionRow->addWidget(m_downloadNowBtn);
    root->addLayout(actionRow);

    m_table = new QTableWidget(0, kColCount, this);
    m_table->setHorizontalHeaderLabels({"", "Loại", "Tên tệp / Tiêu đề", ""});
    m_table->horizontalHeader()->setSectionResizeMode(kCheck, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kType, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kOpen, QHeaderView::Fixed);
    m_table->setColumnWidth(kOpen, 44);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    root->addWidget(m_table, 1);
}

void PageScanTab::refreshFolderLabel()
{
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
}

// ---------------------------------------------------------------------------------------------- Quét
void PageScanTab::onScanClicked()
{
    if (m_busy)
    {
        // Nút đang là "Dừng".
        m_stopped = true;
        if (m_resolver && m_resolver->isRunning())
            m_resolver->cancel();
        else
            endScan();
        return;
    }
    beginScan(false);
}

void PageScanTab::onDeepClicked()
{
    if (m_busy)
        return;
    beginScan(true);
}

void PageScanTab::beginScan(bool forceDeep)
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    if (!FileDownloaderInternal::isHttpUrl(url))
    {
        m_statusLabel->setText("⚠ Địa chỉ không hợp lệ - chỉ hỗ trợ trang bắt đầu bằng http:// hoặc https://.");
        return;
    }
    m_table->setRowCount(0);
    m_rows.clear();
    m_pageUrl = url;
    m_forceDeep = forceDeep;
    m_stopped = false;
    m_deepRunning = false;
    m_staticCount = 0;
    m_staticError.clear();
    m_deepProgress.clear();
    m_deepError.clear();
    m_deepWarning.clear();
    m_busy = true;
    m_scanBtn->setText("⏹ Dừng");
    m_deepBtn->setEnabled(false);
    m_statusLabel->setText("⏳ Đang quét mã nguồn trang...");
    m_scanner->scan(url);
}

void PageScanTab::endScan()
{
    m_busy = false;
    m_deepRunning = false;
    m_scanBtn->setText("🔍 Quét trang");
    m_deepBtn->setEnabled(true);
    updateStatus();
}

void PageScanTab::onScanResult(QList<MediaLink> links)
{
    if (!m_busy || m_stopped)
        return;
    QList<ResolvedMedia> rows;
    bool primary = false;
    for (const MediaLink& l : links)
    {
        ResolvedMedia m;
        m.kind = ResolvedMedia::Kind::HttpFile;
        m.url = l.url;
        m.fileName = FileDownloaderInternal::sanitizeFileName(FileDownloaderInternal::fileNameFromUrl(l.url));
        m.title = m.fileName;
        m.mediaType = MediaLink::typeLabel(l.type);
        m.referer = m_pageUrl;
        m.note = "quét trang";
        rows << m;
        if (l.type == MediaLink::Type::Video || l.type == MediaLink::Type::Audio || l.type == MediaLink::Type::Document)
            primary = true;
    }
    appendRows(rows);
    m_staticCount = rows.size();

    // Quét sâu khi: người dùng yêu cầu, là hồ sơ/bài đăng, hoặc quét tĩnh chưa thấy video/âm thanh/tài liệu nào.
    const bool special = LinkResolverCore::isLikelyProfileOrCollectionUrl(m_pageUrl) || LinkResolverCore::isPhotoPostUrl(m_pageUrl);
    if (m_forceDeep || special || !primary)
        startDeep();
    else
        endScan();
}

void PageScanTab::onScanError(QString message)
{
    if (!m_busy || m_stopped)
        return;
    m_staticError = message;
    startDeep(); // lỗi tải HTML tĩnh (chặn, cần đăng nhập...) vẫn để bộ phân giải đầy đủ thử
}

void PageScanTab::startDeep()
{
    if (!m_resolver)
    {
        m_resolver = new LinkResolver(this);
        connect(m_resolver, &LinkResolver::progress, this, &PageScanTab::onDeepProgress);
        connect(m_resolver, &LinkResolver::finished, this, &PageScanTab::onDeepFinished);
    }
    LinkResolver::Options o;
    o.maxItems = m_settings->profileMaxItems();
    o.useBrowser = m_settings->useBrowserFallback();
    o.ytDlpExtraArgs = m_settings->ytDlpExtraArguments();
    o.cookieFile = m_settings->httpCookieFile();
    m_resolver->setOptions(o);
    m_deepRunning = true;
    m_deepProgress = "bắt đầu...";
    updateStatus();
    m_resolver->resolve(m_pageUrl);
}

void PageScanTab::onDeepProgress(QString message)
{
    m_deepProgress = message;
    updateStatus();
}

void PageScanTab::onDeepFinished(ResolveResult result)
{
    if (!m_busy)
        return;
    if (result.ok())
    {
        QList<ResolvedMedia> rows = result.items;
        for (ResolvedMedia& m : rows)
            if (m.referer.isEmpty())
                m.referer = m_pageUrl;
        appendRows(rows);
        m_deepWarning = result.warning;
        if (result.truncated)
            m_deepWarning += (m_deepWarning.isEmpty() ? "" : " ") + QString("Đã cắt theo giới hạn %1 mục ở tab Cài đặt.").arg(m_settings->profileMaxItems());
    }
    else if (!m_stopped)
    {
        m_deepError = result.error;
    }
    endScan();
}

void PageScanTab::appendRows(const QList<ResolvedMedia>& rows)
{
    for (const ResolvedMedia& m : rows)
    {
        bool dup = false;
        for (const ResolvedMedia& have : std::as_const(m_rows))
            if (have.url == m.url)
            {
                dup = true;
                break;
            }
        if (dup)
            continue;
        const int i = m_rows.size();
        m_rows << m;
        m_table->insertRow(i);

        auto* checkItem = new QTableWidgetItem();
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        checkItem->setCheckState(Qt::Unchecked);
        m_table->setItem(i, kCheck, checkItem);

        auto* typeItem = new QTableWidgetItem(typeLabelOf(m));
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, kType, typeItem);

        auto* nameItem = new QTableWidgetItem(nameOf(m));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        nameItem->setToolTip(m.url + (m.note.isEmpty() ? QString() : "\nNguồn: " + m.note));
        m_table->setItem(i, kName, nameItem);

        auto* openBtn = new QPushButton("🌐", m_table);
        openBtn->setToolTip("Mở link này trên trình duyệt");
        openBtn->setCursor(Qt::PointingHandCursor);
        openBtn->setFixedSize(32, 24);
        openBtn->setStyleSheet(DownloaderUi::buttonStyle() + "QPushButton { padding: 2px 6px; }");
        const QString linkUrl = m.url;
        connect(openBtn, &QPushButton::clicked, this, [linkUrl] {
            const QUrl u(linkUrl);
            if (u.isValid() && (u.scheme() == "http" || u.scheme() == "https"))
                QDesktopServices::openUrl(u);
        });
        m_table->setCellWidget(i, kOpen, openBtn);
    }
    updateStatus();
}

void PageScanTab::updateStatus(const QString& extra)
{
    QStringList parts;
    if (m_busy)
    {
        QString s = "⏳ Đang quét";
        if (m_deepRunning)
            s += " sâu: " + m_deepProgress;
        else
            s += "...";
        parts << s;
    }
    if (!m_rows.isEmpty())
        parts << QString("Tìm thấy %1 mục.").arg(m_rows.size());
    if (!m_busy)
    {
        if (m_stopped)
            parts << "Đã dừng quét.";
        if (m_rows.isEmpty() && !m_stopped)
        {
            if (!m_deepError.isEmpty())
                parts << "⚠ " + m_deepError;
            else if (!m_staticError.isEmpty())
                parts << "⚠ " + m_staticError;
            else
                parts << "Không tìm thấy ảnh/video/âm thanh/tài liệu nào ở địa chỉ này.";
        }
        else if (!m_deepError.isEmpty() && !m_stopped)
        {
            parts << "Quét sâu: " + m_deepError;
        }
        if (!m_deepWarning.isEmpty())
            parts << "⚠ " + m_deepWarning;
    }
    if (!extra.isEmpty())
        parts << extra;
    m_statusLabel->setText(parts.join("  "));
}

// ---------------------------------------------------------------------------------------------- Chọn / tải
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

QList<int> PageScanTab::addCheckedToQueue(bool includeExisting)
{
    QList<int> ids;
    for (int i = 0; i < m_table->rowCount() && i < m_rows.size(); ++i)
    {
        if (m_table->item(i, kCheck)->checkState() != Qt::Checked)
            continue;
        ResolvedMedia media = m_rows[i];
        // Tên tệp lấy từ trang bất kỳ là dữ liệu KHÔNG tin cậy; hàng đợi làm sạch lại lúc chọn đường dẫn đích.
        media.fileName = FileDownloaderInternal::sanitizeFileName(media.fileName);
        int id = m_queue->addResolved(media, m_pageUrl);
        if (id < 0 && includeExisting)
            id = m_queue->idOfMediaUrl(media.url); // đã có trong hàng đợi (thêm lần trước): tải luôn mục đó
        if (id >= 0)
            ids << id;
    }
    return ids;
}

void PageScanTab::onAddSelectedClicked()
{
    const QList<int> ids = addCheckedToQueue(false);
    if (ids.isEmpty())
    {
        QMessageBox::information(this, "Thêm vào hàng đợi",
                                 "Chưa chọn mục nào (hoặc các mục đã có sẵn trong hàng đợi) - tick vào ô ở cột đầu trước.");
        return;
    }
    QMessageBox::information(this, "Đã thêm vào hàng đợi",
                             QString("Đã thêm %1 mục vào hàng đợi (chưa tải). Sang tab \"Tải trực tiếp\" để chọn và tải.").arg(ids.size()));
}

void PageScanTab::onDownloadNowClicked()
{
    const QList<int> ids = addCheckedToQueue(true);
    if (ids.isEmpty())
    {
        QMessageBox::information(this, "Tải ngay",
                                 "Chưa chọn mục nào - tick vào ô ở cột đầu trước.");
        return;
    }
    const int started = m_queue->startItems(ids);
    updateStatus(QString("⬇ Đã bắt đầu tải %1 mục - theo dõi tiến độ ở tab \"Tải trực tiếp\".").arg(started));
}
