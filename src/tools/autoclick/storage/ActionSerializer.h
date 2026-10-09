#pragma once

#include "../model/ActionChain.h"
#include <vector>
#include <QString>

class ActionSerializer
{
public:
    /// Ghi NGUYÊN TỬ (QSaveFile: ghi ra tệp tạm cạnh tệp đích rồi mới đổi tên đè lên) - mất điện/đầy đĩa
    /// giữa chừng không để lại tệp hồ sơ bị cắt cụt. `error` (nếu có) nhận lý do thất bại để hiện cho
    /// người dùng. KHÔNG tự tạo thư mục: tệp hồ sơ mặc định đã có thư mục do AppPaths tạo sẵn, còn xuất
    /// ra nơi khác thì thư mục do người dùng chọn qua hộp thoại.
    static bool saveToFile(const QString& filePath, const std::vector<ActionChain>& chains, QString* error = nullptr);

    /// Trả về false + `error` nếu không mở được tệp, JSON hỏng, hoặc thiếu mảng "chains" - KHÔNG coi một
    /// tệp sai định dạng là "hồ sơ rỗng hợp lệ" (nơi gọi sẽ ghi đè mất tệp gốc của người dùng).
    static bool loadFromFile(const QString& filePath, std::vector<ActionChain>& outChains, QString* error = nullptr);

    static QString toJsonString(const std::vector<ActionChain>& chains);
    static bool fromJsonString(const QString& jsonStr, std::vector<ActionChain>& outChains, QString* error = nullptr);

    /// `%LOCALAPPDATA%\OneForAll\profiles\default.json` (xem core/AppPaths.h) - trước đây là đường dẫn
    /// TƯƠNG ĐỐI "profiles/default.json" theo thư mục làm việc của tiến trình.
    static QString getDefaultProfilePath();
};
