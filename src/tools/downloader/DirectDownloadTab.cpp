#include "DirectDownloadTab.h"

#include "DownloaderSettings.h"
#include "DownloaderUiStyle.h"
#include "engine/DownloadQueue.h"
#include "engine/LinkResolverCore.h"
#include "ui/widgets/FlowLayout.h"
#include "ui/widgets/ItemHoverCursor.h"
#include "ui/widgets/ModernMenu.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
enum Col { kCheck = 0, kName, kType, kSize, kCheckStatus, kProgress, kSpeed, kState, kOpen, kColCount };
constexpr int kIdRole = Qt::UserRole;

QString elided(const QString& s, int maxChars)
{
    return s.size() <= maxChars ? s : s.left(maxChars - 1) + "…";
}
} // namespace

DirectDownloadTab::DirectDownloadTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_queue(queue)
    , m_settings(settings)
{
    buildUi();

    connect(m_queue, &DownloadQueue::itemAdded, this, &DirectDownloadTab::onItemAdded);
    connect(m_queue, &DownloadQueue::itemUpdated, this, &DirectDownloadTab::onItemUpdated);
    connect(m_queue, &DownloadQueue::itemRemoved, this, &DirectDownloadTab::onItemRemoved);
    connect(m_queue, &DownloadQueue::message, this, &DirectDownloadTab::onMessage);
    connect(m_settings, &DownloaderSettings::changed, this, &DirectDownloadTab::onSettingsChanged);
    connect(qApp->clipboard(), &QClipboard::dataChanged, this, &DirectDownloadTab::onClipboardChanged);

    for (int id : m_queue->allIds()) // hàng đợi có thể đã có mục trước khi tab được dựng
        onItemAdded(id);
    refreshSummary();
}

void DirectDownloadTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* introLabel = new QLabel(
        "Dán MỘT hoặc NHIỀU link (mỗi dòng một link) - link tệp trực tiếp, video Facebook/TikTok/YouTube, trang phim, "
        "Google Drive, ảnh bài đăng, hoặc cả hồ sơ/kênh. Bấm \"+ Thêm vào hàng đợi\": link vào hàng đợi, CHƯA tải. Ứng "
        "dụng tự kiểm tra từng link: OK = ra được tệp/video để tải, ERR = không ra (kèm lý do). Sau đó chọn các dòng muốn "
        "tải và bấm \"Tải các mục đã chọn\".",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    m_urlEdit = new QPlainTextEdit(this);
    m_urlEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 8px; "
        "font-size: 12px; } QPlainTextEdit:focus { border-color: #0969da; }");
    m_urlEdit->setPlaceholderText("https://...   (nhiều link: mỗi dòng một link; Ctrl+Enter để thêm)");
    m_urlEdit->setFixedHeight(76);
    m_urlEdit->setTabChangesFocus(true);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), m_urlEdit, this, [this]() { onAddClicked(); }, Qt::WidgetShortcut);

    auto* urlRow = new QHBoxLayout();
    urlRow->addWidget(m_urlEdit, 1);
    m_addBtn = new QPushButton("+ Thêm vào\nhàng đợi", this);
    m_addBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_addBtn->setCursor(Qt::PointingHandCursor);
    m_addBtn->setFixedHeight(76);
    connect(m_addBtn, &QPushButton::clicked, this, &DirectDownloadTab::onAddClicked);
    urlRow->addWidget(m_addBtn);
    root->addLayout(urlRow);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu:", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(QDir::toNativeSeparators(m_settings->downloadFolder()), this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_folderLabel->setWordWrap(true);
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
    folderRow->addWidget(m_folderLabel, 1);
    m_chooseFolderBtn = new QPushButton("Đổi...", this);
    m_chooseFolderBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_chooseFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_chooseFolderBtn, &QPushButton::clicked, this, &DirectDownloadTab::onChooseFolderClicked);
    folderRow->addWidget(m_chooseFolderBtn);
    m_openFolderBtn = new QPushButton("Mở thư mục", this);
    m_openFolderBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_openFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_openFolderBtn, &QPushButton::clicked, this, &DirectDownloadTab::onOpenFolderClicked);
    folderRow->addWidget(m_openFolderBtn);
    root->addLayout(folderRow);

    m_clipboardCheck = new QCheckBox("Tự động điền link vào ô trên khi sao chép (không tự thêm vào hàng đợi)", this);
    m_clipboardCheck->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_clipboardCheck);

    // Hàng nút thao tác trên hàng đợi.
    // FlowLayout: cửa sổ hẹp thì nút tự xuống dòng thay vì bị cắt chữ (đã thấy nút "Tải các mục đã chọn" bị cụt ở ảnh chụp).
    auto* actionRow = new FlowLayout(nullptr, 0, 6, 6);
    auto addButton = [&](const QString& text, const QString& style, auto slot) {
        auto* b = new QPushButton(text, this);
        b->setStyleSheet(style);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, slot);
        actionRow->addWidget(b);
        return b;
    };
    addButton("☑ Chọn tất cả", DownloaderUi::buttonStyle(), &DirectDownloadTab::onSelectAll);
    addButton("☐ Bỏ chọn", DownloaderUi::buttonStyle(), &DirectDownloadTab::onSelectNone);
    addButton("✔ Chọn mục OK", DownloaderUi::buttonStyle(), &DirectDownloadTab::onSelectOk);
    addButton("⏸ Tạm dừng", DownloaderUi::buttonStyle(), &DirectDownloadTab::onPauseSelected);
    addButton("✖ Hủy tải", DownloaderUi::buttonStyle(), &DirectDownloadTab::onCancelSelected);
    addButton("🗑 Xóa đã chọn", DownloaderUi::buttonStyle(), &DirectDownloadTab::onRemoveSelected);
    addButton("🧹 Xóa mục xong/lỗi", DownloaderUi::buttonStyle(), &DirectDownloadTab::onRemoveFinished);
    m_downloadBtn = addButton("⬇ Tải các mục đã chọn", DownloaderUi::primaryButtonStyle(), &DirectDownloadTab::onDownloadSelected);
    root->addLayout(actionRow);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    root->addWidget(m_summaryLabel);
    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_messageLabel);

    m_table = new QTableWidget(0, kColCount, this);
    m_table->setHorizontalHeaderLabels({"", "Tên / Tiêu đề", "Loại", "Kích thước", "Kiểm tra", "Tiến độ", "Tốc độ", "Trạng thái", ""});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kProgress, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kState, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(kOpen, QHeaderView::Fixed);
    m_table->setColumnWidth(kProgress, 110);
    m_table->setColumnWidth(kState, 230);
    m_table->setColumnWidth(kOpen, 44);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &DirectDownloadTab::onTableContextMenu);
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (!m_updatingTable && item && item->column() == kCheck)
            refreshSummary();
    });
    root->addWidget(m_table, 1);
}

// ---------------------------------------------------------------------------------------------- Bảng
int DirectDownloadTab::idOfRow(int row) const
{
    auto* it = m_table->item(row, kCheck);
    return it ? it->data(kIdRole).toInt() : -1;
}

int DirectDownloadTab::rowOfId(int id) const
{
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (idOfRow(r) == id)
            return r;
    return -1;
}

void DirectDownloadTab::onItemAdded(int id)
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
        if (c == kProgress || c == kOpen)
            continue; // widget riêng
        auto* item = new QTableWidgetItem();
        if (c == kCheck)
        {
            item->setFlags((item->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
            item->setCheckState(Qt::Unchecked);
            item->setData(kIdRole, id);
        }
        else
        {
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        }
        m_table->setItem(row, c, item);
    }
    auto* bar = new QProgressBar(m_table);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(true);
    bar->setFixedHeight(18);
    m_table->setCellWidget(row, kProgress, bar);

    auto* open = new QToolButton(m_table);
    open->setText("🌐");
    open->setToolTip("Mở link này trên trình duyệt");
    open->setCursor(Qt::PointingHandCursor);
    open->setAutoRaise(true);
    open->setFixedSize(32, 24); // nút quá hẹp thì Qt rút chữ thành "..." (emoji 🌐 không hiện)
    open->setStyleSheet("QToolButton { border: 1px solid #d0d7de; border-radius: 6px; padding: 1px 4px; background: #ffffff; }"
                        "QToolButton:hover { background: #f3f4f6; border-color: #0969da; }");
    connect(open, &QToolButton::clicked, this, [this, id]() { openLinkOfId(id); });
    m_table->setCellWidget(row, kOpen, open);
    m_updatingTable = false;

    fillRow(row, id);
    refreshSummary();
}

void DirectDownloadTab::onItemUpdated(int id)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    fillRow(row, id);
    refreshSummary();
}

void DirectDownloadTab::onItemRemoved(int id)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    m_updatingTable = true;
    m_table->removeRow(row);
    m_updatingTable = false;
    refreshSummary();
}

void DirectDownloadTab::fillRow(int row, int id)
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
    if (!it->warning.isEmpty())
        tip += "\n⚠ " + it->warning;
    m_table->item(row, kName)->setToolTip(tip);

    const bool ok = it->check == QueueItem::Check::Ok;
    m_table->item(row, kType)->setText(ok && !it->media.mediaType.isEmpty() ? it->media.mediaType : QString("—"));
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

    // Tiến độ + tốc độ.
    if (auto* bar = qobject_cast<QProgressBar*>(m_table->cellWidget(row, kProgress)))
    {
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

    // Cột "Trạng thái": lý do khi ERR, tiến trình khi đang kiểm tra, còn lại là trạng thái tải.
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

QList<int> DirectDownloadTab::targetIds() const
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

void DirectDownloadTab::refreshSummary()
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

// ---------------------------------------------------------------------------------------------- Thao tác
void DirectDownloadTab::enqueueText(const QString& text)
{
    const QStringList urls = LinkResolverCore::extractUrls(text);
    if (urls.isEmpty())
    {
        onMessage("Không thấy link http/https nào trong nội dung vừa dán.");
        return;
    }
    const QList<int> ids = m_queue->addSources(urls);
    if (ids.size() == urls.size())
        onMessage(QString("Đã thêm %1 link vào hàng đợi (chưa tải) - đang kiểm tra...").arg(ids.size()));
    else
        onMessage(QString("Đã thêm %1/%2 link vào hàng đợi (còn lại trùng hoặc không hợp lệ) - đang kiểm tra...")
                      .arg(ids.size()).arg(urls.size()));
}

void DirectDownloadTab::onAddClicked()
{
    const QString text = m_urlEdit->toPlainText().trimmed();
    if (text.isEmpty())
        return;
    enqueueText(text);
    m_urlEdit->clear();
}

void DirectDownloadTab::onMessage(const QString& text)
{
    m_messageLabel->setText(text);
}

void DirectDownloadTab::onSelectAll()
{
    m_updatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, kCheck)->setCheckState(Qt::Checked);
    m_updatingTable = false;
    refreshSummary();
}

void DirectDownloadTab::onSelectNone()
{
    m_updatingTable = true;
    for (int r = 0; r < m_table->rowCount(); ++r)
        m_table->item(r, kCheck)->setCheckState(Qt::Unchecked);
    m_updatingTable = false;
    m_table->clearSelection();
    refreshSummary();
}

void DirectDownloadTab::onSelectOk()
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

void DirectDownloadTab::onDownloadSelected()
{
    const QList<int> ids = targetIds();
    if (ids.isEmpty())
    {
        onMessage("Chưa chọn mục nào - tick ô ở cột đầu (hoặc bôi đen các dòng) rồi bấm lại.");
        return;
    }
    m_queue->startItems(ids);
}

void DirectDownloadTab::onPauseSelected()
{
    for (int id : targetIds())
        m_queue->pause(id);
}

void DirectDownloadTab::onCancelSelected()
{
    for (int id : targetIds())
        m_queue->cancel(id);
}

void DirectDownloadTab::onRemoveSelected()
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

void DirectDownloadTab::onRemoveFinished()
{
    const int n = m_queue->removeFinished(true);
    onMessage(n > 0 ? QString("Đã dọn %1 mục đã xong/lỗi/hủy khỏi hàng đợi.").arg(n) : QString("Không có mục nào để dọn."));
}

void DirectDownloadTab::onChooseFolderClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục lưu", m_settings->downloadFolder());
    if (!dir.isEmpty())
        m_settings->setDownloadFolder(dir);
}

void DirectDownloadTab::onOpenFolderClicked()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings->downloadFolder()));
}

void DirectDownloadTab::onSettingsChanged()
{
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
}

void DirectDownloadTab::openLinkOfId(int id)
{
    const QueueItem* it = m_queue->item(id);
    if (!it)
        return;
    const QUrl url(it->linkToOpen());
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https"))
        return; // chỉ mở link web - không bao giờ mở file:/javascript:/... bằng ứng dụng khác
    QDesktopServices::openUrl(url);
}

void DirectDownloadTab::onTableContextMenu(const QPoint& pos)
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

void DirectDownloadTab::onClipboardChanged()
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
