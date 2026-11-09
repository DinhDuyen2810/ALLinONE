#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QMetaType>
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>

#include <atomic>
#include <memory>
#include <vector>

/// Một tiến trình đang chạy tại thời điểm lấy mẫu.
struct ProcessInfo
{
    quint32 pid{0};
    quint32 parentPid{0};
    QString name;       // tên tệp thực thi ("chrome.exe"; tiến trình nhân: "System", "Registry"...)
    QString path;       // đường dẫn đầy đủ; rỗng nếu không mở được (tiến trình hệ thống khi không có quyền Admin)
    QString user;       // "DOMAIN\user"; rỗng nếu không xác định được
    quint32 sessionId{0};
    quint32 threads{0};
    quint32 handles{0};
    qint64 workingSetBytes{0};        // RAM vật lý đang dùng (gồm cả phần dùng chung với tiến trình khác)
    qint64 privateWorkingSetBytes{0}; // RAM vật lý RIÊNG của tiến trình (cột "Memory" trong Task Manager)
    qint64 privateBytes{0};           // bộ nhớ đã cấp phát riêng (commit), kể cả phần đã đẩy ra pagefile
    qint64 peakWorkingSetBytes{0};
    double cpuPercent{0};             // % của TỔNG CPU (mọi lõi) giữa hai lần lấy mẫu; 0 ở mẫu đầu tiên
    qint64 ioBytesPerSec{0};          // đọc + ghi, mọi loại I/O (tệp, mạng, pipe) giữa hai lần lấy mẫu
    qint64 ioReadBytes{0};            // tích lũy từ lúc tiến trình bắt đầu
    qint64 ioWriteBytes{0};
    quint64 cpuTime100ns{0};          // thời gian CPU tích lũy (kernel + user), đơn vị 100 ns
    QDateTime startTime;
};

/// Tình trạng RAM/bộ nhớ của cả máy.
struct MemoryStatus
{
    qint64 totalPhysical{0};
    qint64 availablePhysical{0}; // RAM có thể cấp ngay (trống + đang chờ)
    qint64 commitUsed{0};        // bộ nhớ ảo đã cam kết
    qint64 commitLimit{0};       // RAM + pagefile
    qint64 commitPeak{0};
    qint64 systemCache{0};       // bộ nhớ đệm tệp của hệ thống
    qint64 kernelPaged{0};
    qint64 kernelNonpaged{0};

    qint64 usedPhysical() const { return totalPhysical - availablePhysical; }
    double usedPercent() const { return totalPhysical > 0 ? 100.0 * static_cast<double>(usedPhysical()) / static_cast<double>(totalPhysical) : 0.0; }
    double commitPercent() const { return commitLimit > 0 ? 100.0 * static_cast<double>(commitUsed) / static_cast<double>(commitLimit) : 0.0; }
};

struct SystemSnapshot
{
    qint64 takenAtMs{0}; // QDateTime::currentMSecsSinceEpoch()
    MemoryStatus memory;
    double cpuPercent{0}; // tổng CPU mọi lõi; 0 ở mẫu đầu tiên
    int logicalProcessors{1};
    quint32 processCount{0};
    quint32 threadCount{0};
    quint32 handleCount{0};
    qint64 uptimeMs{0};
    bool nativeApi{true}; // false: dữ liệu lấy bằng phương án dự phòng (Toolhelp + OpenProcess), thiếu số liệu của tiến trình hệ thống
    QList<ProcessInfo> processes;
};
Q_DECLARE_METATYPE(std::shared_ptr<SystemSnapshot>)

/// Một thanh RAM cắm trên máy (đọc từ Win32_PhysicalMemory).
struct MemoryModule
{
    QString slot;
    qint64 capacityBytes{0};
    int speedMHz{0};
    QString type; // "DDR4"...; rỗng nếu không rõ
    QString manufacturer;
    QString partNumber;
};

namespace ProcessMonitor
{
/// Đọc thanh RAM cắm trên máy (PowerShell, chỉ ĐỌC). Rỗng nếu máy ảo/không đọc được. cancel (nếu có) được kiểm tra
/// định kỳ - đặt true từ luồng khác để dừng sớm (khi thoát ứng dụng không phải chờ hết thời gian chờ của PowerShell).
QList<MemoryModule> memoryModules(QString* error = nullptr, const std::atomic_bool* cancel = nullptr);

/// "DDR4", "DDR5"... từ mã SMBIOSMemoryType; rỗng nếu không biết.
QString memoryTypeName(int smbiosType);

/// Một dòng tóm tắt các thanh RAM, vd "2 thanh RAM: 8 GB + 8 GB · DDR4 2933 MHz · Samsung (tổng 16 GB)"; rỗng nếu
/// không có thanh nào.
QString describeMemoryModules(const QList<MemoryModule>& modules);

namespace internal
{
QList<MemoryModule> parseMemoryModulesJson(const QByteArray& json, QString* error = nullptr);
} // namespace internal
} // namespace ProcessMonitor

/**
 * @brief Lấy mẫu danh sách tiến trình + RAM/CPU của cả máy. Có TRẠNG THÁI (nhớ mẫu trước để tính % CPU và tốc độ I/O
 * theo khoảng giữa hai lần lấy mẫu) nên dùng MỘT thể hiện cho cả phiên theo dõi. KHÔNG an toàn khi gọi từ nhiều luồng.
 *
 * Nguồn dữ liệu chính là NtQuerySystemInformation(SystemProcessInformation) - đúng nguồn Task Manager dùng: một lệnh trả
 * về MỌI tiến trình (kể cả tiến trình hệ thống được bảo vệ) với RAM, handle, I/O, thời gian CPU mà không cần mở từng
 * tiến trình. Không có (hoặc bản 32-bit) thì rơi về Toolhelp + OpenProcess.
 */
class ProcessSampler
{
public:
    ProcessSampler();
    ~ProcessSampler();
    ProcessSampler(const ProcessSampler&) = delete;
    ProcessSampler& operator=(const ProcessSampler&) = delete;

    SystemSnapshot sample();

    /// Quên mẫu trước (mẫu kế tiếp có cpuPercent/ioBytesPerSec = 0).
    void reset();

    /// Chỉ dùng cho test: buộc dùng phương án dự phòng.
    void setForceFallbackForTest(bool on) { m_forceFallback = on; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_forceFallback{false};
};

/// Luồng nền lấy mẫu định kỳ khi được bật (setActive(true)) và NGỦ khi tắt - không tốn CPU lúc tab không hiện.
class ProcessSamplerThread : public QThread
{
    Q_OBJECT

public:
    explicit ProcessSamplerThread(QObject* parent = nullptr);
    ~ProcessSamplerThread() override;

    void setIntervalMs(int ms);
    /// Bật/tắt lấy mẫu. Bật lại sau khi tắt: lấy mẫu "khởi động" trước (để % CPU có ngay) rồi mới báo.
    void setActive(bool on);
    bool isActive() const;
    /// Số mẫu đã báo từ lúc tạo (dùng cho test).
    int sampleCount() const { return m_sampleCount.load(); }
    /// Dừng hẳn luồng (không đợi); gọi wait() sau đó nếu cần.
    void requestStop();

signals:
    void sampled(std::shared_ptr<SystemSnapshot> snapshot);

protected:
    void run() override;

private:
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    bool m_active{false};
    int m_intervalMs{2000};
    std::atomic_bool m_stop{false};
    std::atomic_int m_sampleCount{0};
};
