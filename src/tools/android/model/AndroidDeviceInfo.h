#pragma once

#include <QString>

/// Một thiết bị Android mà adb nhìn thấy (qua USB hoặc qua mạng/Wi-Fi) - phân giải từ dòng xuất của
/// `adb devices -l`. Không có gì tự ý tạo ra ở đây - đây CHÍNH LÀ danh sách adb thật đang thấy.
struct AndroidDeviceInfo
{
    QString serial;      // "R58M80ABCDE" (USB) hoặc "192.168.1.23:5555" (không dây)
    QString state;       // "device" (sẵn sàng), "unauthorized", "offline", "no permissions"...
    QString model;       // vd "Pixel_7" - rỗng nếu adb chưa đọc được (thường khi unauthorized/offline)
    QString product;
    QString transportId;

    bool isWireless() const { return serial.contains(':'); } // dạng IP:Cổng = đang kết nối qua mạng
    bool isReady() const { return state == "device"; }       // sẵn sàng điều khiển (đã cấp quyền gỡ lỗi USB)
    bool isUnauthorized() const { return state == "unauthorized"; } // cần bấm "Cho phép" trên điện thoại
};
