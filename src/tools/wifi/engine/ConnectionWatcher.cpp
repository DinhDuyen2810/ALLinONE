#include "ConnectionWatcher.h"

#include "WlanController.h"

#include <QTimer>

ConnectionWatcher::ConnectionWatcher(WlanController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_timer(new QTimer(this))
{
    m_timer->setInterval(kPollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &ConnectionWatcher::onTick);
}

void ConnectionWatcher::watch(const QString& adapterGuid, const QString& ssid, const QString& profileName)
{
    m_adapterGuid = adapterGuid;
    m_ssid = ssid;
    m_profileName = profileName;
    m_elapsedMs = 0;
    m_sawActivity = false;
    m_timer->start();
}

void ConnectionWatcher::cancel()
{
    m_timer->stop();
}

bool ConnectionWatcher::isActive() const
{
    return m_timer->isActive();
}

void ConnectionWatcher::onTick()
{
    m_elapsedMs += kPollIntervalMs;
    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);

    const bool ssidMatches = !m_ssid.isEmpty() && cur.ssid == m_ssid;
    const bool profileMatches = !m_profileName.isEmpty() && cur.profileName == m_profileName;
    const bool connectedToTarget = cur.isConnected && (ssidMatches || profileMatches);

    // "Hoạt động" = đang liên kết/xác thực, HOẶC đã nối đúng mạng - KHÔNG tính việc đang còn nối mạng CŨ khác.
    if (cur.connecting || connectedToTarget)
        m_sawActivity = true;

    switch (evaluateAttempt(connectedToTarget, cur.connecting, m_sawActivity, m_elapsedMs))
    {
    case Outcome::Succeeded:
        m_timer->stop();
        emit succeeded(cur.ssid);
        return;
    case Outcome::Failed:
        m_timer->stop();
        emit failed(m_ssid.isEmpty() ? m_profileName : m_ssid);
        return;
    case Outcome::Pending:
        break;
    }
}
