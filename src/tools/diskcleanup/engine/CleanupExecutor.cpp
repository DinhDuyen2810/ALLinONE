#include "CleanupExecutor.h"

#include "FsSafety.h"
#include "RecycleBinOps.h"

#include <QDir>
#include <QSet>

CleanupExecutor::CleanupExecutor(QObject* parent)
    : QThread(parent)
{
}

void CleanupExecutor::setItems(const QStringList& paths, const QList<qint64>& sizes)
{
    m_paths = paths;
    m_sizes = sizes;
}

void CleanupExecutor::run()
{
    if (m_paths.isEmpty())
    {
        emit executionFinished(true, QString(), 0, 0);
        return;
    }

    // Bước 1: lọc danh sách trước khi đụng tới đĩa.
    //  - Mục ĐÃ TỰ BIẾN MẤT từ lúc quét tới giờ (rất thường gặp với tệp tạm/cache - Windows hoặc ứng dụng
    //    khác liên tục tạo/xóa chúng) coi là ĐÃ ĐẠT MỤC TIÊU, không đưa vào yêu cầu xóa (và không gây
    //    SHFileOperationW báo lỗi ERROR_FILE_NOT_FOUND cho CẢ LÔ chỉ vì 1 mục không còn đó). Kiểm tra bằng
    //    FsSafety (không đi theo liên kết): QFileInfo::exists() trả về false cho một lối tắt .lnk CÒN ĐÓ
    //    nhưng đích đã mất - lối tắt hỏng trong "Tệp gần đây" từng được tính "đã giải phóng" mà không hề xóa.
    //  - Đường dẫn LẶP (hai hạng mục cùng trỏ vào một thư mục, vd "Tệp tạm hệ thống" và "Tệp nhật ký" đều
    //    quét %WINDIR%\Temp) chỉ xóa/tính MỘT lần.
    //  - Đường dẫn rỗng/tương đối/gốc ổ đĩa, đường dẫn mà Windows hiểu thành một đối tượng khác (tên có dấu
    //    chấm/khoảng trắng cuối...) và junction/symlink THƯ MỤC không bao giờ được xóa từ đây.
    //  - Chế độ Thùng rác: mục KHÔNG vào Thùng rác được (quá lớn, Thùng rác tắt, ổ không có Thùng rác)
    //    được GIỮ NGUYÊN - Shell sẽ hủy hẳn chúng nếu cứ đưa vào (xem RecycleBinOps::moveToRecycleBin).
    QStringList toDelete;
    QList<qint64> toDeleteSizes;
    QSet<QString> seen;
    qint64 freedBytes = 0;
    int freedCount = 0;
    int refusedCount = 0;
    int notRecyclableCount = 0;
    QString notRecyclableExample;
    for (int i = 0; i < m_paths.size(); ++i)
    {
        const QString path = QDir::cleanPath(m_paths[i]);
        const QString key = path.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);

        const qint64 size = i < m_sizes.size() ? m_sizes[i] : 0;
        // unsafeShellPathReason (không chỉ unsafeCleanupRootReason): còn chặn cả tên mà Shell hiểu thành
        // một tệp KHÁC - vd "thư mục/tệp." (dấu chấm cuối) làm Shell xóa "thư mục/tệp".
        if (!FsSafety::unsafeShellPathReason(path).isEmpty())
        {
            ++refusedCount;
            continue;
        }
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists)
        {
            freedBytes += size;
            ++freedCount;
            continue;
        }
        if (info.isDir && info.isLink)
        {
            ++refusedCount;
            continue;
        }
        if (!m_permanent)
        {
            // Tệp có thể đã LỚN LÊN từ lúc quét - so với kích thước hiện tại, không chỉ kích thước cũ.
            const QString reason = RecycleBinOps::notRecyclableReason(path, info.isDir ? size : qMax(size, info.sizeBytes));
            if (!reason.isEmpty())
            {
                ++notRecyclableCount;
                if (notRecyclableExample.isEmpty())
                    notRecyclableExample = QDir::toNativeSeparators(path) + " - " + reason;
                continue;
            }
        }
        toDelete << path;
        toDeleteSizes << size;
    }

    QString error;
    bool ok = true;
    if (!toDelete.isEmpty())
        ok = m_permanent ? RecycleBinOps::permanentlyDelete(toDelete, &error) : RecycleBinOps::moveToRecycleBin(toDelete, &error);

    // Bước 2: SHFileOperationW không cho biết CHÍNH XÁC mục nào thất bại khi có lỗi giữa chừng (có thể
    // đã xóa được phần lớn trước khi gặp 1 mục lỗi) - kiểm tra lại THẬT xem mục nào không còn tồn tại
    // nữa (đã xóa thành công) thay vì đoán mò theo mã trả về, để báo cáo số liệu chính xác dù thất bại
    // một phần.
    int failedCount = 0;
    for (int i = 0; i < toDelete.size(); ++i)
    {
        if (!FsSafety::existsNoFollow(toDelete[i]))
        {
            freedBytes += toDeleteSizes[i];
            ++freedCount;
        }
        else
        {
            ++failedCount;
        }
    }

    // Thành công = có giải phóng được gì đó. Chỉ coi là thất bại thật sự khi KHÔNG có mục nào được giải
    // phóng dù có mục cần xóa - tránh dọa người dùng bằng hộp thoại lỗi đỏ cho một trường hợp thường gặp
    // và vô hại (vài tệp tạm tự mất giữa lúc quét và lúc xóa).
    const int leftCount = failedCount + refusedCount + notRecyclableCount;
    const bool success = freedCount > 0 || leftCount == 0;
    QStringList notes;
    if (failedCount > 0)
    {
        // Luôn kèm SỐ mục: một mục lỗi không còn chặn các mục sau nó (xem RecycleBinOps), nên thông báo
        // lỗi của Shell chỉ nói về mục lỗi ĐẦU TIÊN.
        QString note = QString("%1 mục không xóa được (có thể đang được chương trình khác sử dụng).").arg(failedCount);
        if (!ok && !error.isEmpty())
            note += " Lỗi đầu tiên: " + error;
        notes << note;
    }
    if (notRecyclableCount > 0)
    {
        notes << QString("%1 mục được GIỮ NGUYÊN vì không thể đưa vào Thùng rác để khôi phục (vd: %2). Bật "
                         "\"Xóa vĩnh viễn\" nếu thật sự muốn xóa các mục này.")
                     .arg(notRecyclableCount)
                     .arg(notRecyclableExample);
    }
    if (refusedCount > 0)
        notes << QString("%1 mục bị bỏ qua vì lý do an toàn (liên kết thư mục hoặc đường dẫn không hợp lệ).").arg(refusedCount);

    emit executionFinished(success, notes.join("\n"), freedBytes, freedCount);
}
