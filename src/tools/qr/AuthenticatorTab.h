#pragma once

#include <QImage>
#include <QWidget>

#include "Authenticator.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

/// Tab "Authenticator": thay Google Authenticator/Microsoft Authenticator trên máy tính - nhập tài khoản bằng ảnh mã QR
/// (otpauth:// hoặc mã "Xuất tài khoản" otpauth-migration:// của Google Authenticator), dán ảnh, chụp vùng màn hình hay gõ
/// khóa tay, rồi hiện mã 6 số đổi theo chu kỳ kèm đếm ngược. Khóa bí mật lưu mã hóa bằng DPAPI (xem AuthenticatorStore).
class AuthenticatorTab : public QWidget
{
    Q_OBJECT

public:
    explicit AuthenticatorTab(QWidget* parent = nullptr);

    /// Thêm tài khoản từ nội dung một mã QR (otpauth:// hoặc otpauth-migration://). Trả về số tài khoản MỚI được thêm
    /// (0 nếu không phải mã Authenticator hoặc đã có sẵn); kết quả cũng hiện ở thanh trạng thái.
    int importUri(const QString& text);
    /// Giải mã ảnh rồi nhập mọi mã Authenticator trong đó. Trả về số tài khoản mới thêm.
    int importImage(const QImage& image, const QString& sourceName);

    QString statusText() const;
    int rowCount() const;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void rebuild();
    void tick();
    void openImage();
    void pasteImage();
    void snipScreen();
    void addManual();
    void copyCode();
    void nextHotp();
    void editName();
    void exportQr();
    void removeAccount();
    void updateButtons();

private:
    void setStatus(const QString& text, bool error = false);
    OtpAccount currentAccount(bool* ok = nullptr) const;
    static QString groupDigits(const QString& code);

    QTableWidget* m_table{nullptr};
    QLabel* m_empty{nullptr};
    QLabel* m_status{nullptr};
    QPushButton* m_copyBtn{nullptr};
    QPushButton* m_nextBtn{nullptr};
    QPushButton* m_editBtn{nullptr};
    QPushButton* m_exportBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QTimer* m_timer{nullptr};
};
