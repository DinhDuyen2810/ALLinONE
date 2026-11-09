#pragma once

#include "FolderTreeScanner.h"

#include <QString>

/**
 * @brief Lõi THUẦN (không đụng đĩa, không widget) cập nhật cây thư mục ĐÃ QUÉT sau khi người dùng xóa một mục: gỡ nút/tệp
 * khỏi cây và trừ dung lượng/số tệp/số thư mục khỏi mọi thư mục cha, nhờ đó không phải quét lại cả ổ chỉ để thấy kết
 * quả. Tách riêng để test được: test dựng cây thật, xóa thật trong thư mục tạm, rồi so cây đã sửa với một lượt quét lại.
 *
 * Cây chỉ GIỮ riêng các nhánh/tệp lớn (xem FolderTreeScanner) - phần còn lại được gộp vào số tổng của thư mục chứa chúng
 * (tệp nhỏ: own*; thư mục nhỏ: prunedFolderCount/prunedBytes). Vì thế có 5 kiểu mục bị xóa, mỗi kiểu sửa số liệu một cách riêng.
 */
namespace FolderTreeEdit
{
enum class Target
{
    KeptFolder,   // một thư mục con ĐƯỢC GIỮ trong cây (FolderNode::children) - gỡ cả nhánh
    BigFile,      // một tệp lớn được giữ riêng (FolderNode::bigFiles) - nhận diện theo tên
    SmallFile,    // một tệp nhỏ KHÔNG được giữ riêng, nằm trực tiếp trong 'node'
    PrunedFolder, // một thư mục con nhỏ đã bị gộp, nằm trực tiếp trong 'node'
    InsidePruned  // một tệp/thư mục nằm SÂU BÊN TRONG một thư mục con nhỏ đã bị gộp của 'node'
};

struct Removal
{
    Target target{Target::KeptFolder};
    /// KeptFolder: chính thư mục bị xóa (không được là gốc). Các kiểu khác: thư mục trong cây mà mục bị xóa thuộc về
    /// (SmallFile/BigFile: thư mục chứa trực tiếp; PrunedFolder/InsidePruned: thư mục có dòng gộp "thư mục nhỏ" chứa nó).
    FolderNode* node{nullptr};
    QString fileName; // chỉ BigFile
    /// Số liệu của mục bị xóa. KeptFolder và BigFile lấy từ chính cây (bỏ qua các trường này); các kiểu còn lại dùng.
    qint64 bytes{0};        // chiếm trên đĩa
    qint64 logicalBytes{0}; // kích thước tệp
    qint64 files{0};        // số tệp bị xóa (tệp: 1; thư mục: số tệp bên trong)
    qint64 folders{0};      // số THƯ MỤC bị xóa, kể cả chính nó nếu là thư mục (tệp: 0)
};

/// Áp một lần xóa vào cây. false nếu dữ liệu không khớp (BigFile không tìm thấy, KeptFolder là gốc/không thuộc cây...)
/// - khi đó cây KHÔNG bị sửa. Thành công thì cũng cập nhật tree.stats và sắp lại danh sách con của các thư mục cha.
bool apply(FolderTree& tree, const Removal& removal);
} // namespace FolderTreeEdit
