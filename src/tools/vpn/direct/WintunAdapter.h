#pragma once

#include "DirectVpnTypes.h"

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

/// Adapter mạng ảo dùng wintun.dll (driver TUN tầng 3 của dự án WireGuard, ký số bởi WireGuard LLC) - đóng gói
/// kèm ở `<thư mục chạy>/wintun/wintun.dll`. Nạp ĐỘNG theo đường dẫn tuyệt đối (không dò PATH). Tạo adapter
/// cần quyền Administrator nên chỉ dùng trong tiến trình phụ nâng quyền (xem VpnHelperMain).
class WintunAdapter
{
public:
    WintunAdapter() = default;
    ~WintunAdapter();
    WintunAdapter(const WintunAdapter&) = delete;
    WintunAdapter& operator=(const WintunAdapter&) = delete;

    static QString dllPath();
    static bool isAvailable();

    /// Tạo adapter tên `name` (xóa adapter cũ cùng tên nếu còn sót) và mở phiên vòng đệm.
    bool open(const QString& name, QString* error);
    /// Dừng luồng đọc, đóng phiên, xóa adapter (tuyến/IP gắn trên adapter biến mất theo).
    void close();
    bool isOpen() const { return m_session != nullptr; }

    quint64 luid() const { return m_luid; }

    /// Gửi một gói IP vào máy (từ đường hầm về). An toàn gọi từ luồng khác luồng đọc.
    bool sendPacket(const QByteArray& ip);
    /// Bắt đầu luồng đọc gói máy gửi ra adapter; onPacket chạy TRÊN LUỒNG ĐỌC (nơi gọi tự chuyển luồng).
    void startReader(std::function<void(QByteArray)> onPacket);

private:
    void readerLoop();

    void* m_dll{nullptr};
    void* m_adapter{nullptr};
    void* m_session{nullptr};
    void* m_quitEvent{nullptr};
    quint64 m_luid{0};
    std::thread m_reader;
    std::atomic_bool m_stop{false};
    std::function<void(QByteArray)> m_onPacket;

    struct Fns; // con trỏ hàm wintun.dll đúng chữ ký WINAPI trong wintun.h - định nghĩa trong .cpp
    Fns* m_fn{nullptr};
};

/// Cấu hình IP/tuyến/DNS cho adapter qua iphlpapi (không gọi netsh/route.exe). Ghi lại MỌI tuyến đã thêm
/// để gỡ khi ngắt - kể cả tuyến /32 tới máy chủ VPN qua cổng mạng GỐC (bắt buộc với đường hầm toàn phần,
/// nếu không gói tin của chính đường hầm sẽ bị định tuyến vào đường hầm -> vòng lặp, mất kết nối).
class NetConfigurator
{
public:
    ~NetConfigurator();

    /// Ghi nhớ đường đi hiện tại tới máy chủ VPN (giao diện + next hop) - gọi TRƯỚC khi thêm tuyến mặc định.
    bool captureUnderlay(quint32 serverIp, QString* error);
    bool apply(quint64 adapterLuid, const TunnelConfig& cfg, quint32 serverIp, int mtu, QString* error);
    /// Gỡ mọi tuyến đã thêm (adapter bị xóa sẽ tự mất IP/tuyến gắn trên nó).
    void revert();

private:
    bool addRoute(quint64 luid, const IpRoute& dest, quint32 nextHop, quint32 metric, QString* error);

    struct AddedRoute
    {
        quint64 luid;
        IpRoute dest;
        quint32 nextHop;
    };
    QList<AddedRoute> m_added;
    quint64 m_underlayLuid{0};
    quint32 m_underlayNextHop{0};
    bool m_haveUnderlay{false};
};
