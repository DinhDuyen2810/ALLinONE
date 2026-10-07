#include "VpnConnector.h"

#include <QProcess>

VpnConnector::VpnConnector(QObject* parent)
    : QThread(parent)
{
}

void VpnConnector::setConnectTarget(const QString& connectionName, const QString& username, const QString& password)
{
    m_mode = Mode::Connect;
    m_connectionName = connectionName;
    m_username = username;
    m_password = password;
}

void VpnConnector::setDisconnectTarget(const QString& connectionName)
{
    m_mode = Mode::Disconnect;
    m_connectionName = connectionName;
    m_username.clear();
    m_password.clear();
}

void VpnConnector::run()
{
    QStringList args;
    args << m_connectionName;
    if (m_mode == Mode::Disconnect)
    {
        args << "/disconnect";
    }
    else
    {
        if (!m_username.isEmpty())
            args << m_username << m_password;
    }

    QProcess proc;
    proc.setProgram("rasdial.exe");
    proc.setArguments(args);
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        emit operationFinished(false, "Không khởi chạy được rasdial.exe.");
        return;
    }
    // Bắt tay giao thức VPN qua mạng có thể mất tới nửa phút với máy chủ chậm/xa.
    if (!proc.waitForFinished(45000))
    {
        proc.kill();
        proc.waitForFinished(2000);
        emit operationFinished(false, "Hết thời gian chờ kết nối VPN.");
        return;
    }

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    emit operationFinished(ok, ok ? out : (out.isEmpty() ? QString("rasdial thoát với mã lỗi %1").arg(proc.exitCode()) : out));
}
