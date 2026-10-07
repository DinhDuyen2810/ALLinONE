#pragma once

#include <QList>
#include <QString>

/// Thông tin dung lượng một ổ đĩa - thuần Qt (QStorageInfo), không cần Win32.
struct DriveSpaceInfo
{
    QString rootPath;   // "C:/"
    QString displayName; // "Local Disk (C:)" hoặc tên nhãn ổ nếu có
    qint64 totalBytes{0};
    qint64 freeBytes{0};
    QString fileSystem;

    qint64 usedBytes() const { return totalBytes - freeBytes; }
    double usedRatio() const { return totalBytes > 0 ? static_cast<double>(usedBytes()) / totalBytes : 0.0; }
};

namespace DiskSpaceInfo
{
QList<DriveSpaceInfo> listDrives();
}
