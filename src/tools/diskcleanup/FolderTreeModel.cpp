#include "FolderTreeModel.h"

#include "DiskUiStyle.h"

#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QLocale>
#include <QStyle>

#include <algorithm>

namespace
{
QString count(qint64 n)
{
    return QLocale().toString(n);
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

FolderTreeModel::~FolderTreeModel() = default;

void FolderTreeModel::setTree(std::shared_ptr<FolderTree> tree)
{
    beginResetModel();
    m_root.reset();
    m_tree = std::move(tree);
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
    endResetModel();
}

FolderTreeModel::Item* FolderTreeModel::itemFor(const QModelIndex& parent) const
{
    return parent.isValid() ? itemOf(parent) : m_root.get();
}

bool FolderTreeModel::hasKids(const Item* item)
{
    if (!item || item->kind != Kind::Folder || !item->node)
        return false;
    const FolderNode* n = item->node;
    return !n->children.empty() || !n->bigFiles.empty() || n->prunedFolderCount > 0 ||
           n->ownFileCount > static_cast<qint64>(n->bigFiles.size());
}

void FolderTreeModel::ensureLoaded(Item* item) const
{
    if (!item || item->loaded)
        return;
    item->loaded = true;
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
    if (!item->parent || item->parent == m_root.get() || !item->parent->node)
        return 1.0;
    const qint64 total = item->parent->node->bytes;
    return total > 0 ? static_cast<double>(item->bytes) / static_cast<double>(total) : 0.0;
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
    }
    return {};
}

FolderTreeModel::Kind FolderTreeModel::kindAt(const QModelIndex& index) const
{
    return index.isValid() ? itemOf(index)->kind : Kind::Folder;
}

QString FolderTreeModel::pathAt(const QModelIndex& index) const
{
    if (!index.isValid())
        return {};
    const Item* item = itemOf(index);
    if (item->kind == Kind::Folder)
        return item->node->path();
    if (item->kind == Kind::File)
    {
        const QString dir = item->node->path();
        return dir.endsWith(QLatin1Char('/')) ? dir + item->file->name : dir + QLatin1Char('/') + item->file->name;
    }
    return item->node->path(); // dòng gộp: thư mục chứa chúng
}

qint64 FolderTreeModel::bytesAt(const QModelIndex& index) const
{
    return index.isValid() ? itemOf(index)->bytes : 0;
}

QVariant FolderTreeModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    const Item* item = itemOf(index);
    switch (role)
    {
        case Qt::DisplayRole:
            switch (index.column())
            {
                case NameColumn: return nameOf(item);
                case SizeColumn: return DiskUi::formatBytes(item->bytes);
                case ShareColumn: return QStringLiteral("%1%").arg(QLocale().toString(shareOf(item) * 100.0, 'f', 1));
                case FilesColumn: return item->files >= 0 ? QVariant(count(item->files)) : QVariant();
                case FoldersColumn: return item->kind == Kind::Folder && item->folders >= 0 ? QVariant(count(item->folders)) : QVariant();
            }
            return {};
        case Qt::DecorationRole:
            if (index.column() != NameColumn)
                return {};
            return item->kind == Kind::Folder ? m_folderIcon : item->kind == Kind::File ? m_fileIcon : m_smallIcon;
        case Qt::TextAlignmentRole:
            return (index.column() == NameColumn) ? QVariant(int(Qt::AlignLeft | Qt::AlignVCenter)) : QVariant(int(Qt::AlignRight | Qt::AlignVCenter));
        case Qt::ForegroundRole:
            if (item->kind == Kind::SmallFiles || item->kind == Kind::SmallFolders)
                return QBrush(QColor("#6e7781"));
            if (item->kind == Kind::Folder && item->node->unreadable)
                return QBrush(QColor("#cf222e"));
            return {};
        case Qt::ToolTipRole:
        {
            QString tip = pathAt(index);
            tip += QStringLiteral("\nChiếm trên đĩa: %1\nKích thước tệp: %2").arg(DiskUi::formatBytes(item->bytes), DiskUi::formatBytes(item->logicalBytes));
            if (item->kind == Kind::SmallFolders)
                tip += QStringLiteral("\n(Đã gộp lại để tiết kiệm bộ nhớ - không liệt kê riêng từng thư mục nhỏ.)");
            if (item->kind == Kind::SmallFiles)
                tip += QStringLiteral("\n(Các tệp nhỏ trực tiếp trong thư mục này, gộp thành một dòng.)");
            if (item->kind == Kind::Folder && item->node->unreadable)
                tip += QStringLiteral("\nKhông đọc được nội dung (thiếu quyền truy cập?) - dung lượng thật có thể lớn hơn.");
            return tip;
        }
        case ShareRole:
            return shareOf(item);
        case KindRole:
            return static_cast<int>(item->kind);
        case PathRole:
            return pathAt(index);
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
