#pragma once

#include <QByteArray>
#include <QObject>
#include <QStringList>

#include <functional>

#include "Ipv6LeakGuard.h"
#include "PublicIpv6Checker.h"

class QLocalServer;
class QLocalSocket;
class QTimer;

/**
 * @brief Phía ứng dụng của "Chặn rò rỉ IPv6" cho VPN của Windows (tab Đổi IP theo quốc gia + VPN hệ thống). Xem
 * Ipv6LeakGuard.h (vì sao/cách chặn) và Ipv6GuardHelper.h (tiến trình phụ nâng quyền giữ bộ lọc).
 *
 * Luồng: gọi begin() TRƯỚC khi quay số VPN rồi chờ ready():
 *  - máy KHÔNG có IPv6 ra Internet -> ready(true) ngay, KHÔNG hỏi UAC (không có gì để lộ); vẫn dò lại mỗi vài giây
 *    suốt phiên - IPv6 xuất hiện giữa chừng (đổi WiFi, router cấp IPv6) thì tự xin quyền và chặn;
 *  - máy CÓ IPv6 ra Internet -> chạy tiến trình phụ (một hộp thoại UAC), chặn xong mới ready(true) - không có
 *    khoảnh khắc nào VPN đã nối mà IPv6 thật còn hở;
 *  - cần chặn mà không chặn được (từ chối UAC, lỗi WFP) -> ready(false, lý do): nơi gọi KHÔNG quay số.
 * release() khi người dùng ngắt / nối thất bại. Hủy đối tượng (đóng ứng dụng) KHÔNG gỡ chặn: tiến trình phụ giữ
 * chặn tới khi chính kết nối VPN ngắt (VPN của Windows không tắt theo ứng dụng).
 */
class Ipv6GuardSession : public QObject
{
    Q_OBJECT

public:
    enum class State
    {
        Idle,      ///< chưa bắt đầu / đã gỡ
        NotNeeded, ///< máy không có IPv6 ra Internet - không cần chặn (vẫn theo dõi)
        Starting,  ///< đang xin quyền / chờ tiến trình phụ đặt bộ lọc
        Active,    ///< đã chặn
        Failed     ///< cần chặn nhưng KHÔNG chặn được
    };

    struct TestHooks
    {
        std::function<QList<Ipv6LeakGuard::InterfaceInfo>()> snapshot; ///< null = đọc máy thật
        QString helperProgram;             ///< rỗng = chính ứng dụng (QCoreApplication::applicationFilePath)
        bool launchWithoutElevation{false}; ///< chạy tiến trình phụ trực tiếp, không qua UAC
        int monitorIntervalMs{3000};
        int helperTimeoutMs{30000};
        QStringList extraBlock; ///< dải chặn thêm gửi cho tiến trình phụ (CHỈ công cụ kiểm tra tay)
    };

    explicit Ipv6GuardSession(QObject* parent = nullptr);
    ~Ipv6GuardSession() override;

    void setTestHooks(const TestHooks& hooks) { m_hooks = hooks; }

    /// rasEntry: tên kết nối VPN của Windows sắp quay số. allowRemoteIpv6: địa chỉ IPv6 của máy chủ VPN (nếu có).
    void begin(const QString& rasEntry, const QStringList& allowRemoteIpv6 = {});
    void release();

    State state() const { return m_state; }
    QString statusText() const { return m_text; }
    bool isStarting() const { return m_state == State::Starting; }
    bool tunnelHasIpv6() const { return m_tunnelHasIpv6; }

    /// Dòng trạng thái cho giao diện - THUẦN, test được.
    static QString describe(State state, bool tunnelHasIpv6, const QStringList& exposedInterfaces,
                            const QString& error);

    /// Dòng "IPv6 công khai" (kết quả PublicIpv6Checker) cho giao diện - THUẦN, test được. *warn = true khi IPv6
    /// thật đang lộ trong lúc VPN đã nối (hiện màu đỏ).
    static QString describePublicIpv6(const PublicIpv6Info& info, bool vpnConnected, State guardState,
                                      bool tunnelHasIpv6, bool* warn);

signals:
    /// Kết quả của begin(). ok=false: KHÔNG được quay số (cần chặn mà không chặn được) - message nói rõ.
    void ready(bool ok, QString message);
    void stateChanged(Ipv6GuardSession::State state, QString text);

private:
    QList<Ipv6LeakGuard::InterfaceInfo> takeSnapshot() const;
    bool launchHelper(QString* error);
    void onNewConnection();
    void onReadyRead();
    void onHelperGone();
    void sendGuard();
    void setState(State state, bool tunnelHasIpv6 = false, const QStringList& exposed = {}, const QString& error = {});
    void finishReady(bool ok, const QString& message);
    void monitorTick();
    void dropHelper();

    TestHooks m_hooks;
    State m_state{State::Idle};
    bool m_tunnelHasIpv6{false};
    QString m_text;
    QString m_entry;
    QStringList m_allowRemote;
    bool m_pendingReady{false};
    bool m_verified{false};
    QByteArray m_token;
    QByteArray m_buf;
    QLocalServer* m_server{nullptr};
    QLocalSocket* m_sock{nullptr};
    QTimer* m_monitor{nullptr};
    QTimer* m_launchTimeout{nullptr};
};
