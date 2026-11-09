#include "FolderTreeEdit.h"

#include <algorithm>

namespace FolderTreeEdit
{
namespace
{
qint64 sub(qint64 value, qint64 amount)
{
    return std::max<qint64>(0, value - amount);
}

/// Trừ khỏi 'from' và mọi thư mục cha của nó.
void subtractUp(FolderNode* from, qint64 bytes, qint64 logical, qint64 files, qint64 folders)
{
    for (FolderNode* n = from; n; n = n->parent)
    {
        n->bytes = sub(n->bytes, bytes);
        n->logicalBytes = sub(n->logicalBytes, logical);
        n->fileCount = sub(n->fileCount, files);
        n->folderCount = sub(n->folderCount, folders);
    }
}

/// Sắp lại danh sách thư mục con của 'from' và mọi thư mục cha (dung lượng vừa giảm nên thứ tự có thể đổi).
void resortUp(FolderNode* from)
{
    for (FolderNode* n = from; n; n = n->parent)
    {
        std::stable_sort(n->children.begin(), n->children.end(), [](const auto& a, const auto& b) {
            return a->bytes != b->bytes ? a->bytes > b->bytes : a->name.compare(b->name, Qt::CaseInsensitive) < 0;
        });
    }
}

void applyStats(FolderTree& tree, qint64 bytes, qint64 logical, qint64 files, qint64 folders)
{
    tree.stats.bytes = sub(tree.stats.bytes, bytes);
    tree.stats.logicalBytes = sub(tree.stats.logicalBytes, logical);
    tree.stats.files = sub(tree.stats.files, files);
    tree.stats.folders = sub(tree.stats.folders, folders);
}

bool isInTree(const FolderTree& tree, const FolderNode* node)
{
    for (const FolderNode* n = node; n; n = n->parent)
        if (n == tree.root.get())
            return true;
    return false;
}
} // namespace

bool apply(FolderTree& tree, const Removal& r)
{
    if (!tree.root || !r.node || !isInTree(tree, r.node))
        return false;
    FolderNode* node = r.node;

    switch (r.target)
    {
        case Target::KeptFolder:
        {
            FolderNode* parent = node->parent;
            if (!parent)
                return false; // gốc quét không bị gỡ
            auto it = std::find_if(parent->children.begin(), parent->children.end(), [&](const auto& c) { return c.get() == node; });
            if (it == parent->children.end())
                return false;
            const qint64 bytes = node->bytes, logical = node->logicalBytes, files = node->fileCount, folders = 1 + node->folderCount;
            parent->children.erase(it); // giải phóng cả nhánh
            subtractUp(parent, bytes, logical, files, folders);
            applyStats(tree, bytes, logical, files, folders);
            resortUp(parent);
            return true;
        }
        case Target::BigFile:
        {
            auto it = std::find_if(node->bigFiles.begin(), node->bigFiles.end(),
                                   [&](const FolderFile& f) { return f.name.compare(r.fileName, Qt::CaseInsensitive) == 0; });
            if (it == node->bigFiles.end())
                return false;
            const qint64 bytes = it->bytes, logical = it->logicalBytes;
            node->bigFiles.erase(it);
            node->ownBytes = sub(node->ownBytes, bytes);
            node->ownLogicalBytes = sub(node->ownLogicalBytes, logical);
            node->ownFileCount = sub(node->ownFileCount, 1);
            subtractUp(node, bytes, logical, 1, 0);
            applyStats(tree, bytes, logical, 1, 0);
            resortUp(node);
            return true;
        }
        case Target::SmallFile:
        {
            // Phải còn ít nhất một tệp "nhỏ" trong thư mục này (tổng tệp trực tiếp > số tệp lớn đang giữ riêng).
            if (node->ownFileCount <= static_cast<qint64>(node->bigFiles.size()))
                return false;
            node->ownBytes = sub(node->ownBytes, r.bytes);
            node->ownLogicalBytes = sub(node->ownLogicalBytes, r.logicalBytes);
            node->ownFileCount = sub(node->ownFileCount, 1);
            subtractUp(node, r.bytes, r.logicalBytes, 1, 0);
            applyStats(tree, r.bytes, r.logicalBytes, 1, 0);
            resortUp(node);
            return true;
        }
        case Target::PrunedFolder:
        {
            if (node->prunedFolderCount <= 0)
                return false;
            node->prunedFolderCount -= 1;
            node->prunedBytes = sub(node->prunedBytes, r.bytes);
            subtractUp(node, r.bytes, r.logicalBytes, r.files, r.folders);
            applyStats(tree, r.bytes, r.logicalBytes, r.files, r.folders);
            resortUp(node);
            return true;
        }
        case Target::InsidePruned:
        {
            if (node->prunedFolderCount <= 0)
                return false;
            node->prunedBytes = sub(node->prunedBytes, r.bytes);
            subtractUp(node, r.bytes, r.logicalBytes, r.files, r.folders);
            applyStats(tree, r.bytes, r.logicalBytes, r.files, r.folders);
            resortUp(node);
            return true;
        }
    }
    return false;
}
} // namespace FolderTreeEdit
