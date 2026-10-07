#include "PartitionResizer.h"

#include "PartitionManager.h"

PartitionResizer::PartitionResizer(QObject* parent)
    : QThread(parent)
{
}

void PartitionResizer::setTarget(int diskNumber, int partitionNumber, qint64 newSizeBytes)
{
    m_diskNumber = diskNumber;
    m_partitionNumber = partitionNumber;
    m_newSizeBytes = newSizeBytes;
}

void PartitionResizer::run()
{
    QString error;
    const bool ok = PartitionManager::resizePartition(m_diskNumber, m_partitionNumber, m_newSizeBytes, &error);
    emit resizeFinished(ok, error);
}
