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

    // Chờ theo từng bước nhỏ (200ms) thay vì một lần chờ dài 45s duy nhất - cho phép requestCancel()
    // dừng SỚM (vd người dùng đóng cửa sổ giữa chừng) thay vì phải đợi hết timeout.
    bool finished = false;
    bool canceled = false;
    for (int waited = 0; waited < 45000; waited += 200)
    {
        finished = proc.waitForFinished(200);
        if (finished)
            break;
        if (m_cancelRequested)
        {
            canceled = true;
            break;
        }
    }
    if (!finished)
    {
        proc.kill();
        proc.waitForFinished(2000);
        emit operationFinished(false, canceled ? "Đã hủy." : "Hết thời gian chờ kết nối VPN.");
        return;
    }

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    const bool ok = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    emit operationFinished(ok, ok ? out : (out.isEmpty() ? QString("rasdial thoát với mã lỗi %1").arg(proc.exitCode()) : out));
}
