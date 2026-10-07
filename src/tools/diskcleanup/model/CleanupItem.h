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
};
