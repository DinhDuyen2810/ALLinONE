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
    PairRequest = 1,   // kèm proof theo khóa suy ra từ mã ghép đôi
    PairAccept = 2,    // kèm khóa dài hạn mới sinh cho phiên sau
    PairReject = 3,
    SessionHello = 4,  // câu đầu tiên của một phiên dùng khóa dài hạn đã lưu - giải mã được tức là xác thực hợp lệ
    ControlHandoff = 5, // trao/nhận quyền điều khiển (người dùng di chuột qua biên màn hình)
    MouseMove = 6,     // dịch chuyển tương đối (độ phân giải 2 máy có thể khác nhau)
    MouseButton = 7,
    MouseWheel = 8,
    KeyEvent = 9,
    ClipboardText = 10,
    Heartbeat = 11,
    Goodbye = 12,
};

struct ProtocolMessage
{
    MessageType type{MessageType::Heartbeat};

    // Dùng trường nào tùy theo 'type'; để đơn giản hóa serialize, gộp chung một struct phẳng.
    QString textA;      // PairRequest/PairAccept/SessionHello: machineName; ClipboardText: nội dung
    QString textB;      // PairRequest/PairAccept: id của máy gửi
    QByteArray longTermKey; // PairAccept: khóa dài hạn mới (32 byte)

    int intA{0};  // MouseMove: dx; MouseButton: mã nút; KeyEvent: virtual-key code; ControlHandoff: side (int ScreenSide) mà máy nhận đi vào
    int intB{0};  // MouseMove: dy; MouseWheel: deltaY; KeyEvent: scan code; ControlHandoff: vị trí dọc biên, cố định phần nghìn (0..1000)
    bool flagA{false}; // MouseButton/KeyEvent: pressed=true/released=false; KeyEvent: extended key
    bool flagB{false};

    QByteArray toBytes() const;
    static bool fromBytes(const QByteArray& data, ProtocolMessage* out);
};
