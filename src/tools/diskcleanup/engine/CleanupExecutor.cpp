#include "CleanupExecutor.h"

#include "RecycleBinOps.h"

#include <QFileInfo>

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

    // Bước 1: những mục ĐÃ TỰ BIẾN MẤT từ lúc quét tới giờ (rất thường gặp với tệp tạm/cache - Windows
    // hoặc ứng dụng khác liên tục tạo/xóa chúng) coi là ĐÃ ĐẠT MỤC TIÊU, không đưa vào yêu cầu xóa (và
    // không gây SHFileOperationW báo lỗi ERROR_FILE_NOT_FOUND cho CẢ LÔ chỉ vì 1 mục không còn đó).
    QStringList toDelete;
    QList<qint64> toDeleteSizes;
    qint64 freedBytes = 0;
    int freedCount = 0;
    for (int i = 0; i < m_paths.size(); ++i)
    {
        const qint64 size = i < m_sizes.size() ? m_sizes[i] : 0;
        if (QFileInfo::exists(m_paths[i]))
        {
            toDelete << m_paths[i];
            toDeleteSizes << size;
        }
        else
        {
            freedBytes += size;
            ++freedCount;
        }
    }

    if (toDelete.isEmpty())
    {
        // Mọi mục đều đã tự mất trước khi xóa - vẫn là thành công, không có gì để báo lỗi.
        emit executionFinished(true, QString(), freedBytes, freedCount);
        return;
    }

    QString error;
    const bool ok = m_permanent ? RecycleBinOps::permanentlyDelete(toDelete, &error)
                                : RecycleBinOps::moveToRecycleBin(toDelete, &error);

    // Bước 2: SHFileOperationW không cho biết CHÍNH XÁC mục nào thất bại khi có lỗi giữa chừng (có thể
    // đã xóa được phần lớn trước khi gặp 1 mục lỗi) - kiểm tra lại THẬT xem mục nào không còn tồn tại
    // nữa (đã xóa thành công) thay vì đoán mò theo mã trả về, để báo cáo số liệu chính xác dù thất bại
    // một phần.
    int failedCount = 0;
    for (int i = 0; i < toDelete.size(); ++i)
    {
        if (!QFileInfo::exists(toDelete[i]))
        {
            freedBytes += toDeleteSizes[i];
            ++freedCount;
        }
        else
        {
            ++failedCount;
        }
    }

    // Thành công = có giải phóng được gì đó (hoặc không có gì cần xóa). Chỉ coi là thất bại thật sự
    // khi KHÔNG có mục nào được giải phóng dù có mục cần xóa - tránh dọa người dùng bằng hộp thoại lỗi
    // đỏ cho một trường hợp thường gặp và vô hại (vài tệp tạm tự mất giữa lúc quét và lúc xóa).
    const bool success = freedCount > 0 || toDelete.isEmpty();
    QString finalNote;
    if (failedCount > 0)
    {
        finalNote = !ok && !error.isEmpty()
                        ? error
                        : QString("%1 mục không xóa được (có thể đang được chương trình khác sử dụng).").arg(failedCount);
    }

    emit executionFinished(success, finalNote, freedBytes, freedCount);
}
