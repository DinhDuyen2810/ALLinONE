#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

class QTcpSocket;
class QTimer;

/// Kiểm tra "có chạm được tới một máy chủ hay không" bằng cách MỞ MỘT KẾT NỐI TCP tới host:port và đo thời
/// gian tới khi kết nối được (hoặc lỗi/hết giờ). Thuần Qt Network (QTcpSocket) - không cần quyền
/// Administrator, không gửi/nhận dữ liệu gì sau khi bắt tay TCP (đóng ngay), không dùng ICMP raw.
///
/// Lưu ý: VPN L2TP/IPsec chạy trên UDP (500/4500/1701) nên không "kết nối TCP" tới cổng đó được - phép thử
/// TCP này là CHỈ BÁO máy chủ còn sống/đường mạng thông (thường thử cổng 443 mà nhiều máy chủ VPN Gate có
/// bật), không khẳng định chắc chắn L2TP sẽ kết nối được. Dùng để lọc nhanh các máy chủ đã chết.
class ConnectivityTester : public QObject
{
    Q_OBJECT

public:
    explicit ConnectivityTester(QObject* parent = nullptr);
    ~ConnectivityTester() override;

    /// Bắt đầu một phép thử (bất đồng bộ). Gọi lại khi đang chạy sẽ hủy phép thử trước. Kết quả qua finished().
    void test(const QString& host, quint16 port, int timeoutMs = 5000);
    void cancel();
    bool isRunning() const;

signals:
    /// reachable=true: đã mở được kết nối TCP trong `elapsedMs` ms. reachable=false: `detail` là lý do.
    void finished(bool reachable, int elapsedMs, QString detail);

private:
    void cleanup();

    QPointer<QTcpSocket> m_socket;
    QTimer* m_timeout{nullptr};
    QElapsedTimer m_clock;
    bool m_done{true};
};
