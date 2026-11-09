#pragma once

#include "engine/ProcessMonitor.h"

#include <QAbstractTableModel>
#include <QDateTime>
#include <QString>

#include <vector>

/// Bảng tiến trình cho tab "Tiến trình & RAM". Hai chế độ: mỗi tiến trình một dòng, hoặc GỘP THEO TÊN (như Task Manager
/// gộp các tiến trình cùng tên - cộng CPU/RAM/luồng/handle). Cập nhật TẠI CHỖ khi tập tiến trình không đổi (không mất
/// lựa chọn/vị trí cuộn); khác thì dựng lại và báo qua giá trị trả về của update().
class ProcessTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    enum Column { NameColumn = 0, PidColumn, CpuColumn, RamColumn, CommitColumn, ThreadsColumn, HandlesColumn, IoColumn, UserColumn, PathColumn, ColumnCount };
    enum Role
    {
        SortRole = Qt::UserRole + 1, // giá trị để sắp xếp (số hoặc chuỗi chữ thường)
        PidRole,                     // PID (0 cho dòng gộp có nhiều tiến trình)
        PathRole
    };

    struct Row
    {
        QString key;   // khóa nhận diện dòng giữa các lần cập nhật
        quint32 pid{0};
        int count{1};  // số tiến trình trong dòng (chế độ gộp)
        QString name;
        QString path;
        QString user;
        quint32 parentPid{0};
        quint32 sessionId{0};
        QDateTime start;
        double cpu{0};
        qint64 ram{0};     // RAM riêng (private working set; không có thì working set)
        qint64 workingSet{0};
        qint64 commit{0};
        quint32 threads{0};
        quint32 handles{0};
        qint64 io{0};      // byte/giây
    };

    explicit ProcessTableModel(QObject* parent = nullptr);

    /// Nạp mẫu mới. true = phải dựng lại toàn bộ (tập dòng đổi hoặc đổi chế độ gộp); false = cập nhật tại chỗ.
    bool update(const SystemSnapshot& snapshot, bool grouped);

    bool grouped() const { return m_grouped; }
    const Row* rowAt(int row) const { return row >= 0 && row < static_cast<int>(m_rows.size()) ? &m_rows[static_cast<size_t>(row)] : nullptr; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// "12,3 %" / "0 %" - dùng chung cho hiển thị.
    static QString formatCpu(double percent);
    static QString formatRate(qint64 bytesPerSec);

private:
    std::vector<Row> m_rows;
    bool m_grouped{false};
    qint64 m_totalPhysical{0};
};
