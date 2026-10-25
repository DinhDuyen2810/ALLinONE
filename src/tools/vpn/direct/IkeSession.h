#pragma once

#include "GlobalProtectProtocol.h" // EspConfig (tái dùng cho tầng dữ liệu ESP)
#include "IkeCrypto.h"
#include "IkeKeys.h"
#include "IsakmpMessage.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>

#include <functional>
#include <memory>

/// Máy trạng thái IKEv1 Aggressive + PSK + XAuth + Mode-config + Quick Mode cho FortiGate IPsec (VPN FortiClient
/// kiểu "IPsec"). KHÔNG tự mở socket: phát sendDatagram() để tầng truyền tải (UDP 500/4500, NAT-T) gửi đi, và
/// nhận bản tin ISAKMP đã bóc dấu qua onDatagram(). Kết quả: IP/DNS/tuyến được cấp + khóa/SPI ESP (đóng gói trong
/// EspConfig để dùng lại GpEspCrypto). Crypto thuần (IkeCrypto/IkeKeys/IsakmpMessage) nên phần lớn test được bằng
/// một responder giả. LƯU Ý: luồng FortiGate thật có nhiều biến thể (VID, kiểu ID, thứ tự XAuth/Mode-cfg) chỉ kiểm
/// chứng được với máy chủ thật - phiên bản này ghi log kỹ để dò.
class IkeSession : public QObject
{
    Q_OBJECT

public:
    struct Config
    {
        quint32 gatewayIp{0};   ///< IPv4 cổng (thứ tự byte máy)
        QByteArray psk;         ///< khóa chia sẻ trước
        QString xauthUser;
        QString xauthPass;
        quint8 localIdType{Isakmp::ID_IPV4_ADDR}; ///< kiểu IDii; nếu KEY_ID/FQDN thì localId là chuỗi
        QByteArray localId;     ///< dữ liệu IDii (rỗng + ID_IPV4_ADDR => dùng 0.0.0.0)
        IkeCrypto::DhGroup dhGroup{IkeCrypto::DhGroup::Modp2048};
        int encKeyBits{256};    ///< AES-128/256
        IkeCrypto::Hash hash{IkeCrypto::Hash::Sha256}; ///< prf + hash Phase 1
        bool natT{true};        ///< ép đóng gói UDP-4500 (cần khi sau NAT - gần như luôn đúng ở máy người dùng)
        bool pfs{true};         ///< Perfect Forward Secrecy ở Quick Mode (trao KE riêng, cùng nhóm Phase 1)
        bool qmIdAssigned{true}; ///< Quick Mode IDci = IP được cấp/32 (kiểu FortiClient dialup); false => 0.0.0.0/0
        int qmEncap{0};          ///< encap ESP chào: 0={1 Tunnel}, 1={61443 draft UDP}, 2={3 RFC UDP}, 3={61443,3,1 đa transform}
    };

    struct Result
    {
        quint32 assignedIp{0};
        quint32 assignedMask{0xFFFFFFFF};
        QList<quint32> dns;
        QList<quint32> subnetsBase;  ///< tuyến split-include (base) từ mode-cfg; rỗng => full tunnel
        QList<quint32> subnetsMask;
        GlobalProtectProtocol::EspConfig esp; ///< khóa/SPI/thuật toán cho GpEspCrypto
        quint32 gatewayIp{0};
        // Nguyên liệu KEYMAT (để công cụ kiểm tra tay thử biến thể dẫn xuất khóa ESP) - KHÔNG dùng trong luồng app.
        QByteArray kmSkeyidD, kmGxy, kmNi, kmNr;
        IkeCrypto::Hash kmHash{IkeCrypto::Hash::Sha256};
        int kmEncLen{32}, kmHmacLen{20};
    };

    explicit IkeSession(QObject* parent = nullptr);
    ~IkeSession() override;

    void configure(const Config& c) { m_cfg = c; }
    /// Điểm cuối UDP cục bộ (IP:port tiến trình đang gửi từ) - để dựng NAT-D cho NAT-T. Đặt trước start().
    void setLocalEndpoint(quint32 ip, quint16 port) { m_localIp = ip; m_localPort = port; }
    /// Transport gọi khi thấy gateway đã chuyển sang cổng 4500 (NAT-T) -> các bản tin sau gửi qua 4500.
    void setNatActive(bool on) { m_natActive = on; }
    void start();                        ///< gửi Aggressive msg1
    void onDatagram(const QByteArray& isakmp); ///< nạp một bản tin ISAKMP đã bóc dấu non-ESP
    void onRetransmit();                 ///< gọi định kỳ: gửi lại bản tin cuối nếu chưa có hồi đáp
    void sendDpd();                      ///< gửi DPD R-U-THERE (chỉ khi Up) - keepalive + kiểm IKE SA còn sống
    void stop();

signals:
    void sendDatagram(QByteArray isakmp, bool useNatT); ///< useNatT => tầng truyền tải thêm dấu 4 byte 0 + cổng 4500
    void switchToNatT();                 ///< chuyển hẳn sang cổng 4500 (sau khi phát hiện NAT ở Phase 1)
    void established(IkeSession::Result result);
    void failed(QString reason);
    void log(QString text);

private:
    enum class State { Idle, Aggr1Sent, Phase1Done, XauthWait, ModeCfgWait, QuickWait, Up, Failed };

    // --- Phase 1 ---
    void sendAggr1();
    void onAggr2(const Isakmp::Message& m);
    void sendAggr3();
    // --- XAuth / Mode-config (Transaction, exchange=6) ---
    void onTransaction(const Isakmp::Message& m);
    void sendModeCfgRequest();
    // --- Quick Mode (exchange=32) ---
    void sendQuick1();
    void onQuick2(const Isakmp::Message& m);
    void applyChosenEspTransform(const QByteArray& sa); // đọc transform ESP gateway chọn trong QM2
    void deriveEsp();
    // --- Thông tin/Lỗi ---
    void onInformational(const Isakmp::Message& m);

    // Mã hóa/giải mã payload của một bản tin theo message-ID (CBC + chuỗi IV). flags Encryption tự đặt.
    QByteArray encryptMessage(Isakmp::Message& m);
    bool decryptMessage(const QByteArray& raw, Isakmp::Message* out);
    QByteArray& ivFor(quint32 msgid);

    // Dựng SA Phase 1 (một proposal, nhiều transform) + lưu SAi_b để tính HASH.
    QByteArray buildPhase1Sa();
    // Đọc transform gateway chọn trong SA msg2 -> cập nhật m_cfg.hash + encKeyBits.
    void applyChosenTransform(const QByteArray& sa);
    // Nối byte các payload (đã có generic header) theo thứ tự - để tính HASH "phần sau HASH".
    static QByteArray chain(const QList<QPair<quint8, QByteArray>>& payloads);
    quint32 newMsgId();
    void send(Isakmp::Message& m, bool encrypted);
    void fail(const QString& why);
    int blockSize() const { return 16; } // AES
    int encKeyLen() const { return m_cfg.encKeyBits / 8; }
    int hmacKeyLen() const; // dựa trên m_cfg.hash

    Config m_cfg;
    State m_state{State::Idle};

    std::unique_ptr<IkeCrypto::Dh> m_dh;
    QByteArray m_ckyI, m_ckyR;       // cookie
    QByteArray m_gxi, m_gxr;         // KE công khai
    QByteArray m_gxy;                // bí mật DH chung
    QByteArray m_ni, m_nr;           // nonce
    QByteArray m_saiB;               // thân SA msg1 (cho HASH)
    QByteArray m_idiiB;              // thân IDii (cho HASH)
    QByteArray m_idirB;              // thân IDir (cho HASH)
    IkeKeys::Skeyids m_sk;           // SKEYID d/a/e
    QByteArray m_encKey;             // khóa mã hóa phiên (mở rộng từ SKEYID_e)
    QByteArray m_phase1Iv;           // IV gốc Phase 1
    QMap<quint32, QByteArray> m_iv;  // IV hiện tại theo message-ID

    bool m_natDetected{false};
    bool m_natActive{false};         // đã chuyển cổng 4500
    quint32 m_localIp{0};            // IP cục bộ (cho NAT-D)
    quint16 m_localPort{0};          // cổng UDP cục bộ (cho NAT-D)

    // Quick Mode
    std::unique_ptr<IkeCrypto::Dh> m_qmDh; // PFS: khóa DH riêng cho Quick Mode
    QByteArray m_qmGxy;                     // PFS: bí mật DH Quick Mode (rỗng = không PFS)
    quint32 m_qmMsgId{0};
    quint32 m_spiIn{0};              // SPI ta chọn (máy chủ gửi về ta dùng SPI này)
    quint32 m_spiOut{0};            // SPI máy chủ chọn (ta gửi đi dùng SPI này)
    QByteArray m_qmNi, m_qmNr;       // nonce Quick Mode
    QByteArray m_qmEspSaB;           // thân SA ESP ta gửi (để nếu cần)
    QString m_espEnc, m_espHmac;     // thuật toán ESP chốt được
    int m_espKeyBits{256};           // độ dài khóa ESP gateway chọn (128/256)

    Result m_result;
    QByteArray m_lastSent;           // để phát lại
    bool m_lastWasNatT{false};
    quint32 m_txMsgId{1};            // nguồn sinh message-ID
};
