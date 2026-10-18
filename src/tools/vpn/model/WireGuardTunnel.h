#pragma once

#include <QString>

/// Thông tin HIỂN THỊ của một tunnel WireGuard đã cài trên máy - KHÔNG BAO GIỜ chứa PrivateKey/
/// PresharedKey. Khóa riêng chỉ sống trong bộ nhớ đúng lúc gọi `wireguard.exe /installtunnelservice` rồi
/// bị CHÍNH wireguard.exe mã hóa bằng DPAPI vào `<thư mục cài WireGuard>\Data\Configurations\` - ứng dụng
/// này không đọc lại, không lưu một bản sao nào của khóa.
struct WireGuardTunnel
{
    QString name;          // tên tunnel = tên dịch vụ Windows "WireGuardTunnel$<name>" (đã chuẩn hóa)
    QString countryLabel;  // nhãn quốc gia tự đặt, vd "Hà Lan - ProtonVPN" - chỉ để hiển thị/sắp xếp
    QString address;       // Address= trong [Interface] của .conf lúc nhập - chỉ để hiển thị
    QString dns;            // DNS= trong [Interface]
    QString endpoint;      // Endpoint= trong [Peer] - địa chỉ máy chủ
    QString allowedIps;    // AllowedIPs= trong [Peer]
    bool isRunning{false}; // trạng thái dịch vụ Windows lúc đọc (RUNNING hay không)
};
