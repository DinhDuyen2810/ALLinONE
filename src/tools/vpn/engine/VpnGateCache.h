#pragma once

#include <QByteArray>
#include <QList>

#include "../model/VpnGateServer.h"

/// Cache TÍCH LŨY danh sách máy chủ VPN Gate. API công khai của VPN Gate chỉ trả về khoảng 100 máy chủ
/// "tốt nhất" mỗi lần gọi (gọi lại ngay thì gần như y hệt) - để có NHIỀU máy chủ nhất có thể từ nguồn
/// miễn phí này, ta GỘP kết quả mỗi lần tải vào một danh sách lưu trên đĩa, dedup theo IP, giữ lại qua
/// các lần chạy ứng dụng; danh sách top xoay vòng theo thời gian nên tải định kỳ sẽ tích lũy thêm máy chủ.
/// Máy chủ quá cũ (không còn xuất hiện sau một thời gian - IP của VPN Gate là động) bị loại để không phình
/// mãi và không giữ IP đã chết.
namespace VpnGateCache
{
/// Số giây coi một máy chủ là "quá cũ" nếu không thấy lại (mặc định 3 ngày) và trần số máy chủ lưu.
inline constexpr qint64 kMaxAgeSecs = 3 * 24 * 60 * 60;
inline constexpr int kMaxServers = 1000;

/// Gộp danh sách mới (`fresh`) vào danh sách hiện có (`existing`): dedup theo IP (bản MỚI thắng, cập nhật
/// các trường + đặt lastSeenEpoch = nowEpoch), loại máy chủ có lastSeenEpoch cũ hơn `maxAgeSecs`, sắp theo
/// điểm giảm dần và cắt còn tối đa `maxCount`. THUẦN (không đĩa/mạng) - test được.
QList<VpnGateServer> merge(QList<VpnGateServer> existing, const QList<VpnGateServer>& fresh, qint64 nowEpoch,
                           qint64 maxAgeSecs = kMaxAgeSecs, int maxCount = kMaxServers);

/// Nạp/ghi cache từ `%LOCALAPPDATA%\OneForAll\profiles\vpngate_servers.json` (ghi nguyên tử). Lỗi đọc = rỗng.
QList<VpnGateServer> load();
void save(const QList<VpnGateServer>& servers);

namespace internal
{
/// Chuyển đổi JSON <-> danh sách (thuần, test được).
QByteArray toJson(const QList<VpnGateServer>& servers);
QList<VpnGateServer> fromJson(const QByteArray& json);
} // namespace internal
} // namespace VpnGateCache
