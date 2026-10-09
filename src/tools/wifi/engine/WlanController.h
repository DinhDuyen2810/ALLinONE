#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "../model/WifiNetwork.h"
#include "../model/WifiProfile.h"

/**
 * @brief Lớp duy nhất giao tiếp với Windows WLAN API (wlanapi.dll).
 * Tuân thủ nguyên tắc cô lập Win32 của dự án (xem InputController ở module Auto Click):
 * mọi lời gọi WlanXxx() chỉ nằm trong file .cpp này.
 *
 * Giới hạn có chủ đích: lớp này KHÔNG cung cấp bất kỳ thao tác nào thử nhiều mật khẩu
 * liên tiếp để "dò" vào một mạng. connectWithPassword() luôn kết nối với ĐÚNG MỘT
 * mật khẩu do người dùng nhập, giống hệt cách Windows tự làm khi bấm "Connect" trên taskbar.
 *
 * Không đăng ký thông báo (WlanRegisterNotification): tầng UI tự polling bằng QTimer
 * sau khi gọi requestScan()/connectWithPassword(), đơn giản và dễ kiểm chứng hơn callback
 * chạy trên luồng dịch vụ WLAN.
 */
struct WifiAdapterInfo
{
    QString guidString; // chuỗi GUID, dùng làm khóa tra cứu ổn định qua Qt signal/slot
    QString description;
    bool connected{false};
};

struct WifiCurrentConnection
{
    bool isConnected{false};
    bool connecting{false}; // adapter đang dò/liên kết/xác thực (chưa xong, chưa thất bại)
    QString ssid;
    QString bssid;
    WifiSecurity security{WifiSecurity::Unknown};
    int signalQuality{0};
    QString profileName;
    QString phyType;
    double rxRateMbps{0};
    double txRateMbps{0};
};

/// Bản sao lưu của MỘT hồ sơ trước khi connectWithPassword() ghi đè/tạo nó - đưa lại cho
/// rollbackProfile() khi lần kết nối đó thất bại/bị hủy để máy trở về đúng trạng thái trước khi thử.
struct WifiProfileBackup
{
    bool valid{false};      // false = không có gì để hoàn tác
    QString adapterGuid;
    QString profileName;
    bool existed{false};    // true: hồ sơ cùng tên đã có từ trước (khôi phục bằng xml); false: hồ sơ mới (xóa)
    bool perUser{false};    // hồ sơ cũ là loại riêng từng người dùng (WLAN_PROFILE_USER)
    QString xml;            // XML hồ sơ cũ, chỉ giữ trong bộ nhớ, không ghi ra đĩa
};

class WlanController : public QObject
{
    Q_OBJECT

public:
    explicit WlanController(QObject* parent = nullptr);
    ~WlanController() override;

    /// Mở handle WLAN. false nếu máy không có dịch vụ WLAN (driver chưa cài, bị tắt, v.v.)
    bool open(QString* error = nullptr);
    bool isOpen() const { return m_handle != nullptr; }

    QList<WifiAdapterInfo> adapters(QString* error = nullptr) const;

    /// Yêu cầu quét lại (bất đồng bộ, không chặn). Gọi availableNetworks() lại sau vài giây.
    bool requestScan(const QString& adapterGuid, QString* error = nullptr);
    QList<WifiNetwork> availableNetworks(const QString& adapterGuid, QString* error = nullptr) const;

    /// withPlaintextPassword=true cần chạy với quyền Administrator; nếu không mỗi hồ sơ sẽ có
    /// passwordAccessDenied=true thay vì làm hỏng toàn bộ danh sách.
    QList<WifiProfile> profiles(const QString& adapterGuid, bool withPlaintextPassword, QString* error = nullptr) const;
    bool deleteProfile(const QString& adapterGuid, const QString& profileName, QString* error = nullptr);
    /// Đã có hồ sơ mang tên này trên adapter chưa (chỉ đọc).
    bool hasProfile(const QString& adapterGuid, const QString& profileName) const;

    /// includePlaintextKey=true: xin mật khẩu dạng chữ (cần quyền; nếu không được cấp thì vẫn trả XML
    /// với khóa mã hóa và điền error mang tính cảnh báo). false: KHÔNG xin - <keyMaterial> ở dạng mã hóa
    /// (<protected>true), chỉ nhập lại được trên chính máy này.
    QString exportProfileXml(const QString& adapterGuid, const QString& profileName, bool includePlaintextKey,
                             QString* error = nullptr) const;
    bool importProfileXml(const QString& adapterGuid, const QString& xml, QString* error = nullptr);

    /// Tạo/ghi đè một hồ sơ với mật khẩu do người dùng cung cấp rồi kết nối NGAY (đúng một lần thử).
    /// Hồ sơ cùng tên đang có (nếu có) được sao lưu TRƯỚC khi ghi đè và trả về qua backup: nơi gọi PHẢI
    /// gọi rollbackProfile(backup) khi lần kết nối này không thành công (hết giờ, người dùng hủy) - nếu
    /// không, một lần gõ sai mật khẩu sẽ phá hỏng hồ sơ đang dùng tốt. Hàm tự hoàn tác nếu chính nó thất
    /// bại giữa chừng (tạo được hồ sơ nhưng không gửi được yêu cầu kết nối).
    bool connectWithPassword(const QString& adapterGuid, const WifiNetwork& network, const QString& password,
                             WifiSecurity securityOverride, bool autoConnect, WifiProfileBackup* backup,
                             QString* error = nullptr);
    /// Hoàn tác thay đổi hồ sơ của connectWithPassword(): khôi phục hồ sơ cũ, hoặc xóa hồ sơ vừa tạo.
    bool rollbackProfile(const WifiProfileBackup& backup, QString* error = nullptr);
    bool connectToSavedProfile(const QString& adapterGuid, const QString& profileName, QString* error = nullptr);
    bool disconnect(const QString& adapterGuid, QString* error = nullptr);

    WifiCurrentConnection currentConnection(const QString& adapterGuid, QString* error = nullptr) const;

private:
    void* m_handle{nullptr}; // HANDLE, tránh lộ kiểu Win32 ra header dùng chung
};
