#include "DiskSpaceInfo.h"

#include <QStorageInfo>

QList<DriveSpaceInfo> DiskSpaceInfo::listDrives()
{
    QList<DriveSpaceInfo> result;
    for (const QStorageInfo& vol : QStorageInfo::mountedVolumes())
    {
        if (!vol.isValid() || !vol.isReady() || vol.bytesTotal() <= 0)
            continue;
        // Chỉ ổ đĩa thật (có ký tự ổ đĩa), bỏ qua các điểm mount đặc biệt không có dung lượng ý nghĩa.
        if (vol.rootPath().length() > 4) // "C:/" dài 3; các mount point khác thường dài hơn nhiều
            continue;

        DriveSpaceInfo d;
        d.rootPath = vol.rootPath();
        d.displayName = vol.name().isEmpty() ? QString("Ổ đĩa (%1)").arg(vol.rootPath()) : vol.name();
        d.totalBytes = vol.bytesTotal();
        d.freeBytes = vol.bytesAvailable();
        d.fileSystem = QString::fromUtf8(vol.fileSystemType());
        result.push_back(d);
    }
    return result;
}
