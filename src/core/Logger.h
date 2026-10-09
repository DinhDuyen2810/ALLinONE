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

private:
    Logger();
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    /// Nếu tệp log đã vượt quá kích thước cho phép, đổi tên thành ".1" (ghi đè bản ".1" cũ nếu có) rồi
    /// bắt đầu tệp mới - tránh tệp log phình to không giới hạn khi ứng dụng/một phiên Auto Click chạy
    /// rất lâu (vd 24 giờ liên tục, hàng chục nghìn hành động mỗi phiên).
    void rotateIfTooLarge(QFile& file);

    QMutex m_mutex;
    QFile m_appLogFile;
    QFile m_autoclickLogFile;
    int m_callsSinceFlush{0};
    int m_callsSinceRotateCheck{0};
};
