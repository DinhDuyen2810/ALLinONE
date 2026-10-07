#pragma once

#include <QDialog>

class QLineEdit;

/// Hộp thoại "Ghép đôi gỡ lỗi không dây" (Android 11+): nhập địa chỉ IP:Cổng ghép đôi VÀ mã 6 số hiển
/// thị trên điện thoại (Cài đặt > Tùy chọn nhà phát triển > Gỡ lỗi qua mạng > Ghép đôi bằng mã ghép
/// đôi). Chỉ cần làm một lần cho mỗi mạng Wi-Fi - sau đó dùng "Kết nối không dây" với IP:Cổng thường
/// (khác cổng ghép đôi) để kết nối lại mà không cần nhập mã nữa.
class WirelessPairDialog : public QDialog
{
    Q_OBJECT

public:
    explicit WirelessPairDialog(QWidget* parent = nullptr);

    QString ipAndPort() const;
    QString pairingCode() const;

private:
    QLineEdit* m_ipPortEdit{nullptr};
    QLineEdit* m_codeEdit{nullptr};
};
