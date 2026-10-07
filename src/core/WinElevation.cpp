#include "WinElevation.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <string>
#endif

namespace
{
#ifdef Q_OS_WIN
/// Tiến trình hiện tại có token "Administrators" đã kích hoạt hay không - API Win32 thô
/// (OpenProcessToken/GetTokenInformation), không gọi PowerShell.
bool currentProcessElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elevation{};
    DWORD size = sizeof(elevation);
    const bool got = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size);
    CloseHandle(token);
    return got && elevation.TokenIsElevated != 0;
}
#endif
} // namespace

namespace WinElevation
{

bool isElevated()
{
#ifdef Q_OS_WIN
    return currentProcessElevated();
#else
    return false;
#endif
}

bool relaunchElevated(QString* error)
{
#ifdef Q_OS_WIN
    const QString exePath = QCoreApplication::applicationFilePath();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas"; // yêu cầu UAC - Windows tự hiện hộp thoại xác nhận chuẩn, không qua mặt gì cả
    const std::wstring exePathW = exePath.toStdWString();
    info.lpFile = exePathW.c_str();
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info))
    {
        const DWORD err = GetLastError();
        if (error)
            *error = (err == ERROR_CANCELLED) ? "Người dùng đã từ chối cấp quyền Administrator (hộp thoại UAC)."
                                               : QString("Không khởi chạy lại được với quyền Administrator (mã lỗi %1).").arg(err);
        return false;
    }
    if (info.hProcess)
        CloseHandle(info.hProcess);
    return true;
#else
    if (error) *error = "Chỉ hỗ trợ trên Windows";
    return false;
#endif
}

} // namespace WinElevation
