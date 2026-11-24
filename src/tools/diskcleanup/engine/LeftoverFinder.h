#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <atomic>

/**
 * @brief Tìm thư mục trong AppData (Local/Roaming/LocalLow) nhiều khả năng của ứng dụng ĐÃ GỠ (v1.32.0, theo
 * krezio/roomy LeftoverFinder.cs). Cố ý thận trọng: chỉ đề xuất khi tên thư mục KHÔNG khớp bất cứ thứ gì đang
 * cài (khóa Uninstall HKLM 64/32-bit + HKCU, gói Store trong Packages, thư mục trong Program Files /
 * Local\Programs), không phải thư mục của Windows/công cụ đã biết, KHÔNG có gì thay đổi trong 90 ngày và đủ
 * lớn (>= 50 MB). Kết quả luôn là hạng mục "xem kỹ": không bao giờ tick sẵn, mặc định vào Thùng rác.
 */
namespace LeftoverFinder
{
struct Leftover
{
    QString path;
    qint64 sizeBytes{0};
    qint64 newestWriteTime{0}; // FILETIME
    int fileCount{0};
};

constexpr qint64 kMinSizeBytes = 50LL * 1024 * 1024;
constexpr int kMinIdleDays = 90;

/// Chữ thường, chỉ giữ chữ cái/chữ số ("Google Chrome" -> "googlechrome").
QString normalizeName(const QString& name);
/// Tập khóa so khớp từ tên hiển thị/nhà phát hành/thư mục cài: cả chuỗi (>= 3 ký tự) + từng từ >= 4 ký tự.
QSet<QString> nameKeys(const QStringList& rawNames);
/// Tên thư mục có vẻ thuộc một thứ đang cài không. Tên quá ngắn (< 4 ký tự sau chuẩn hóa) -> coi là CÓ (không
/// đủ cơ sở để đề xuất xóa).
bool belongsToInstalled(const QString& folderName, const QSet<QString>& keys);
/// Thư mục của Windows/trình điều khiển/công cụ đã biết hoặc đã được dọn ở hạng mục khác - không bao giờ đề xuất.
bool isNeverCandidate(const QString& folderName);

/// Tên các ứng dụng đang cài trên máy THẬT (registry + Packages + Program Files + Local\Programs). Chỉ đọc.
QStringList installedRawNames(const QString& localAppData);

/// Duyệt thư mục cấp 1 của từng root, trả về các thư mục mồ côi thỏa mọi điều kiện, lớn nhất trước.
/// protectedDirs: thư mục không bao giờ đề xuất (FsSafety::systemProtectedDirs).
QList<Leftover> find(const QStringList& roots, const QSet<QString>& installedKeys, qint64 nowFileTime,
                     const std::atomic_bool& stopFlag, const QStringList& protectedDirs = {},
                     qint64 minSizeBytes = kMinSizeBytes, int minIdleDays = kMinIdleDays);
} // namespace LeftoverFinder
