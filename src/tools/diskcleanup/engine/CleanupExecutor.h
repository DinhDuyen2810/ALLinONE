#pragma once

#include <QStringList>
#include <QThread>

/**
 * @brief Thực hiện xóa các mục đã chọn, trên QThread riêng (có thể mất thời gian với thư mục lớn).
 * Mặc định chuyển vào Thùng rác (có thể khôi phục) qua RecycleBinOps - xóa vĩnh viễn chỉ khi người
 * dùng chủ động bật tùy chọn đó.
 */
class CleanupExecutor : public QThread
{
    Q_OBJECT

public:
    explicit CleanupExecutor(QObject* parent = nullptr);

    void setItems(const QStringList& paths, qint64 totalBytes);
    void setPermanentDelete(bool permanent) { m_permanent = permanent; }

signals:
    // Đặt tên khác "finished" vì QThread đã có sẵn signal finished() không tham số - trùng tên sẽ
    // gây xung đột overload.
    void executionFinished(bool success, QString error, qint64 freedBytes, int deletedCount);

protected:
    void run() override;

private:
    QStringList m_paths;
    qint64 m_totalBytes{0};
    bool m_permanent{false};
};
