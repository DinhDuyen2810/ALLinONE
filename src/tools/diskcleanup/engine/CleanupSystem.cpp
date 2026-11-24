#include "CleanupSystem.h"

#include "FsSafety.h"

#include <QDir>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <string>
#endif

namespace CleanupSystem
{

QSet<QString> runningExeNamesLower()
{
    QSet<QString> names;
#ifdef Q_OS_WIN
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return names;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            names.insert(QString::fromWCharArray(pe.szExeFile).toLower());
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
#endif
    return names;
}

QString hibernationFilePath(const QString& systemDrive)
{
    if (systemDrive.isEmpty())
        return {};
    QString root = QDir::fromNativeSeparators(systemDrive);
    if (!root.endsWith(QLatin1Char('/')))
        root += QLatin1Char('/');
    return root + "hiberfil.sys";
}

qint64 hibernationFileSize(const QString& systemDrive)
{
    const QString path = hibernationFilePath(systemDrive);
    if (path.isEmpty())
        return 0;
    const FsSafety::RawInfo info = FsSafety::rawInfo(path);
    return (info.exists && !info.isDir) ? info.sizeBytes : 0;
}

bool turnOffHibernation(QString* error)
{
#ifdef Q_OS_WIN
    wchar_t sysDir[MAX_PATH + 1] = {};
    const UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        if (error)
            *error = "Không xác định được thư mục System32.";
        return false;
    }
    const std::wstring exe = std::wstring(sysDir, len) + L"\\powercfg.exe";

    const HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = L"/hibernate off";
    sei.nShow = SW_HIDE;
    const BOOL started = ShellExecuteExW(&sei);
    const DWORD startError = started ? ERROR_SUCCESS : GetLastError();
    DWORD exitCode = 1;
    if (started && sei.hProcess)
    {
        WaitForSingleObject(sei.hProcess, 120000);
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
    }
    if (SUCCEEDED(coHr))
        CoUninitialize();
    if (!started)
    {
        if (error)
            *error = startError == ERROR_CANCELLED ? QString("Đã hủy ở hộp thoại quyền Quản trị (UAC).")
                                                   : QString("Không chạy được powercfg (mã lỗi %1).").arg(startError);
        return false;
    }
    if (exitCode != 0)
    {
        if (error)
            *error = QString("powercfg /hibernate off báo lỗi (mã thoát %1).").arg(exitCode);
        return false;
    }
    return true;
#else
    if (error)
        *error = "Chỉ hỗ trợ trên Windows.";
    return false;
#endif
}
} // namespace CleanupSystem
