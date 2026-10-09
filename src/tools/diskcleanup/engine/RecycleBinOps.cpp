#include "RecycleBinOps.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>

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

/// Dịch mã lỗi trả về từ SHFileOperationW sang tiếng Việt dễ hiểu. Microsoft tài liệu hóa là có thể
/// trả về mã lỗi Win32 thường (vd ERROR_FILE_NOT_FOUND=2) lẫn với bộ mã riêng của Shell (DE_*, bắt đầu
/// từ 0x71) - chỉ dịch các mã hay gặp nhất, còn lại hiện nguyên mã hex để không giấu thông tin.
QString describeFileOperationError(int code)
{
    switch (code)
    {
        case 0x02: return "Tệp không còn tồn tại (có thể đã bị xóa hoặc di chuyển trước đó).";
        case 0x03: return "Không tìm thấy đường dẫn.";
        case 0x05: return "Không có quyền truy cập (thử chạy ứng dụng với quyền Administrator).";
        case 0x20: return "Tệp đang được một chương trình khác sử dụng.";
        case 0x7C: return "Danh sách tệp không hợp lệ.";
        case 0x7E: return "Đích đến là một tệp, không phải thư mục.";
        case 0x80: return "Đích đến là một thư mục, không phải tệp.";
        case 0x81: return "Tên tệp quá dài.";
        default: return QString("mã lỗi 0x%1").arg(code, 0, 16);
    }
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
            *error = "Thao tác xóa thất bại: " + describeFileOperationError(res);
        return false;
    }
    return true;
}
} // namespace

bool RecycleBinOps::moveToRecycleBin(const QStringList& paths, QString* error)
{
    // FOF_WANTNUKEWARNING: nếu một mục KHÔNG vào Thùng rác được, Windows phải hỏi lại trước khi hủy hẳn
    // (cờ này "ghi đè một phần" FOF_NOCONFIRMATION theo tài liệu SHFILEOPSTRUCT) - không có nó, lời
    // hứa "có thể khôi phục" của giao diện thành xóa vĩnh viễn âm thầm với tệp lớn.
    return runFileOperation(paths, FOF_ALLOWUNDO | FOF_WANTNUKEWARNING, error);
}

QString RecycleBinOps::internal::recycleVerdict(DriveKind kind, bool nukeOnDelete, qint64 maxCapacityMb, qint64 sizeBytes)
{
    if (kind == DriveKind::Network)
        return "nằm trên ổ mạng (không có Thùng rác)";
    if (kind == DriveKind::Removable)
        return "nằm trên ổ tháo rời (không có Thùng rác)";
    if (kind == DriveKind::Other)
        return "nằm trên loại ổ đĩa không có Thùng rác";
    if (nukeOnDelete)
        return "Thùng rác đang bị tắt cho ổ đĩa này";
    if (maxCapacityMb >= 0 && sizeBytes > maxCapacityMb * 1024 * 1024)
        return QString("lớn hơn dung lượng tối đa của Thùng rác trên ổ này (%1 MB)").arg(maxCapacityMb);
    return {};
}

QString RecycleBinOps::notRecyclableReason(const QString& path, qint64 sizeBytes)
{
    const std::wstring native = QDir::toNativeSeparators(path).toStdWString();
    wchar_t volumeRoot[MAX_PATH + 1] = {};
    if (!GetVolumePathNameW(native.c_str(), volumeRoot, MAX_PATH))
        return {};

    internal::DriveKind kind = internal::DriveKind::Other;
    switch (GetDriveTypeW(volumeRoot))
    {
        case DRIVE_FIXED: kind = internal::DriveKind::Fixed; break;
        case DRIVE_REMOVABLE: kind = internal::DriveKind::Removable; break;
        case DRIVE_REMOTE: kind = internal::DriveKind::Network; break;
        default: kind = internal::DriveKind::Other; break;
    }

    bool nukeOnDelete = false;
    qint64 maxCapacityMb = -1;
    wchar_t volumeGuidPath[64] = {};
    if (kind == internal::DriveKind::Fixed && GetVolumeNameForVolumeMountPointW(volumeRoot, volumeGuidPath, 64))
    {
        // Tên ổ dạng Volume{GUID} - chỉ cần phần "{GUID}" để tìm khóa cấu hình Thùng rác của ổ đó.
        const QString guidPath = QString::fromWCharArray(volumeGuidPath);
        const int open = guidPath.indexOf(QLatin1Char('{'));
        const int close = guidPath.indexOf(QLatin1Char('}'));
        if (open >= 0 && close > open)
        {
            const std::wstring key =
                (QStringLiteral("Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\BitBucket\\Volume\\") +
                 guidPath.mid(open, close - open + 1))
                    .toStdWString();
            DWORD value = 0;
            DWORD size = sizeof(value);
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"NukeOnDelete", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
                nukeOnDelete = value != 0;
            size = sizeof(value);
            if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"MaxCapacity", RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS)
                maxCapacityMb = static_cast<qint64>(value);
        }
    }
    return internal::recycleVerdict(kind, nukeOnDelete, maxCapacityMb, sizeBytes);
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
