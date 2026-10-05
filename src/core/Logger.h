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

    void ensureLogDir();

    QMutex m_mutex;
    QFile m_appLogFile;
    QFile m_autoclickLogFile;
};
