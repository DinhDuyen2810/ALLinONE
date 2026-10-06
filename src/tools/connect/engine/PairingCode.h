#pragma once

#include <QString>

/**
 * @brief Mã ghép đôi 9 chữ số, đọc ngoài kênh (out-of-band) - người dùng tự đọc trên màn hình máy A
 * rồi gõ vào máy B. Đây là bước xác nhận "cả hai máy đều là của cùng một người/được phép ghép đôi",
 * giống hệt mã PIN ghép đôi Bluetooth hoặc mã bảo mật của Mouse without Borders.
 *
 * Mã chỉ dùng MỘT LẦN để thiết lập một khóa dài hạn mạnh hơn (xem CryptoSession/PairedPeer) -
 * không phải mật khẩu lâu dài, nên độ dài 9 chữ số là đủ dùng trong một phiên ghép đôi ngắn.
 */
namespace PairingCode
{
constexpr int kLength = 9;
constexpr int kExpirySeconds = 300; // phiên chờ ghép đôi tự hủy sau 5 phút nếu không ai nhập mã
constexpr int kMaxAttempts = 5;     // đóng phiên chờ ghép đôi sau quá nhiều lần nhập sai

/// Sinh mã ngẫu nhiên an toàn (QRandomGenerator::system()), dạng "123 456 789" khi hiển thị.
QString generate();

/// Chuẩn hóa chuỗi người dùng nhập (bỏ khoảng trắng/dấu gạch) thành 9 chữ số, rỗng nếu không hợp lệ.
QString normalize(const QString& input);

bool isValidFormat(const QString& normalized);

/// Định dạng để hiển thị: "123 456 789"
QString formatForDisplay(const QString& normalized);
} // namespace PairingCode
