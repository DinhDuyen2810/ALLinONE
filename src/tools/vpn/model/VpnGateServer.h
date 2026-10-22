#pragma once

#include <QString>

/// Một máy chủ relay công cộng của VPN Gate (dự án nghiên cứu học thuật của Đại học Tsukuba, Nhật Bản -
/// https://www.vpngate.net). Đây là các máy chủ do TÌNH NGUYỆN VIÊN trên khắp thế giới chia sẻ, KHÔNG
/// phải máy chủ của ứng dụng này (ứng dụng không vận hành máy chủ VPN nào) - danh sách được tải từ API
/// công khai mà VPN Gate cung cấp sẵn cho phần mềm khách bên thứ ba. IP của các máy chủ này là ĐỘNG (đổi
/// theo thời gian) và tốc độ/độ ổn định không được bảo đảm; người dùng tự chịu trách nhiệm khi sử dụng.
///
/// Chỉ giữ các trường cần để HIỂN THỊ và KẾT NỐI (L2TP/IPsec qua RAS có sẵn của Windows). Cố ý KHÔNG giữ
/// cấu hình OpenVPN base64 (cột cuối của CSV) - bản này kết nối bằng L2TP, không nhúng máy khách OpenVPN.
struct VpnGateServer
{
    QString hostName;      // vd "vpn123456789" - tên máy chủ do VPN Gate đặt
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

    /// Tốc độ quy ra Mbps để hiển thị (speedBps là bit/giây).
    double speedMbps() const { return speedBps / 1000000.0; }
};
