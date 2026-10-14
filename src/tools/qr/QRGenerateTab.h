#pragma once

#include <QColor>
#include <QImage>
#include <QList>
#include <QWidget>

#include "QRCodec.h"
#include "QRPayload.h"
#include "tools/wifi/model/WifiProfile.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QNetworkReply;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;
class WlanController;
class PublicIpChecker;

/// Tab tạo mã QR: chọn loại nội dung, tùy chỉnh giao diện, xem trước trực tiếp, lưu/sao chép.
class QRGenerateTab : public QWidget
{
    Q_OBJECT

public:
    explicit QRGenerateTab(QWidget* parent = nullptr);

    /// Đặt nội dung dạng văn bản thô (dùng cho "Tạo lại mã").
    void setRawText(const QString& text);

    // ---- Dùng cho kiểm thử / truy xuất ----
    QString currentContent() const;           // nội dung sẽ được mã hóa ("" nếu chưa hợp lệ)
    QImage currentImage() const { return m_image; }
    QRMatrix currentMatrix() const { return m_matrix; }
    bool saveTo(const QString& path, QString* error = nullptr);
    bool copyToClipboard();

public slots:
    void refresh();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTypeChanged(int index);
    void scheduleRefresh();
    void onPickForeground();
    void onPickBackground();
    void onSwapColors();
    void onPickLogo();
    void onClearLogo();
    void onSave();
    void onCopy();
    void onWifiProfileSelected(int index);
    void onLocateMe();
    void onOpenMap();
    void onApplyMapsLink();

private:
    /// Điền tọa độ đã đọc được từ link Google Maps vào hai ô Vĩ độ/Kinh độ + báo kết quả ở nhãn trạng thái.
    /// `sourceText` = chuỗi (link) đã đọc ra tọa độ - dùng để báo thêm các điểm ghim KHÁC nếu link có nhiều điểm.
    void applyMapsCoordinates(double lat, double lon, const QString& what, const QString& sourceText = QString());
    /// Mở một link RÚT GỌN (maps.app.goo.gl/...) bằng MỘT yêu cầu GET, KHÔNG theo chuyển hướng tự động: chỉ
    /// đọc header Location (đích chuyển hướng - URL đầy đủ có tọa độ), không tải nội dung trang. Mỗi bước
    /// chuyển hướng chỉ được đi tiếp nếu còn thuộc Google (QRPayload::isAllowedMapsRedirectUrl), tối đa 5 bước.
    void resolveShortMapsLink(const QUrl& url, int hop);
    void cancelMapsLookup();

    void buildUi();
    QWidget* makeTextPage();
    QWidget* makeUrlPage();
    QWidget* makeWifiPage();
    QWidget* makePhonePage();
    QWidget* makeGeoPage();

    QString buildContent(QString* problem) const;
    QRContentType currentType() const;
    QRStyle currentStyle() const;
    void updateColorButtons();
    void setPreviewMessage(const QString& msg);
    void updatePreviewPixmap();
    /// Nạp danh sách "WiFi đã lưu" từ adapter WLAN đầu tiên của máy (lần đầu chọn loại WiFi). Không báo lỗi
    /// gì nếu không có dịch vụ WLAN/adapter - combo chỉ còn mỗi mục mặc định, người dùng vẫn nhập tay bình
    /// thường. Trả về false khi không mở được WLAN/không có adapter (lần chọn WiFi sau sẽ thử lại).
    bool refreshWifiSavedProfiles();

    QTimer* m_timer{nullptr};
    QComboBox* m_typeCombo{nullptr};
    QStackedWidget* m_pages{nullptr};

    // Text / URL
    QPlainTextEdit* m_textEdit{nullptr};
    QLineEdit* m_urlEdit{nullptr};
    // WiFi
    QLineEdit* m_wifiSsid{nullptr};
    QLineEdit* m_wifiPass{nullptr};
    QComboBox* m_wifiSec{nullptr};
    QCheckBox* m_wifiHidden{nullptr};
    QComboBox* m_wifiSavedCombo{nullptr}; // "Chọn từ WiFi đã lưu" - tự điền SSID/mật khẩu/bảo mật khi chọn
    QLabel* m_wifiSavedStatus{nullptr};
    // Phone
    QLineEdit* m_phoneNumber{nullptr};
    // Geo
    QDoubleSpinBox* m_geoLat{nullptr};
    QDoubleSpinBox* m_geoLon{nullptr};
    QPushButton* m_geoLocateButton{nullptr}; // "Lấy vị trí hiện tại" - ước lượng gần đúng theo IP công khai
    QPushButton* m_geoMapButton{nullptr};    // "Mở bản đồ" - mở Google Maps tại tọa độ đang nhập
    QLabel* m_geoLocateStatus{nullptr};
    QLineEdit* m_mapsLinkEdit{nullptr};          // dán link Google Maps vào đây để đọc tọa độ
    QPushButton* m_mapsLinkApplyButton{nullptr};
    QNetworkAccessManager* m_mapsNet{nullptr};   // tạo lười - chỉ khi dán link RÚT GỌN lần đầu
    QNetworkReply* m_mapsReply{nullptr};         // yêu cầu đang chạy (nullptr nếu không có)

    // Options
    QComboBox* m_eccCombo{nullptr};
    QSpinBox* m_sizeSpin{nullptr};
    QSpinBox* m_quietSpin{nullptr};
    QPushButton* m_fgButton{nullptr};
    QPushButton* m_bgButton{nullptr};
    QPushButton* m_swapButton{nullptr};
    QCheckBox* m_roundedCheck{nullptr};
    QPushButton* m_logoButton{nullptr};
    QPushButton* m_logoClearButton{nullptr};
    QSlider* m_logoSlider{nullptr};
    QLabel* m_logoLabel{nullptr};

    // Preview
    QLabel* m_preview{nullptr};
    QLabel* m_info{nullptr};
    QLabel* m_warning{nullptr};
    QPushButton* m_saveButton{nullptr};
    QPushButton* m_copyButton{nullptr};

    QColor m_fg{Qt::black};
    QColor m_bg{Qt::white};
    QImage m_logo;

    // "Tạo lại mã": nội dung NGUYÊN VĂN khi ô văn bản không hiển thị/trả lại đúng được nó (xem setRawText);
    // null khi không dùng. m_rawTextShown là thứ ô văn bản đang trả về cho bản nguyên văn đó.
    QString m_rawText;
    QString m_rawTextShown;

    QRMatrix m_matrix;
    QImage m_image;
    QString m_content;

    // Tạo/mở lười - đa số phiên tạo mã QR không bao giờ chạm tới WiFi/Geo.
    WlanController* m_wlan{nullptr};
    QList<WifiProfile> m_wifiSavedProfiles; // song song với item data của m_wifiSavedCombo (chỉ số trong list này)
    /// true sau lần nạp ĐẦU TIÊN - refreshWifiSavedProfiles() đọc mật khẩu dạng chữ cho MỌI hồ sơ
    /// (WlanGetProfile + giải mã DPAPI cho từng hồ sơ) nên không rẻ trên máy có nhiều hồ sơ đã lưu; nạp lại
    /// MỖI LẦN chuyển sang loại WiFi (như thiết kế ban đầu) gây giật trên máy thật nhiều hồ sơ. Đánh đổi:
    /// danh sách có thể hơi cũ nếu người dùng thêm/xóa hồ sơ WiFi trong lúc cửa sổ QR vẫn đang mở - chấp
    /// nhận được, hiếm khi xảy ra trong một phiên tạo mã QR.
    bool m_wifiProfilesLoaded{false};
    PublicIpChecker* m_ipChecker{nullptr};
};
