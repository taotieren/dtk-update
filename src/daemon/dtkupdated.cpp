#include "dtkupdated.h"
#include "logger.h"
#include "core/package/backendfactory.h"
#include "core/security/securityadvisor.h"

#include <QDBusConnection>
#include <QDBusError>

namespace DtkUpdate {

Daemon::Daemon(QObject *parent) : QObject(parent)
{
    m_config = new AppConfig(this);
    m_backend = BackendFactory::createBackend(this, m_config->preferredBackend());
    if (m_backend)
        m_backend->setConfig(m_config);
    m_advisor = new SecurityAdvisor(this);
    m_monitor = new UpdateMonitor(m_backend, m_config, this);
    m_monitor->setSecurityAdvisor(m_advisor);
    m_monitor->start();
}

Daemon::~Daemon() = default;

bool Daemon::registerOnBus()
{
    return QDBusConnection::sessionBus().registerService(
               QStringLiteral("com.dtk.update.Daemon"))
        && QDBusConnection::sessionBus().registerObject(
               QStringLiteral("/com/dtk/update/Daemon"), this);
}

void Daemon::checkNow()
{
    m_monitor->checkNow();
}

QVariantMap Daemon::status() const
{
    QVariantMap m;
    m.insert(QStringLiteral("updatable"),
             m_monitor ? m_monitor->upgradable().size() : 0);
    m.insert(QStringLiteral("lastCheck"),
             m_monitor ? m_monitor->lastCheck().toString(Qt::ISODate) : QString());
    return m;
}

}  // namespace DtkUpdate
