#include "RecycleBinOps.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>

#include "FsSafety.h"

#include <QDir>
#include <QSet>
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

/// Một lần gọi SHFileOperationW cho đúng danh sách này. *cancelled = thao tác bị HỦY (người dùng trả lời
/// "Không" ở hộp hỏi lại của FOF_WANTNUKEWARNING, hoặc Shell tự dừng) - khác với lỗi của riêng một mục.
int shellDelete(const QStringList& paths, FILEOP_FLAGS extraFlags, bool* cancelled)
{
    std::vector<wchar_t> fromBuffer = buildDoubleNullList(paths);

    SHFILEOPSTRUCTW op{};
    op.hwnd = nullptr;
    op.wFunc = FO_DELETE;
    op.pFrom = fromBuffer.data();
    op.pTo = nullptr;
    op.fFlags = static_cast<FILEOP_FLAGS>(FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI | extraFlags);

    const int res = SHFileOperationW(&op);
    *cancelled = (res == 0 && op.fAnyOperationsAborted) || res == 0x75 /*DE_OPCANCELLED*/ || res == ERROR_CANCELLED;
    return res;
}

/// Số đường dẫn tối đa giao cho Shell trong MỘT lần gọi - xem runFileOperation().
constexpr int kShellBatchSize = 64;
/// Số lần tối đa chấp nhận Shell báo lỗi trong một thao tác; quá số này thì dừng, các mục còn lại giữ nguyên.
constexpr int kMaxShellFailures = 40;

bool runFileOperation(const QStringList& paths, FILEOP_FLAGS extraFlags, QString* error)
{
    // Lọc trước khi giao cho Shell (đã xác nhận từng điểm bằng chạy thử thật trên thư mục tạm):
    //  - Đường dẫn mà Shell sẽ hiểu thành một đối tượng KHÁC (tên có dấu chấm/khoảng trắng cuối, ký tự đại
    //    diện, tương đối, gốc ổ đĩa...) bị TỪ CHỐI - xem FsSafety::unsafeShellPathReason.
    //  - Mục đã tự biến mất và mục lặp bị bỏ: chỉ MỘT đường dẫn không tồn tại trong danh sách là Shell từ
    //    chối CẢ LÔ (ERROR_FILE_NOT_FOUND) và không xóa gì.
    //  - TỆP đang bị chương trình khác giữ được để lại ngay, không giao cho Shell: mỗi tệp như vậy làm Shell
    //    tự thử lại ~1,2 giây rồi mới báo lỗi (xem FsSafety::lockedAgainstDelete).
    QStringList pending;
    QSet<QString> seen;
    int refusedCount = 0;
    int lockedCount = 0;
    for (const QString& raw : paths)
    {
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(raw));
        if (!FsSafety::unsafeShellPathReason(path).isEmpty())
        {
            ++refusedCount;
            continue;
        }
        const QString key = path.toLower();
        if (seen.contains(key))
            continue;
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists)
            continue;
        seen.insert(key);
        if (!info.isDir && FsSafety::lockedAgainstDelete(path))
        {
            ++lockedCount;
            continue;
        }
        pending << path;
    }

    bool allOk = refusedCount == 0 && lockedCount == 0;
    QString firstError;
    if (refusedCount > 0)
        firstError = QString("%1 đường dẫn bị từ chối vì Windows có thể hiểu thành một tệp/thư mục khác.").arg(refusedCount);
    else if (lockedCount > 0)
        firstError = "Thao tác xóa thất bại: " + describeFileOperationError(ERROR_SHARING_VIOLATION);

    // Shell DỪNG CẢ LÔ ở mục lỗi đầu tiên (vd một tệp đang bị chương trình khác khóa): các mục đứng sau nó
    // không bao giờ được xử lý dù hoàn toàn xóa được - với thư mục tạm (luôn có vài tệp đang mở) nghĩa là
    // "Dọn dẹp" gần như không dọn được gì. Vì thế chia thành lô nhỏ và, khi một lô lỗi, bỏ đúng mục gây
    // lỗi (mục đầu tiên còn tồn tại - Shell xử lý theo thứ tự) rồi chạy tiếp phần còn lại. Mỗi vòng danh
    // sách ngắn đi ít nhất một mục nên luôn kết thúc; số lần Shell báo lỗi cũng có trần (mỗi lần có thể
    // mất hơn 1 giây) để thao tác không kéo dài vô hạn khi rất nhiều mục cùng không xóa được.
    bool cancelled = false;
    int shellFailures = 0;
    for (int offset = 0; offset < pending.size() && !cancelled && shellFailures < kMaxShellFailures; offset += kShellBatchSize)
    {
        QStringList batch = pending.mid(offset, kShellBatchSize);
        while (!batch.isEmpty() && shellFailures < kMaxShellFailures)
        {
            const int res = shellDelete(batch, extraFlags, &cancelled);
            if (res == 0 && !cancelled)
                break;
            allOk = false;
            ++shellFailures;
            if (firstError.isEmpty())
                firstError = "Thao tác xóa thất bại: " + (cancelled ? QString("thao tác đã bị hủy.") : describeFileOperationError(res));
            if (cancelled)
                break; // không hỏi lại người dùng thêm lần nào nữa cho các mục còn lại

            QStringList remaining;
            for (const QString& path : batch)
                if (FsSafety::existsNoFollow(path))
                    remaining << path;
            // "Không tìm thấy" + có mục vừa tự biến mất: lỗi do chính mục đã mất đó, chưa mục nào được xử
            // lý - thử lại nguyên phần còn lại. Mọi trường hợp khác: mục đầu tiên còn tồn tại là mục lỗi.
            const bool vanishedOnly = (res == ERROR_FILE_NOT_FOUND || res == ERROR_PATH_NOT_FOUND) && remaining.size() < batch.size();
            if (!vanishedOnly && !remaining.isEmpty())
                remaining.removeFirst();
            batch = remaining;
        }
    }

    if (!allOk && error)
        *error = firstError;
    return allOk;
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
    {
        // Không xác định được ổ chứa (vd đường dẫn quá dài): dùng gốc ổ đĩa "X:\" của chính đường dẫn.
        if (native.size() < 3 || native[1] != L':' || native[2] != L'\\')
            return {};
        volumeRoot[0] = native[0];
        volumeRoot[1] = L':';
        volumeRoot[2] = L'\\';
        volumeRoot[3] = L'\0';
    }

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
    // Chính sách "Không chuyển tệp đã xóa vào Thùng rác" (NoRecycleFiles, đặt qua Group Policy cho người
    // dùng hoặc cả máy): Shell xóa hẳn MỌI thứ dù cấu hình từng ổ ra sao.
    for (HKEY policyRoot : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    {
        DWORD value = 0;
        DWORD size = sizeof(value);
        if (RegGetValueW(policyRoot, L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\Explorer", L"NoRecycleFiles",
                         RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value != 0)
            nukeOnDelete = true;
    }
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
                nukeOnDelete = nukeOnDelete || value != 0;
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
