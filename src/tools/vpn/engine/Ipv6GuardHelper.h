#pragma once

#include <QtGlobal>

/// Tiến trình phụ NÂNG QUYỀN giữ bộ lọc chặn rò rỉ IPv6 (Ipv6LeakGuard::WfpBlock) trong suốt phiên VPN:
///   OneForAll.exe --ipv6-guard <tên kênh điều khiển> <mã xác thực>
/// Ứng dụng chính chạy KHÔNG quyền Admin; thêm bộ lọc WFP thì cần - nên chỉ tiến trình nhỏ này được nâng quyền
/// (một hộp thoại UAC lúc bấm Kết nối), giống "--vpn-helper" của VPN trực tiếp.
///
/// VÒNG ĐỜI (quan trọng - quyết định IPv6 có lộ hay không):
/// - Giữ chặn chừng nào ứng dụng còn nối kênh điều khiển, HOẶC kết nối VPN của Windows (tên RAS) vẫn còn. Đóng
///   ứng dụng mà VPN vẫn nối (VPN của Windows không tắt theo ứng dụng) thì IPv6 VẪN bị chặn tới khi VPN ngắt.
/// - Ứng dụng báo "release" (người dùng bấm Ngắt / nối thất bại) -> gỡ chặn và thoát ngay.
/// - Mất ứng dụng + VPN không còn (liên tục kGoneGraceMs) -> thoát. Tiến trình này chết vì bất cứ lý do gì thì
///   Windows tự gỡ bộ lọc (phiên WFP động) - không có trạng thái nào sót lại trên máy.
/// - Mỗi giây dò lại card đường hầm: VPN vừa lên (hoặc nối lại) với card mới thì cập nhật bộ lọc cho phép IPv6 qua
///   đúng card đó (khi VPN có cấp IPv6).
namespace Ipv6GuardHelper
{
constexpr const char* kSwitch = "--ipv6-guard";
/// Mất cả ứng dụng lẫn kết nối VPN liên tục bấy nhiêu ms thì thoát (chịu được một nhịp dò RAS lỡ).
constexpr qint64 kGoneGraceMs = 3000;

int run(int argc, char** argv);

namespace internal
{
/// THUẦN - test được. released: ứng dụng đã báo gỡ chặn. appConnected: kênh điều khiển còn. msTunnelAbsent: kết nối
/// VPN (tên RAS) đã vắng mặt liên tục bao lâu (0 = đang có).
bool shouldKeepGuarding(bool released, bool appConnected, qint64 msTunnelAbsent);
} // namespace internal
} // namespace Ipv6GuardHelper
