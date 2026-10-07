#pragma once

#include <QThread>

/// Thực hiện Resize-Partition thật trên QThread riêng - co giãn phân vùng lớn có thể mất tới vài
/// phút (Windows di chuyển dữ liệu), không được chặn UI trong lúc đó. Chỉ gọi sau khi người dùng đã
/// xác nhận rõ ràng (xem PartitionTab) - đây là thao tác đĩa thật, không có "hoàn tác".
class PartitionResizer : public QThread
{
    Q_OBJECT

public:
    explicit PartitionResizer(QObject* parent = nullptr);

    void setTarget(int diskNumber, int partitionNumber, qint64 newSizeBytes);

signals:
    void resizeFinished(bool success, QString error);

protected:
    void run() override;

private:
    int m_diskNumber{-1};
    int m_partitionNumber{-1};
    qint64 m_newSizeBytes{0};
};
