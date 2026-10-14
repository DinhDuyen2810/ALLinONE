#pragma once

#include <QByteArray>
#include <QDeadlineTimer>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QTcpSocket>

#include <atomic>

/// Máy khách Chrome DevTools Protocol (CDP) cho Edge/Chrome chạy với `--remote-debugging-port`, viết trên QTcpSocket
/// (WebSocket RFC 6455 tối giản) - KHÔNG cần module Qt WebSockets (bộ Qt MinGW của dự án không có) hay QtWebEngine.
///
/// MỌI hàm ở đây CHẶN luồng gọi (dành cho luồng worker, xem DriveViewerCapture), nhưng chia nhỏ thời gian chờ thành lát
/// 200 ms để kiểm tra cờ hủy `cancel` - nên hủy có hiệu lực trong chưa tới nửa giây. Không dùng trên luồng giao diện.
/// Đối tượng phải được tạo và dùng trong CÙNG MỘT luồng (QTcpSocket gắn với luồng tạo ra nó).
class CdpClient
{
public:
    enum class Status
    {
        Ok,
        Failed,       ///< Lệnh bị từ chối / script trong trang ném lỗi - kết nối vẫn tốt, có thể gọi tiếp
        Disconnected, ///< Trình duyệt đóng kết nối / không kết nối được
        TimedOut,
        Canceled
    };

    /// `cancel` có thể là nullptr (không bao giờ hủy).
    explicit CdpClient(const std::atomic_bool* cancel = nullptr);

    /// GET http://127.0.0.1:<port><path> (HTTP/1.1, Connection: close). Dùng lấy `/json/list` - danh sách tab và địa chỉ WebSocket.
    Status httpGetLocal(quint16 port, const QByteArray& path, int timeoutMs, QByteArray* body);

    /// Bắt tay WebSocket tới ws://127.0.0.1:<port><wsPath> (vd `/devtools/page/<id>`).
    Status connectTo(quint16 port, const QString& wsPath, int timeoutMs);

    /// Gửi lệnh CDP và đợi phản hồi CÙNG id (bỏ qua các sự kiện chen giữa). Phản hồi `error` -> Failed.
    Status call(const QString& method, const QJsonObject& params, int timeoutMs, QJsonObject* result = nullptr);

    /// `Runtime.evaluate` (awaitPromise + returnByValue): chạy biểu thức JS trong trang, trả giá trị JSON. Script ném lỗi -> Failed.
    Status evaluate(const QString& expression, int timeoutMs, QJsonValue* value = nullptr);

    /// Mô tả lỗi của lần gọi thất bại gần nhất (đọc được cho người dùng/nhật ký).
    QString error() const { return m_error; }

    void close();

    // ---- Phần thuần, test được không cần trình duyệt ----
    /// Một khung WebSocket TỪ MÁY KHÁCH (bắt buộc che bằng `mask` 4 byte).
    static QByteArray encodeClientFrame(quint8 opcode, const QByteArray& payload, const char mask[4]);

private:
    bool canceled() const { return m_cancel && m_cancel->load(); }
    Status fill(const QDeadlineTimer& deadline);
    Status take(qsizetype n, const QDeadlineTimer& deadline, QByteArray* out);
    Status readMessage(const QDeadlineTimer& deadline, QByteArray* message);
    Status sendFrame(quint8 opcode, const QByteArray& payload);
    Status fail(Status status, const QString& text);

    const std::atomic_bool* m_cancel{nullptr};
    QTcpSocket m_sock;
    QByteArray m_buf;
    QString m_error;
    int m_nextId{0};
};
