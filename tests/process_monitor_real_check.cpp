// KIỂM TRA TAY (CHỈ ĐỌC): lấy mẫu THẬT bằng ProcessSampler - đúng lớp mà tab "Tiến trình & RAM" dùng - rồi in tổng quan
// RAM/CPU và danh sách tiến trình để đối chiếu với nguồn độc lập (Get-Process / Win32_OperatingSystem của PowerShell).
//
//   cmake --build build --target process_monitor_real_check
//   build\process_monitor_real_check.exe [số giây giữa hai mẫu, mặc định 2] [--all]
//
// Dòng "PROC|pid|tên|workingSet|privateWS|commit|luồng|handle" ở cuối dùng để máy đọc (script so sánh).

#include "tools/diskcleanup/engine/ProcessMonitor.h"

#include <QCoreApplication>
#include <QThread>

#include <algorithm>
#include <cstdio>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int seconds = 2;
    bool all = false;
    for (int i = 1; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--all")
            all = true;
        else if (a.toInt() > 0)
            seconds = a.toInt();
    }
    ProcessSampler sampler;
    sampler.sample();
    QThread::sleep(static_cast<unsigned long>(seconds));
    SystemSnapshot s = sampler.sample();

    const MemoryStatus& m = s.memory;
    std::printf("RAM|total=%lld|available=%lld|used_percent=%.1f\n", static_cast<long long>(m.totalPhysical), static_cast<long long>(m.availablePhysical), m.usedPercent());
    std::printf("COMMIT|used=%lld|limit=%lld|peak=%lld|cache=%lld|paged=%lld|nonpaged=%lld\n", static_cast<long long>(m.commitUsed),
                static_cast<long long>(m.commitLimit), static_cast<long long>(m.commitPeak), static_cast<long long>(m.systemCache),
                static_cast<long long>(m.kernelPaged), static_cast<long long>(m.kernelNonpaged));
    std::printf("CPU|total_percent=%.1f|logical=%d|processes=%u|threads=%u|handles=%u|uptime_ms=%lld|native=%d\n", s.cpuPercent, s.logicalProcessors,
                s.processCount, s.threadCount, s.handleCount, static_cast<long long>(s.uptimeMs), s.nativeApi ? 1 : 0);
    QString err;
    const auto modules = ProcessMonitor::memoryModules(&err);
    std::printf("MODULES|%s\n", qPrintable(ProcessMonitor::describeMemoryModules(modules)));

    std::sort(s.processes.begin(), s.processes.end(), [](const ProcessInfo& a, const ProcessInfo& b) { return a.privateWorkingSetBytes > b.privateWorkingSetBytes; });
    int shown = 0;
    for (const ProcessInfo& p : s.processes)
    {
        if (!all && shown++ >= 15)
            break;
        std::printf("PROC|%u|%s|%lld|%lld|%lld|%u|%u|cpu=%.1f|io=%lld|user=%s\n", p.pid, qPrintable(p.name), static_cast<long long>(p.workingSetBytes),
                    static_cast<long long>(p.privateWorkingSetBytes), static_cast<long long>(p.privateBytes), p.threads, p.handles, p.cpuPercent,
                    static_cast<long long>(p.ioBytesPerSec), qPrintable(p.user));
    }
    return 0;
}
