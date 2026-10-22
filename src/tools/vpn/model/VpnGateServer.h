#pragma once

#include <QString>

/// Một máy chủ relay công cộng của VPN Gate (dự án nghiên cứu học thuật của Đại học Tsukuba, Nhật Bản -
/// https://www.vpngate.net). Đây là các máy chủ do TÌNH NGUYỆN VIÊN trên khắp thế giới chia sẻ, KHÔNG
/// phải máy chủ của ứng dụng này (ứng dụng không vận hành máy chủ VPN nào) - danh sách được tải từ API
/// công khai mà VPN Gate cung cấp sẵn cho phần mềm khách bên thứ ba. IP của các máy chủ này là ĐỘNG (đổi
/// theo thời gian) và tốc độ/độ ổn định không được bảo đảm; người dùng tự chịu trách nhiệm khi sử dụng.
///
/// Chỉ giữ các trường cần để HIỂN THỊ và KẾT NỐI qua RAS có sẵn của Windows (SSTP hoặc L2TP/IPsec). Không
/// giữ cấu hình OpenVPN base64 (cột cuối của CSV) - ứng dụng không chạy máy khách OpenVPN.
struct VpnGateServer
{
    QString hostName;      // vd "vpn123456789" - tên máy chủ do VPN Gate đặt (= tiền tố tên miền DDNS, xem sstpHost)
    QString ip;            // địa chỉ IPv4 - đã kiểm định dạng khi phân tích CSV (xem VpnGateClient)
    QString countryLong;   // tên quốc gia đầy đủ, vd "Japan", "United States"
    QString countryShort;  // mã quốc gia 2 chữ (ISO), vd "JP", "US"
    int ping{0};           // ms (0 = không rõ)
    qint64 speedBps{0};    // tốc độ đo được, bit/giây (0 = không rõ)
    qint64 score{0};       // điểm chất lượng tổng hợp do VPN Gate tính (càng cao càng tốt) - dùng để sắp xếp
    int sessions{0};       // số phiên VPN đang kết nối tới máy chủ này (càng đông càng có thể chậm/đầy)
    /// Giây epoch của lần gần nhất THẤY máy chủ này trong danh sách tải về - dùng cho cache TÍCH LŨY
    /// (API công khai của VPN Gate chỉ trả ~100 máy chủ "tốt nhất" mỗi lần, xoay vòng theo thời gian; gộp
    /// và lưu lại qua nhiều lần tải để có nhiều máy chủ hơn, rồi tự loại máy chủ quá cũ). 0 = chưa rõ.
    qint64 lastSeenEpoch{0};

    /// Tên miền để kết nối SSTP: `<hostName>.opengw.net` - DDNS của chính máy chủ (đã xác nhận thật: phân
    /// giải đúng IP trong cột IP), khớp chứng chỉ Let's Encrypt `*.opengw.net` mà máy chủ trình ra trên cổng
    /// 443 nên Windows xác thực được. Rỗng nếu hostName không phải nhãn DNS hợp lệ (chữ/số/gạch nối, 1-63 ký
    /// tự, không bắt đầu/kết thúc bằng gạch nối) - dữ liệu đến từ mạng, sẽ thành đích VPN và đi vào lệnh
    /// PowerShell, nên lọc chặt thay vì tin.
    QString sstpHost() const
    {
        if (hostName.isEmpty() || hostName.size() > 63 || hostName.startsWith('-') || hostName.endsWith('-'))
            return {};
        for (const QChar c : hostName)
        {
            const ushort u = c.unicode();
            const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-';
            if (!ok)
                return {};
        }
        return hostName.toLower() + QStringLiteral(".opengw.net");
    }

    /// Tốc độ quy ra Mbps để hiển thị (speedBps là bit/giây).
    double speedMbps() const { return speedBps / 1000000.0; }
};
