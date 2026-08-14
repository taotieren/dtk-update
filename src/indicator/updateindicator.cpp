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

        // 跨发行系接入可选的玲珑(linyaps)后端：由 BackendFactory 统一探测，
        // 可用则接入 monitor 聚合，不可用则自动丢弃，此处无需重复样板。
        BackendFactory::attachLinyaps(m_monitor, m_config, this);

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
