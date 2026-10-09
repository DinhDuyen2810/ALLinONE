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
    // Không mở được log (đĩa đầy, thư mục bị khóa...) KHÔNG được làm ứng dụng dừng - log() bỏ qua tệp chưa
    // mở và thử mở lại định kỳ (kReopenRetryEveryCalls); chỉ báo ra kênh debug để còn dấu vết.
    if (!openLogFile(m_appLogFile))
        qWarning("Logger: không mở được %s", qUtf8Printable(m_appLogFile.fileName()));

    m_autoclickLogFile.setFileName(AppPaths::logFile("autoclick.log"));
    if (!openLogFile(m_autoclickLogFile))
        qWarning("Logger: không mở được %s", qUtf8Printable(m_autoclickLogFile.fileName()));
}

bool Logger::openLogFile(QFile& file)
{
    rotateIfTooLarge(file);
    return file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Logger::closeFiles()
{
    QMutexLocker locker(&m_mutex);
    m_closedOnPurpose = true;
    if (m_appLogFile.isOpen())
        m_appLogFile.close();
    if (m_autoclickLogFile.isOpen())
        m_autoclickLogFile.close();
}

void Logger::reopenFiles()
{
    // Lấy đường dẫn TRƯỚC khi khóa m_mutex - không gọi sang AppPaths trong lúc đang giữ khóa của Logger.
    const QString appPath = AppPaths::logFile("app.log");
    const QString autoclickPath = AppPaths::logFile("autoclick.log");

    QMutexLocker locker(&m_mutex);
    if (m_appLogFile.isOpen())
        m_appLogFile.close();
    if (m_autoclickLogFile.isOpen())
        m_autoclickLogFile.close();
    m_closedOnPurpose = false;
    m_appSkippedWrites = 0;
    m_autoclickSkippedWrites = 0;
    m_appLogFile.setFileName(appPath);
    openLogFile(m_appLogFile);
    m_autoclickLogFile.setFileName(autoclickPath);
    openLogFile(m_autoclickLogFile);
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

    auto writeTo = [&](QFile& file, int& skippedWrites) {
        if (!file.isOpen())
        {
            // Tệp đang không mở được (lỗi lúc khởi tạo hoặc sau khi xoay vòng). Trước đây bỏ qua MÃI MÃI -
            // một lỗi tạm thời (phần mềm diệt virus giữ tệp, đĩa đầy rồi được dọn) tắt log cả phiên. Nay
            // thử mở lại, nhưng chỉ sau mỗi kReopenRetryEveryCalls lần ghi bị bỏ qua để lỗi kéo dài không
            // tốn một lần gọi hệ thống thất bại cho từng dòng log. closeFiles() (test) thì không mở lại.
            if (m_closedOnPurpose || ++skippedWrites < kReopenRetryEveryCalls)
                return;
            skippedWrites = 0;
            if (!openLogFile(file))
                return;
        }
        else if (checkRotate && file.size() > kMaxLogFileBytes)
        {
            file.close();
            if (!openLogFile(file)) // mở lại sau xoay vòng thất bại: bỏ dòng này, lần sau thử lại như trên
                return;
        }

        QTextStream stream(&file);
        stream << formatted;
        if (doFlush)
            file.flush();
    };

    writeTo(m_appLogFile, m_appSkippedWrites);
    if (module.compare("AutoClick", Qt::CaseInsensitive) == 0)
        writeTo(m_autoclickLogFile, m_autoclickSkippedWrites);
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
