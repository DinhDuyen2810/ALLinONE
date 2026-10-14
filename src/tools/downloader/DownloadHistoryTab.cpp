#include "DownloadHistoryTab.h"

#include "DownloadHistoryStore.h"
#include "DownloaderUiStyle.h"
#include "ui/widgets/ModernMenu.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
enum Col { kTime = 0, kName, kStatus, kSize, kSource, kColCount };
constexpr int kIndexRole = Qt::UserRole;

bool isWebUrl(const QString& s)
{
    const QUrl u(s);
    return u.isValid() && (u.scheme() == "http" || u.scheme() == "https");
}
} // namespace

DownloadHistoryTab::DownloadHistoryTab(DownloadHistoryStore* store, QWidget* parent)
    : QWidget(parent)
    , m_store(store)
{
    buildUi();
    connect(m_store, &DownloadHistoryStore::changed, this, &DownloadHistoryTab::rebuild);
    rebuild();
}

void DownloadHistoryTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* intro = new QLabel("Lịch sử các lần tải (tệp/video đã tải xong, thất bại hoặc bị hủy). Bấm đúp một dòng để mở tệp.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(intro);

    auto* top = new QHBoxLayout();
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setStyleSheet(DownloaderUi::inputStyle());
    m_filterEdit->setPlaceholderText("Lọc theo tên, trạng thái hoặc link...");
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &DownloadHistoryTab::onFilterChanged);
    top->addWidget(m_filterEdit, 1);
    auto addBtn = [&](const QString& text, auto slot, bool danger = false) {
        auto* b = new QPushButton(text, this);
        b->setStyleSheet(danger ? DownloaderUi::dangerButtonStyle() : DownloaderUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, slot);
        top->addWidget(b);
    };
    addBtn("Mở tệp", &DownloadHistoryTab::onOpenFile);
    addBtn("Mở thư mục", &DownloadHistoryTab::onOpenFolder);
    addBtn("🌐 Mở link nguồn", &DownloadHistoryTab::onOpenSource);
    addBtn("Xóa mục", &DownloadHistoryTab::onRemoveSelected);
    addBtn("Xóa tất cả", &DownloadHistoryTab::onClearAll, true);
    root->addLayout(top);

    m_countLabel = new QLabel(this);
    m_countLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_countLabel);

    m_table = new QTableWidget(0, kColCount, this);
    m_table->setHorizontalHeaderLabels({"Thời gian", "Tên", "Trạng thái", "Kích thước", "Nguồn"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(kSource, QHeaderView::Interactive);
    m_table->setColumnWidth(kSource, 220);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setTextElideMode(Qt::ElideMiddle);
    m_table->setStyleSheet(DownloaderUi::tableStyle());
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &DownloadHistoryTab::onContextMenu);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { onOpenFile(); });
    root->addWidget(m_table, 1);
}

void DownloadHistoryTab::rebuild()
{
    const QList<DownloadHistoryEntry>& all = m_store->entries();
    const QString filter = m_filterEdit->text().trimmed();
    m_table->setRowCount(0);
    int shown = 0;
    for (int i = 0; i < all.size(); ++i)
    {
        const DownloadHistoryEntry& e = all[i];
        if (!filter.isEmpty() && !e.title.contains(filter, Qt::CaseInsensitive) && !e.fileName.contains(filter, Qt::CaseInsensitive) &&
            !e.status.contains(filter, Qt::CaseInsensitive) && !e.sourceUrl.contains(filter, Qt::CaseInsensitive) &&
            !e.mediaUrl.contains(filter, Qt::CaseInsensitive))
            continue;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto put = [&](int col, const QString& text, const QString& tip = QString()) {
            auto* it = new QTableWidgetItem(text);
            it->setFlags(it->flags() & ~Qt::ItemIsEditable);
            it->setToolTip(tip);
            if (col == kTime)
                it->setData(kIndexRole, i);
            m_table->setItem(row, col, it);
            return it;
        };
        put(kTime, e.time.toString("dd/MM/yyyy HH:mm:ss"));
        put(kName, e.title.isEmpty() ? e.fileName : e.title, e.path.isEmpty() ? e.fileName : e.path);
        QTableWidgetItem* st = put(kStatus, e.status, e.error);
        st->setForeground(e.status == "Hoàn tất" ? QColor("#1a7f37") : e.status == "Thất bại" ? QColor("#cf222e") : QColor("#57606a"));
        put(kSize, e.size >= 0 ? DownloaderUi::formatBytes(e.size) : QString("—"));
        put(kSource, e.sourceUrl, e.sourceUrl);
        ++shown;
    }
    m_countLabel->setText(all.isEmpty() ? QString("Chưa có lịch sử tải.")
                                        : filter.isEmpty() ? QString("%1 mục.").arg(all.size())
                                                           : QString("%1/%2 mục khớp bộ lọc.").arg(shown).arg(all.size()));
}

void DownloadHistoryTab::onFilterChanged()
{
    rebuild();
}

int DownloadHistoryTab::currentEntryIndex() const
{
    const int row = m_table->currentRow();
    if (row < 0)
        return -1;
    auto* it = m_table->item(row, kTime);
    return it ? it->data(kIndexRole).toInt() : -1;
}

void DownloadHistoryTab::onOpenFile()
{
    const int i = currentEntryIndex();
    if (i < 0 || i >= m_store->entries().size())
        return;
    const QString path = m_store->entries()[i].path;
    if (!path.isEmpty() && QFileInfo::exists(path))
    {
        if (DownloaderUi::confirmOpenDownloadedFile(this, path))
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    else
        m_countLabel->setText("Tệp không còn tồn tại (đã bị di chuyển/xóa) hoặc lần tải đó không tạo ra tệp.");
}

void DownloadHistoryTab::onOpenFolder()
{
    const int i = currentEntryIndex();
    if (i < 0 || i >= m_store->entries().size())
        return;
    const DownloadHistoryEntry& e = m_store->entries()[i];
    QString dir = e.path.isEmpty() ? e.folder : QFileInfo(e.path).absolutePath();
    if (dir.isEmpty() || !QFileInfo::exists(dir))
        dir = e.folder;
    if (!dir.isEmpty() && QFileInfo::exists(dir))
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void DownloadHistoryTab::onOpenSource()
{
    const int i = currentEntryIndex();
    if (i < 0 || i >= m_store->entries().size())
        return;
    const DownloadHistoryEntry& e = m_store->entries()[i];
    const QString link = isWebUrl(e.sourceUrl) ? e.sourceUrl : e.mediaUrl;
    if (isWebUrl(link))
        QDesktopServices::openUrl(QUrl(link));
}

void DownloadHistoryTab::onRemoveSelected()
{
    const int i = currentEntryIndex();
    if (i >= 0)
        m_store->removeAt(i);
}

void DownloadHistoryTab::onClearAll()
{
    if (m_store->entries().isEmpty())
        return;
    const auto reply = QMessageBox::question(this, "Xóa lịch sử",
                                             QString("Xóa toàn bộ %1 mục lịch sử tải? (Các tệp đã tải KHÔNG bị xóa khỏi ổ đĩa.)")
                                                 .arg(m_store->entries().size()),
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes)
        m_store->clear();
}

void DownloadHistoryTab::onContextMenu(const QPoint& pos)
{
    const int row = m_table->rowAt(pos.y());
    if (row < 0)
        return;
    m_table->selectRow(row);
    QMenu menu(this);
    ModernMenu::style(&menu);
    QAction* openFile = ModernMenu::addAction(menu, "Mở tệp");
    QAction* openDir = ModernMenu::addAction(menu, "Mở thư mục chứa tệp");
    QAction* openSrc = ModernMenu::addAction(menu, "Mở link nguồn trên trình duyệt");
    menu.addSeparator();
    QAction* remove = ModernMenu::addAction(menu, "Xóa khỏi lịch sử");
    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (chosen == openFile) onOpenFile();
    else if (chosen == openDir) onOpenFolder();
    else if (chosen == openSrc) onOpenSource();
    else if (chosen == remove) onRemoveSelected();
}
