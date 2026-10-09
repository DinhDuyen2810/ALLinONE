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
/// ĐỒNG BỘ - chặn luồng gọi tới 15-20 giây nếu adb treo; giao diện phải dùng AdbDeviceLister (bất đồng
/// bộ) thay vì hàm này.
QList<AndroidDeviceInfo> listDevices(QString* error = nullptr);

/// Ghép đôi gỡ lỗi không dây (Android 11+): người dùng mở điện thoại > Cài đặt > Tùy chọn nhà phát
/// triển > Gỡ lỗi qua mạng > Ghép đôi bằng mã ghép đôi - nhập ĐÚNG địa chỉ IP:Cổng ghép đôi (khác cổng
/// kết nối thường) và mã 6 số hiển thị trên điện thoại. Chỉ cần làm MỘT LẦN cho mỗi mạng Wi-Fi.
bool pairWireless(const QString& ipAndPairPort, const QString& pairingCode, QString* error = nullptr);

/// Kết nối tới một thiết bị đã ghép đôi (hoặc đã bật chế độ TCP/IP qua USB trước đó) theo IP:Cổng.
bool connectWireless(const QString& ipAndPort, QString* error = nullptr);

bool disconnectWireless(const QString& ipAndPort, QString* error = nullptr);

/// Kiểm tra dạng "máy:cổng" (IPv4, tên máy, hoặc IPv6 trong ngoặc vuông; cổng 1-65535) và mã ghép đôi
/// đúng 6 chữ số. Các chuỗi này do người dùng gõ rồi trở thành ĐỐI SỐ dòng lệnh của adb.exe - một chuỗi
/// bắt đầu bằng '-' sẽ bị adb hiểu thành tùy chọn chứ không phải địa chỉ, nên phải chặn trước khi gọi.
bool isValidIpAndPort(const QString& text);
bool isValidPairingCode(const QString& text);

/// Dừng mọi tiến trình `adb.exe` mà ĐƯỜNG DẪN TỆP ẢNH đúng là adb.exe đóng gói kèm của ta (so qua
/// QueryFullProcessImageNameW) - gọi khi đóng cửa sổ Android Phone Control/thoát ứng dụng. Lý do: lệnh
/// adb đầu tiên tự sinh một tiến trình daemon ("adb fork-server") SỐNG TIẾP sau khi ứng dụng thoát và
/// giữ khóa chính tệp `scrcpy\adb.exe`, làm trình cài đặt bản cập nhật không ghi đè được tệp đó.
/// KHÔNG dùng `adb kill-server`: lệnh đó dừng daemon đang nghe ở cổng 5037 BẤT KỂ của ai - nếu Android
/// Studio (hay công cụ khác) đang chạy adb của riêng nó thì chính daemon của công cụ đó sẽ bị dừng.
/// So theo đường dẫn tệp ảnh thì adb của công cụ khác không bao giờ bị đụng tới. Trả về số tiến trình
/// đã dừng.
int stopBundledAdbServer();

// ---- Lõi thuần (không gọi adb thật) - tách riêng để kiểm thử được bằng dữ liệu mẫu ----
namespace internal
{
/// CHỈ truyền stdout của `adb devices -l` (không ghép stderr): dòng nào có trạng thái (từ thứ hai)
/// không phải trạng thái adb hợp lệ đều bị bỏ qua.
QList<AndroidDeviceInfo> parseDevicesOutput(const QString& output);
bool isKnownDeviceState(const QString& state);

/// Hai đường dẫn có trỏ tới CÙNG một tệp thực thi không (không phân biệt hoa/thường, kiểu dấu gạch,
/// tiền tố "\\?\"; phân giải liên kết/junction nếu tệp tồn tại).
bool isSameExecutablePath(const QString& a, const QString& b);

/// TerminateProcess mọi tiến trình có đường dẫn tệp ảnh khớp `exePath` (trừ chính tiến trình đang gọi),
/// chờ mỗi tiến trình thoát tối đa 2 giây để khóa tệp được nhả. Trả về số tiến trình đã dừng.
int terminateProcessesByImagePath(const QString& exePath);
} // namespace internal
} // namespace AdbController
