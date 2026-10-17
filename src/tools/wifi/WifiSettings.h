#pragma once

#include <QSettings>
#include <QString>

/**
 * @brief Cài đặt của công cụ WiFi, lưu bền qua QSettings.
 *
 * Hỗ trợ:
 * - Mật khẩu mặc định hiện tại.
 * - Giá trị bắt đầu (From) và kết thúc (To) của khoảng mã hệ 36.
 *
 * Bảng ký tự hệ 36:
 * 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ
 *
 * Ví dụ:
 * From = A000
 * To   = A00Z
 *
 * Lưu ý:
 * File này chỉ quản lý cấu hình. Việc kiểm tra định dạng và
 * lựa chọn giá trị cần được xử lý ở lớp giao diện/logic tương ứng.
 */
namespace WifiSettings
{

// Các khóa lưu trong QSettings.
inline constexpr char kDefaultConnectPasswordKey[] =
    "wifi/defaultConnectPassword";

inline constexpr char kLabPasswordStartKey[] =
    "wifi/labPasswordStart";

inline constexpr char kLabPasswordEndKey[] =
    "wifi/labPasswordEnd";

// Giá trị mặc định ban đầu.
inline constexpr char kBuiltinDefaultPassword[] =
    "Duyen123456";

// Bảng ký tự hệ 36.
inline constexpr char kBase36Characters[] =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

// ============================================================
// Mật khẩu mặc định
// ============================================================

inline QString defaultConnectPassword()
{
    QSettings s;

    return s.value(
        QString::fromLatin1(kDefaultConnectPasswordKey),
        QString::fromLatin1(kBuiltinDefaultPassword)
    ).toString();
}

inline void setDefaultConnectPassword(const QString& password)
{
    QSettings s;

    s.setValue(
        QString::fromLatin1(kDefaultConnectPasswordKey),
        password
    );
}

// ============================================================
// Giá trị bắt đầu (From)
// ============================================================

inline QString labPasswordStart()
{
    QSettings s;

    return s.value(
        QString::fromLatin1(kLabPasswordStartKey),
        QString()
    ).toString();
}

inline void setLabPasswordStart(const QString& password)
{
    QSettings s;

    s.setValue(
        QString::fromLatin1(kLabPasswordStartKey),
        password.trimmed().toUpper()
    );
}

// ============================================================
// Giá trị kết thúc (To)
// ============================================================

inline QString labPasswordEnd()
{
    QSettings s;

    return s.value(
        QString::fromLatin1(kLabPasswordEndKey),
        QString()
    ).toString();
}

inline void setLabPasswordEnd(const QString& password)
{
    QSettings s;

    s.setValue(
        QString::fromLatin1(kLabPasswordEndKey),
        password.trimmed().toUpper()
    );
}

// ============================================================
// Kiểm tra chuỗi có dùng ký tự hệ 36 hay không
// ============================================================

inline bool isValidBase36(const QString& value)
{
    if (value.isEmpty())
        return false;

    for (const QChar ch : value)
    {
        if (!((ch >= QLatin1Char('0') &&
               ch <= QLatin1Char('9')) ||
              (ch >= QLatin1Char('A') &&
               ch <= QLatin1Char('Z'))))
        {
            return false;
        }
    }

    return true;
}

// ============================================================
// Kiểm tra cấu hình From/To
// ============================================================

inline bool hasValidLabPasswordRange()
{
    const QString from = labPasswordStart();
    const QString to   = labPasswordEnd();

    if (!isValidBase36(from) || !isValidBase36(to))
        return false;

    // Dùng cùng độ dài để tránh nhập nhằng khi so sánh mã.
    if (from.length() != to.length())
        return false;

    return from <= to;
}

} // namespace WifiSettings
