#include "Logger.h"
#include "core/AppPaths.h"
#include <QDateTime>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

namespace
{
// Phát hiện THẬT khi rà soát độ ổn định chạy dài (Auto Click 24 giờ liên tục): mỗi hành động ghi ít
// nhất 1 dòng log, chạy nhiều giờ không giới hạn kích thước có thể khiến tệp log phình to - đặt mức
// trần hợp lý (không ảnh hưởng chẩn đoán sự cố bình thường, vài MB là quá đủ cho một phiên làm việc).
constexpr qint64 kMaxLogFileBytes = 10 * 1024 * 1024; // 10 MB
} // namespace

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

void Logger::rotateIfTooLarge(QFile& file)
{
    QFileInfo info(file.fileName());
    if (info.exists() && info.size() > kMaxLogFileBytes)
    {
        const QString oldPath = file.fileName() + ".1";
        QFile::remove(oldPath);      // bỏ bản ".1" cũ nếu có (chỉ giữ 1 bản sao lưu, đủ cho nhu cầu xem lại gần nhất)
        QFile::rename(file.fileName(), oldPath);
    }
}

Logger::Logger()
{
    // Đường dẫn tuyệt đối trong thư mục dữ liệu người dùng (xem AppPaths.h) - trước đây là "logs/..."
    // tương đối theo CWD, không ghi được khi cài vào Program Files hoặc khi CWD không phải thư mục exe.
    m_appLogFile.setFileName(AppPaths::logFile("app.log"));
    rotateIfTooLarge(m_appLogFile);
    // Không mở được log (đĩa đầy, thư mục bị khóa...) KHÔNG được làm ứng dụng dừng - log() tự bỏ qua tệp
    // chưa mở; chỉ báo ra kênh debug để còn dấu vết.
    if (!m_appLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning("Logger: không mở được %s", qUtf8Printable(m_appLogFile.fileName()));

    m_autoclickLogFile.setFileName(AppPaths::logFile("autoclick.log"));
    rotateIfTooLarge(m_autoclickLogFile);
    if (!m_autoclickLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        qWarning("Logger: không mở được %s", qUtf8Printable(m_autoclickLogFile.fileName()));
}

Logger::~Logger()
{
    QMutexLocker locker(&m_mutex);
    if (m_appLogFile.isOpen())
        m_appLogFile.close();
    if (m_autoclickLogFile.isOpen())
        m_autoclickLogFile.close();
}

void Logger::log(LogLevel level, const QString& module, const QString& message)
{
    QMutexLocker locker(&m_mutex);

    QString levelStr;
    switch (level)
    {
        case LogLevel::Debug:   levelStr = "DEBUG"; break;
        case LogLevel::Info:    levelStr = "INFO"; break;
        case LogLevel::Warning: levelStr = "WARN"; break;
        case LogLevel::Error:   levelStr = "ERROR"; break;
    }

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
    QString formatted = QString("[%1] [%2] [%3] %4\n")
                            .arg(timestamp)
                            .arg(levelStr)
                            .arg(module)
                            .arg(message);

    // Print to debug console
    qDebug().noquote() << formatted.trimmed();

    // Phát hiện THẬT khi rà soát độ ổn định chạy dài: flush() ép ghi đĩa thật ngay lập tức, tốn chi phí
    // I/O thật mỗi lần gọi. Một phiên Auto Click chạy 24 giờ với hành động lặp lại nhanh (vd mỗi 1-2
    // giây) có thể ghi log hàng chục nghìn lần - flush() đồng bộ mỗi lần như cũ có thể làm chậm/giật
    // thời gian của chính luồng thực thi hành động (ActionRunner gọi Logger trực tiếp, không qua luồng
    // riêng). Chỉ flush NGAY với Warning/Error (hiếm, quan trọng để chẩn đoán sự cố không bị mất nếu
    // crash đột ngột) - còn Info/Debug chỉ flush định kỳ (mỗi 20 dòng), vẫn đủ mới để xem log gần thời
    // gian thực khi cần, không ép ghi đĩa mỗi dòng.
    const bool forceFlush = (level == LogLevel::Warning || level == LogLevel::Error);
    ++m_callsSinceFlush;
    const bool doFlush = forceFlush || m_callsSinceFlush >= 20;
    if (doFlush)
        m_callsSinceFlush = 0;

    // Xoay vòng tệp log nếu đã quá lớn - kiểm tra ĐỊNH KỲ (mỗi 500 dòng, không phải mỗi dòng, tránh
    // thêm một lệnh gọi hệ thống stat() mỗi lần ghi) để một phiên chạy rất dài (không khởi động lại ứng
    // dụng) vẫn được xoay vòng giữa chừng, không chỉ lúc khởi động Logger.
    ++m_callsSinceRotateCheck;
    const bool checkRotate = m_callsSinceRotateCheck >= 500;
    if (checkRotate)
        m_callsSinceRotateCheck = 0;

    auto writeTo = [&](QFile& file) {
        if (!file.isOpen())
            return;

        if (checkRotate && file.size() > kMaxLogFileBytes)
        {
            file.close();
            rotateIfTooLarge(file);
            file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        }

        QTextStream stream(&file);
        stream << formatted;
        if (doFlush)
            file.flush();
    };

    writeTo(m_appLogFile);
    if (module.compare("AutoClick", Qt::CaseInsensitive) == 0)
        writeTo(m_autoclickLogFile);
}

void Logger::info(const QString& module, const QString& message)
{
    log(LogLevel::Info, module, message);
}

void Logger::warning(const QString& module, const QString& message)
{
    log(LogLevel::Warning, module, message);
}

void Logger::error(const QString& module, const QString& message)
{
    log(LogLevel::Error, module, message);
}

void Logger::debug(const QString& module, const QString& message)
{
    log(LogLevel::Debug, module, message);
}
