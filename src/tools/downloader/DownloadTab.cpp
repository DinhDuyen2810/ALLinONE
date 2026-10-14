#include "DownloadTab.h"

#include "DownloaderSettings.h"
#include "DownloaderUiStyle.h"
#include "engine/DownloadQueue.h"
#include "engine/FileDownloader.h"
#include "engine/LinkResolver.h"
#include "engine/LinkResolverCore.h"
#include "engine/PageMediaScanner.h"
#include "engine/YtDlpController.h"
#include "engine/YtDlpDownloadWorker.h"
#include "engine/YtDlpInfoWorker.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/ModernMenu.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace
{
enum Col { kCheck = 0, kName, kType, kSize, kCheckStatus, kProgress, kSpeed, kState, kOpen, kColCount };
constexpr int kIdRole = Qt::UserRole;
constexpr int kInputHeight = 52; ///< Ô dán link và nút Quét cùng chiều cao (đủ 2 dòng chữ)

QString elided(const QString& s, int maxChars)
{
    return s.size() <= maxChars ? s : s.left(maxChars - 1) + "…";
}
} // namespace

DownloadTab::DownloadTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_queue(queue)
    , m_settings(settings)
{
    buildUi();

    m_infoWorker = new YtDlpInfoWorker(this);
    connect(m_infoWorker, &YtDlpInfoWorker::infoFetched, this, &DownloadTab::onFormatsFetched);

    connect(m_queue, &DownloadQueue::itemAdded, this, &DownloadTab::onItemAdded);
    connect(m_queue, &DownloadQueue::itemUpdated, this, &DownloadTab::onItemUpdated);
    connect(m_queue, &DownloadQueue::itemRemoved, this, &DownloadTab::onItemRemoved);
    connect(m_queue, &DownloadQueue::message, this, &DownloadTab::onMessage);
    connect(m_settings, &DownloaderSettings::changed, this, &DownloadTab::onSettingsChanged);
    connect(qApp->clipboard(), &QClipboard::dataChanged, this, &DownloadTab::onClipboardChanged);

    for (int id : m_queue->allIds()) // hàng đợi có thể đã có mục trước khi tab được dựng
        onItemAdded(id);
    refreshSummary();
}

DownloadTab::~DownloadTab()
{
    // Yêu cầu hủy rồi chờ KHÔNG giới hạn: luồng tự kết thúc yt-dlp.exe và thoát trong vài trăm ms (chỉ wait(5000) trong khi
    // yt-dlp được chờ tới 45 giây là hủy một QThread còn đang chạy - hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt).
    if (m_infoWorker)
    {
        m_infoWorker->requestCancel();
        m_infoWorker->wait();
    }
    if (m_resolver && m_resolver->isRunning())
    {
        m_resolver->disconnect(this);
        m_resolver->cancel();
    }
}

void DownloadTab::cancelBackgroundWork()
{
    m_scanStopped = true;
    m_scanQueue.clear();
    if (m_resolver && m_resolver->isRunning())
        m_resolver->cancel(); // phát finished("Đã hủy.") -> onDeepFinished -> endScan
    else if (m_scanBusy)
        endScan();
    if (m_infoWorker && m_infoWorker->isRunning())
        m_infoWorker->requestCancel();
}

void DownloadTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_bundleBanner = new QLabel(this);
    m_bundleBanner->setWordWrap(true);
    m_bundleBanner->setStyleSheet(DownloaderUi::bannerStyle("danger"));
    root->addWidget(m_bundleBanner);
    updateBundleBanner();

    auto* introLabel = new QLabel(
        "Dán MỘT hoặc NHIỀU link (mỗi dòng một link): tệp trực tiếp, video Facebook/TikTok/YouTube, Google Drive, trang phim, "
        "ảnh bài đăng, trang web hoặc cả hồ sơ/kênh, rồi bấm \"Quét\". Ứng dụng liệt kê mọi ảnh/video/âm thanh/tài liệu tìm "
        "được vào bảng: OK = tải được, ERR = không tải được (kèm lý do) - CHƯA tải gì. Tick các dòng muốn tải rồi bấm \"Tải các "
        "mục đã chọn\". Bạn tự chịu trách nhiệm tuân thủ điều khoản của trang nguồn và luật bản quyền.",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    m_urlEdit = new QPlainTextEdit(this);
    m_urlEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 8px; "
        "font-size: 12px; } QPlainTextEdit:focus { border-color: #0969da; }");
    m_urlEdit->setPlaceholderText("https://...   (nhiều link: mỗi dòng một link; Ctrl+Enter để quét)");
    m_urlEdit->setFixedHeight(kInputHeight);
    m_urlEdit->setTabChangesFocus(true);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_urlEdit, this, [this]() { onScanClicked(); }, Qt::WidgetShortcut);

    auto* urlRow = new QHBoxLayout();
    urlRow->addWidget(m_urlEdit, 1);
    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_scanBtn->setFixedSize(130, kInputHeight);
    m_scanBtn->setToolTip("Liệt kê MỌI ảnh/video/âm thanh/tài liệu tìm được ở các link đã dán rồi đưa vào hàng đợi (chưa tải)");
    connect(m_scanBtn, &QPushButton::clicked, this, &DownloadTab::onScanClicked);
    urlRow->addWidget(m_scanBtn);
    root->addLayout(urlRow);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu (đổi ở tab Cài đặt):", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(QDir::toNativeSeparators(m_settings->downloadFolder()), this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
    folderRow->addWidget(m_folderLabel, 1);
    root->addLayout(folderRow);

    auto* optionRow = new QHBoxLayout();
    m_clipboardCheck = new QCheckBox("Tự điền link khi sao chép (không tự thêm)", this);
    m_clipboardCheck->setStyleSheet("color: #57606a; font-size: 12px;");
    optionRow->addWidget(m_clipboardCheck);
    m_deepCheck = new QCheckBox("Quét sâu (chậm hơn, kỹ hơn)", this);
    m_deepCheck->setStyleSheet("color: #57606a; font-size: 12px;");
    m_deepCheck->setToolTip("Khi quét trang: luôn dùng thêm yt-dlp, quét mã nguồn và trình duyệt ẩn, kể cả khi quét thường đã thấy tệp");
    optionRow->addWidget(m_deepCheck);
    optionRow->addStretch();
    auto* qualityCaption = new QLabel("Chất lượng video:", this);
    qualityCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    optionRow->addWidget(qualityCaption);
    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->setStyleSheet(DownloaderUi::inputStyle());
    m_qualityCombo->setMinimumWidth(150);
    for (const QString& key : DownloaderSettings::videoQualityKeys())
        m_qualityCombo->addItem(DownloaderSettings::videoQualityLabel(key), key);
    m_qualityCombo->setCurrentIndex(qMax(0, m_qualityCombo->findData(m_settings->videoQuality())));
    m_qualityCombo->setToolTip("Áp dụng cho video tải bằng yt-dlp (Facebook/TikTok/YouTube...); tệp và ảnh tải thẳng không bị ảnh "
                               "hưởng.\nMuốn chọn riêng cho một video: bấm phải vào dòng đó -> \"Chọn chất lượng khác...\".");
    connect(m_qualityCombo, &QComboBox::currentIndexChanged, this, &DownloadTab::onQualityChanged);
    optionRow->addWidget(m_qualityCombo);
    root->addLayout(optionRow);

    // Hàng nút thao tác trên hàng đợi.
    // FlowLayout: cửa sổ hẹp thì nút tự xuống dòng thay vì bị cắt chữ.
    auto* actionRow = new FlowLayout(nullptr, 0, 6, 6);
    auto addButton = [&](const QString& text, const QString& style, auto slot) {
        auto* b = new QPushButton(text, this);
        b->setStyleSheet(style);
        connect(b, &QPushButton::clicked, this, slot);
        actionRow->addWidget(b);
        return b;
    };
    addButton("☑ Chọn tất cả", DownloaderUi::buttonStyle(), &DownloadTab::onSelectAll);
    addButton("☐ Bỏ chọn", DownloaderUi::buttonStyle(), &DownloadTab::onSelectNone);
    addButton("✔ Chọn mục OK", DownloaderUi::buttonStyle(), &DownloadTab::onSelectOk);
    addButton("⏸ Tạm dừng", DownloaderUi::buttonStyle(), &DownloadTab::onPauseSelected);
    addButton("✖ Hủy tải", DownloaderUi::buttonStyle(), &DownloadTab::onCancelSelected);
    addButton("🗑 Xóa đã chọn", DownloaderUi::buttonStyle(), &DownloadTab::onRemoveSelected);
    addButton("🧹 Xóa mục xong/lỗi", DownloaderUi::buttonStyle(), &DownloadTab::onRemoveFinished);
    m_downloadBtn = addButton("⬇ Tải các mục đã chọn", DownloaderUi::primaryButtonStyle(), &DownloadTab::onDownloadSelected);
    root->addLayout(actionRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    root->addWidget(m_summaryLabel);
    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); // lý do lỗi dài - cho phép bôi đen để sao chép
    m_messageLabel->setVisible(false); // chỉ hiện khi có nội dung, không chừa khoảng trống
    root->addWidget(m_messageLabel);

    m_table = new QTableWidget(0, kColCount, this);
    m_table->setHorizontalHeaderLabels({"", "Tên / Tiêu đề", "Loại", "Kích thước", "Kiểm tra", "Tiến độ", "Tốc độ", "Trạng thái", "Mở"});
    QHeaderView* header = m_table->horizontalHeader();
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(QHeaderView::ResizeToContents);
    header->setSectionResizeMode(kName, QHeaderView::Stretch);
    header->setSectionResizeMode(kProgress, QHeaderView::Fixed);
    header->setSectionResizeMode(kState, QHeaderView::Fixed);
    header->setSectionResizeMode(kOpen, QHeaderView::Fixed);
    m_table->setColumnWidth(kProgress, 100);
    m_table->setColumnWidth(kState, 190);
    m_table->setColumnWidth(kOpen, 40);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus); // không vẽ khung chấm quanh ô đang trỏ
    m_table->setShowGrid(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setStyleSheet(DownloaderUi::tableStyle() + DownloaderUi::progressBarStyle());
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->setMouseTracking(true);
    m_table->viewport()->setMouseTracking(true);
    m_table->viewport()->installEventFilter(this);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &DownloadTab::onTableContextMenu);
    connect(m_table, &QTableWidget::cellClicked, this, [this](int row, int col) {
        if (col == kOpen)
            openLinkOfId(idOfRow(row));
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, &DownloadTab::onSelectionChanged);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!m_updatingTable && item && item->column() == kCheck)
            refreshSummary();
    });
    root->addWidget(m_table, 1);
}

void DownloadTab::updateBundleBanner()
{
    QString missing;
    const bool available = YtDlpController::isBundleAvailable(&missing);
    m_bundleBanner->setVisible(!available);
    if (!available)
        m_bundleBanner->setText(QString("⚠ Thiếu %1 trong thư mục \"yt-dlp\" cạnh file chạy - video Facebook/TikTok/YouTube và quét "
                                        "sâu chưa dùng được (tệp/ảnh tải thẳng vẫn tải bình thường). Xem THIRD_PARTY.md để biết cách "
                                        "tải, rồi chạy lại build_app.bat.")
                                    .arg(missing));
}

bool DownloadTab::eventFilter(QObject* obj, QEvent* event)
{
    // Con trỏ ngón tay chỉ ở hai cột bấm được (ô tick, nút mở link) - các ô còn lại và vùng trống giữ mũi tên.
    if (m_table && obj == m_table->viewport())
    {
        if (event->type() == QEvent::MouseMove)
        {
            const QModelIndex idx = m_table->indexAt(static_cast<QMouseEvent*>(event)->position().toPoint());
            if (idx.isValid() && (idx.column() == kOpen || idx.column() == kCheck))
                m_table->viewport()->setCursor(Qt::PointingHandCursor);
            else
                m_table->viewport()->unsetCursor();
        }
        else if (event->type() == QEvent::Leave)
        {
            m_table->viewport()->unsetCursor();
        }
    }
    return QWidget::eventFilter(obj, event);
}

// ---------------------------------------------------------------------------------------------- Bảng
int DownloadTab::idOfRow(int row) const
{
    auto* it = m_table->item(row, kCheck);
    return it ? it->data(kIdRole).toInt() : -1;
}

int DownloadTab::rowOfId(int id) const
{
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (idOfRow(r) == id)
            return r;
    return -1;
}

void DownloadTab::onItemAdded(int id)
{
    if (rowOfId(id) >= 0)
        return;
    int row = m_queue->allIds().indexOf(id);
    if (row < 0 || row > m_table->rowCount())
        row = m_table->rowCount();
    m_updatingTable = true;
    m_table->insertRow(row);
    for (int c = 0; c < kColCount; ++c)
    {
        if (c == kProgress)
            continue; // widget riêng
        auto* item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        if (c == kCheck)
        {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            item->setData(kIdRole, id);
        }
        else if (c == kOpen)
        {
            // Chữ thường thay vì nút: ô chọn dòng tự đổi màu theo dòng, không có khung/vòng trắng nằm trên nền xanh.
            item->setText("↗");
            item->setTextAlignment(Qt::AlignCenter);
            item->setForeground(QColor("#0969da"));
            item->setToolTip("Mở link này trên trình duyệt");
        }
        m_table->setItem(row, c, item);
    }
    auto* bar = new QProgressBar(m_table);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setFixedHeight(18);
    bar->setProperty("idle", false); // fillRow() đặt lại theo trạng thái thật ngay sau đó
    m_table->setCellWidget(row, kProgress, bar);
    m_updatingTable = false;

    fillRow(row, id);
    refreshSummary();
}

void DownloadTab::onItemUpdated(int id)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    fillRow(row, id);
    refreshSummary();
}

void DownloadTab::onItemRemoved(int id)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    m_updatingTable = true;
    m_table->removeRow(row);
    m_updatingTable = false;
    refreshSummary();
}

void DownloadTab::fillRow(int row, int id)
{
    const QueueItem* it = m_queue->item(id);
    if (!it)
        return;
    m_updatingTable = true;

    QString name = it->displayName();
    if (!it->warning.isEmpty())
        name = "⚠ " + name;
    m_table->item(row, kName)->setText(name);
    QString tip = it->displayName() + "\n" + it->linkToOpen();
    if (it->sourceUrl != it->linkToOpen())
        tip += "\nTrang nguồn: " + it->sourceUrl;
    if (!it->checkNote.isEmpty())
        tip += "\n" + (it->check == QueueItem::Check::Err ? QString("Lỗi: ") : QString("Nguồn: ")) + it->checkNote;
    if (!it->formatLabel.isEmpty())
        tip += "\nChất lượng đã chọn: " + it->formatLabel;
    if (!it->warning.isEmpty())
        tip += "\n⚠ " + it->warning;
    m_table->item(row, kName)->setToolTip(tip);

    const bool ok = it->check == QueueItem::Check::Ok;
    QTableWidgetItem* typeItem = m_table->item(row, kType);
    typeItem->setText(ok && !it->media.mediaType.isEmpty() ? it->media.mediaType : QString("—"));
    typeItem->setToolTip(it->formatLabel.isEmpty() ? QString() : "Chất lượng đã chọn: " + it->formatLabel);
    m_table->item(row, kSize)->setText(ok ? (it->total > 0 ? DownloaderUi::formatBytes(it->total) : QString("?")) : QString("—"));

    // Cột "Kiểm tra": OK / ERR / đang kiểm tra.
    QTableWidgetItem* chk = m_table->item(row, kCheckStatus);
    chk->setText(it->check == QueueItem::Check::Ok || it->check == QueueItem::Check::Err ? QueueItem::checkLabel(it->check) : QString("…"));
    QFont f = chk->font();
    f.setBold(true);
    chk->setFont(f);
    chk->setForeground(it->check == QueueItem::Check::Ok ? QColor("#1a7f37")
                       : it->check == QueueItem::Check::Err ? QColor("#cf222e")
                                                            : QColor("#8c959f"));
    chk->setToolTip(it->check == QueueItem::Check::Err ? it->checkNote : QString());

    // Tiến độ + tốc độ. Chữ "%" chỉ hiện khi mục đã chạy - dòng chưa tải để thanh trống cho đỡ rối.
    if (auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, kProgress)))
    {
        const bool started = it->state == QueueItem::State::Downloading || it->state == QueueItem::State::Paused ||
                             it->state == QueueItem::State::Completed || it->received > 0;
        bar->setTextVisible(started);
        // Dòng chưa tải: thanh trong suốt (không khung/nền trắng nằm trên nền dòng chọn) - chỉ hiện khi đã chạy.
        if (bar->property("idle").toBool() == started)
        {
            bar->setProperty("idle", !started);
            bar->setStyleSheet(started ? QString() : QStringLiteral("QProgressBar { background: transparent; border: none; }"));
        }
        if (it->state == QueueItem::State::Completed)
        {
            bar->setRange(0, 100);
            bar->setValue(100);
        }
        else if (it->total > 0)
        {
            bar->setRange(0, 100);
            bar->setValue(static_cast<int>(100.0 * qMin(it->received, it->total) / it->total));
        }
        else if (it->state == QueueItem::State::Downloading)
        {
            bar->setRange(0, 0); // chưa biết tổng dung lượng
        }
        else
        {
            bar->setRange(0, 100);
            bar->setValue(0);
        }
    }
    m_table->item(row, kSpeed)->setText(it->state == QueueItem::State::Downloading ? DownloaderUi::formatSpeed(it->speed) : QString("-"));

    // Cột "Trạng thái": lý do khi ERR, tiến trình khi đang kiểm tra, còn lại là trạng thái tải. Lý do đầy đủ: tooltip, và hiện
    // ở dòng thông báo phía trên bảng khi chọn dòng đó.
    QString state;
    switch (it->check)
    {
        case QueueItem::Check::Err: state = "✖ " + elided(it->checkNote, 60); break;
        case QueueItem::Check::Checking: state = elided(it->checkNote, 60); break;
        case QueueItem::Check::Waiting: state = "Chờ kiểm tra..."; break;
        case QueueItem::Check::Ok:
            state = QueueItem::stateLabel(it->state);
            if (it->state == QueueItem::State::Failed && !it->error.isEmpty())
                state += " (" + elided(it->error, 40) + ")";
            break;
    }
    QTableWidgetItem* st = m_table->item(row, kState);
    st->setText(state);
    st->setForeground(it->check == QueueItem::Check::Err || it->state == QueueItem::State::Failed ? QColor("#cf222e")
                      : it->state == QueueItem::State::Completed ? QColor("#1a7f37")
                                                                 : QColor("#1f2328"));
    QString stateTip = it->check == QueueItem::Check::Err ? it->checkNote : it->error;
    if (it->state == QueueItem::State::Completed && !it->destPath.isEmpty())
        stateTip = "Đã lưu: " + it->destPath;
    st->setToolTip(stateTip);
    m_updatingTable = false;
}

QList<int> DownloadTab::targetIds() const
{
    QList<int> checked;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (auto* it = m_table->item(r, kCheck); it && it->checkState() == Qt::Checked)
            checked << idOfRow(r);
    if (!checked.isEmpty())
        return checked;
    QList<int> selected;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows())
        selected << idOfRow(idx.row());
    std::sort(selected.begin(), selected.end(), [this](int a, int b) { return rowOfId(a) < rowOfId(b); });
    return selected;
}

void DownloadTab::refreshSummary()
{
    int ok = 0, err = 0, checking = 0, downloading = 0, done = 0;
    for (int id : m_queue->allIds())
    {
        const QueueItem* it = m_queue->item(id);
        if (!it)
            continue;
        if (it->check == QueueItem::Check::Ok) ++ok;
        else if (it->check == QueueItem::Check::Err) ++err;
        else ++checking;
        if (it->state == QueueItem::State::Downloading || it->state == QueueItem::State::Queued) ++downloading;
        if (it->state == QueueItem::State::Completed) ++done;
    }
    const int total = m_queue->allIds().size();
    m_summaryLabel->setText(total == 0
                                ? QString("Hàng đợi trống.")
                                : QString("Hàng đợi: %1 mục  •  OK %2  •  ERR %3  •  đang kiểm tra %4  •  đang tải/chờ %5  •  hoàn tất %6")
                                      .arg(total).arg(ok).arg(err).arg(checking).arg(downloading).arg(done));
    int checkedCount = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (auto* it = m_table->item(r, kCheck); it && it->checkState() == Qt::Checked)
            ++checkedCount;
    m_downloadBtn->setText(checkedCount > 0 ? QString("⬇ Tải các mục đã chọn (%1)").arg(checkedCount) : QString("⬇ Tải các mục đã chọn"));
}

void DownloadTab::setMessage(const QString& text, bool isError)
{
    m_messageLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(isError ? "#cf222e" : "#57606a"));
    m_messageLabel->setText(text);
    m_messageLabel->setVisible(!text.isEmpty());
}

void DownloadTab::onMessage(const QString& text)
{
    setMessage(text);
}

void DownloadTab::onSelectionChanged()
{
    // Chọn đúng MỘT dòng lỗi/cảnh báo: hiện lý do ĐẦY ĐỦ (cột trạng thái chỉ đủ chỗ cho một đoạn đầu).
    const QModelIndexList rows = m_table->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return;
    const QueueItem* it = m_queue->item(idOfRow(rows.first().row()));
    if (!it)
        return;
    if (it->check == QueueItem::Check::Err)
        setMessage("✖ Không tải được - " + it->checkNote, true);
    else if (it->state == QueueItem::State::Failed && !it->error.isEmpty())
        setMessage("✖ Tải thất bại - " + it->error, true);
    else if (!it->warning.isEmpty())
        setMessage("⚠ " + it->warning);
}

// ---------------------------------------------------------------------------------------------- Thao tác
void DownloadTab::onSelectAll()
{
    m_updatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, kCheck)->setCheckState(Qt::Checked);
    m_updatingTable = false;
    refreshSummary();
}

void DownloadTab::onSelectNone()
{
    m_updatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, kCheck)->setCheckState(Qt::Unchecked);
    m_updatingTable = false;
    m_table->clearSelection();
    refreshSummary();
}

void DownloadTab::onSelectOk()
{
    m_updatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
    {
        const QueueItem* it = m_queue->item(idOfRow(r));
        const bool ok = it && it->check == QueueItem::Check::Ok && it->state != QueueItem::State::Completed;
        m_table->item(r, kCheck)->setCheckState(ok ? Qt::Checked : Qt::Unchecked);
    }
    m_updatingTable = false;
    refreshSummary();
}

void DownloadTab::onDownloadSelected()
{
    const QList<int> ids = targetIds();
    if (ids.isEmpty())
    {
        onMessage("Chưa chọn mục nào - tick ô ở cột đầu (hoặc bôi đen các dòng) rồi bấm lại.");
        return;
    }
    m_queue->startItems(ids);
}

void DownloadTab::onPauseSelected()
{
    for (int id : targetIds())
        m_queue->pause(id);
}

void DownloadTab::onCancelSelected()
{
    for (int id : targetIds())
        m_queue->cancel(id);
}

void DownloadTab::onRemoveSelected()
{
    const QList<int> ids = targetIds();
    if (ids.isEmpty())
    {
        onMessage("Chưa chọn mục nào để xóa.");
        return;
    }
    m_queue->remove(ids);
    onMessage(QString("Đã xóa %1 mục khỏi hàng đợi (tệp đã tải xong KHÔNG bị xóa khỏi ổ đĩa).").arg(ids.size()));
}

void DownloadTab::onRemoveFinished()
{
    const int n = m_queue->removeFinished(true);
    onMessage(n > 0 ? QString("Đã dọn %1 mục đã xong/lỗi/hủy khỏi hàng đợi.").arg(n) : QString("Không có mục nào để dọn."));
}

void DownloadTab::onSettingsChanged()
{
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
    const int idx = m_qualityCombo->findData(m_settings->videoQuality());
    if (idx >= 0 && idx != m_qualityCombo->currentIndex())
    {
        const QSignalBlocker block(m_qualityCombo);
        m_qualityCombo->setCurrentIndex(idx);
    }
}

void DownloadTab::onQualityChanged()
{
    m_settings->setVideoQuality(m_qualityCombo->currentData().toString());
}

void DownloadTab::openLinkOfId(int id)
{
    const QueueItem* it = m_queue->item(id);
    if (!it)
        return;
    const QUrl url(it->linkToOpen());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
        return; // chỉ mở link web - không bao giờ mở file:/javascript:/... bằng ứng dụng khác
    QDesktopServices::openUrl(url);
}

void DownloadTab::onTableContextMenu(const QPoint& pos)
{
    const int row = m_table->rowAt(pos.y());
    if (row < 0)
        return;
    // Bấm phải vào dòng CHƯA được chọn thì chọn riêng dòng đó trước (không đổi ô tick).
    if (!m_table->selectionModel()->isRowSelected(row, QModelIndex()))
    {
        m_table->clearSelection();
        m_table->selectRow(row);
    }
    QList<int> ids;
    for (const QModelIndex& idx : m_table->selectionModel()->selectedRows())
        ids << idOfRow(idx.row());
    if (ids.isEmpty())
        return;
    const QueueItem* first = m_queue->item(ids.first());
    if (!first)
        return;
    const bool multi = ids.size() > 1;

    QMenu menu(this);
    ModernMenu::style(&menu);
    QAction* startAct = ModernMenu::addAction(menu, multi ? QString("Tải %1 mục đã chọn").arg(ids.size()) : QString("Tải mục này"));
    QAction* pauseAct = ModernMenu::addAction(menu, "Tạm dừng");
    QAction* cancelAct = ModernMenu::addAction(menu, "Hủy tải");
    QAction* recheckAct = ModernMenu::addAction(menu, "Kiểm tra lại link");
    QAction* formatAct = ModernMenu::addAction(menu, "Chọn chất lượng khác...");
    menu.addSeparator();
    QAction* openLinkAct = ModernMenu::addAction(menu, "Mở link trên trình duyệt");
    QAction* openSourceAct = ModernMenu::addAction(menu, "Mở trang nguồn trên trình duyệt");
    QAction* copyAct = ModernMenu::addAction(menu, "Sao chép link");
    const bool done = first->state == QueueItem::State::Completed && !first->destPath.isEmpty() && QFileInfo::exists(first->destPath);
    QAction* openFileAct = ModernMenu::addAction(menu, "Mở tệp đã tải");
    QAction* openDirAct = ModernMenu::addAction(menu, "Mở thư mục chứa tệp");
    menu.addSeparator();
    QAction* removeAct = ModernMenu::addAction(menu, multi ? QString("Xóa %1 mục khỏi hàng đợi").arg(ids.size()) : QString("Xóa khỏi hàng đợi"));

    startAct->setEnabled(first->check == QueueItem::Check::Ok);
    pauseAct->setEnabled(first->state == QueueItem::State::Downloading && first->httpId >= 0);
    cancelAct->setEnabled(first->state == QueueItem::State::Downloading || first->state == QueueItem::State::Queued ||
                          first->state == QueueItem::State::Paused || first->check == QueueItem::Check::Checking ||
                          first->check == QueueItem::Check::Waiting);
    recheckAct->setEnabled(first->check == QueueItem::Check::Err || first->check == QueueItem::Check::Ok);
    formatAct->setEnabled(!multi && first->check == QueueItem::Check::Ok && first->media.kind == ResolvedMedia::Kind::YtDlp &&
                          first->state != QueueItem::State::Downloading && first->state != QueueItem::State::Queued &&
                          first->state != QueueItem::State::Completed && YtDlpController::isBundleAvailable());
    openSourceAct->setEnabled(first->sourceUrl != first->linkToOpen());
    openFileAct->setEnabled(done);
    openDirAct->setEnabled(done);

    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == startAct) m_queue->startItems(ids);
    else if (chosen == pauseAct) { for (int id : ids) m_queue->pause(id); }
    else if (chosen == cancelAct) { for (int id : ids) m_queue->cancel(id); }
    else if (chosen == recheckAct) { for (int id : ids) m_queue->recheck(id); }
    else if (chosen == formatAct) pickFormatFor(ids.first());
    else if (chosen == openLinkAct) openLinkOfId(ids.first());
    else if (chosen == openSourceAct)
    {
        const QUrl u(first->sourceUrl);
        if (u.isValid() && (u.scheme() == "http" || u.scheme() == "https"))
            QDesktopServices::openUrl(u);
    }
    else if (chosen == copyAct) qApp->clipboard()->setText(first->linkToOpen());
    else if (chosen == openFileAct) QDesktopServices::openUrl(QUrl::fromLocalFile(first->destPath));
    else if (chosen == openDirAct) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(first->destPath).absolutePath()));
    else if (chosen == removeAct) m_queue->remove(ids);
}

void DownloadTab::onClipboardChanged()
{
    if (!m_clipboardCheck->isChecked())
        return;
    const QString text = qApp->clipboard()->text().trimmed();
    if (text.isEmpty() || text == m_lastClipboardText)
        return;
    m_lastClipboardText = text;
    const QStringList urls = LinkResolverCore::extractUrls(text);
    if (urls.isEmpty())
        return;
    // Chỉ ĐIỀN SẴN vào ô nhập (nối thêm dòng mới nếu đã có) - không tự thêm vào hàng đợi, người dùng vẫn phải tự bấm.
    const QString current = m_urlEdit->toPlainText();
    if (!current.contains(urls.first()))
        m_urlEdit->setPlainText(current.isEmpty() ? urls.join('\n') : current + "\n" + urls.join('\n'));
}

// ---------------------------------------------------------------------------------------------- Quét trang
void DownloadTab::onScanClicked()
{
    if (m_scanBusy)
    {
        stopScan(); // nút đang là "Dừng quét"
        return;
    }
    const QString text = m_urlEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    scanText(text, m_deepCheck->isChecked());
    if (m_scanBusy)
        m_urlEdit->clear();
}

void DownloadTab::scanText(const QString& text, bool forceDeep)
{
    if (m_scanBusy)
    {
        setMessage("Đang quét - bấm \"Dừng quét\" hoặc đợi quét xong rồi quét tiếp.");
        return;
    }
    const QStringList urls = LinkResolverCore::extractUrls(text);
    if (urls.isEmpty())
    {
        setMessage("Không thấy link http/https nào để quét - dán link trang/hồ sơ vào ô nhập.");
        return;
    }
    m_scanQueue = urls;
    m_scanForceDeep = forceDeep;
    m_scanStopped = false;
    m_scanAddedTotal = 0;
    m_scanFoundTotal = 0;
    m_scanPagesDone = 0;
    m_scanPagesTotal = urls.size();
    m_scanNotes.clear();
    m_scanBusy = true;
    m_scanBtn->setText("⏹ Dừng quét");
    startNextScan();
}

void DownloadTab::startNextScan()
{
    if (m_scanStopped || m_scanQueue.isEmpty())
    {
        endScan();
        return;
    }
    m_scanUrl = m_scanQueue.takeFirst();
    m_scanPageFound = 0;
    m_scanStaticError.clear();
    m_scanDeepError.clear();
    m_scanDeepWarning.clear();
    m_scanDeepRunning = false;
    updateScanStatus();

    // Google Drive/Docs: chỉ bộ phân giải đầy đủ xử lý (đổi sang link tải, nhận ra "chủ không cho tải"). Quét HTML tĩnh trang
    // xem Drive chỉ ra ảnh xem trước (`drive-viewer/...`) của tệp mà chủ đã khóa tải - không được liệt kê nó thành mục tải được.
    QString exportUrl, ext, driveId;
    if (LinkResolverCore::googleDocsExportUrl(m_scanUrl, &exportUrl, &ext) || LinkResolverCore::parseDriveFileId(m_scanUrl, &driveId))
    {
        startDeepScan();
        return;
    }

    m_scanner = new PageMediaScanner(this);
    connect(m_scanner, &PageMediaScanner::result, this, &DownloadTab::onScanResult);
    connect(m_scanner, &PageMediaScanner::errorOccurred, this, &DownloadTab::onScanError);
    m_scanner->scan(m_scanUrl);
}

void DownloadTab::onScanResult(QList<MediaLink> links)
{
    if (!m_scanBusy || m_scanStopped || sender() != m_scanner)
        return;
    m_scanner->deleteLater();
    m_scanner = nullptr;

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
        m.referer = m_scanUrl;
        m.note = "quét trang";
        rows << m;
        if (l.type == MediaLink::Type::Video || l.type == MediaLink::Type::Audio || l.type == MediaLink::Type::Document)
            primary = true;
    }
    m_scanPageFound += rows.size();
    m_scanFoundTotal += rows.size();
    m_scanAddedTotal += addScanned(rows);

    // Quét sâu khi: người dùng yêu cầu, là hồ sơ/bài đăng, hoặc quét tĩnh chưa thấy video/âm thanh/tài liệu nào.
    const bool special = LinkResolverCore::isLikelyProfileOrCollectionUrl(m_scanUrl) || LinkResolverCore::isPhotoPostUrl(m_scanUrl);
    if (m_scanForceDeep || special || !primary)
        startDeepScan();
    else
        finishScanPage();
}

void DownloadTab::onScanError(QString message)
{
    if (!m_scanBusy || m_scanStopped || sender() != m_scanner)
        return;
    m_scanner->deleteLater();
    m_scanner = nullptr;
    m_scanStaticError = message;
    startDeepScan(); // lỗi tải HTML tĩnh (chặn, cần đăng nhập...) vẫn để bộ phân giải đầy đủ thử
}

void DownloadTab::startDeepScan()
{
    if (!m_resolver)
    {
        m_resolver = new LinkResolver(this);
        connect(m_resolver, &LinkResolver::progress, this, &DownloadTab::onDeepProgress);
        connect(m_resolver, &LinkResolver::finished, this, &DownloadTab::onDeepFinished);
    }
    LinkResolver::Options o;
    o.maxItems = m_settings->profileMaxItems();
    o.useBrowser = m_settings->useBrowserFallback();
    o.ytDlpExtraArgs = m_settings->ytDlpExtraArguments();
    o.cookieFile = m_settings->httpCookieFile();
    m_resolver->setOptions(o);
    m_scanDeepRunning = true;
    m_scanDeepProgress = "bắt đầu...";
    updateScanStatus();
    m_resolver->resolve(m_scanUrl);
}

void DownloadTab::onDeepProgress(QString message)
{
    m_scanDeepProgress = message;
    updateScanStatus();
}

void DownloadTab::onDeepFinished(ResolveResult result)
{
    if (!m_scanBusy)
        return;
    m_scanDeepRunning = false;
    if (result.ok())
    {
        m_scanPageFound += result.items.size();
        m_scanFoundTotal += result.items.size();
        m_scanAddedTotal += addScanned(result.items);
        m_scanDeepWarning = result.warning;
        if (result.truncated)
            m_scanDeepWarning += (m_scanDeepWarning.isEmpty() ? "" : " ") +
                                 QString("Đã cắt theo giới hạn %1 mục ở tab Cài đặt.").arg(m_settings->profileMaxItems());
    }
    else if (!m_scanStopped)
    {
        m_scanDeepError = result.error;
    }
    finishScanPage();
}

int DownloadTab::addScanned(const QList<ResolvedMedia>& items)
{
    int added = 0;
    for (ResolvedMedia m : items)
    {
        if (m.referer.isEmpty())
            m.referer = m_scanUrl;
        // Tên tệp lấy từ trang bất kỳ là dữ liệu KHÔNG tin cậy; hàng đợi làm sạch lại lúc chọn đường dẫn đích.
        m.fileName = FileDownloaderInternal::sanitizeFileName(m.fileName);
        if (m_queue->addResolved(m, m_scanUrl) >= 0)
            ++added;
    }
    updateScanStatus();
    return added;
}

void DownloadTab::finishScanPage()
{
    if (!m_scanStopped)
    {
        const QString shortUrl = elided(m_scanUrl, 70);
        if (m_scanPageFound == 0)
        {
            const QString why = !m_scanDeepError.isEmpty()    ? m_scanDeepError
                                : !m_scanStaticError.isEmpty() ? m_scanStaticError
                                                               : QStringLiteral("không thấy ảnh/video/âm thanh/tài liệu nào.");
            m_scanNotes << "⚠ " + shortUrl + ": " + why;
            m_queue->addFailed(m_scanUrl, why); // dòng ERR trong bảng: thấy lý do, bấm "Kiểm tra lại" được
        }
        else if (!m_scanDeepError.isEmpty())
        {
            m_scanNotes << "Quét sâu " + shortUrl + ": " + m_scanDeepError;
        }
        if (!m_scanDeepWarning.isEmpty())
            m_scanNotes << "⚠ " + m_scanDeepWarning;
    }
    ++m_scanPagesDone;
    startNextScan();
}

void DownloadTab::stopScan()
{
    m_scanStopped = true;
    m_scanQueue.clear();
    if (m_resolver && m_resolver->isRunning())
        m_resolver->cancel(); // phát finished("Đã hủy.") -> onDeepFinished -> endScan
    else
        endScan();
}

void DownloadTab::endScan()
{
    if (m_scanner)
    {
        m_scanner->disconnect(this);
        m_scanner->deleteLater();
        m_scanner = nullptr;
    }
    m_scanBusy = false;
    m_scanDeepRunning = false;
    m_scanBtn->setText("🔍 Quét");

    QStringList parts;
    if (m_scanStopped)
        parts << "Đã dừng quét.";
    if (m_scanAddedTotal > 0)
        parts << QString("%1 %2 mục vào hàng đợi (chưa tải) - tick các mục cần tải rồi bấm \"Tải các mục đã chọn\".")
                     .arg(m_scanStopped ? "Đã thêm" : "Quét xong: đã thêm")
                     .arg(m_scanAddedTotal);
    else if (m_scanFoundTotal > 0)
        parts << "Không có mục mới - tất cả mục tìm được đã có trong hàng đợi.";
    else if (!m_scanStopped && m_scanNotes.isEmpty())
        parts << "Không tìm thấy ảnh/video/âm thanh/tài liệu nào.";
    parts << m_scanNotes;
    setMessage(parts.join(QLatin1Char('\n')), m_scanAddedTotal == 0 && !m_scanNotes.isEmpty()); // mỗi ghi chú một dòng
}

void DownloadTab::updateScanStatus()
{
    if (!m_scanBusy)
        return;
    QString s = "⏳ Đang quét " + elided(m_scanUrl, 60);
    if (m_scanPagesTotal > 1)
        s += QString(" (%1/%2)").arg(m_scanPagesDone + 1).arg(m_scanPagesTotal);
    s += m_scanDeepRunning ? " - quét sâu: " + m_scanDeepProgress : " - đọc mã nguồn trang...";
    if (m_scanAddedTotal > 0)
        s += QString("  •  đã thêm %1 mục").arg(m_scanAddedTotal);
    setMessage(s);
}

// ---------------------------------------------------------------------------------------------- Chất lượng riêng
void DownloadTab::pickFormatFor(int id)
{
    const QueueItem* it = m_queue->item(id);
    if (!it || it->media.kind != ResolvedMedia::Kind::YtDlp)
        return;
    if (m_infoWorker->isRunning())
    {
        setMessage("Đang lấy danh sách chất lượng của một video khác - đợi xong rồi thử lại.");
        return;
    }
    if (!YtDlpController::isSupportedVideoUrl(it->media.url))
    {
        setMessage("Địa chỉ video không hợp lệ.", true);
        return;
    }
    m_formatItemId = id;
    setMessage("⏳ Đang lấy danh sách chất lượng của video (có thể mất vài giây)...");
    m_infoWorker->setUrl(it->media.url);
    m_infoWorker->start();
}

void DownloadTab::onFormatsFetched(bool ok, VideoInfo info, QString error)
{
    const int id = m_formatItemId;
    m_formatItemId = -1;
    if (!ok)
    {
        setMessage("⚠ Không lấy được danh sách chất lượng: " + error, true);
        return;
    }
    const QueueItem* it = m_queue->item(id);
    if (!it)
        return; // mục đã bị xóa trong lúc chờ

    QStringList labels{QStringLiteral("Theo mục \"Chất lượng video\" chung")};
    QStringList selectors{QString()};
    for (auto fmt = info.formats.crbegin(); fmt != info.formats.crend(); ++fmt) // tốt nhất lên đầu
    {
        if (!fmt->hasVideo && !fmt->hasAudio)
            continue; // ảnh xem trước (storyboard)... - không phải thứ để tải như video
        const bool videoOnly = fmt->hasVideo && !fmt->hasAudio;
        labels << fmt->displayLabel() + (videoOnly ? " + ghép âm thanh tốt nhất" : "");
        selectors << YtDlpDownloadWorkerInternal::formatSelectorFor(*fmt);
    }
    if (labels.size() == 1)
    {
        setMessage("Video này không có định dạng nào để chọn riêng - dùng \"Chất lượng video\" chung.", true);
        return;
    }
    setMessage(QString());
    bool accepted = false;
    const QString chosen = QInputDialog::getItem(this, "Chọn chất lượng",
                                                 QString("%1  (%2)").arg(info.title, info.durationLabel()), labels, 0, false, &accepted);
    if (!accepted)
        return;
    const int index = labels.indexOf(chosen);
    if (index <= 0)
    {
        m_queue->setItemFormat(id, QString(), QString());
        setMessage("Video này sẽ tải theo \"Chất lượng video\" chung.");
        return;
    }
    m_queue->setItemFormat(id, selectors.at(index), chosen);
    setMessage("Đã chọn chất lượng riêng cho video: " + chosen);
}
