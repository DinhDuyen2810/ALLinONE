#pragma once

#include <QString>
#include <QThread>
#include <atomic>

/**
 * @brief Kết nối/ngắt kết nối một VPN đã khai báo trong Windows qua RAS API (RasDialW/RasHangUpW trong
 * rasapi32 - chính API mà rasdial.exe và trang Cài đặt > VPN của Windows dùng), trên QThread riêng vì có
 * thể mất vài giây (bắt tay giao thức VPN qua mạng) - không được chặn giao diện.
 *
 * Trước đây lớp này chạy `rasdial.exe <tên> <người dùng> <mật khẩu>`: mật khẩu nằm NGUYÊN VĂN trên dòng
 * lệnh của tiến trình con - mọi tiến trình cùng người dùng (và mọi công cụ ghi nhật ký tạo tiến trình
 * như audit 4688/Sysmon/EDR) đều đọc được, và tên/tên đăng nhập bắt đầu bằng "/" bị rasdial hiểu thành
 * tham số (vd /PHONEBOOK:). Gọi thẳng RAS API thì mật khẩu chỉ đi qua bộ nhớ của CHÍNH tiến trình này và
 * được xóa trắng ngay sau khi dùng; không còn dòng lệnh nào để chèn tham số.
 *
 * Mật khẩu KHÔNG được ứng dụng này ghi ra đĩa/log dưới bất kỳ hình thức nào.
 *
 * Hỗ trợ HỦY GIỮA CHỪNG qua requestCancel() - bắt buộc phải có: bắt tay VPN có thể mất tới 45 giây với
 * máy chủ chậm, nhưng nơi gọi (VpnTab) chỉ có thể chờ một khoảng ngắn khi đóng cửa sổ trước khi phải hủy
 * đối tượng QThread này - hủy một QThread đang thực sự chạy là hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt,
 * lỗi này đã được chủ động phát hiện khi tự rà soát toàn bộ ứng dụng. RasDialW được gọi ở chế độ KHÔNG
 * ĐỒNG BỘ (trả về ngay) rồi luồng này tự thăm dò trạng thái mỗi 200ms, nên vẫn hủy/hết giờ được như cũ.
 */
class VpnConnector : public QThread
{
    Q_OBJECT

public:
    explicit VpnConnector(QObject* parent = nullptr);
    ~VpnConnector() override;

    /// username rỗng = dùng thông tin đăng nhập Windows đã nhớ sẵn cho kết nối này (hoặc xác thực bằng
    /// chứng chỉ máy đã cấu hình sẵn). password rỗng hợp lệ. Gọi TRƯỚC start(); cũng đặt lại cờ hủy.
    void setConnectTarget(const QString& connectionName, const QString& username, const QString& password);
    void setDisconnectTarget(const QString& connectionName);

    /// Hủy giữa chừng (an toàn - gác máy phiên RAS đang quay số, không để lại trạng thái dở dang nguy
    /// hiểm vì VPN chỉ là "đã kết nối" hoặc "chưa kết nối", không có trạng thái trung gian cần lo).
    void requestCancel() { m_cancelRequested = true; }

    /// Thông báo tiếng Việt cho một mã lỗi RAS/Win32 (623 = không có hồ sơ, 691 = sai tài khoản...), kèm
    /// mã số + mô tả gốc của Windows để tra cứu. Thuần (chỉ đọc bảng thông báo hệ thống) - test được.
    static QString describeRasError(quint32 code);

    /// Giá trị RASCONNSTATE của hai trạng thái KẾT THÚC (RASCS_Connected / RASCS_Disconnected trong ras.h).
    static constexpr quint32 kRasStateConnected = 0x2000;
    static constexpr quint32 kRasStateDisconnected = 0x2001;

    /// Những gì đọc được ở MỘT nhịp thăm dò của vòng chờ quay số.
    struct DialPoll
    {
        bool callbackConnected{false};    ///< hàm gọi lại của RasDial đã báo RASCS_Connected cho phiên này
        bool callbackDisconnected{false}; ///< hàm gọi lại đã báo RASCS_Disconnected cho phiên này
        quint32 callbackError{0};         ///< mã lỗi hàm gọi lại báo (0 = chưa có)
        quint32 statusResult{0};          ///< giá trị trả về của RasGetConnectStatusW (0 = đọc được)
        quint32 connState{0};             ///< RASCONNSTATUS::rasconnstate - chỉ có nghĩa khi statusResult == 0
        quint32 statusError{0};           ///< RASCONNSTATUS::dwError - chỉ có nghĩa khi statusResult == 0
    };
    enum class DialOutcome { Pending, Connected, Failed };

    /// Quyết định của vòng chờ cho một nhịp thăm dò; khi Failed thì *failureMessage là thông báo cho người
    /// dùng. Thuần (không gọi RAS) - test được. RASCS_Disconnected KHÔNG kèm mã lỗi cũng là thất bại ngay:
    /// đó là trạng thái kết thúc, trước đây vòng chờ bỏ qua nó và đứng tới hết 45 giây rồi báo "hết thời
    /// gian chờ" cho một phiên đã bị ngắt từ giây đầu.
    static DialOutcome evaluateDialPoll(const DialPoll& poll, QString* failureMessage);

signals:
    // Đặt tên khác "finished" vì QThread đã có sẵn signal finished() không tham số - trùng tên sẽ gây
    // xung đột overload (đã gặp lỗi này thật khi xây CleanupExecutor, chủ động tránh lại ở đây).
    void operationFinished(bool success, QString message);

protected:
    void run() override;

private:
    bool doConnect(QString& password, QString* message);
    bool doDisconnect(QString* message);

    enum class Mode { Connect, Disconnect } m_mode{Mode::Connect};
    QString m_connectionName;
    QString m_username;
    QString m_password; // chỉ sống từ setConnectTarget() tới đầu run(), rồi bị xóa trắng
    std::atomic_bool m_cancelRequested{false};
};
