#pragma once

#include "../model/ThreatRecord.h"

#include <QDateTime>
#include <QList>
#include <QString>

/// Lớp duy nhất gọi PowerShell module "Defender" có sẵn của Windows (Get/Set-MpPreference,
/// Get-MpComputerStatus, Start-MpScan, Get-MpThreat, Remove-MpThreat) - KHÔNG tự viết lại công cụ quét
/// mã độc (việc đó cần cơ sở dữ liệu chữ ký/máy học được cập nhật liên tục, không thể tự làm tốt hơn
/// Microsoft Defender đã có sẵn trên mọi máy Windows). Đã xác nhận qua tài liệu Microsoft Learn chính
/// thức trước khi viết (xem ghi chú ở từng hàm).
namespace DefenderController
{

struct DefenderStatus
{
    bool antivirusEnabled{false};
    bool realTimeProtectionEnabled{false};
    /// Tamper Protection: khi bật, Set-MpPreference có thể ÂM THẦM không đổi được cài đặt (không báo
    /// lỗi, cmdlet vẫn trả về như thành công) - đã xác nhận qua tài liệu Microsoft Learn. UI PHẢI đọc
    /// lại trạng thái thật sau mỗi lần bật/tắt, không được tin mù quáng rằng gọi xong là đã đổi thật.
    bool isTamperProtected{false};
    /// AMRunningMode thật từ Get-MpComputerStatus: "Normal" (Defender đang là AV chính, đang bảo vệ
    /// thật), "Passive" (có AV hãng khác đang hoạt động, Defender chỉ chạy nền), "EDR Block Mode", hoặc
    /// rỗng nếu không đọc được. UI nên cảnh báo rõ khi khác "Normal" - các nút bật/tắt bảo vệ không có
    /// nhiều ý nghĩa nếu Defender không phải AV đang hoạt động chính trên máy.
    QString runningMode;
    QString signatureVersion;
    QDateTime signatureLastUpdated;
    QDateTime lastQuickScan;
    QDateTime lastFullScan;
    /// Giá trị thật của Set-MpPreference -EnableNetworkProtection: "Enabled"/"Disabled"/"AuditMode"/rỗng.
    QString networkProtectionMode;

    bool isActivelyProtecting() const { return runningMode == "Normal"; }
    bool networkProtectionEnabled() const { return networkProtectionMode == "Enabled"; }
};

enum class ScanType { Quick, Full, Custom };

DefenderStatus getStatus(QString* error = nullptr);

/// Bật/tắt bảo vệ thời gian thực (Set-MpPreference -DisableRealtimeMonitoring). Cần quyền Administrator.
/// LUÔN đọc lại trạng thái thật ngay sau khi gọi và trả `*verifiedState` = trạng thái ĐỌC LẠI (không
/// phải trạng thái mong muốn) - nếu khác giá trị `enabled` yêu cầu, rất có thể do Tamper Protection chặn
/// âm thầm, KHÔNG phải lỗi của ứng dụng. UI phải hiển thị đúng `*verifiedState`, không hiển thị theo ý
/// định người dùng vừa bấm.
bool setRealtimeProtectionEnabled(bool enabled, bool* verifiedState, QString* error = nullptr);

/// Bật/tắt Network Protection (chặn kết nối tới trang/máy chủ độc hại theo danh tiếng thời gian thực
/// của Microsoft - đây là cơ chế THẬT đứng sau yêu cầu "chặn link độc hại ngay lúc truy vấn"). Cùng lưu
/// ý về Tamper Protection như trên - luôn đọc lại `*verifiedEnabled` sau khi gọi.
bool setNetworkProtectionEnabled(bool enabled, bool* verifiedEnabled, QString* error = nullptr);

/// Danh sách mối đe dọa Defender biết tới (Get-MpThreat - "danh mục" mối đe dọa duy nhất, khác
/// Get-MpThreatDetection là lịch sử TỪNG LẦN phát hiện).
QList<ThreatRecord> listThreats(QString* error = nullptr);

/// Xóa TẤT CẢ mối đe dọa đang hoạt động (Remove-MpThreat - đã xác nhận cmdlet này KHÔNG nhận tham số
/// chọn từng mục, luôn xóa toàn bộ). Cần quyền Administrator.
bool removeAllActiveThreats(QString* error = nullptr);

namespace internal
{
QString buildGetComputerStatusScript();
DefenderStatus parseComputerStatusJson(const QByteArray& json, QString* error);

QString buildSetRealtimeMonitoringScript(bool enabled);
QString buildSetNetworkProtectionScript(bool enabled);

QString buildStartScanScript(ScanType type, const QString& customPath);

QString buildListThreatsScript();
QList<ThreatRecord> parseThreatsJson(const QByteArray& json, QString* error);

QString buildRemoveAllThreatsScript();
} // namespace internal

} // namespace DefenderController
