#include "PackageIdentity.h"

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace
{
std::atomic_int g_override{-1};

// Đọc thật một lần. GetCurrentPackageFullName có từ Windows 8 nhưng nạp động qua GetProcAddress để không phụ
// thuộc appmodel.h của MinGW. Mã 15700 (APPMODEL_ERROR_NO_PACKAGE) = tiến trình không có danh tính gói.
QString readPackageFullName()
{
#ifdef Q_OS_WIN
    using Fn = LONG(WINAPI*)(UINT32*, PWSTR);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32)
        return {};
    auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k32, "GetCurrentPackageFullName")));
    if (!fn)
        return {};
    UINT32 len = 0;
    LONG rc = fn(&len, nullptr);
    if (rc != ERROR_INSUFFICIENT_BUFFER || len == 0)
        return {};
    std::wstring buf(len, L'\0');
    rc = fn(&len, buf.data());
    if (rc != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(buf.c_str());
#else
    return {};
#endif
}

const QString& cachedFullName()
{
    static const QString name = readPackageFullName();
    return name;
}
} // namespace

namespace PackageIdentity
{
bool isPackaged()
{
    const int o = g_override.load();
    if (o >= 0)
        return o == 1;
    return !cachedFullName().isEmpty();
}

QString packageFullName()
{
    return cachedFullName();
}

void setPackagedOverrideForTest(int value)
{
    g_override.store(value < 0 ? -1 : (value ? 1 : 0));
}
} // namespace PackageIdentity
