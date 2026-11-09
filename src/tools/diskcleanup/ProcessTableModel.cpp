#include "ProcessTableModel.h"

#include "DiskUiStyle.h"

#include <QBrush>
#include <QColor>
#include <QHash>
#include <QLocale>

ProcessTableModel::ProcessTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int ProcessTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int ProcessTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

bool ProcessTableModel::update(const SystemSnapshot& snapshot, bool grouped)
{
    std::vector<Row> next;
    next.reserve(static_cast<size_t>(snapshot.processes.size()));
    if (!grouped)
    {
        for (const ProcessInfo& p : snapshot.processes)
        {
            Row r;
            r.key = QString::number(p.pid) + QLatin1Char(':') + QString::number(p.startTime.toMSecsSinceEpoch());
            r.pid = p.pid;
            r.name = p.name;
            r.path = p.path;
            r.user = p.user;
            r.parentPid = p.parentPid;
            r.sessionId = p.sessionId;
            r.start = p.startTime;
            r.cpu = p.cpuPercent;
            r.ram = p.privateWorkingSetBytes > 0 ? p.privateWorkingSetBytes : p.workingSetBytes;
            r.workingSet = p.workingSetBytes;
            r.commit = p.privateBytes;
            r.threads = p.threads;
            r.handles = p.handles;
            r.io = p.ioBytesPerSec;
            next.push_back(std::move(r));
        }
    }
    else
    {
        QHash<QString, int> index; // tên chữ thường -> vị trí trong next (giữ thứ tự xuất hiện đầu tiên)
        for (const ProcessInfo& p : snapshot.processes)
        {
            const QString key = p.name.toLower();
            const auto it = index.constFind(key);
            if (it == index.constEnd())
            {
                Row r;
                r.key = key;
                r.pid = p.pid;
                r.count = 1;
                r.name = p.name;
                r.path = p.path;
                r.user = p.user;
                r.parentPid = p.parentPid;
                r.sessionId = p.sessionId;
                r.start = p.startTime;
                r.cpu = p.cpuPercent;
                r.ram = p.privateWorkingSetBytes > 0 ? p.privateWorkingSetBytes : p.workingSetBytes;
                r.workingSet = p.workingSetBytes;
                r.commit = p.privateBytes;
                r.threads = p.threads;
                r.handles = p.handles;
                r.io = p.ioBytesPerSec;
                index.insert(key, static_cast<int>(next.size()));
                next.push_back(std::move(r));
            }
            else
            {
                Row& r = next[static_cast<size_t>(it.value())];
                ++r.count;
                r.pid = 0; // nhiều tiến trình -> không còn một PID duy nhất
                r.cpu += p.cpuPercent;
                r.ram += p.privateWorkingSetBytes > 0 ? p.privateWorkingSetBytes : p.workingSetBytes;
                r.workingSet += p.workingSetBytes;
                r.commit += p.privateBytes;
                r.threads += p.threads;
                r.handles += p.handles;
                r.io += p.ioBytesPerSec;
                if (r.user != p.user)
                    r.user = QStringLiteral("(nhiều người dùng)");
                if (p.startTime.isValid() && (!r.start.isValid() || p.startTime < r.start))
                    r.start = p.startTime;
            }
        }
    }
    m_totalPhysical = snapshot.memory.totalPhysical;

    bool same = grouped == m_grouped && next.size() == m_rows.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].key == m_rows[i].key;

    if (same)
    {
        m_rows = std::move(next);
        if (!m_rows.empty())
            emit dataChanged(index(0, 0), index(static_cast<int>(m_rows.size()) - 1, ColumnCount - 1));
        return false;
    }
    beginResetModel();
    m_rows = std::move(next);
    m_grouped = grouped;
    endResetModel();
    return true;
}

QString ProcessTableModel::formatCpu(double percent)
{
    return percent < 0.05 ? QStringLiteral("0 %") : QStringLiteral("%1 %").arg(QLocale().toString(percent, 'f', 1));
}

QString ProcessTableModel::formatRate(qint64 bytesPerSec)
{
    return bytesPerSec < 1024 ? QStringLiteral("0") : DiskUi::formatBytes(bytesPerSec) + QStringLiteral("/s");
}

QVariant ProcessTableModel::data(const QModelIndex& index, int role) const
{
    const Row* r = index.isValid() ? rowAt(index.row()) : nullptr;
    if (!r)
        return {};
    const bool group = r->count > 1;
    switch (role)
    {
        case Qt::DisplayRole:
            switch (index.column())
            {
                case NameColumn: return group ? QStringLiteral("%1  (×%2)").arg(r->name).arg(r->count) : r->name;
                case PidColumn: return group ? QString() : QString::number(r->pid); // chuỗi thuần: không dấu phân cách hàng nghìn
                case CpuColumn: return formatCpu(r->cpu);
                case RamColumn: return DiskUi::formatBytes(r->ram);
                case CommitColumn: return DiskUi::formatBytes(r->commit);
                case ThreadsColumn: return QLocale().toString(r->threads);
                case HandlesColumn: return QLocale().toString(r->handles);
                case IoColumn: return formatRate(r->io);
                case UserColumn: return r->user;
                case PathColumn: return r->path;
            }
            return {};
        case SortRole:
            switch (index.column())
            {
                case NameColumn: return r->name.toLower();
                case PidColumn: return static_cast<qint64>(r->pid);
                case CpuColumn: return r->cpu;
                case RamColumn: return r->ram;
                case CommitColumn: return r->commit;
                case ThreadsColumn: return static_cast<qint64>(r->threads);
                case HandlesColumn: return static_cast<qint64>(r->handles);
                case IoColumn: return r->io;
                case UserColumn: return r->user.toLower();
                case PathColumn: return r->path.toLower();
            }
            return {};
        case PidRole:
            return r->pid;
        case PathRole:
            return r->path;
        case Qt::TextAlignmentRole:
            switch (index.column())
            {
                case NameColumn:
                case UserColumn:
                case PathColumn: return int(Qt::AlignLeft | Qt::AlignVCenter);
                default: return int(Qt::AlignRight | Qt::AlignVCenter);
            }
        case Qt::BackgroundRole:
        {
            // Bản đồ nhiệt nhẹ như Task Manager: ô CPU/RAM đậm dần khi tiến trình dùng nhiều.
            if (r->pid == 0 && r->count == 1)
                return {}; // System Idle Process: "CPU" của nó là phần CPU RẢNH, không tô màu
            if (index.column() == CpuColumn)
            {
                if (r->cpu >= 50) return QBrush(QColor(207, 34, 46, 60));
                if (r->cpu >= 20) return QBrush(QColor(191, 135, 0, 60));
                if (r->cpu >= 5) return QBrush(QColor(191, 135, 0, 28));
            }
            if (index.column() == RamColumn && m_totalPhysical > 0)
            {
                const double share = static_cast<double>(r->ram) / static_cast<double>(m_totalPhysical);
                if (share >= 0.15) return QBrush(QColor(207, 34, 46, 60));
                if (share >= 0.05) return QBrush(QColor(191, 135, 0, 60));
                if (share >= 0.02) return QBrush(QColor(191, 135, 0, 28));
            }
            return {};
        }
        case Qt::ForegroundRole:
            if (r->pid == 0 && r->count == 1)
                return QBrush(QColor("#8c959f"));
            return {};
        case Qt::ToolTipRole:
            switch (index.column())
            {
                case NameColumn:
                {
                    QString tip = r->name;
                    if (group)
                        return tip + QStringLiteral("\n%1 tiến trình cùng tên được gộp thành một dòng").arg(r->count);
                    tip += QStringLiteral("\nPID %1 · tiến trình cha: PID %2 · phiên %3").arg(r->pid).arg(r->parentPid).arg(r->sessionId);
                    if (r->start.isValid())
                        tip += QStringLiteral("\nBắt đầu: ") + r->start.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    if (!r->path.isEmpty())
                        tip += QStringLiteral("\n") + r->path;
                    return tip;
                }
                case CpuColumn:
                    return QStringLiteral("% của TỔNG CPU (mọi lõi) giữa hai lần làm mới.");
                case RamColumn:
                    return QStringLiteral("RAM vật lý RIÊNG của tiến trình (như cột \"Memory\" trong Task Manager).\nWorking set (gồm cả phần dùng chung với tiến trình khác): %1")
                        .arg(DiskUi::formatBytes(r->workingSet));
                case CommitColumn:
                    return QStringLiteral("Bộ nhớ đã cấp phát riêng cho tiến trình (commit), kể cả phần đã đẩy ra pagefile.");
                case IoColumn:
                    return QStringLiteral("Đọc + ghi mọi loại I/O (tệp, mạng, pipe) giữa hai lần làm mới - không chỉ riêng ổ đĩa.");
                case PathColumn:
                    return r->path;
            }
            return {};
        default:
            return {};
    }
}

QVariant ProcessTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::TextAlignmentRole)
    {
        switch (section)
        {
            case NameColumn:
            case UserColumn:
            case PathColumn: return int(Qt::AlignLeft | Qt::AlignVCenter);
            default: return int(Qt::AlignRight | Qt::AlignVCenter);
        }
    }
    if (role != Qt::DisplayRole)
        return {};
    switch (section)
    {
        case NameColumn: return QStringLiteral("Tên tiến trình");
        case PidColumn: return QStringLiteral("PID");
        case CpuColumn: return QStringLiteral("CPU");
        case RamColumn: return QStringLiteral("RAM");
        case CommitColumn: return QStringLiteral("Cấp phát");
        case ThreadsColumn: return QStringLiteral("Luồng");
        case HandlesColumn: return QStringLiteral("Handle");
        case IoColumn: return QStringLiteral("I/O");
        case UserColumn: return QStringLiteral("Người dùng");
        case PathColumn: return QStringLiteral("Đường dẫn");
    }
    return {};
}
