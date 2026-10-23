#pragma once

#include "GlobalProtectProtocol.h"

#include <QByteArray>
#include <QHostAddress>
#include <QObject>

#include <functional>

class QUdpSocket;

/// Mã hóa/giải mã gói ESP (IPsec, RFC 4303) cho kênh dữ liệu GlobalProtect - dùng BCrypt của Windows
/// (AES-128/256-CBC + HMAC-SHA1-96/SHA256-128/MD5-96). Khóa + SPI do gateway cấp sẵn trong cấu hình (không IKE).
/// THUẦN về logic gói (không mạng) nên test được bằng vòng seal->open với khóa khớp.
///
/// Khuôn gói trên dây: [SPI 4][seq 4][IV 16][ciphertext][auth trunc]. ciphertext giải mã ra
/// [payload][padding 1..n][pad_len 1][next_header 1]. HMAC tính trên [SPI..ciphertext], cắt ngắn.
class GpEspCrypto
{
public:
    GpEspCrypto() = default;
    ~GpEspCrypto();
    GpEspCrypto(const GpEspCrypto&) = delete;
    GpEspCrypto& operator=(const GpEspCrypto&) = delete;

    /// Nạp khóa/SPI/thuật toán. false nếu thuật toán không hỗ trợ hoặc độ dài khóa sai.
    bool init(const GlobalProtectProtocol::EspConfig& cfg, QString* error);

    /// Bọc một payload (gói IP trong đường hầm) thành gói ESP gửi đi. nextHeader = 4 (IP-in-IP).
    QByteArray seal(const QByteArray& payload, quint8 nextHeader = 4);
    /// Mở một gói ESP nhận được -> *payload (gói IP bên trong), *nextHeader. false nếu SPI sai/HMAC sai/đệm hỏng.
    bool open(const QByteArray& esp, QByteArray* payload, quint8* nextHeader);

    quint32 spiOut() const { return m_spiOut; }
    quint32 spiIn() const { return m_spiIn; }
    int hmacLen() const { return m_hmacLen; }
    bool ready() const { return m_ready; }

private:
    QByteArray hmac(bool out, const QByteArray& data) const;
    QByteArray aes(bool out, bool encrypt, const QByteArray& iv, const QByteArray& in) const;

    void* m_aesAlg{nullptr};
    void* m_encKeyOut{nullptr};
    void* m_encKeyIn{nullptr};
    void* m_hmacAlg{nullptr};
    QByteArray m_hmacKeyOut, m_hmacKeyIn;
    QByteArray m_encKeyOutBytes, m_encKeyInBytes; // BCrypt cần giữ bộ đệm khóa còn sống
    void* m_aesKeyObjOut{nullptr};
    void* m_aesKeyObjIn{nullptr};

    quint32 m_spiOut{0}; // thứ tự byte MẠNG (đặt lên dây trực tiếp)
    quint32 m_spiIn{0};
    quint32 m_seqOut{0};
    int m_hmacLen{12};
    static constexpr int kIvLen = 16;
    static constexpr int kBlock = 16;
    bool m_ready{false};
};

/// Kênh dữ liệu ESP của GlobalProtect: mở socket UDP tới gateway:udp-port, gửi gói "ping" dò (ICMP echo mang
/// payload magic của GP tới địa chỉ gw-address) để gateway bắt đầu gửi; khi nhận được ESP hợp lệ -> established.
/// Sau đó: gói IP từ máy -> seal -> UDP; UDP nhận -> open -> gói IP về máy.
class GpEspChannel : public QObject
{
    Q_OBJECT

public:
    explicit GpEspChannel(QObject* parent = nullptr);

    struct Callbacks
    {
        std::function<void(const QByteArray& ipPacket)> ipReceived;
        std::function<void()> established; ///< nhận được phản hồi ESP hợp lệ đầu tiên
        std::function<void()> activity;    ///< MỖI gói ESP hợp lệ nhận được (kể cả phản hồi dò) - để chống "mất kết nối"
        std::function<void(const QString& m)> log;
    };

    /// clientIpv4/gateway thứ tự byte máy. Mở UDP + bắt đầu gửi dò. false + *error nếu lỗi nạp khóa/socket.
    bool start(const GlobalProtectProtocol::EspConfig& cfg, quint32 clientIpv4, const QHostAddress& gateway,
               Callbacks cb, QString* error);
    void stop();
    bool isEstablished() const { return m_established; }

    /// Gửi một gói IPv4 (từ adapter) qua ESP.
    bool sendIp(const QByteArray& ipPacket);
    /// Gửi lại gói dò (DPD/keepalive) - gọi định kỳ.
    void sendProbe();

    // ---- Thuần (test được) ----
    /// Dựng gói IPv4/ICMP-echo "ping" dò của GlobalProtect (payload magic bắt buộc để gateway phản hồi).
    static QByteArray buildProbePacket(quint32 srcIp, quint32 dstMagic, quint16 seq);
    /// Nhận diện phản hồi dò: gói IPv4/ICMP-echo-reply từ địa chỉ magic, mang đúng payload magic.
    static bool isProbeReply(const QByteArray& ipPacket, quint32 magic);

private:
    void onDatagrams();

    QUdpSocket* m_sock{nullptr};
    GpEspCrypto m_crypto;
    Callbacks m_cb;
    QHostAddress m_gateway;
    quint16 m_port{4501};
    quint32 m_clientIp{0};
    quint32 m_magic{0};
    quint16 m_probeSeq{0};
    bool m_established{false};
};
