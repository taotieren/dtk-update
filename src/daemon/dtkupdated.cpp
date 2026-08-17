#include "dtkupdated.h"

#include <QDBusConnection>
#include <QDBusError>

#include "core/package/backendfactory.h"
#include "core/security/securityadvisor.h"
#include "logger.h"

namespace DtkUpdate
{

    Daemon::Daemon(QObject* parent) : QObject(parent)
    {
        m_config = new AppConfig(this);
        m_backend = BackendFactory::createBackend(this, m_config->preferredBackend());
        if (m_backend)
            m_backend->setConfig(m_config);
        m_advisor = new SecurityAdvisor(this);
        m_advisor->setFetchUpstream(m_config->fetchUpstreamAdvisories());
        m_monitor = new UpdateMonitor(m_backend, m_config, this);
        m_monitor->setSecurityAdvisor(m_advisor);
        // 聚合沙箱式应用商店后端（linglong/snap/flatpak）：与系统后端正交、跨发行系，
        // 必须显式接入 monitor 才能参与可升级列表聚合，否则 daemon 上报的 updatable
        // 会漏掉沙箱应用。
        BackendFactory::attachSandboxBackends(m_monitor, m_config, this);
        m_monitor->start();

        // 订阅系统唤醒与网络恢复信号，唤醒/联网后自动重新检查更新。
        // Qt6 D-Bus 无 functor 重载，必须用旧式 SLOT() 字符串连接，且槽参数须匹配。
        QDBusConnection systemBus = QDBusConnection::systemBus();
        systemBus.connect(
            QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
            QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"),
            QStringLiteral("b"), this, SLOT(onPrepareForSleep(bool)));
        systemBus.connect(QStringLiteral("org.freedesktop.NetworkManager"),
                          QStringLiteral("/org/freedesktop/NetworkManager"),
                          QStringLiteral("org.freedesktop.NetworkManager"),
                          QStringLiteral("StateChanged"), QStringLiteral("u"), this,
                          SLOT(onNmStateChanged(uint)));
    }

    Daemon::~Daemon() = default;

    void Daemon::onPrepareForSleep(bool sleeping)
    {
        // sleeping=true 为即将挂起，false 为已唤醒；仅在唤醒后重检。
        if (!sleeping)
            m_monitor->checkNow();
    }

    void Daemon::onNmStateChanged(uint state)
    {
        // NM_STATE_CONNECTED_GLOBAL = 70，仅在网络达全局连通时重检。
        if (state == 70)
            m_monitor->checkNow();
    }

    bool Daemon::registerOnBus()
    {
        return QDBusConnection::sessionBus().registerService(
                   QStringLiteral("com.dtk.update.Daemon")) &&
               QDBusConnection::sessionBus().registerObject(
                   QStringLiteral("/com/dtk/update/Daemon"), this);
    }

    void Daemon::checkNow()
    {
        m_monitor->checkNow();
    }

    QVariantMap Daemon::status() const
    {
        QVariantMap m;
        m.insert(QStringLiteral("updatable"), m_monitor ? m_monitor->upgradable().size() : 0);
        m.insert(QStringLiteral("lastCheck"),
                 m_monitor ? m_monitor->lastCheck().toString(Qt::ISODate) : QString());
        return m;
    }

} // namespace DtkUpdate
