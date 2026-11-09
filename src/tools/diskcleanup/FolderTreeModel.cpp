#include "FolderTreeModel.h"

#include "DiskUiStyle.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QLocale>
#include <QRunnable>
#include <QSet>
#include <QStyle>
#include <QThread>
#include <QThreadPool>

#include <algorithm>

namespace
{
/// Số mục tối đa mỗi loại (tệp / thư mục) hiện ra khi mở một dòng gộp - phần còn lại ghi chú bằng một dòng "[… còn N mục]".
constexpr int kMaxLiveEntries = 5000;

QString count(qint64 n)
{
    return QLocale().toString(n);
}

QString joinPath(const QString& dir, const QString& name)
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

QString normalizedPath(const QString& p)
{
    QString n = QDir::cleanPath(QDir::fromNativeSeparators(p));
    while (n.size() > 3 && n.endsWith(QLatin1Char('/')))
        n.chop(1);
    return n;
}

/// 'target' == 'dir' hoặc nằm bên trong 'dir' (không phân biệt hoa thường).
bool isSameOrInside(const QString& target, const QString& dir)
{
    if (target.compare(dir, Qt::CaseInsensitive) == 0)
        return true;
    return target.startsWith(dir.endsWith(QLatin1Char('/')) ? dir : dir + QLatin1Char('/'), Qt::CaseInsensitive);
}
} // namespace

FolderTreeModel::FolderTreeModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    if (QStyle* style = QApplication::style())
    {
        m_folderIcon = style->standardIcon(QStyle::SP_DirIcon);
        m_fileIcon = style->standardIcon(QStyle::SP_FileIcon);
        m_smallIcon = style->standardIcon(QStyle::SP_FileDialogDetailedView);
    }
}

FolderTreeModel::~FolderTreeModel()
{
    // Các lượt đọc trực tiếp còn chạy trên QThreadPool giữ con trỏ tới mô hình này để gửi kết quả về: dừng chúng và chờ
    // chúng thoát hẳn (chúng kiểm tra cờ dừng ở từng mục nên rất nhanh) trước khi mô hình bị hủy.
    if (m_stopLive)
        m_stopLive->store(true);
    while (m_inflight && m_inflight->load() > 0)
        QThread::msleep(1);
}

void FolderTreeModel::cancelLiveLoads()
{
    if (m_stopLive)
        m_stopLive->store(true);
    m_stopLive = std::make_shared<std::atomic_bool>(false); // các lượt mới dùng cờ mới
    ++m_generation;
}

void FolderTreeModel::rebuildRoot()
{
    m_root.reset();
    if (m_tree && m_tree->root)
    {
        m_root = std::make_unique<Item>();
        m_root->loaded = true;
        auto top = std::make_unique<Item>();
        top->parent = m_root.get();
        top->row = 0;
        top->kind = Kind::Folder;
        top->node = m_tree->root.get();
        top->bytes = top->node->bytes;
        top->logicalBytes = top->node->logicalBytes;
        top->files = top->node->fileCount;
        top->folders = top->node->folderCount;
        m_root->kids.push_back(std::move(top));
    }
}

void FolderTreeModel::setTree(std::shared_ptr<FolderTree> tree)
{
    beginResetModel();
    cancelLiveLoads();
    m_tree = std::move(tree);
    rebuildRoot();
    endResetModel();
}

int FolderTreeModel::applyRemovals(const std::vector<FolderTreeEdit::Removal>& removals)
{
    if (!m_tree)
        return 0;
    beginResetModel();
    cancelLiveLoads(); // các Item cũ sắp bị hủy: kết quả đọc trực tiếp đang bay phải bị bỏ
    m_root.reset();    // Item giữ con trỏ vào FolderNode/FolderFile - hủy TRƯỚC khi sửa cây
    int applied = 0;
    for (const auto& r : removals)
        if (FolderTreeEdit::apply(*m_tree, r))
            ++applied;
    rebuildRoot();
    endResetModel();
    return applied;
}

FolderTreeModel::Item* FolderTreeModel::itemFor(const QModelIndex& parent) const
{
    return parent.isValid() ? itemOf(parent) : m_root.get();
}

bool FolderTreeModel::hasKids(const Item* item)
{
    if (!item)
        return false;
    switch (item->kind)
    {
        case Kind::Folder:
        {
            if (!item->node)
                return false;
            const FolderNode* n = item->node;
            return !n->children.empty() || !n->bigFiles.empty() || n->prunedFolderCount > 0 ||
                   n->ownFileCount > static_cast<qint64>(n->bigFiles.size());
        }
        case Kind::SmallFiles:
        case Kind::SmallFolders:
            return true; // mở ra sẽ đọc trực tiếp thư mục
        case Kind::LiveFolder:
            return item->files > 0 || item->folders > 0;
        default:
            return false;
    }
}

void FolderTreeModel::ensureLoaded(Item* item) const
{
    if (!item || item->loaded)
        return;
    item->loaded = true;
    if (item->kind == Kind::SmallFiles || item->kind == Kind::SmallFolders || item->kind == Kind::LiveFolder)
    {
        startLiveLoad(item);
        return;
    }
    if (item->kind != Kind::Folder || !item->node)
        return;
    const FolderNode* n = item->node;

    for (const auto& c : n->children)
    {
        auto it = std::make_unique<Item>();
        it->kind = Kind::Folder;
        it->node = c.get();
        it->bytes = c->bytes;
        it->logicalBytes = c->logicalBytes;
        it->files = c->fileCount;
        it->folders = c->folderCount;
        item->kids.push_back(std::move(it));
    }
    qint64 bigBytes = 0, bigLogical = 0;
    for (const FolderFile& f : n->bigFiles)
    {
        auto it = std::make_unique<Item>();
        it->kind = Kind::File;
        it->node = n;
        it->file = &f;
        it->bytes = f.bytes;
        it->logicalBytes = f.logicalBytes;
        it->files = 1;
        item->kids.push_back(std::move(it));
        bigBytes += f.bytes;
        bigLogical += f.logicalBytes;
    }
    const qint64 smallFiles = n->ownFileCount - static_cast<qint64>(n->bigFiles.size());
    if (smallFiles > 0)
    {
        auto it = std::make_unique<Item>();
        it->kind = Kind::SmallFiles;
        it->node = n;
        it->bytes = n->ownBytes - bigBytes;
        it->logicalBytes = n->ownLogicalBytes - bigLogical;
        it->files = smallFiles;
        it->smallCount = smallFiles;
        item->kids.push_back(std::move(it));
    }
    if (n->prunedFolderCount > 0)
    {
        auto it = std::make_unique<Item>();
        it->kind = Kind::SmallFolders;
        it->node = n;
        it->bytes = n->prunedBytes;
        it->smallCount = n->prunedFolderCount;
        item->kids.push_back(std::move(it));
    }
    std::stable_sort(item->kids.begin(), item->kids.end(), [](const auto& a, const auto& b) { return a->bytes > b->bytes; });
    for (size_t i = 0; i < item->kids.size(); ++i)
    {
        item->kids[i]->parent = item;
        item->kids[i]->row = static_cast<int>(i);
    }
}

void FolderTreeModel::startLiveLoad(Item* item) const
{
    // Dòng "đang đọc" hiện ngay; kết quả thật thay vào khi luồng nền xong (finishLiveLoad).
    auto placeholder = std::make_unique<Item>();
    placeholder->kind = Kind::Loading;
    placeholder->node = item->node;
    placeholder->parent = item;
    placeholder->row = 0;
    placeholder->loaded = true;
    item->kids.push_back(std::move(placeholder));

    const QString path = pathOf(item);
    // Tên đã được liệt kê riêng ở cấp trên (tệp lớn/thư mục con được giữ) không lặp lại trong dòng gộp.
    std::vector<QString> exclude;
    if (item->node && item->kind == Kind::SmallFiles)
        for (const FolderFile& f : item->node->bigFiles)
            exclude.push_back(f.name.toLower());
    if (item->node && item->kind == Kind::SmallFolders)
        for (const auto& c : item->node->children)
            exclude.push_back(c->name.toLower());

    FolderTreeModel* self = const_cast<FolderTreeModel*>(this);
    const auto stop = m_stopLive;
    const auto inflight = m_inflight;
    const quint64 generation = m_generation;
    inflight->fetch_add(1);
    QThreadPool::globalInstance()->start(QRunnable::create([self, stop, inflight, generation, item, path, exclude]() {
        auto listing = std::make_shared<FolderLiveListing>(FolderTreeScanner::listFolderLive(path, *stop, kMaxLiveEntries));
        if (!stop->load() && !listing->stopped)
        {
            // 'self' còn sống: ~FolderTreeModel chờ inflight về 0 trước khi hủy.
            QMetaObject::invokeMethod(
                self, [self, generation, item, listing, exclude]() { self->finishLiveLoad(generation, item, listing, exclude); },
                Qt::QueuedConnection);
        }
        inflight->fetch_sub(1);
    }));
}

void FolderTreeModel::finishLiveLoad(quint64 generation, Item* item, std::shared_ptr<FolderLiveListing> listing,
                                     std::vector<QString> exclude)
{
    if (generation != m_generation || !item || !m_root || item->kids.empty() || item->kids.front()->kind != Kind::Loading)
        return; // mô hình đã được dựng lại trong lúc chờ
    const QSet<QString> skip(exclude.begin(), exclude.end());

    std::vector<std::unique_ptr<Item>> fresh;
    qint64 excludedFound = 0;
    auto addFolder = [&](const FolderLiveFolder& d) {
        auto it = std::make_unique<Item>();
        it->kind = Kind::LiveFolder;
        it->node = item->node;
        it->name = d.name;
        it->bytes = d.bytes;
        it->logicalBytes = d.logicalBytes;
        it->files = d.fileCount;
        it->folders = d.folderCount;
        it->unreadable = d.unreadable;
        it->loaded = false;
        fresh.push_back(std::move(it));
    };
    auto addFile = [&](const FolderLiveFile& f) {
        auto it = std::make_unique<Item>();
        it->kind = Kind::LiveFile;
        it->node = item->node;
        it->name = f.name;
        it->bytes = f.bytes;
        it->logicalBytes = f.logicalBytes;
        it->files = 1;
        it->loaded = true;
        fresh.push_back(std::move(it));
    };

    qint64 hidden = 0;
    switch (item->kind)
    {
        case Kind::SmallFiles:
            for (const auto& f : listing->files)
            {
                if (skip.contains(f.name.toLower()))
                {
                    ++excludedFound;
                    continue;
                }
                addFile(f);
            }
            hidden = listing->totalFiles - excludedFound - static_cast<qint64>(fresh.size());
            break;
        case Kind::SmallFolders:
            for (const auto& d : listing->folders)
            {
                if (skip.contains(d.name.toLower()))
                {
                    ++excludedFound;
                    continue;
                }
                addFolder(d);
            }
            hidden = listing->totalFolders - excludedFound - static_cast<qint64>(fresh.size());
            break;
        default: // LiveFolder: mọi thứ bên trong
            for (const auto& d : listing->folders)
                addFolder(d);
            for (const auto& f : listing->files)
                addFile(f);
            hidden = (listing->totalFiles - static_cast<qint64>(listing->files.size())) +
                     (listing->totalFolders - static_cast<qint64>(listing->folders.size()));
            break;
    }
    std::stable_sort(fresh.begin(), fresh.end(), [](const auto& a, const auto& b) {
        return a->bytes != b->bytes ? a->bytes > b->bytes : a->name.compare(b->name, Qt::CaseInsensitive) < 0;
    });
    if (listing->unreadable)
    {
        auto note = std::make_unique<Item>();
        note->kind = Kind::LiveMore;
        note->node = item->node;
        note->name = QStringLiteral("[không đọc được thư mục này - thiếu quyền truy cập hoặc đã bị xóa]");
        note->loaded = true;
        fresh.push_back(std::move(note));
    }
    else if (hidden > 0)
    {
        auto note = std::make_unique<Item>();
        note->kind = Kind::LiveMore;
        note->node = item->node;
        note->name = QStringLiteral("[… và %1 mục nhỏ hơn không liệt kê ở đây - dùng \"Mở trong Explorer\" để xem hết]").arg(count(hidden));
        note->loaded = true;
        fresh.push_back(std::move(note));
    }

    const QModelIndex parentIndex = createIndex(item->row, 0, item);
    if (fresh.empty())
    {
        beginRemoveRows(parentIndex, 0, 0);
        item->kids.clear();
        endRemoveRows();
        return;
    }
    // Chèn mục thật SAU dòng "đang đọc" rồi mới gỡ nó: thư mục cha không bao giờ ở trạng thái "không có con" giữa chừng nên
    // QTreeView không tự thu gọn nó.
    const int n = static_cast<int>(fresh.size());
    beginInsertRows(parentIndex, 1, n);
    for (int i = 0; i < n; ++i)
    {
        fresh[static_cast<size_t>(i)]->parent = item;
        fresh[static_cast<size_t>(i)]->row = i + 1;
        item->kids.push_back(std::move(fresh[static_cast<size_t>(i)]));
    }
    endInsertRows();
    beginRemoveRows(parentIndex, 0, 0);
    item->kids.erase(item->kids.begin());
    endRemoveRows();
    for (size_t i = 0; i < item->kids.size(); ++i)
        item->kids[i]->row = static_cast<int>(i);
}

int FolderTreeModel::columnCount(const QModelIndex&) const
{
    return ColumnCount;
}

int FolderTreeModel::rowCount(const QModelIndex& parent) const
{
    if (parent.column() > 0)
        return 0;
    Item* item = itemFor(parent);
    if (!item)
        return 0;
    ensureLoaded(item);
    return static_cast<int>(item->kids.size());
}

QModelIndex FolderTreeModel::index(int row, int column, const QModelIndex& parent) const
{
    if (row < 0 || column < 0 || column >= ColumnCount)
        return {};
    Item* item = itemFor(parent);
    if (!item)
        return {};
    ensureLoaded(item);
    if (static_cast<size_t>(row) >= item->kids.size())
        return {};
    return createIndex(row, column, item->kids[static_cast<size_t>(row)].get());
}

QModelIndex FolderTreeModel::parent(const QModelIndex& child) const
{
    if (!child.isValid())
        return {};
    const Item* item = itemOf(child);
    if (!item || !item->parent || item->parent == m_root.get())
        return {};
    return createIndex(item->parent->row, 0, item->parent);
}

bool FolderTreeModel::hasChildren(const QModelIndex& parent) const
{
    if (!parent.isValid())
        return m_root && !m_root->kids.empty();
    if (parent.column() > 0)
        return false;
    return hasKids(itemOf(parent));
}

double FolderTreeModel::shareOf(const Item* item) const
{
    if (!item->parent || item->parent == m_root.get())
        return 1.0;
    // Mục "Live" bên trong một thư mục đọc trực tiếp: so với CHÍNH thư mục đó (nó không có FolderNode riêng).
    qint64 total = 0;
    if (item->parent->kind == Kind::LiveFolder)
        total = item->parent->bytes;
    else if (item->parent->node)
        total = item->parent->node->bytes;
    else
        return 1.0;
    return total > 0 ? static_cast<double>(item->bytes) / static_cast<double>(total) : 0.0;
}

QString FolderTreeModel::containerPathOf(const Item* item) const
{
    if (!item)
        return {};
    switch (item->kind)
    {
        case Kind::Folder:
        case Kind::SmallFiles:
        case Kind::SmallFolders:
            return item->node ? item->node->path() : QString();
        case Kind::LiveFolder:
            return pathOf(item);
        default:
            return {};
    }
}

QString FolderTreeModel::pathOf(const Item* item) const
{
    if (!item)
        return {};
    switch (item->kind)
    {
        case Kind::Folder:
            return item->node->path();
        case Kind::File:
            return joinPath(item->node->path(), item->file->name);
        case Kind::SmallFiles:
        case Kind::SmallFolders:
            return item->node->path(); // dòng gộp: thư mục chứa chúng
        case Kind::LiveFile:
        case Kind::LiveFolder:
            return joinPath(containerPathOf(item->parent), item->name);
        case Kind::LiveMore:
        case Kind::Loading:
            break;
    }
    return {};
}

QString FolderTreeModel::nameOf(const Item* item) const
{
    switch (item->kind)
    {
        case Kind::Folder:
            return item->node->unreadable ? item->node->name + QStringLiteral("  (không đọc được)") : item->node->name;
        case Kind::File:
            return item->file->name;
        case Kind::SmallFiles:
            return QStringLiteral("[%1 tệp nhỏ]").arg(count(item->smallCount));
        case Kind::SmallFolders:
        {
            const qint64 limit = m_tree ? m_tree->stats.keepFolderBytes : 0;
            return limit > 0 ? QStringLiteral("[%1 thư mục nhỏ, mỗi thư mục < %2]").arg(count(item->smallCount), DiskUi::formatBytes(limit))
                             : QStringLiteral("[%1 thư mục nhỏ]").arg(count(item->smallCount));
        }
        case Kind::LiveFile:
            return item->name;
        case Kind::LiveFolder:
            return item->unreadable ? item->name + QStringLiteral("  (không đọc được)") : item->name;
        case Kind::LiveMore:
            return item->name;
        case Kind::Loading:
            return QStringLiteral("⏳ Đang đọc thư mục...");
    }
    return {};
}

FolderTreeModel::Kind FolderTreeModel::kindAt(const QModelIndex& index) const
{
    return index.isValid() ? itemOf(index)->kind : Kind::Folder;
}

QString FolderTreeModel::pathAt(const QModelIndex& index) const
{
    return index.isValid() ? pathOf(itemOf(index)) : QString();
}

qint64 FolderTreeModel::bytesAt(const QModelIndex& index) const
{
    return index.isValid() ? itemOf(index)->bytes : 0;
}

bool FolderTreeModel::isFolderAt(const QModelIndex& index) const
{
    if (!index.isValid())
        return false;
    const Kind k = itemOf(index)->kind;
    return k == Kind::Folder || k == Kind::LiveFolder;
}

bool FolderTreeModel::isScanRoot(const QModelIndex& index) const
{
    if (!index.isValid())
        return false;
    const Item* item = itemOf(index);
    return item->kind == Kind::Folder && item->parent == m_root.get();
}

bool FolderTreeModel::isDeletable(const QModelIndex& index) const
{
    FolderTreeEdit::Removal ignored;
    return removalFor(index, &ignored);
}

bool FolderTreeModel::removalFor(const QModelIndex& index, FolderTreeEdit::Removal* out) const
{
    if (!index.isValid() || !out)
        return false;
    const Item* item = itemOf(index);
    FolderTreeEdit::Removal r;
    switch (item->kind)
    {
        case Kind::Folder:
            if (!item->parent || item->parent == m_root.get())
                return false; // gốc quét
            r.target = FolderTreeEdit::Target::KeptFolder;
            r.node = const_cast<FolderNode*>(item->node);
            break;
        case Kind::File:
            r.target = FolderTreeEdit::Target::BigFile;
            r.node = const_cast<FolderNode*>(item->node);
            r.fileName = item->file->name;
            break;
        case Kind::LiveFile:
            r.target = (item->parent && item->parent->kind == Kind::SmallFiles) ? FolderTreeEdit::Target::SmallFile
                                                                                 : FolderTreeEdit::Target::InsidePruned;
            r.node = const_cast<FolderNode*>(item->node);
            r.bytes = item->bytes;
            r.logicalBytes = item->logicalBytes;
            r.files = 1;
            r.folders = 0;
            break;
        case Kind::LiveFolder:
            r.target = (item->parent && item->parent->kind == Kind::SmallFolders) ? FolderTreeEdit::Target::PrunedFolder
                                                                                   : FolderTreeEdit::Target::InsidePruned;
            r.node = const_cast<FolderNode*>(item->node);
            r.bytes = item->bytes;
            r.logicalBytes = item->logicalBytes;
            r.files = item->files;
            r.folders = 1 + item->folders;
            break;
        default:
            return false;
    }
    if (!r.node)
        return false;
    *out = r;
    return true;
}

QModelIndex FolderTreeModel::indexForFolderPath(const QString& path) const
{
    if (!m_root || m_root->kids.empty())
        return {};
    const QString target = normalizedPath(path);
    Item* cur = m_root->kids.front().get();
    for (int depth = 0; depth < 100000; ++depth)
    {
        const QString curPath = normalizedPath(cur->node->path());
        if (target.compare(curPath, Qt::CaseInsensitive) == 0)
            return createIndex(cur->row, 0, cur);
        if (!isSameOrInside(target, curPath))
            return {};
        ensureLoaded(cur);
        Item* next = nullptr;
        for (const auto& k : cur->kids)
        {
            if (k->kind != Kind::Folder)
                continue;
            if (isSameOrInside(target, normalizedPath(k->node->path())))
            {
                next = k.get();
                break;
            }
        }
        if (!next)
            return {};
        cur = next;
    }
    return {};
}

QModelIndex FolderTreeModel::aggregateIndex(const QModelIndex& folderIndex, Kind kind) const
{
    if (!folderIndex.isValid())
        return {};
    Item* folder = itemOf(folderIndex);
    if (folder->kind != Kind::Folder)
        return {};
    ensureLoaded(folder);
    for (const auto& k : folder->kids)
        if (k->kind == kind)
            return createIndex(k->row, 0, k.get());
    return {};
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const Item* item = itemOf(index);
    const bool note = item->kind == Kind::LiveMore || item->kind == Kind::Loading;
    const bool folderLike = item->kind == Kind::Folder || item->kind == Kind::LiveFolder;
    switch (role)
    {
        case Qt::DisplayRole:
            switch (index.column())
            {
                case NameColumn: return nameOf(item);
                case SizeColumn: return note ? QVariant() : QVariant(DiskUi::formatBytes(item->bytes));
                case ShareColumn: return note ? QVariant() : QVariant(QStringLiteral("%1%").arg(QLocale().toString(shareOf(item) * 100.0, 'f', 1)));
                case FilesColumn: return (!note && item->files >= 0) ? QVariant(count(item->files)) : QVariant();
                case FoldersColumn: return (folderLike && item->folders >= 0) ? QVariant(count(item->folders)) : QVariant();
            }
            return {};
        case Qt::DecorationRole:
            if (index.column() != NameColumn)
                return {};
            return folderLike ? m_folderIcon : (item->kind == Kind::File || item->kind == Kind::LiveFile) ? m_fileIcon : m_smallIcon;
        case Qt::TextAlignmentRole:
            return (index.column() == NameColumn) ? QVariant(int(Qt::AlignLeft | Qt::AlignVCenter)) : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        case Qt::ForegroundRole:
            if (item->kind == Kind::SmallFiles || item->kind == Kind::SmallFolders || note)
                return QBrush(QColor("#6e7781"));
            if (item->kind == Kind::Folder && item->node->unreadable)
                return QBrush(QColor("#cf222e"));
            if (item->kind == Kind::LiveFolder && item->unreadable)
                return QBrush(QColor("#cf222e"));
            return {};
        case Qt::ToolTipRole:
        {
            if (item->kind == Kind::Loading)
                return {};
            if (item->kind == Kind::LiveMore)
                return item->name;
            QString tip = pathOf(item);
            tip += QStringLiteral("\nChiếm trên đĩa: %1\nKích thước tệp: %2").arg(DiskUi::formatBytes(item->bytes), DiskUi::formatBytes(item->logicalBytes));
            if (item->kind == Kind::SmallFolders)
                tip += QStringLiteral("\n(Gộp lại để tiết kiệm bộ nhớ - mở dòng này để xem từng thư mục nhỏ.)");
            if (item->kind == Kind::SmallFiles)
                tip += QStringLiteral("\n(Các tệp nhỏ trực tiếp trong thư mục này, gộp thành một dòng - mở dòng này để xem/xóa từng tệp.)");
            if (item->kind == Kind::LiveFile || item->kind == Kind::LiveFolder)
                tip += QStringLiteral("\n(Đọc trực tiếp từ đĩa lúc mở - có thể khác chút so với lúc quét.)");
            if (item->kind == Kind::Folder && item->node->unreadable)
                tip += QStringLiteral("\nKhông đọc được nội dung (thiếu quyền truy cập?) - dung lượng thật có thể lớn hơn.");
            return tip;
        }
        case ShareRole:
            return shareOf(item);
        case KindRole:
            return static_cast<int>(item->kind);
        case PathRole:
            return pathOf(item);
        case BytesRole:
            return item->bytes;
        default:
            return {};
    }
}

QVariant FolderTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole)
        return section == NameColumn ? QVariant(int(Qt::AlignLeft | Qt::AlignVCenter)) : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
    if (role == Qt::ToolTipRole && section == SizeColumn)
        return QStringLiteral("Dung lượng thật sự chiếm trên đĩa (làm tròn theo cụm, tệp nén/thưa/OneDrive tính đúng) - giống \"Size on disk\" của Explorer.");
    if (role != Qt::DisplayRole)
        return {};
    switch (section)
    {
        case NameColumn: return QStringLiteral("Thư mục / tệp");
        case SizeColumn: return QStringLiteral("Chiếm trên đĩa");
        case ShareColumn: return QStringLiteral("% của thư mục cha");
        case FilesColumn: return QStringLiteral("Số tệp");
        case FoldersColumn: return QStringLiteral("Số thư mục");
    }
    return {};
}
