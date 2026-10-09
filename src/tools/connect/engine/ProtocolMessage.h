#pragma once

#include <QByteArray>
#include <QString>

/**
 * @brief Định dạng thông điệp trao đổi giữa 2 máy đã ghép đôi.
 * Thuần Qt (QDataStream), không phụ thuộc Win32 hay mạng - test độc lập được.
 * NetworkSession chịu trách nhiệm đóng khung độ dài + mã hóa (xem CryptoSession).
 */
enum class MessageType : quint8
{
    PairRequest = 1,   // mã hóa bằng khóa suy từ mã ghép đôi - giải mã được tức là bên gửi biết đúng mã
    PairAccept = 2,    // kèm khóa dài hạn mới sinh cho phiên sau
    PairReject = 3,
    SessionHello = 4,  // bước 1 bắt tay phiên (khóa dài hạn): bên mở kết nối gửi nonce ngẫu nhiên của mình
    ControlHandoff = 5, // trao/nhận quyền điều khiển (người dùng di chuột qua biên màn hình)
    MouseMove = 6,     // dịch chuyển tương đối (độ phân giải 2 máy có thể khác nhau)
    MouseButton = 7,
    MouseWheel = 8,
    KeyEvent = 9,
    ClipboardText = 10,
    Heartbeat = 11,    // flagA=true: "hãy trả lời ngay" (dò phiên còn sống); flagA=false: nhịp định kỳ/câu trả lời
    Goodbye = 12,      // bên gửi chủ động đóng phiên (dừng Connect Together)
    SessionHelloAck = 13, // bước 2 bắt tay phiên (khóa dài hạn): nonce bên mở kết nối (lặp lại) + nonce bên nhận
    SessionConfirm = 14,  // bước 3/4: câu đầu tiên mã hóa bằng KHÓA PHIÊN - giải mã được mới coi là đã xác thực
};

struct ProtocolMessage
{
    MessageType type{MessageType::Heartbeat};

    // Dùng trường nào tùy theo 'type'; để đơn giản hóa serialize, gộp chung một struct phẳng.
    QString textA;      // PairRequest/PairAccept/SessionHello/SessionConfirm: machineName; ClipboardText: nội dung
    QString textB;      // PairRequest/PairAccept/SessionHello/SessionHelloAck/SessionConfirm: id của máy gửi
    QByteArray longTermKey; // PairAccept: khóa dài hạn mới (32 byte)
    QByteArray nonce;       // SessionHello: nonceI (16 byte); SessionHelloAck: nonceI || nonceR (32 byte)

    int intA{0};  // MouseMove: dx; MouseButton: mã nút; KeyEvent: virtual-key code; ControlHandoff: side (int ScreenSide) mà máy nhận đi vào;
                  // PairRequest/PairAccept/SessionHello/SessionHelloAck: phiên bản giao thức (ConnectProtocol::kVersion)
    int intB{0};  // MouseMove: dy; MouseWheel: deltaY; KeyEvent: scan code; ControlHandoff: vị trí dọc biên, cố định phần nghìn (0..1000);
                  // PairRequest/PairAccept/SessionHello: cổng TCP đang LẮNG NGHE của máy gửi (để kết nối lại về sau)
    bool flagA{false}; // MouseButton/KeyEvent: pressed=true/released=false; Heartbeat: yêu cầu trả lời ngay
    bool flagB{false}; // KeyEvent: extended key

    QByteArray toBytes() const;
    static bool fromBytes(const QByteArray& data, ProtocolMessage* out);
};

/**
 * @brief Hằng số + phần "mở đầu" (preamble) của giao thức Connect Together phiên bản 2.
 *
 * Khung ĐẦU TIÊN của mọi kết nối TCP là một preamble KHÔNG mã hóa do bên mở kết nối gửi: nó chỉ chứa
 * thông tin vốn đã công khai (phiên bản giao thức, mục đích kết nối, id của máy gửi - chính là id đang
 * được phát quảng bá trong PeerDiscovery), để bên nhận biết phải dùng khóa nào (khóa suy từ mã ghép đôi
 * đang mở, hay khóa dài hạn của đúng peer đó) mà KHÔNG phải đoán theo địa chỉ IP như bản 1 (đoán theo
 * gói quảng bá không xác thực - giả mạo được, và khiến việc kết nối lại phụ thuộc hoàn toàn vào UDP
 * broadcast). Preamble không cấp quyền gì: mọi thứ sau nó đều phải giải mã + xác thực được.
 */
namespace ConnectProtocol
{
constexpr int kVersion = 2;
constexpr int kNonceBytes = 16;      // nonce bắt tay của mỗi bên
constexpr int kMaxIdChars = 64;
constexpr int kMaxNameChars = 64;
constexpr int kMaxClipboardBytes = 1024 * 1024; // trần văn bản clipboard đồng bộ (tính theo byte UTF-16)

enum class LinkPurpose : quint8
{
    Pairing = 1, // tiếp theo là PairRequest mã hóa bằng khóa suy từ mã ghép đôi
    Session = 2  // tiếp theo là SessionHello mã hóa bằng khóa dài hạn của peer có id nêu trong preamble
};

/// "OFCT" | phiên bản (1 byte) | mục đích (1 byte) | độ dài id (1 byte) | id (UTF-8).
QByteArray buildPreamble(LinkPurpose purpose, const QString& senderId);

/// false nếu không đúng định dạng (vd kết nối từ bản 1, hoặc dữ liệu rác). *version luôn được điền khi
/// đọc được phần đầu, kể cả khi KHÁC kVersion - nơi gọi tự so sánh để báo "khác phiên bản" cho rõ.
bool parsePreamble(const QByteArray& data, int* version, LinkPurpose* purpose, QString* senderId);

/// id hợp lệ: 1..kMaxIdChars ký tự ASCII thuộc [A-Za-z0-9._-] (id thật là một UUID không ngoặc).
bool isValidPeerId(const QString& id);

/// Tên máy do máy kia tự khai: cắt còn kMaxNameChars, bỏ ký tự điều khiển; rỗng -> "(không tên)".
QString sanitizeMachineName(const QString& name);
} // namespace ConnectProtocol
