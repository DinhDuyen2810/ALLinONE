#include "RecycleBinOps.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <QDir>
#include <QStorageInfo>
#include <vector>

namespace
{
/// Dựng chuỗi kết thúc bằng 2 ký tự NULL liên tiếp mà SHFileOperationW yêu cầu cho pFrom.
std::vector<wchar_t> buildDoubleNullList(const QStringList& paths)
{
    std::vector<wchar_t> buffer;
    for (const QString& p : paths)
    {
        const QString native = QDir::toNativeSeparators(p);
        const std::wstring w = native.toStdWString();
        buffer.insert(buffer.end(), w.begin(), w.end());
        buffer.push_back(L'\0');
    }
    buffer.push_back(L'\0'); // NULL thứ 2 kết thúc toàn bộ danh sách
    return buffer;
}

bool runFileOperation(const QStringList& paths, FILEOP_FLAGS extraFlags, QString* error)
{
    if (paths.isEmpty())
        return true;

    std::vector<wchar_t> fromBuffer = buildDoubleNullList(paths);

    SHFILEOPSTRUCTW op{};
    op.hwnd = nullptr;
    op.wFunc = FO_DELETE;
    op.pFrom = fromBuffer.data();
    op.pTo = nullptr;
    op.fFlags = static_cast<FILEOP_FLAGS>(FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | extraFlags);

    const int res = SHFileOperationW(&op);
    if (res != 0 || op.fAnyOperationsAborted)
    {
        if (error)
            *error = QString("Thao tác xóa thất bại (mã lỗi 0x%1).").arg(res, 0, 16);
        return false;
    }
    return true;
}
} // namespace

bool RecycleBinOps::moveToRecycleBin(const QStringList& paths, QString* error)
{
    return runFileOperation(paths, FOF_ALLOWUNDO, error);
}

bool RecycleBinOps::permanentlyDelete(const QStringList& paths, QString* error)
{
    return runFileOperation(paths, 0, error);
}

RecycleBinOps::Status RecycleBinOps::queryStatus()
{
    Status total;
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes())
    {
        if (!vol.isValid() || vol.rootPath().isEmpty())
            continue;

        SHQUERYRBINFO info{};
        info.cbSize = sizeof(SHQUERYRBINFO);
        const std::wstring root = QDir::toNativeSeparators(vol.rootPath()).toStdWString();
        if (SUCCEEDED(SHQueryRecycleBinW(root.c_str(), &info)))
        {
            total.totalBytes += static_cast<qint64>(info.i64Size);
            total.itemCount += static_cast<qint64>(info.i64NumItems);
        }
    }
    return total;
}

bool RecycleBinOps::empty(QString* error)
{
    const HRESULT hr = SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    // S_FALSE / RPC_S_CALLPENDING-ish "đã trống sẵn" không phải lỗi thật; chỉ coi là lỗi với mã khác 0 rõ ràng.
    if (FAILED(hr) && hr != S_FALSE)
    {
        if (error)
            *error = QString("Không làm trống được Thùng rác (mã lỗi 0x%1).").arg(static_cast<unsigned long>(hr), 0, 16);
        return false;
    }
    return true;
}
