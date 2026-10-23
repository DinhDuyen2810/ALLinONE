#include "GpEsp.h"

#include "DirectVpnTypes.h"

#include <QRandomGenerator>
#include <QUdpSocket>

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace
{
// Payload magic BẮT BUỘC của GlobalProtect để gateway phản hồi gói dò (16 byte đầu là tối thiểu; theo mã công
// khai OpenConnect: "monitor\x00\x00pan ha "). KHÔNG kết thúc bằng NUL.
const char kMagicPing[16] = {'m', 'o', 'n', 'i', 't', 'o', 'r', '\x00', '\x00', 'p', 'a', 'n', ' ', 'h', 'a', ' '};
constexpr int kIcmpMinLen = 8;

void putBe16(QByteArray& b, quint16 v)
{
    b.append(char(v >> 8));
    b.append(char(v & 0xFF));
}
void putBe32(QByteArray& b, quint32 v)
{
    putBe16(b, quint16(v >> 16));
    putBe16(b, quint16(v & 0xFFFF));
}

/// Tổng kiểm (checksum) Internet 16-bit trên một vùng byte (độ dài chẵn).
quint16 inetChecksum(const unsigned char* p, int len)
{
    quint32 sum = 0;
    for (int i = 0; i + 1 < len; i += 2)
        sum += (quint32(p[i]) << 8) | p[i + 1];
    if (len & 1)
        sum += quint32(p[len - 1]) << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return quint16(~sum);
}
} // namespace

// ---------------------------------------------------------------------------------------------------------
// GpEspCrypto
// ---------------------------------------------------------------------------------------------------------

GpEspCrypto::~GpEspCrypto()
{
    if (m_encKeyOut)
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_encKeyOut));
    if (m_encKeyIn)
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_encKeyIn));
    if (m_aesAlg)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_aesAlg), 0);
    if (m_hmacAlg)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_hmacAlg), 0);
    delete[] static_cast<char*>(m_aesKeyObjOut);
    delete[] static_cast<char*>(m_aesKeyObjIn);
}

bool GpEspCrypto::init(const GlobalProtectProtocol::EspConfig& cfg, QString* error)
{
    auto fail = [&](const QString& m) {
        if (error)
            *error = m;
        return false;
    };
    const int encBits = cfg.encAlgo.contains("256") ? 256 : 128;
    if (static_cast<int>(cfg.encKeyOut.size()) != encBits / 8 || cfg.encKeyIn.size() != cfg.encKeyOut.size())
        return fail(QStringLiteral("Độ dài khóa mã hóa ESP không khớp thuật toán."));

    const wchar_t* macAlg = nullptr;
    if (cfg.hmacAlgo == "sha1")
    {
        macAlg = BCRYPT_SHA1_ALGORITHM;
        m_hmacLen = 12;
    }
    else if (cfg.hmacAlgo == "sha256")
    {
        macAlg = BCRYPT_SHA256_ALGORITHM;
        m_hmacLen = 16;
    }
    else if (cfg.hmacAlgo == "md5")
    {
        macAlg = BCRYPT_MD5_ALGORITHM;
        m_hmacLen = 12;
    }
    else
        return fail(QStringLiteral("Thuật toán HMAC ESP không hỗ trợ: %1").arg(cfg.hmacAlgo));

    BCRYPT_ALG_HANDLE aesAlg = nullptr, hmacAlg = nullptr;
    if (BCryptOpenAlgorithmProvider(&aesAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không mở được AES (BCrypt)."));
    m_aesAlg = aesAlg;
    if (BCryptSetProperty(aesAlg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                          sizeof(BCRYPT_CHAIN_MODE_CBC), 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không đặt được chế độ CBC."));
    if (BCryptOpenAlgorithmProvider(&hmacAlg, macAlg, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không mở được HMAC (BCrypt)."));
    m_hmacAlg = hmacAlg;

    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(aesAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0);
    auto makeKey = [&](const QByteArray& keyBytes, void** keyObj, BCRYPT_KEY_HANDLE* outKey) {
        *keyObj = new char[objLen];
        return BCryptGenerateSymmetricKey(aesAlg, outKey, reinterpret_cast<PUCHAR>(*keyObj), objLen,
                                          reinterpret_cast<PUCHAR>(const_cast<char*>(keyBytes.constData())),
                                          static_cast<ULONG>(keyBytes.size()), 0) == STATUS_SUCCESS;
    };
    BCRYPT_KEY_HANDLE kOut = nullptr, kIn = nullptr;
    if (!makeKey(cfg.encKeyOut, &m_aesKeyObjOut, &kOut) || !makeKey(cfg.encKeyIn, &m_aesKeyObjIn, &kIn))
        return fail(QStringLiteral("Không tạo được khóa AES ESP."));
    m_encKeyOut = kOut;
    m_encKeyIn = kIn;

    m_hmacKeyOut = cfg.hmacKeyOut;
    m_hmacKeyIn = cfg.hmacKeyIn;
    m_spiOut = htonl(cfg.spiOut);
    m_spiIn = htonl(cfg.spiIn);
    m_seqOut = 0;
    m_ready = true;
    return true;
}

QByteArray GpEspCrypto::hmac(bool out, const QByteArray& data) const
{
    const QByteArray& key = out ? m_hmacKeyOut : m_hmacKeyIn;
    BCRYPT_HASH_HANDLE h = nullptr;
    if (BCryptCreateHash(static_cast<BCRYPT_ALG_HANDLE>(m_hmacAlg), &h, nullptr, 0,
                         reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())), static_cast<ULONG>(key.size()), 0) != STATUS_SUCCESS)
        return {};
    BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), static_cast<ULONG>(data.size()), 0);
    DWORD hashLen = 0, cb = 0;
    BCryptGetProperty(static_cast<BCRYPT_ALG_HANDLE>(m_hmacAlg), BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen),
                      sizeof(hashLen), &cb, 0);
    QByteArray digest(static_cast<int>(hashLen), Qt::Uninitialized);
    BCryptFinishHash(h, reinterpret_cast<PUCHAR>(digest.data()), hashLen, 0);
    BCryptDestroyHash(h);
    return digest.left(m_hmacLen);
}

QByteArray GpEspCrypto::aes(bool out, bool encrypt, const QByteArray& iv, const QByteArray& in) const
{
    BCRYPT_KEY_HANDLE key = static_cast<BCRYPT_KEY_HANDLE>(out ? m_encKeyOut : m_encKeyIn);
    QByteArray ivCopy = iv; // BCrypt cập nhật IV tại chỗ
    QByteArray outBuf(in.size() + kBlock, Qt::Uninitialized);
    ULONG done = 0;
    const NTSTATUS st = encrypt
        ? BCryptEncrypt(key, reinterpret_cast<PUCHAR>(const_cast<char*>(in.constData())), static_cast<ULONG>(in.size()),
                        nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), kIvLen, reinterpret_cast<PUCHAR>(outBuf.data()),
                        static_cast<ULONG>(outBuf.size()), &done, 0)
        : BCryptDecrypt(key, reinterpret_cast<PUCHAR>(const_cast<char*>(in.constData())), static_cast<ULONG>(in.size()),
                        nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), kIvLen, reinterpret_cast<PUCHAR>(outBuf.data()),
                        static_cast<ULONG>(outBuf.size()), &done, 0);
    if (st != STATUS_SUCCESS)
        return {};
    outBuf.truncate(static_cast<int>(done));
    return outBuf;
}

QByteArray GpEspCrypto::seal(const QByteArray& payload, quint8 nextHeader)
{
    if (!m_ready)
        return {};
    // Đệm để (payload + padlen(1) + nexthdr(1)) chia hết cho khối; đệm = 1,2,3,...
    QByteArray plain = payload;
    const int padLen = (kBlock - ((payload.size() + 2) % kBlock)) % kBlock;
    for (int i = 0; i < padLen; ++i)
        plain.append(char(i + 1));
    plain.append(char(padLen));
    plain.append(char(nextHeader));

    QByteArray iv(kIvLen, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(iv.data()), kIvLen / 4);
    const QByteArray cipher = aes(true, true, iv, plain);
    if (cipher.isEmpty())
        return {};

    QByteArray pkt;
    pkt.reserve(8 + kIvLen + cipher.size() + m_hmacLen);
    pkt.append(reinterpret_cast<const char*>(&m_spiOut), 4);
    putBe32(pkt, m_seqOut++);
    pkt.append(iv);
    pkt.append(cipher);
    pkt.append(hmac(true, pkt)); // HMAC trên [spi][seq][iv][cipher]
    return pkt;
}

bool GpEspCrypto::open(const QByteArray& esp, QByteArray* payload, quint8* nextHeader)
{
    if (!m_ready || esp.size() < 8 + kIvLen + kBlock + m_hmacLen)
        return false;
    if (std::memcmp(esp.constData(), &m_spiIn, 4) != 0)
        return false; // SPI không phải phiên này
    const int authPos = esp.size() - m_hmacLen;
    const QByteArray mac = hmac(false, esp.left(authPos));
    if (mac.size() != m_hmacLen || std::memcmp(mac.constData(), esp.constData() + authPos, m_hmacLen) != 0)
        return false; // HMAC sai
    const QByteArray iv = esp.mid(8, kIvLen);
    const QByteArray cipher = esp.mid(8 + kIvLen, authPos - 8 - kIvLen);
    if (cipher.isEmpty() || (cipher.size() % kBlock) != 0)
        return false;
    const QByteArray plain = aes(false, false, iv, cipher);
    if (plain.size() < 2)
        return false;
    const int padLen = static_cast<quint8>(plain[plain.size() - 2]);
    if (plain.size() < padLen + 2)
        return false;
    if (nextHeader)
        *nextHeader = static_cast<quint8>(plain[plain.size() - 1]);
    if (payload)
        *payload = plain.left(plain.size() - padLen - 2);
    return true;
}

// ---------------------------------------------------------------------------------------------------------
// GpEspChannel
// ---------------------------------------------------------------------------------------------------------

GpEspChannel::GpEspChannel(QObject* parent)
    : QObject(parent)
{
}

QByteArray GpEspChannel::buildProbePacket(quint32 srcIp, quint32 dstMagic, quint16 seq)
{
    // ICMP echo request + payload magic.
    QByteArray icmp;
    icmp.append(char(8)); // type = echo request
    icmp.append(char(0)); // code
    putBe16(icmp, 0);     // checksum (điền sau)
    putBe16(icmp, 0x4747); // id (như client Windows)
    putBe16(icmp, seq);
    icmp.append(kMagicPing, sizeof(kMagicPing));
    const quint16 icsum = inetChecksum(reinterpret_cast<const unsigned char*>(icmp.constData()), icmp.size());
    icmp[2] = char(icsum >> 8);
    icmp[3] = char(icsum & 0xFF);

    // IPv4 header (20 byte).
    const int total = 20 + icmp.size();
    QByteArray ip;
    ip.append(char(0x45));                  // version 4, IHL 5
    ip.append(char(0));                     // DSCP/ECN
    putBe16(ip, quint16(total));
    putBe16(ip, 0x4747);                    // id
    putBe16(ip, 0x4000);                    // flags=DF, offset 0
    ip.append(char(64));                    // TTL
    ip.append(char(1));                     // protocol = ICMP
    putBe16(ip, 0);                         // checksum (điền sau)
    putBe32(ip, srcIp);
    putBe32(ip, dstMagic);
    const quint16 ipsum = inetChecksum(reinterpret_cast<const unsigned char*>(ip.constData()), ip.size());
    ip[10] = char(ipsum >> 8);
    ip[11] = char(ipsum & 0xFF);
    return ip + icmp;
}

bool GpEspChannel::isProbeReply(const QByteArray& ip, quint32 magic)
{
    if (ip.size() < 20 + kIcmpMinLen + static_cast<int>(sizeof(kMagicPing)))
        return false;
    if ((static_cast<quint8>(ip[0]) >> 4) != 4 || static_cast<quint8>(ip[9]) != 1) // IPv4, ICMP
        return false;
    const quint32 src = (quint32(quint8(ip[12])) << 24) | (quint8(ip[13]) << 16) | (quint8(ip[14]) << 8) | quint8(ip[15]);
    if (src != magic)
        return false;
    const int ihl = (static_cast<quint8>(ip[0]) & 0x0F) * 4;
    if (ip.size() < ihl + kIcmpMinLen + static_cast<int>(sizeof(kMagicPing)))
        return false;
    if (static_cast<quint8>(ip[ihl]) != 0) // ICMP echo REPLY = type 0
        return false;
    return std::memcmp(ip.constData() + ihl + kIcmpMinLen, kMagicPing, sizeof(kMagicPing)) == 0;
}

bool GpEspChannel::start(const GlobalProtectProtocol::EspConfig& cfg, quint32 clientIpv4, const QHostAddress& gateway,
                         Callbacks cb, QString* error)
{
    m_cb = std::move(cb);
    m_gateway = gateway;
    m_port = static_cast<quint16>(cfg.udpPort);
    m_clientIp = clientIpv4;
    m_magic = cfg.magicV4;
    if (!m_crypto.init(cfg, error))
        return false;
    m_sock = new QUdpSocket(this);
    if (!m_sock->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
    {
        if (error)
            *error = QStringLiteral("Không mở được socket UDP cho ESP: %1").arg(m_sock->errorString());
        return false;
    }
    connect(m_sock, &QUdpSocket::readyRead, this, &GpEspChannel::onDatagrams);
    sendProbe();
    return true;
}

void GpEspChannel::stop()
{
    if (m_sock)
    {
        m_sock->disconnect(this);
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_established = false;
}

void GpEspChannel::sendProbe()
{
    if (!m_sock)
        return;
    const QByteArray probe = buildProbePacket(m_clientIp, m_magic, m_probeSeq++);
    const QByteArray esp = m_crypto.seal(probe, 4 /* IP-in-IP */);
    if (!esp.isEmpty())
        m_sock->writeDatagram(esp, m_gateway, m_port);
}

bool GpEspChannel::sendIp(const QByteArray& ipPacket)
{
    if (!m_sock || !m_established)
        return false;
    const QByteArray esp = m_crypto.seal(ipPacket, 4);
    if (esp.isEmpty())
        return false;
    m_sock->writeDatagram(esp, m_gateway, m_port);
    return true;
}

void GpEspChannel::onDatagrams()
{
    while (m_sock && m_sock->hasPendingDatagrams())
    {
        QByteArray dg(static_cast<int>(m_sock->pendingDatagramSize()), Qt::Uninitialized);
        m_sock->readDatagram(dg.data(), dg.size());
        QByteArray inner;
        quint8 nh = 0;
        if (!m_crypto.open(dg, &inner, &nh))
            continue; // gói lạ/HMAC sai
        if (m_cb.activity)
            m_cb.activity();
        if (!m_established)
        {
            // Chờ phản hồi dò hợp lệ đầu tiên rồi mới coi là ESP đã lên.
            if (isProbeReply(inner, m_magic))
            {
                m_established = true;
                if (m_cb.log)
                    m_cb.log(QStringLiteral("ESP: đã nhận phản hồi dò - kênh dữ liệu ESP đã lên."));
                if (m_cb.established)
                    m_cb.established();
            }
            continue;
        }
        if (isProbeReply(inner, m_magic))
            continue; // phản hồi keepalive - không phải dữ liệu
        // Gói dữ liệu: chỉ nhận IPv4 (nibble phiên bản = 4).
        if (!inner.isEmpty() && (static_cast<quint8>(inner[0]) >> 4) == 4 && m_cb.ipReceived)
            m_cb.ipReceived(inner);
    }
}
