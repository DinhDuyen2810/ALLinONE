#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <array>

/// Chặn rò rỉ IPv6 khi "đổi IP" bằng VPN (tab "Đổi IP theo quốc gia" + "VPN hệ thống").
///
/// VẤN ĐỀ: máy chủ VPN Gate (và đa số VPN hệ thống) chỉ cấp IPv4 cho đường hầm. Windows vẫn giữ IPv6 thật
/// của card mạng (nhà mạng Việt Nam cấp IPv6 rộng rãi) cùng tuyến mặc định ::/0 qua card đó - trình duyệt ưu
/// tiên IPv6 cho mọi trang hỗ trợ (Google, Facebook, YouTube, Cloudflare...) nên trang web thấy IPv6 THẬT ở Việt
/// Nam dù IPv4 đã "ở Nhật". Chỉ đổi được IPv4 là đổi nửa vời.
///
/// CÁCH CHẶN: bộ lọc Windows Filtering Platform (WFP) trong một phiên ĐỘNG (FWPM_SESSION_FLAG_DYNAMIC): chặn
/// mọi kết nối IPv6 tới/từ địa chỉ Internet (2000::/3 + NAT64) ở hai tầng ALE (kết nối ra / chấp nhận vào),
/// trừ qua chính card đường hầm (khi VPN có cấp IPv6 - lúc đó IPv6 cũng "đổi" theo VPN) và trừ địa chỉ IPv6
/// của chính máy chủ VPN (đường truyền của đường hầm). Ứng dụng thấy kết nối IPv6 bị từ chối NGAY (không chờ
/// hết giờ) nên tự quay về IPv4 - IPv4 đi qua VPN. Thêm bộ lọc ở tầng ALE làm Windows "tái xét" luôn các
/// kết nối IPv6 ĐANG mở ở gói kế tiếp (ALE reauthorization - tài liệu Microsoft) nên kết nối cũ cũng bị cắt.
///
/// AN TOÀN: phiên động nghĩa là Windows (dịch vụ BFE) TỰ XÓA mọi bộ lọc khi phiên đóng - kể cả khi tiến trình
/// giữ phiên bị tắt cưỡng bức/crash. Không có trạng thái nào còn sót lại trên máy (khác hẳn sửa tường lửa,
/// tắt IPv6 trên card mạng hay sửa bảng định tuyến - những cách đó để lại cấu hình hỏng nếu ứng dụng chết
/// giữa chừng). Không đụng IPv4, loopback (::1), link-local (fe80::/10), ULA (fc00::/7 - vd Tailscale).
///
/// Thêm bộ lọc WFP cần quyền Administrator - phần nâng quyền nằm ở Ipv6GuardHelper/Ipv6GuardSession.
namespace Ipv6LeakGuard
{
using Addr16 = std::array<quint8, 16>;

struct Prefix
{
    Addr16 addr{};
    int length{0};
    bool operator==(const Prefix&) const = default;
};

/// Dải địa chỉ IPv6 ĐÍCH bị chặn: 2000::/3 (toàn bộ unicast toàn cầu = mọi IPv6 ra Internet, gồm cả Teredo
/// 2001::/32 và 6to4 2002::/16), 64:ff9b::/96 và 64:ff9b:1::/48 (NAT64 - trên mạng chỉ có IPv6, trang IPv4 được
/// truy cập qua các địa chỉ này và cũng lộ IPv6 thật).
QList<Prefix> blockedPrefixes();
bool inPrefix(const Addr16& address, const Prefix& prefix);
/// 2000::/3
bool isGlobalUnicast(const Addr16& address);
/// Nằm trong một dải của blockedPrefixes().
bool isBlockedDestination(const Addr16& address);
/// Chuỗi IPv6 (không nhận IPv4, kể cả dạng ::ffff:a.b.c.d vẫn nhận vì là IPv6 hợp lệ) -> 16 byte.
bool parseAddress(const QString& text, Addr16* out);
QString formatAddress(const Addr16& address);
/// "2000::/3"; false nếu sai cú pháp hoặc độ dài ngoài 0..128.
bool parsePrefix(const QString& text, Prefix* out);

/// Ảnh chụp một card mạng (chỉ phần IPv6 cần cho việc đánh giá).
struct InterfaceInfo
{
    quint64 luid{0};
    QString alias;      ///< FriendlyName - với VPN của Windows (RAS) chính là TÊN KẾT NỐI
    quint32 ifType{0};  ///< IF_TYPE_* (23 = PPP - VPN của Windows, 24 = loopback)
    bool up{false};
    QList<Addr16> globalAddresses; ///< địa chỉ unicast toàn cầu (2000::/3) đang dùng được
    /// Có tuyến bao trùm IPv6 Internet trên card này (::/0, ::/1, ::/2 hoặc 2000::/3 - dạng "tuyến mặc định").
    bool hasDefaultRoute{false};
};

enum class Exposure
{
    NoIpv6, ///< không card nào (ngoài đường hầm) vừa có IPv6 toàn cầu vừa có tuyến mặc định IPv6 - không có gì để lộ
    Exposed ///< có card mạng thật ra được Internet bằng IPv6 - không chặn thì IPv6 thật bị lộ
};

struct Assessment
{
    Exposure exposure{Exposure::NoIpv6};
    QStringList exposedInterfaces; ///< tên các card ra được Internet bằng IPv6 (không tính đường hầm)
    quint64 tunnelLuid{0};         ///< LUID card đường hầm (0 = chưa thấy/chưa nối)
    bool tunnelHasIpv6{false};     ///< đường hầm có IPv6 toàn cầu + tuyến mặc định IPv6 (VPN cấp IPv6)
};

/// THUẦN - test được bằng dữ liệu dựng sẵn. tunnelAlias: tên kết nối VPN của Windows (rỗng = chưa có).
/// Card loopback và card đang không hoạt động bị bỏ qua.
Assessment assess(const QList<InterfaceInfo>& interfaces, const QString& tunnelAlias);

/// Đọc trạng thái IPv6 THẬT của máy (GetAdaptersAddresses + GetIpForwardTable2) - chỉ đọc, không cần Admin.
QList<InterfaceInfo> snapshot(QString* error = nullptr);

/// Mô tả mã lỗi Win32/WFP bằng tiếng Việt kèm mã gốc.
QString describeError(quint32 code);

/**
 * @brief Bộ lọc WFP chặn IPv6 ra Internet trong một phiên ĐỘNG - xem đầu tệp. Hủy đối tượng (hoặc tiến trình
 * chết) = phiên đóng = Windows tự gỡ mọi bộ lọc. apply() gọi lại được để đổi cấu hình (vd đường hầm vừa lên
 * và có IPv6) - thay toàn bộ bộ lọc trong MỘT giao dịch WFP nên không có khoảnh khắc nào "hở".
 */
class WfpBlock
{
public:
    struct Config
    {
        quint64 allowInterfaceLuid{0}; ///< card được phép đi IPv6 (đường hầm VPN), 0 = không có
        QList<Addr16> allowRemote;     ///< địa chỉ IPv6 của chính máy chủ VPN (đường truyền của đường hầm)
        /// CHỈ cho công cụ kiểm tra tay: chặn thêm dải này (vd ::1/128 để thử cơ chế chặn trên máy không có IPv6).
        QList<Prefix> extraBlockedForTest;
        bool operator==(const Config&) const = default;
    };

    WfpBlock() = default;
    ~WfpBlock();
    WfpBlock(const WfpBlock&) = delete;
    WfpBlock& operator=(const WfpBlock&) = delete;

    /// Mở phiên (lần đầu) rồi đặt bộ lọc theo cfg. Không phải Administrator -> false + *error nói rõ.
    bool apply(const Config& cfg, QString* error);
    /// Đóng phiên = gỡ mọi bộ lọc (Windows tự làm).
    void close();
    bool isActive() const { return m_engine != nullptr && !m_filterIds.isEmpty(); }
    int filterCount() const { return static_cast<int>(m_filterIds.size()); }
    const Config& config() const { return m_cfg; }

private:
    void* m_engine{nullptr};
    QList<quint64> m_filterIds;
    bool m_sublayerAdded{false};
    std::array<quint8, 16> m_sublayerKey{};
    Config m_cfg;
};

} // namespace Ipv6LeakGuard
