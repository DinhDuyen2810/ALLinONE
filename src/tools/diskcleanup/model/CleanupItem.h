#pragma once

#include <QString>

#include "CleanupCategory.h"

/// Một file hoặc thư mục cụ thể tìm thấy khi quét, sẵn sàng để hiển thị/chọn/xóa.
struct CleanupItem
{
    QString path;
    qint64 sizeBytes{0};
    bool isDirectory{false};
    CleanupCategoryId categoryId;
    bool selected{true}; // mặc định theo CleanupRisk của hạng mục (Safe=true, Caution/High=false) khi thêm vào UI

    // ---- v1.32.0 ----
    /// Thư mục gốc (CleanupCategory::rootPaths) chứa mục này - giới hạn TIN CẬY khi xóa hẳn từng tệp: mọi tệp
    /// bị xóa phải THẬT SỰ nằm trong thư mục này (kiểm bằng handle, xem SafePurge). Rỗng với mục không thuộc
    /// một thư mục gốc dọn dẹp (tệp trong Downloads, thư mục mồ côi...).
    QString root;
    /// Số tệp bên trong (thư mục) hoặc 1 (tệp).
    int fileCount{0};
    /// Mô tả ngắn hiển thị cạnh mục "xem kỹ" (vd "tải về 3 tháng trước").
    QString detail;
};
