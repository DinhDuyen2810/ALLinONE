#pragma once

#include <QString>
#include <QMutex>
#include <QFile>

enum class LogLevel
{
    Debug,
    Info,
    Warning,
    Error
};

class Logger
{
public:
    static Logger& instance();

    void log(LogLevel level, const QString& module, const QString& message);
    void info(const QString& module, const QString& message);
    void warning(const QString& module, const QString& message);
    void error(const QString& module, const QString& message);
    void debug(const QString& module, const QString& message);

    /// Một tệp log đang KHÔNG mở được (mở thất bại lúc khởi tạo hoặc sau khi xoay vòng: đĩa đầy, thư mục bị
    /// khóa, phần mềm diệt virus đang giữ tệp...) được thử mở lại sau mỗi chừng này lần ghi bị bỏ qua.
    /// Trước đây không bao giờ thử lại - một lỗi tạm thời lúc khởi động tắt log của cả phiên làm việc.
    /// Giới hạn tần suất để lỗi kéo dài không biến mỗi dòng log thành một lần gọi hệ thống thất bại.
    static constexpr int kReopenRetryEveryCalls = 50;

    /// CHỈ dành cho bộ kiểm thử: đóng cả hai tệp log và THÔI ghi tệp (kênh debug vẫn in) cho tới khi gọi
    /// reopenFiles() - KHÔNG tự thử mở lại. Lý do: Logger là singleton sống tới sau khi `main` trả về và giữ
    /// `logs\app.log` + `logs\autoclick.log` mở suốt; trên Windows tệp đang mở không xóa được, nên thư mục
    /// dữ liệu tạm của test (AppPaths::setDataDirOverride) không bao giờ dọn sạch nếu không đóng trước -
    /// xem tests/TestDataDir.h.
    void closeFiles();

    /// CHỈ dành cho bộ kiểm thử: đóng rồi mở lại hai tệp log theo đường dẫn HIỆN TẠI của AppPaths (dùng
    /// sau khi đổi AppPaths::setDataDirOverride). Mở thất bại thì cơ chế tự thử lại ở trên tiếp quản.
    void reopenFiles();

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Nếu tệp log đã vượt quá kích thước cho phép, đổi tên thành ".1" (ghi đè bản ".1" cũ nếu có) rồi
    /// bắt đầu tệp mới - tránh tệp log phình to không giới hạn khi ứng dụng/một phiên Auto Click chạy
    /// rất lâu (vd 24 giờ liên tục, hàng chục nghìn hành động mỗi phiên).
    void rotateIfTooLarge(QFile& file);

    /// Xoay vòng nếu cần rồi mở tệp ở chế độ ghi nối. Gọi khi đang giữ m_mutex (hoặc trong constructor).
    bool openLogFile(QFile& file);

    QMutex m_mutex;
    QFile m_appLogFile;
    QFile m_autoclickLogFile;
    int m_callsSinceFlush{0};
    int m_callsSinceRotateCheck{0};
    int m_appSkippedWrites{0};       // số lần ghi bị bỏ qua từ lần thử mở app.log gần nhất
    int m_autoclickSkippedWrites{0}; // như trên, cho autoclick.log
    bool m_closedOnPurpose{false};   // closeFiles() đã gọi - không ghi tệp, không tự mở lại
};
