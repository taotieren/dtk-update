#include "dtkupdated.h"

#include <QDBusConnection>

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

        // 自动更新场景：daemon 无 UI，无法弹确认框。若定时检测发现需确认的安全公告/预检
        // 建议（needConfirm=true），自动更新会在该闸门安全暂停（绝不静默继续）；此处仅记录
        // 日志，用户可稍后经 GUI/托盘确认后继续。
        connect(m_monitor, &UpdateMonitor::securityPrompt, this,
                [](const QString& sev, const QList<SecurityAdvisor::Advisory>& advs,
                   const PreCheckReport& pre)
                {
                    Q_UNUSED(advs);
                    Q_UNUSED(pre);
                    qCWarning(dtkUpdateDaemon) << "auto-update paused pending user confirmation"
                                               << "(severity:" << sev << ')';
                });

        // 系统唤醒（login1 PrepareForSleep）与网络恢复（NetworkManager StateChanged）的
        // 自动重检由 UpdateMonitor 构造内订阅（watchLogindResume + 连接 NM StateChanged），
        // daemon 若再订阅一遍会对同一信号触发两次 checkNow；故此处不再重复订阅。
        m_monitor->start();
    }

    Daemon::~Daemon() = default;

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
