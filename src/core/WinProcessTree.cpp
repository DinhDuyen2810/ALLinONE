#include "WinProcessTree.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <utility>

namespace
{
/// Thời điểm tạo tiến trình (FILETIME gộp thành 64-bit), 0 nếu không mở/đọc được.
quint64 processCreationTime(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return 0;
    FILETIME created{}, exited{}, kernel{}, user{};
    quint64 result = 0;
    if (GetProcessTimes(h, &created, &exited, &kernel, &user))
        result = (static_cast<quint64>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    CloseHandle(h);
    return result;
}
} // namespace

namespace WinProcessTree
{

std::vector<qint64> findDescendants(qint64 rootPid)
{
    std::vector<qint64> result;
    if (rootPid <= 0)
        return result;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    // Lấy toàn bộ (pid, ppid) một lần - danh sách tiến trình hệ thống không lớn tới mức dò lại nhiều
    // vòng (tìm hậu duệ nhiều cấp) gây vấn đề hiệu năng, và tránh phải mở lại snapshot mỗi vòng lặp.
    std::vector<std::pair<DWORD, DWORD>> all; // (pid, ppid)
    if (Process32FirstW(snapshot, &entry))
    {
        do { all.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID); }
        while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    // BFS tìm mọi hậu duệ của rootPid - không chỉ con trực tiếp, phòng trường hợp tiến trình đó tự phân
    // tầng thêm tiến trình trung gian ở phiên bản khác với bản đang đóng gói.
    const DWORD root = static_cast<DWORD>(rootPid);
    std::vector<DWORD> toKill;
    std::vector<DWORD> frontier{root};
    while (!frontier.empty())
    {
        const DWORD parent = frontier.back();
        frontier.pop_back();
        for (const auto& pair : all)
        {
            if (pair.second != parent)
                continue;
            // th32ParentProcessID chỉ là CON SỐ ghi lại lúc tiến trình con ra đời - nếu tiến trình cha
            // thật đã thoát và Windows cấp lại đúng PID đó cho một tiến trình khác, mọi tiến trình mồ
            // côi cũ vẫn "trỏ" vào PID này dù không liên quan. Một tiến trình con THẬT không thể ra đời
            // TRƯỚC cha nó: bỏ qua mọi ứng viên có thời điểm tạo sớm hơn cha. Không đọc được thời điểm
            // (tiến trình được bảo vệ/đã thoát) thì giữ hành vi cũ, không loại.
            const quint64 parentCreated = processCreationTime(parent);
            const quint64 childCreated = processCreationTime(pair.first);
            if (parentCreated != 0 && childCreated != 0 && childCreated < parentCreated)
                continue;
            bool already = false;
            for (DWORD pid : toKill)
                if (pid == pair.first) { already = true; break; }
            if (already)
                continue;
            toKill.push_back(pair.first);
            frontier.push_back(pair.first);
        }
    }

    result.reserve(toKill.size());
    for (DWORD pid : toKill)
        result.push_back(static_cast<qint64>(pid));
    return result;
}

void terminateProcessList(const std::vector<qint64>& pids)
{
    for (qint64 pid64 : pids)
    {
        if (pid64 <= 0)
            continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid64));
        if (h)
        {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
}

void terminateDescendants(qint64 rootPid)
{
    terminateProcessList(findDescendants(rootPid));
}

} // namespace WinProcessTree
