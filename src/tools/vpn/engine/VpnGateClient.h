#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include "../model/VpnGateServer.h"

class QNetworkAccessManager;
class QNetworkReply;

/// Tải danh sách máy chủ công cộng của VPN Gate (https://www.vpngate.net) qua API CSV công khai mà dự án
/// này cung cấp sẵn cho phần mềm khách bên thứ ba. Chỉ đọc (một yêu cầu GET), không gửi gì về máy chủ
/// ngoài yêu cầu HTTP tiêu chuẩn; không kèm thông tin định danh máy người dùng.
///
/// Ứng dụng KHÔNG vận hành máy chủ VPN nào - đây là cách HỢP PHÁP duy nhất để có trải nghiệm "chọn quốc
/// gia rồi kết nối đổi IP" mà không tự dựng hạ tầng: dùng lại danh sách relay tình nguyện công khai của
/// VPN Gate (giống tinh thần dùng adb/scrcpy/yt-dlp có sẵn thay vì tự viết lại). Việc kết nối tới một máy
/// chủ trong danh sách do VpnController/VpnConnector (RAS, L2TP/IPsec) đảm nhiệm - lớp này chỉ TẢI danh sách.
class VpnGateClient : public QObject
{
    Q_OBJECT

public:
    explicit VpnGateClient(QObject* parent = nullptr);

    /// Bắt đầu tải danh sách (bất đồng bộ). Kết quả qua tín hiệu finished(). Gọi lại khi đang tải sẽ hủy
    /// lần trước rồi tải lại.
    void fetch();
    /// Hủy lần tải đang chạy (nếu có) - không phát finished() cho lần bị hủy.
    void cancel();

    /// CHỈ dùng cho kiểm thử: trỏ sang một máy chủ cục bộ (127.0.0.1) và rút ngắn thời gian chờ.
    void setEndpointForTest(const QUrl& url, int timeoutMs);

signals:
    /// servers rỗng + error khác rỗng = lỗi; servers có nội dung + error rỗng = thành công.
    void finished(QList<VpnGateServer> servers, QString error);

private:
    QNetworkAccessManager* m_nam{nullptr};
    QPointer<QNetworkReply> m_activeReply;
    QUrl m_url;
    int m_timeoutMs{40000};
};

namespace VpnGateClientInternal
{
/// Phân tích nội dung CSV của API VPN Gate thành danh sách máy chủ - THUẦN (không mạng), test được bằng
/// dữ liệu mẫu lấy từ API thật. Định dạng CSV của VPN Gate:
///   dòng "*vpn_servers"  (bỏ qua)
///   dòng "#HostName,IP,Score,Ping,Speed,CountryLong,CountryShort,NumVpnSessions,..."  (dòng tiêu đề cột, bỏ qua)
///   nhiều dòng dữ liệu, mỗi dòng 15 trường ngăn bằng dấu phẩy
///   dòng "*"  (kết thúc, bỏ qua)
/// Dòng có IP không hợp lệ (không phải IPv4) hoặc mã quốc gia không phải 2 chữ cái bị BỎ QUA (dòng bị
/// lệch cột do tên quốc gia hiếm hoi chứa dấu phẩy, hoặc dữ liệu hỏng) - an toàn vì IP sẽ trở thành đối
/// số máy chủ của lệnh tạo kết nối VPN.
QList<VpnGateServer> parseCsv(const QByteArray& csv);

/// Địa chỉ này có phải một IPv4 hợp lệ không (VPN Gate luôn trả IPv4). Dùng để lọc dòng CSV và để chắc
/// chắn chuỗi đưa vào lệnh tạo kết nối VPN là một địa chỉ, không phải thứ gì khác.
bool isValidIpv4(const QString& address);
} // namespace VpnGateClientInternal
