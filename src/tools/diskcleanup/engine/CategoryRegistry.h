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

    /// Thư mục KHÔNG BAO GIỜ được làm thư mục gốc dọn dẹp (hồ sơ người dùng, Program Files...) - xem
    /// FsSafety::unsafeCleanupRootReason(). current() điền từ máy thật; môi trường giả của test để rỗng
    /// (các kiểm tra cơ bản - rỗng/tương đối/gốc ổ đĩa - vẫn luôn áp dụng).
    QStringList protectedDirs;

    /// Đọc biến môi trường THẬT của máy đang chạy. Biến thiếu/không phải đường dẫn tuyệt đối thì trường
    /// tương ứng để RỖNG (các hạng mục phụ thuộc nó sẽ không khả dụng) thay vì sinh đường dẫn kiểu
    /// "/Temp" bị Windows hiểu theo ổ đĩa hiện hành.
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
