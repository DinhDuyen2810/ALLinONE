#pragma once

#include "engine/FolderTreeEdit.h"
#include "engine/FolderTreeScanner.h"

#include <QAbstractItemModel>
#include <QIcon>

#include <atomic>
#include <memory>
#include <vector>

/// Mô hình cây cho tab "Cây thư mục": mỗi thư mục -> thư mục con giữ lại + tệp lớn + dòng gộp ("N tệp nhỏ",
/// "N thư mục nhỏ"), luôn sắp GIẢM DẦN theo dung lượng chiếm trên đĩa. Các nhánh chỉ được dựng khi mở ra (lười) nên
/// cây hàng trăm nghìn thư mục vẫn mở tức thì.
///
/// Mở một dòng gộp "[N tệp nhỏ]"/"[N thư mục nhỏ]" sẽ ĐỌC TRỰC TIẾP thư mục đó trên luồng nền (FolderTreeScanner::listFolderLive)
/// và hiện từng tệp/thư mục riêng (dòng "⏳ Đang đọc..." trong lúc chờ) - nên các mục này phản ánh đĩa HIỆN TẠI, có thể
/// khác chút so với số lúc quét. Mô hình không bao giờ ghi/xóa đĩa; sau khi tab xóa xong, applyRemovals() chỉ sửa lại cây đã quét.
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
    /// Folder/File: từ cây đã quét. SmallFiles/SmallFolders: dòng gộp. LiveFile/LiveFolder: mục đọc trực tiếp bên trong dòng
    /// gộp (LiveFolder mở tiếp được). LiveMore: dòng ghi chú "còn N mục nhỏ hơn không liệt kê". Loading: đang đọc.
    enum class Kind { Folder = 0, File, SmallFiles, SmallFolders, LiveFile, LiveFolder, LiveMore, Loading };

    explicit FolderTreeModel(QObject* parent = nullptr);
    ~FolderTreeModel() override;

    void setTree(std::shared_ptr<FolderTree> tree);
    void clear() { setTree(nullptr); }
    std::shared_ptr<FolderTree> tree() const { return m_tree; }

    Kind kindAt(const QModelIndex& index) const;
    QString pathAt(const QModelIndex& index) const;
    qint64 bytesAt(const QModelIndex& index) const;

    /// Mục này là thư mục (cả thư mục trong cây lẫn thư mục đọc trực tiếp)?
    bool isFolderAt(const QModelIndex& index) const;
    /// Mục này là chính thư mục gốc đã quét?
    bool isScanRoot(const QModelIndex& index) const;
    /// Có thể đưa vào thao tác xóa không (thư mục/tệp thật, không phải gốc quét, không phải dòng gộp/ghi chú).
    bool isDeletable(const QModelIndex& index) const;
    /// Mô tả mục này cho FolderTreeEdit::apply(); false nếu không xóa được (xem isDeletable).
    bool removalFor(const QModelIndex& index, FolderTreeEdit::Removal* out) const;
    /// Gỡ các mục ĐÃ XÓA THẬT khỏi cây đã quét (sửa số liệu mọi thư mục cha) rồi dựng lại mô hình. Trả về số mục áp được.
    /// Mọi chỉ mục cũ KHÔNG còn hợp lệ sau lệnh này.
    int applyRemovals(const std::vector<FolderTreeEdit::Removal>& removals);

    /// Chỉ mục của thư mục (thuộc cây đã quét, được giữ riêng) có đường dẫn này; {} nếu không có. Mở các nhánh cha khi cần.
    QModelIndex indexForFolderPath(const QString& path) const;
    /// Dòng gộp (SmallFiles/SmallFolders) bên dưới một thư mục; {} nếu thư mục đó không có.
    QModelIndex aggregateIndex(const QModelIndex& folderIndex, Kind kind) const;
    /// Số lượt đọc trực tiếp thư mục đang chạy nền.
    int liveLoadsInFlight() const { return m_inflight ? m_inflight->load() : 0; }

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
        const FolderNode* node{nullptr}; // Folder: chính nó; các loại khác: thư mục chứa (mục "Live": thư mục có dòng gộp chứa chúng)
        const FolderFile* file{nullptr};
        QString name;            // LiveFile/LiveFolder: tên; LiveMore: nội dung ghi chú
        bool unreadable{false};  // LiveFolder không đọc được
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
    QString pathOf(const Item* item) const;
    QString containerPathOf(const Item* item) const; // đường dẫn thư mục mà các con của 'item' nằm trong
    void rebuildRoot();
    void cancelLiveLoads();
    void startLiveLoad(Item* item) const;
    void finishLiveLoad(quint64 generation, Item* item, std::shared_ptr<FolderLiveListing> listing,
                        std::vector<QString> exclude);

    std::shared_ptr<FolderTree> m_tree;
    mutable std::unique_ptr<Item> m_root; // nút ẩn, chỉ có một con: thư mục gốc đã quét
    // Đọc trực tiếp thư mục chạy trên QThreadPool: kết quả chỉ nhận nếu cùng "thế hệ" (mỗi lần dựng lại mô hình tăng một bậc).
    quint64 m_generation{0};
    mutable std::shared_ptr<std::atomic_bool> m_stopLive{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<std::atomic<int>> m_inflight{std::make_shared<std::atomic<int>>(0)};
    QIcon m_folderIcon;
    QIcon m_fileIcon;
    QIcon m_smallIcon;
};
