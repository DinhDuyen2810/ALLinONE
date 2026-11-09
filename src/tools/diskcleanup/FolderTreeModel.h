#pragma once

#include "engine/FolderTreeScanner.h"

#include <QAbstractItemModel>
#include <QIcon>

#include <memory>
#include <vector>

/// Mô hình cây cho tab "Cây thư mục": mỗi thư mục -> thư mục con giữ lại + tệp lớn + dòng gộp ("N tệp nhỏ",
/// "N thư mục nhỏ"), luôn sắp GIẢM DẦN theo dung lượng chiếm trên đĩa. Các nhánh chỉ được dựng khi mở ra (lười) nên
/// cây hàng trăm nghìn thư mục vẫn mở tức thì. Mô hình chỉ ĐỌC dữ liệu đã quét - không đụng tới đĩa.
class FolderTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Column { NameColumn = 0, SizeColumn, ShareColumn, FilesColumn, FoldersColumn, ColumnCount };
    enum Role
    {
        ShareRole = Qt::UserRole + 1, // 0..1: tỉ lệ so với thư mục cha (gốc = 1)
        KindRole,                     // Kind
        PathRole,                     // đường dẫn thật (dòng gộp: thư mục chứa chúng)
        BytesRole                     // chiếm trên đĩa (qint64)
    };
    enum class Kind { Folder = 0, File, SmallFiles, SmallFolders };

    explicit FolderTreeModel(QObject* parent = nullptr);
    ~FolderTreeModel() override;

    void setTree(std::shared_ptr<FolderTree> tree);
    void clear() { setTree(nullptr); }
    std::shared_ptr<FolderTree> tree() const { return m_tree; }

    Kind kindAt(const QModelIndex& index) const;
    QString pathAt(const QModelIndex& index) const;
    qint64 bytesAt(const QModelIndex& index) const;

    int columnCount(const QModelIndex& parent = {}) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    bool hasChildren(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    struct Item
    {
        Item* parent{nullptr};
        int row{0};
        Kind kind{Kind::Folder};
        const FolderNode* node{nullptr}; // Folder: chính nó; các loại khác: thư mục chứa
        const FolderFile* file{nullptr};
        qint64 bytes{0};
        qint64 logicalBytes{0};
        qint64 files{-1};   // -1 = không áp dụng
        qint64 folders{-1}; // -1 = không áp dụng
        qint64 smallCount{0};
        bool loaded{false};
        std::vector<std::unique_ptr<Item>> kids;
    };

    static Item* itemOf(const QModelIndex& index) { return static_cast<Item*>(index.internalPointer()); }
    Item* itemFor(const QModelIndex& parent) const;
    void ensureLoaded(Item* item) const;
    static bool hasKids(const Item* item);
    QString nameOf(const Item* item) const;
    double shareOf(const Item* item) const;

    std::shared_ptr<FolderTree> m_tree;
    mutable std::unique_ptr<Item> m_root; // nút ẩn, chỉ có một con: thư mục gốc đã quét
    QIcon m_folderIcon;
    QIcon m_fileIcon;
    QIcon m_smallIcon;
};
