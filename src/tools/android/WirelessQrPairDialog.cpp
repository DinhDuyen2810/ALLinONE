#include "WirelessQrPairDialog.h"

#include "engine/AdbController.h"
#include "engine/AdbPairer.h"
#include "tools/qr/QRCodec.h"

#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QProcess>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
constexpr int kPollIntervalMs = 1500;

// Tên dịch vụ ngẫu nhiên (chữ/số) nhúng vào mã QR - điện thoại sẽ quảng bá lại đúng tên này qua mDNS.
QString randomServiceName()
{
    const QString hex = QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, '0');
    return "ADB_WIFI_" + hex.left(8).toUpper();
}

// Mã ghép đôi 6 chữ số (đúng định dạng AdbController::isValidPairingCode để tái dùng AdbPairer::pair()).
QString randomPairingCode()
{
    const quint32 n = QRandomGenerator::global()->bounded(1000000u); // 0..999999
    return QString::number(n).rightJustified(6, '0');
}
} // namespace

WirelessQrPairDialog::WirelessQrPairDialog(const QString& adbPath, QWidget* parent)
    : QDialog(parent)
    , m_adbPath(adbPath)
    , m_serviceName(randomServiceName())
    , m_password(randomPairingCode())
{
    // Định dạng ghép đôi QR của ADB: WIFI:T:ADB;S:<tên>;P:<mã>;; (tên/mã chỉ gồm chữ/số nên không cần escape).
    const QString payload = "WIFI:T:ADB;S:" + m_serviceName + ";P:" + m_password + ";;";

    QString encodeError;
    const QRMatrix matrix = QRCodec::encode(payload, QREcc::Medium, &encodeError);
    QImage qr;
    if (matrix.isValid())
    {
        QRStyle style;
        style.targetSize = 340;
        qr = QRCodec::render(matrix, style);
    }

    buildUi(qr, encodeError);

    m_pairer = new AdbPairer(this);
    connect(m_pairer, &AdbPairer::finished, this, &WirelessQrPairDialog::onPairFinished);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &WirelessQrPairDialog::pollMdns);

    if (m_adbPath.isEmpty())
        setStatus("⚠ Không tìm thấy adb.exe đóng gói - không thể ghép đôi.");
    else if (qr.isNull())
        setStatus("⚠ Không tạo được mã QR.");
    else
        startDiscovery();
}

WirelessQrPairDialog::~WirelessQrPairDialog()
{
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_pairer)
        m_pairer->cancel();
    if (m_mdnsProcess)
    {
        m_mdnsProcess->disconnect(this);
        if (m_mdnsProcess->state() != QProcess::NotRunning)
        {
            m_mdnsProcess->kill();
            m_mdnsProcess->waitForFinished(500);
        }
    }
}

void WirelessQrPairDialog::buildUi(const QImage& qr, const QString& encodeError)
{
    setWindowTitle("Ghép đôi Android bằng mã QR");
    setStyleSheet("QDialog { background-color: #f6f8fa; } QLabel { color: #1f2328; background: transparent; }");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(12);

    auto* steps = new QLabel(
        "Trên điện thoại: Cài đặt > Tùy chọn nhà phát triển > Gỡ lỗi không dây > "
        "\"Ghép thiết bị bằng mã QR\", rồi quét mã bên dưới.\n"
        "Máy tính và điện thoại phải cùng một mạng Wi-Fi.",
        this);
    steps->setWordWrap(true);
    steps->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(steps);

    auto* qrLabel = new QLabel(this);
    qrLabel->setAlignment(Qt::AlignCenter);
    if (!qr.isNull())
        qrLabel->setPixmap(QPixmap::fromImage(qr));
    else
        qrLabel->setText(encodeError.isEmpty() ? "(không tạo được mã QR)" : encodeError);
    root->addWidget(qrLabel);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void WirelessQrPairDialog::setStatus(const QString& text)
{
    if (m_statusLabel)
        m_statusLabel->setText(text);
}

void WirelessQrPairDialog::startDiscovery()
{
    setStatus("⏳ Chờ điện thoại quét mã QR...");
    m_pollTimer->start();
    pollMdns(); // chạy ngay lần đầu, không đợi hết chu kỳ
}

void WirelessQrPairDialog::pollMdns()
{
    if (m_pairing || m_adbPath.isEmpty())
        return;
    if (m_mdnsProcess && m_mdnsProcess->state() != QProcess::NotRunning)
        return; // lần dò trước chưa xong

    if (!m_mdnsProcess)
    {
        m_mdnsProcess = new QProcess(this);
        m_mdnsProcess->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_mdnsProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int, QProcess::ExitStatus) {
                    const QString out = QString::fromLocal8Bit(m_mdnsProcess->readAll());
                    const QString ipPort = AdbController::internal::parseMdnsPairingService(out, m_serviceName);
                    if (ipPort.isEmpty() || m_pairing)
                        return;

                    m_pairing = true;
                    m_pollTimer->stop();
                    m_pairedHost = ipPort.left(ipPort.lastIndexOf(QChar(':')));
                    setStatus("⏳ Đã thấy điện thoại, đang ghép đôi...");
                    if (!m_pairer->pair(ipPort, m_password))
                    {
                        // pair() chỉ trả false khi đang bận - ở đây không thể, nhưng vẫn mở lại dò cho chắc.
                        m_pairing = false;
                        m_pollTimer->start();
                    }
                });
    }

    m_mdnsProcess->start(m_adbPath, {"mdns", "services"});
}

void WirelessQrPairDialog::onPairFinished(bool ok, const QString& error)
{
    if (ok)
    {
        setStatus("✓ Ghép đôi thành công! Thiết bị sẽ sớm hiện trong danh sách.");
        emit pairedSuccessfully(m_pairedHost);
        QTimer::singleShot(1200, this, &QDialog::accept);
        return;
    }

    // Thất bại (thường do mã QR đã hết hạn hoặc quét nhầm): dò lại để người dùng quét mã mới của chính QR này.
    setStatus("⚠ Ghép đôi chưa thành công (" + error + "). Vẫn đang chờ, bạn có thể quét lại mã.");
    m_pairing = false;
    m_pollTimer->start();
}
