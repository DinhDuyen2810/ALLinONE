#pragma once

#include <QList>
#include <QString>

#include "../model/WireGuardTunnel.h"

/**
 * @brief Quản lý tunnel WireGuard bằng CHÍNH ứng dụng WireGuard for Windows chính thức đã cài trên máy
 * (https://www.wireguard.com/install/) - ứng dụng này KHÔNG đóng gói/tự dựng driver WireGuard: driver
 * mạng mức kernel (wintun) chỉ nên cài qua trình cài đặt chính thức (ký số, đúng quy trình driver store
 * của Windows), không phải chép tay một .exe cạnh exe như adb/scrcpy. Cùng lý do ứng dụng không đóng gói
 * trình duyệt cho tính năng quét sâu của Downloader - xem HeadlessBrowser::findBrowserExecutable().
 *
 * Không có máy chủ WireGuard nào được ứng dụng này tự cung cấp sẵn - đúng tinh thần đã áp dụng cho
 * VpnController (VPN hệ thống Windows): người dùng tự đưa vào một tệp .conf lấy từ nhà cung cấp VPN họ
 * đã có tài khoản (vd ProtonVPN/Windscribe/Mullvad bản free đều cho xuất file WireGuard chuẩn) hoặc VPN
 * tự dựng/cơ quan.
 *
 * Mọi thao tác cài/gỡ/bật/tắt tunnel đều cần quyền Administrator (dịch vụ Windows + đọc thư mục cấu hình
 * WireGuard, ACL chỉ cho SYSTEM/Administrators) - nơi gọi (UI) phải tự kiểm tra WinElevation::isElevated()
 * TRƯỚC và mời người dùng bấm nút chạy lại với quyền Administrator, không cố âm thầm tự nâng quyền -
 * đúng mẫu đã dùng cho PartitionManager/MalwareScanTab/WebProtectionTab. Một khi CHÍNH ứng dụng đã chạy
 * elevated thì mọi QProcess con (wireguard.exe/sc.exe) spawn từ đây tự động kế thừa quyền đó.
 *
 * Tên tunnel = tên dịch vụ Windows mà wireguard.exe tạo ra: "WireGuardTunnel$<tên đã chuẩn hóa>". Bật/tắt
 * một tunnel ĐÃ cài dùng Service Control Manager (sc.exe start/stop) - wireguard.exe không có cờ dòng
 * lệnh riêng cho việc này, chỉ có /installtunnelservice (cài + chạy ngay) và /uninstalltunnelservice (gỡ).
 *
 * CHƯA KIỂM CHỨNG trên máy thật có cài WireGuard + máy chủ WireGuard thật (môi trường build không có cả
 * hai điều kiện này). Hành vi CLI dựa theo tài liệu công khai của dự án WireGuard for Windows - cần tự
 * kiểm tra lại bằng tay (import một tunnel thật, bật/tắt, xem đổi IP công khai) trước khi coi là hoàn
 * thiện, theo đúng quy tắc "Xác nhận thật, không đoán" của dự án.
 */
namespace WireGuardController
{
/// Đường dẫn wireguard.exe nếu đã cài WireGuard for Windows chính thức (rỗng nếu chưa cài).
QString executablePath();
bool isInstalled();

/// Nhập một tệp .conf (vd xuất từ ProtonVPN/Windscribe/Mullvad, hoặc VPN tự dựng) thành một tunnel mới:
/// chuẩn hóa tên mong muốn, gọi `wireguard.exe /installtunnelservice` (cài + CHẠY NGAY), rồi lưu các
/// trường KHÔNG bí mật (address/dns/endpoint/allowedIps) + nhãn quốc gia vào metadata riêng của ứng dụng
/// (KHÔNG BAO GIỜ lưu khóa riêng). Cần quyền Administrator - nơi gọi tự kiểm tra trước. *resultName (nếu
/// khác rỗng) nhận tên tunnel đã chuẩn hóa thật sự được dùng.
bool importTunnel(const QString& confFilePath, const QString& desiredName, const QString& countryLabel,
                  QString* resultName, QString* error);

/// Gỡ hẳn một tunnel (dừng dịch vụ + wireguard.exe xóa cấu hình đã mã hóa + xóa metadata của ứng dụng).
/// Cần quyền Administrator.
bool removeTunnel(const QString& name, QString* error = nullptr);

/// Bật/tắt một tunnel ĐÃ cài (qua Service Control Manager). Cần quyền Administrator.
bool startTunnel(const QString& name, QString* error = nullptr);
bool stopTunnel(const QString& name, QString* error = nullptr);

/// Liệt kê các tunnel ĐÃ cài thật trên máy (đọc thư mục cấu hình của WireGuard - thường cần quyền
/// Administrator vì ACL thư mục đó chỉ cho SYSTEM/Administrators), ghép với trạng thái dịch vụ (sc query)
/// và metadata hiển thị của ứng dụng (nếu có - tunnel cài bằng GUI WireGuard gốc thay vì qua ứng dụng này
/// vẫn hiện ra, chỉ thiếu các trường hiển thị phụ).
QList<WireGuardTunnel> listTunnels(QString* error = nullptr);

/// Chỉ đổi nhãn quốc gia đã lưu (không đụng tới tunnel thật trong WireGuard).
void setCountryLabel(const QString& name, const QString& countryLabel);

// ---- Lõi thuần (không gọi tiến trình/đĩa thật) - tách riêng để kiểm thử được bằng dữ liệu mẫu ----
namespace internal
{
/// Giữ lại [A-Za-z0-9_.-] (ASCII), gộp mọi ký tự khác (kể cả chữ có dấu) thành "_", bỏ "." đầu/cuối, cắt
/// tối đa 32 ký tự (giới hạn tên tunnel của WireGuard for Windows) - trả "tunnel" nếu kết quả rỗng.
QString sanitizeTunnelName(const QString& rawName);

/// "WireGuardTunnel$" + tên đã chuẩn hóa - tên dịch vụ Windows mà wireguard.exe tạo khi cài tunnel.
QString serviceName(const QString& tunnelName);

struct ParsedConf
{
    bool valid{false};
    QString error;
    QString address;
    QString dns;
    QString endpoint;
    QString allowedIps;
};
/// Đọc các trường KHÔNG BÍ MẬT (Address/DNS trong [Interface], Endpoint/AllowedIPs trong [Peer]) từ nội
/// dung .conf để hiển thị - KHÔNG BAO GIỜ đọc/trả về PrivateKey hay PresharedKey.
ParsedConf parseConfForDisplay(const QString& confText);

/// Diễn giải output của `sc query "<tên dịch vụ>"` - true nếu dòng STATE là RUNNING (dịch vụ không tồn
/// tại thì sc in lỗi "FAILED 1060", không có dòng STATE nào -> false, đúng nghĩa "chưa chạy").
bool isServiceRunningOutput(const QString& scQueryOutput);

/// Tên tunnel từ tên tệp cấu hình đã mã hóa của WireGuard (vd "NL-Proton.conf.dpapi" -> "NL-Proton").
/// Trả về rỗng nếu tên tệp không đúng đuôi ".conf.dpapi".
QString tunnelNameFromConfigFileName(const QString& fileName);
} // namespace internal
} // namespace WireGuardController
