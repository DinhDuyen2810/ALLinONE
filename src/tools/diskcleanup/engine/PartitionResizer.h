#pragma once

#include <QThread>

/// Thực hiện Resize-Partition thật trên QThread riêng - co giãn phân vùng lớn có thể mất tới vài
/// phút (Windows di chuyển dữ liệu), không được chặn UI trong lúc đó. Chỉ gọi sau khi người dùng đã
/// xác nhận rõ ràng (xem PartitionTab) - đây là thao tác đĩa thật, không có "hoàn tác".
///
/// Ngay TRƯỚC khi chạy lệnh thật, luồng này tự đọc lại từ Windows danh sách phân vùng + khoảng kích
/// thước cho phép và đối chiếu với thứ người dùng đã xác nhận (PartitionManager::internal::
/// resizeBlockReason) - mọi sai khác đều dừng lại, KHÔNG đụng tới đĩa.
class PartitionResizer : public QThread
{
    Q_OBJECT

public:
    explicit PartitionResizer(QObject* parent = nullptr);

    /// 'sizeAtQueryBytes' = kích thước của phân vùng lúc người dùng tra khoảng cho phép và xác nhận -
    /// dùng để phát hiện số đĩa/số phân vùng đã trỏ sang một phân vùng khác kể từ lúc đó.
    void setTarget(int diskNumber, int partitionNumber, qint64 newSizeBytes, qint64 sizeAtQueryBytes);

signals:
    void resizeFinished(bool success, QString error);

protected:
    void run() override;

private:
    int m_diskNumber{-1};
    int m_partitionNumber{-1};
    qint64 m_newSizeBytes{0};
    qint64 m_sizeAtQueryBytes{-1};
};
