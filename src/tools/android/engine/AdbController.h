#pragma once

#include <QList>
#include <QString>

#include "../model/AndroidDeviceInfo.h"

/**
 * @brief Lớp duy nhất gọi `adb.exe` (Android Debug Bridge, Apache-2.0, đóng gói kèm theo ứng dụng
 * trong thư mục `scrcpy/` cạnh file exe - xem THIRD_PARTY.md). Chỉ các lệnh adb THUẦN TÚY QUẢN LÝ KẾT
 * NỐI (liệt kê/ghép đôi/kết nối thiết bị) - việc gương màn hình + điều khiển thời gian thực do
 * ScrcpyLauncher đảm nhiệm (gọi `scrcpy.exe`, chương trình đã làm chính xác việc đó, không tự viết
 * lại luồng giải mã H.264 + tiêm sự kiện chạm/phím mức thấp).
 */
namespace AdbController
{
/// Đường dẫn adb.exe/scrcpy.exe đã đóng gói cạnh file exe đang chạy (thư mục `scrcpy/`) - rỗng nếu
/// không tìm thấy (gói portable/zip chưa chạy `build_app.bat` để đóng gói, hoặc bị xóa nhầm).
QString adbExecutablePath();
QString scrcpyExecutablePath();
bool isBundleAvailable(QString* missingWhat = nullptr);

/// Liệt kê thiết bị adb đang thấy (USB đã cắm + đã kết nối không dây trước đó), chạy `adb devices -l`.
QList<AndroidDeviceInfo> listDevices(QString* error = nullptr);

/// Ghép đôi gỡ lỗi không dây (Android 11+): người dùng mở điện thoại > Cài đặt > Tùy chọn nhà phát
/// triển > Gỡ lỗi qua mạng > Ghép đôi bằng mã ghép đôi - nhập ĐÚNG địa chỉ IP:Cổng ghép đôi (khác cổng
/// kết nối thường) và mã 6 số hiển thị trên điện thoại. Chỉ cần làm MỘT LẦN cho mỗi mạng Wi-Fi.
bool pairWireless(const QString& ipAndPairPort, const QString& pairingCode, QString* error = nullptr);

/// Kết nối tới một thiết bị đã ghép đôi (hoặc đã bật chế độ TCP/IP qua USB trước đó) theo IP:Cổng.
bool connectWireless(const QString& ipAndPort, QString* error = nullptr);

bool disconnectWireless(const QString& ipAndPort, QString* error = nullptr);

// ---- Lõi thuần (không gọi adb thật) - tách riêng để kiểm thử được bằng dữ liệu mẫu ----
namespace internal
{
QList<AndroidDeviceInfo> parseDevicesOutput(const QString& output);
}
} // namespace AdbController
