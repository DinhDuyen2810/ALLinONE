#pragma once

#include <QList>
#include <QString>

#include "../model/CleanupCategory.h"

/// Các thư mục gốc theo biến môi trường, dùng để dựng danh sách hạng mục dọn dẹp. Tách riêng khỏi
/// việc đọc biến môi trường thật (CleanupEnvironment::current()) để buildCategories() thuần túy, test
/// được bằng cách trỏ vào một thư mục tạm giả lập thay vì hệ thống thật của máy.
struct CleanupEnvironment
{
    QString tempDir;        // %TEMP%
    QString windowsDir;     // %WINDIR%, ví dụ C:\Windows
    QString localAppData;   // %LOCALAPPDATA%
    QString roamingAppData; // %APPDATA%

    /// Đọc biến môi trường THẬT của máy đang chạy.
    static CleanupEnvironment current();
};

namespace CategoryRegistry
{
/// Dựng toàn bộ danh sách hạng mục dọn dẹp đã biết, với đường dẫn đã phân giải theo 'env'. Hạng mục
/// nào có thư mục gốc không tồn tại trên máy (vd: chưa cài Chrome) vẫn được trả về nhưng rootPaths rỗng
/// - CleanupScanner sẽ tự bỏ qua khi quét.
QList<CleanupCategory> buildCategories(const CleanupEnvironment& env);

QString riskName(CleanupRisk risk);
QString categoryIdKey(CleanupCategoryId id); // định danh ổn định dạng chuỗi, dùng để lưu tùy chọn người dùng
}
