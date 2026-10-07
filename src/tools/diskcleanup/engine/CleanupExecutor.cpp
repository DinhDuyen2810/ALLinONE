#include "CleanupExecutor.h"

#include "RecycleBinOps.h"

CleanupExecutor::CleanupExecutor(QObject* parent)
    : QThread(parent)
{
}

void CleanupExecutor::setItems(const QStringList& paths, qint64 totalBytes)
{
    m_paths = paths;
    m_totalBytes = totalBytes;
}

void CleanupExecutor::run()
{
    if (m_paths.isEmpty())
    {
        emit executionFinished(true, QString(), 0, 0);
        return;
    }

    QString error;
    const bool ok = m_permanent ? RecycleBinOps::permanentlyDelete(m_paths, &error)
                                : RecycleBinOps::moveToRecycleBin(m_paths, &error);

    // SHFileOperationW xử lý theo lô: nếu thất bại, một số mục có thể vẫn đã bị xóa trước khi lỗi xảy
    // ra. Không có cách đáng tin cậy để biết CHÍNH XÁC mục nào đã xóa khi thất bại giữa chừng, nên báo
    // cáo "đã giải phóng toàn bộ" khi thành công, và 0 khi thất bại (an toàn hơn là báo sai số liệu).
    emit executionFinished(ok, error, ok ? m_totalBytes : 0, ok ? m_paths.size() : 0);
}
