#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

/// Một mối đe dọa Microsoft Defender đã phát hiện (từ Get-MpThreat) - struct thuần, không phụ thuộc
/// UI/PowerShell, test được bằng dữ liệu mẫu.
///
/// LƯU Ý QUAN TRỌNG (đã xác nhận qua tài liệu Microsoft Learn chính thức): `Remove-MpThreat` của
/// Windows KHÔNG có tham số `-ThreatID` - nó xóa TẤT CẢ mối đe dọa đang hoạt động cùng lúc, không chọn
/// riêng từng mục. Vì vậy `threatId` ở đây chỉ để HIỂN THỊ/đối chiếu, không dùng để xóa riêng lẻ - UI chỉ
/// có MỘT nút "Xóa tất cả mối đe dọa đang hoạt động" (xem DefenderController::removeAllActiveThreats).
struct ThreatRecord
{
    QString threatName;    ///< Vd "TrojanDownloader:PowerShell/EICAR_Test_File"
    QStringList resources; ///< Đường dẫn tệp/registry bị phát hiện (Resources - có thể nhiều mục/rỗng)
    int severity{0};       ///< SeverityID thật từ Defender: 0=Unknown, 1=Low, 2=Moderate, 3=High, 4=Severe
    qint64 threatId{0};    ///< ThreatID - chỉ để hiển thị/đối chiếu (xem lưu ý ở trên)
    bool isActive{false};  ///< IsActive thật từ Defender - còn hoạt động (chưa được xử lý dứt điểm) hay không

    QString severityLabel() const
    {
        // Tài liệu Microsoft Learn ghi nhận 0=Unknown,1=Low,2=Moderate,3=High,4=Severe, nhưng kiểm tra
        // tay trên máy thật (mối đe dọa EICAR) cho SeverityID=5 - ngoài phạm vi tài liệu. Coi mọi giá
        // trị >= 4 là "Nghiêm trọng" thay vì trả về "Không rõ" (tránh hiểu lầm Defender không đánh giá
        // được mức độ, trong khi thực ra chỉ là thang đo có thêm bậc cao hơn tài liệu liệt kê).
        switch (severity)
        {
            case 1: return "Thấp";
            case 2: return "Trung bình";
            case 3: return "Cao";
            default: return severity >= 4 ? "Nghiêm trọng" : "Không rõ";
        }
    }
};
