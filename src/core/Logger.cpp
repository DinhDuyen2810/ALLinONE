#include "Logger.h"
#include <QDir>
#include <QDateTime>
#include <QTextStream>
#include <QDebug>

Logger& Logger::instance()
{
    static Logger s_instance;
    return s_instance;
}

Logger::Logger()
{
    ensureLogDir();
    m_appLogFile.setFileName("logs/app.log");
    m_appLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);

    m_autoclickLogFile.setFileName("logs/autoclick.log");
    m_autoclickLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

Logger::~Logger()
{
    QMutexLocker locker(&m_mutex);
    if (m_appLogFile.isOpen())
        m_appLogFile.close();
    if (m_autoclickLogFile.isOpen())
        m_autoclickLogFile.close();
}

void Logger::ensureLogDir()
{
    QDir dir("logs");
    if (!dir.exists())
    {
        dir.mkpath(".");
    }
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

    // Write to app.log
    if (m_appLogFile.isOpen())
    {
        QTextStream stream(&m_appLogFile);
        stream << formatted;
        m_appLogFile.flush();
    }

    // If autoclick module, write to autoclick.log too
    if (module.compare("AutoClick", Qt::CaseInsensitive) == 0 && m_autoclickLogFile.isOpen())
    {
        QTextStream stream(&m_autoclickLogFile);
        stream << formatted;
        m_autoclickLogFile.flush();
    }
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
