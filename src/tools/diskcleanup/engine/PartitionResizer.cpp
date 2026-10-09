#include "PartitionResizer.h"

#include "PartitionManager.h"

PartitionResizer::PartitionResizer(QObject* parent)
    : QThread(parent)
{
}

void PartitionResizer::setTarget(int diskNumber, int partitionNumber, qint64 newSizeBytes, qint64 sizeAtQueryBytes)
{
    m_diskNumber = diskNumber;
    m_partitionNumber = partitionNumber;
    m_newSizeBytes = newSizeBytes;
    m_sizeAtQueryBytes = sizeAtQueryBytes;
}

void PartitionResizer::run()
{
    if (!PartitionManager::isElevated())
    {
        emit resizeFinished(false, "Cần quyền Administrator để đổi kích thước phân vùng.");
        return;
    }

    // Đọc lại từ Windows NGAY trước khi chạy (2 lệnh chỉ đọc) rồi đối chiếu - xem ghi chú ở header.
    QString listError;
    const auto partitions = PartitionManager::listPartitions(&listError);
    if (!listError.isEmpty() || partitions.isEmpty())
    {
        emit resizeFinished(false, "Không đọc lại được danh sách phân vùng để xác minh trước khi đổi kích thước" +
                                       (listError.isEmpty() ? QString(".") : (": " + listError)));
        return;
    }
    QString rangeError;
    const auto range = PartitionManager::querySupportedSize(m_diskNumber, m_partitionNumber, &rangeError);
    const QString blockReason = PartitionManager::internal::resizeBlockReason(
        m_diskNumber, m_partitionNumber, m_sizeAtQueryBytes, partitions, range, m_newSizeBytes);
    if (!blockReason.isEmpty())
    {
        emit resizeFinished(false, blockReason + (rangeError.isEmpty() ? QString() : (" (" + rangeError + ")")));
        return;
    }

    QString error;
    const bool ok = PartitionManager::resizePartition(m_diskNumber, m_partitionNumber, m_newSizeBytes, &error);
    emit resizeFinished(ok, error);
}
