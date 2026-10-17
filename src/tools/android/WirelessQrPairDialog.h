#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QProcess;
class QTimer;
class AdbPairer;

/**
 * @brief Hộp thoại "Ghép đôi bằng mã QR" (Android 11+).
 *
 * Hiển thị một mã QR định dạng ghép đôi ADB:  WIFI:T:ADB;S:<tên dịch vụ>;P:<mã>;;
 * Người dùng mở điện thoại > Tùy chọn nhà phát triển > Gỡ lỗi không dây > "Ghép thiết bị bằng mã QR" và
 * quét. Khi quét xong, điện thoại tự quảng bá một dịch vụ mDNS `_adb-tls-pairing._tcp` mang đúng tên dịch
 * vụ trong mã QR. Hộp thoại dò dịch vụ đó bằng `adb mdns services`, lấy IP:cổng rồi chạy `adb pair` với
 * đúng mã đã nhúng trong QR - hoàn toàn tự động, không cần người dùng gõ gì.
 *
 * Tự chứa: có AdbPairer riêng và tiến trình `adb mdns services` riêng, không đụng tới m_pairer của DevicesTab.
 * KHÔNG dò nhiều mã: mã ghép đôi do CHÍNH hộp thoại sinh ngẫu nhiên và nhúng vào QR; đây là ghép đôi gỡ lỗi
 * tiêu chuẩn của Android, không liên quan tới mật khẩu WiFi.
 */
class WirelessQrPairDialog : public QDialog
{
    Q_OBJECT

public:
    /// adbPath: đường dẫn adb.exe đóng gói (AdbController::adbExecutablePath()); rỗng thì hộp thoại báo lỗi.
    explicit WirelessQrPairDialog(const QString& adbPath, QWidget* parent = nullptr);
    ~WirelessQrPairDialog() override;

    /// IP (không kèm cổng) của thiết bị vừa ghép đôi thành công - điền sẵn cho bước "Kết nối không dây".
    QString pairedHost() const { return m_pairedHost; }

signals:
    void pairedSuccessfully(const QString& host);

private slots:
    void pollMdns();
    void onPairFinished(bool ok, const QString& error);

private:
    void buildUi(const class QImage& qr, const QString& encodeError);
    void setStatus(const QString& text);
    void startDiscovery();

    QString m_adbPath;
    QString m_serviceName;
    QString m_password;
    QString m_pairedHost;

    QLabel* m_statusLabel{nullptr};
    QTimer* m_pollTimer{nullptr};
    QProcess* m_mdnsProcess{nullptr};
    AdbPairer* m_pairer{nullptr};
    bool m_pairing{false};
};
