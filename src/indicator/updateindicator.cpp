#include "updateindicator.h"

#include "core/package/backendfactory.h"
#include "logger.h"

namespace DtkUpdate
{

    UpdateIndicator::UpdateIndicator(QObject* parent) : QObject(parent)
    {
        setupBackend();
    }

    UpdateIndicator::~UpdateIndicator() = default;

    void UpdateIndicator::setupBackend()
    {
        m_config = new AppConfig(this);
        m_backend = BackendFactory::createBackend(this, m_config->preferredBackend());
        if (m_backend)
            m_backend->setConfig(m_config);
        m_monitor = new UpdateMonitor(m_backend, m_config, this);
        m_monitor->setSecurityAdvisor(m_advisor = new SecurityAdvisor(this));
        m_advisor->setFetchUpstream(m_config->fetchUpstreamAdvisories());

        // 跨发行版接入可选的玲珑(linyaps)沙箱应用后端：无论当前发行系为何，
        // 只要 ll-cli 运行环境健康即聚合；不可用时经 onBackendUnavailable 提示用户。
        m_linyaps = BackendFactory::createById(QStringLiteral("linyaps"), this);
        if (m_linyaps)
        {
            if (!m_linyaps->isAvailable())
            {
                qCInfo(dtkUpdateTray)
                    << "linglong backend not available:" << m_linyaps->availabilityError();
                m_linyaps->deleteLater();
                m_linyaps = nullptr;
            }
            else
            {
                m_linyaps->setConfig(m_config);
                m_monitor->setLinyapsBackend(m_linyaps);
            }
        }

        connect(m_monitor, &UpdateMonitor::backendUnavailable, this,
                &UpdateIndicator::onBackendUnavailable);
        connect(m_monitor, &UpdateMonitor::updatesAvailable, this,
                [this](const PackageList& pkgs)
                {
                    Q_UNUSED(pkgs)
                    emit stateChanged();
                });
        connect(m_monitor, &UpdateMonitor::securityPrompt, this,
                [this](const QString& sev, const QList<SecurityAdvisor::Advisory>& advs,
                       const PreCheckReport& pre) { onSecurityPrompt(sev, advs, pre); });
        connect(m_monitor, &UpdateMonitor::postCheck, this,
                [this](const PostCheckReport& rep) { onPostCheck(rep); });
        connect(m_monitor, &UpdateMonitor::stateChanged, this,
                [this]
                {
                    const bool has =
                        m_monitor && m_monitor->state() == UpdateMonitor::State::HasUpdates;
                    const int count = m_monitor ? m_monitor->upgradable().size() : 0;
                    onStateChanged(has, count);
                });
        m_monitor->start();
    }

} // namespace DtkUpdate
