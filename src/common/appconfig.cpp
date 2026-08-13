#include "appconfig.h"

#include <DConfig>
#include <QDebug>

#include "presetconfig.h"

DCORE_USE_NAMESPACE

namespace DtkUpdate
{

    // 后端选项键
    namespace Opt
    {
        const char* NoInstallRecommends = "NoInstallRecommends";
        const char* AutoRemoveOrphans = "AutoRemoveOrphans";
        const char* AutoCleanCache = "AutoCleanCache";
    } // namespace Opt

    class AppConfig::Private
    {
      public:
        DConfig* cfg{nullptr};
        BackendConfig* backendConfig{nullptr};
        DistroProbe::Family family{DistroProbe::Family::Unknown};
        QString effectiveBackendId; // 解析后的首选后端
    };

    AppConfig::AppConfig(QObject* parent) : QObject(parent), d(new Private)
    {
        // 固定 appId，避免不同前端（gui/daemon/tray）派生出不同配置域
        d->cfg = DConfig::create(QStringLiteral("org.deepin.dtk-update"),
                                 QStringLiteral("org.deepin.dtk-update"), QString(), this);
        if (!d->cfg)
        {
            qWarning() << "DConfig unavailable, fall back to defaults";
        }
        if (d->cfg)
        {
            connect(d->cfg, &DConfig::valueChanged, this, &AppConfig::configChanged);
        }

        d->family = DistroProbe::detectFamily();
        loadConfigFile();
    }

    void AppConfig::loadConfigFile()
    {
        d->backendConfig = new BackendConfig(this);
        d->backendConfig->load();

        // 解析最终后端 id：配置文件 > DConfig > 发行版预设
        QStringList registered = PresetConfig::knownBackendIds();
        d->effectiveBackendId = d->backendConfig->effectiveBackend(registered);
        if (d->effectiveBackendId.isEmpty())
        {
            // 仍为空则尝试 DConfig 指定的后端（若已注册）
            QString fromDc =
                d->cfg ? d->cfg->value("preferredBackend", QString()).toString() : QString();
            if (!fromDc.isEmpty() && registered.contains(fromDc))
                d->effectiveBackendId = fromDc;
            else
                // 否则按发行版预设选择；若预设后端未实现（如无 pacman/zypper 后端），
                // 保持为空，交由 BackendFactory 据实探测，绝不静默回退到无关后端。
                d->effectiveBackendId = PresetConfig::defaultBackendFor(d->family);
        }
    }

    int AppConfig::checkIntervalMinutes() const
    {
        return d->cfg ? d->cfg->value("checkIntervalMinutes", 360).toInt() : 360;
    }

    bool AppConfig::showSecurityAdvisory() const
    {
        return boolOption(QStringLiteral("ShowSecurityAdvisory"), true);
    }

    bool AppConfig::fetchUpstreamAdvisories() const
    {
        return boolOption(QStringLiteral("FetchUpstreamAdvisories"), true);
    }

    QString AppConfig::preferredBackend() const
    {
        return d->effectiveBackendId;
    }

    bool AppConfig::boolOption(const QString& key, bool dconfigDefault) const
    {
        // 优先级：配置文件该后端段 > 配置文件全局 > DConfig > 发行版预设
        QVariantMap fileOpts = d->backendConfig->optionsFor(d->effectiveBackendId);
        if (fileOpts.contains(key) && !fileOpts[key].isNull())
            return fileOpts[key].toBool();

        if (d->cfg && d->cfg->keyList().contains(key))
            return d->cfg->value(key, dconfigDefault).toBool();

        QVariantMap preset = PresetConfig::defaultOptionsFor(d->effectiveBackendId, d->family);
        if (preset.contains(key))
            return preset[key].toBool();
        return dconfigDefault;
    }

    bool AppConfig::noInstallRecommends() const
    {
        return boolOption(Opt::NoInstallRecommends, true);
    }

    bool AppConfig::autoRemoveOrphans() const
    {
        return boolOption(Opt::AutoRemoveOrphans, true);
    }

    bool AppConfig::autoCleanCache() const
    {
        return boolOption(Opt::AutoCleanCache, false);
    }

    DistroProbe::Family AppConfig::distroFamily() const
    {
        return d->family;
    }

    QString AppConfig::showConfig() const
    {
        // 配置透明化：把最终生效配置（合并后）以 conf 风格文本输出，便于用户核实。
        QString out;
        out += QStringLiteral(
            "# Effective configuration (merged: backend.conf > DConfig > distro preset)\n");
        out += QStringLiteral("PreferredBackend = ") + d->effectiveBackendId + QLatin1Char('\n');
        out += QStringLiteral("CheckIntervalMinutes = ") + QString::number(checkIntervalMinutes()) +
               QLatin1Char('\n');
        out += QStringLiteral("NoInstallRecommends = ") +
               (noInstallRecommends() ? "true" : "false") + QLatin1Char('\n');
        out += QStringLiteral("AutoRemoveOrphans = ") + (autoRemoveOrphans() ? "true" : "false") +
               QLatin1Char('\n');
        out += QStringLiteral("AutoCleanCache = ") + (autoCleanCache() ? "true" : "false") +
               QLatin1Char('\n');
        out += QStringLiteral("ShowSecurityAdvisory = ") +
               (showSecurityAdvisory() ? "true" : "false") + QLatin1Char('\n');
        out += QStringLiteral("FetchUpstreamAdvisories = ") +
               (fetchUpstreamAdvisories() ? "true" : "false") + QLatin1Char('\n');
        out += QStringLiteral("DistroFamily = ") + DistroProbe::familyName(d->family) +
               QLatin1Char('\n');
        return out;
    }

} // namespace DtkUpdate
