#pragma once

#include <QString>
#include <QStringList>

/// Phân tích TĨNH (không chạy lệnh) một dòng lệnh PowerShell để tìm dấu hiệu nguy hiểm/bất thường
/// thường gặp trong mã độc/kỹ thuật tấn công (download-and-execute, lệnh mã hóa Base64, bỏ qua chính
/// sách thực thi, công cụ tấn công đã biết, kỹ thuật né tránh phòng thủ, thiết lập duy trì...). Thuần
/// Qt, không gọi PowerShell/Win32 - test được đầy đủ bằng chuỗi mẫu.
///
/// LƯU Ý QUAN TRỌNG: đây là lớp TƯ VẤN/CẢNH BÁO (giống công cụ phân tích của một nhà phân tích bảo mật
/// tự kiểm tra dòng lệnh khả nghi trước khi chạy thật), KHÔNG phải lớp chặn thực thi - ứng dụng này
/// không bao giờ tự chạy lệnh người dùng nhập (xem CommandLauncher.h). Vì vậy danh sách mẫu dấu hiệu
/// dưới đây ưu tiên ít bỏ sót hơn là tuyệt đối chính xác; người dùng luôn là người quyết định cuối cùng.
namespace CommandAnalyzer
{

enum class RiskLevel
{
    Safe,       ///< Không phát hiện dấu hiệu bất thường nào
    Suspicious, ///< Có dấu hiệu đáng chú ý nhưng không chắc chắn là tấn công (vd ẩn cửa sổ)
    Dangerous,  ///< Khớp mẫu thường gặp trong mã độc/tấn công thật (tải+thực thi, mã hóa, công cụ tấn
                ///< công đã biết, né tránh phòng thủ, thiết lập duy trì...)
};

struct Verdict
{
    RiskLevel level{RiskLevel::Safe};
    QStringList reasons; ///< Giải thích bằng tiếng Việt cho từng dấu hiệu khớp được, để người dùng tự xem xét

    bool isSafe() const { return level == RiskLevel::Safe; }
};

/// Phân tích một hoặc nhiều dòng lệnh (dán nguyên khối cũng được - xét trên toàn bộ văn bản).
Verdict analyze(const QString& command);

QString riskLevelLabel(RiskLevel level);

} // namespace CommandAnalyzer
