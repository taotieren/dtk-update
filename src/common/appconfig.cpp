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
                // 否则按发行版预设选择；若该预设后端当前不可用，
                // 保持为空，交由 BackendFactory 据实探测，绝不静默回退到无关后端。
                d->effectiveBackendId = PresetConfig::defaultBackendFor(d->family);
        }
    }

    QString AppConfig::checkIntervalUnit() const
    {
        const QString unit =
            d->cfg ? d->cfg->value("checkIntervalUnit", QStringLiteral("disabled")).toString()
                   : QStringLiteral("disabled");
        // 只接受合法值；非法值一律视为 disabled（绝不把未知配置解释成自动检测）
        if (unit == QLatin1String("hour") || unit == QLatin1String("day") ||
            unit == QLatin1String("month"))
            return unit;
        return QStringLiteral("disabled");
    }

    int AppConfig::checkIntervalValue() const
    {
        return d->cfg ? qMax(1, d->cfg->value("checkIntervalValue", 1).toInt()) : 1;
    }

    int AppConfig::effectiveCheckIntervalMinutes() const
    {
        const QString unit = checkIntervalUnit();
        const int v = checkIntervalValue();
        if (unit == QLatin1String("hour"))
            return v * 60;
        if (unit == QLatin1String("day"))
            return v * 24 * 60;
        if (unit == QLatin1String("month"))
            return v * 30 * 24 * 60; // 近似 30 天/月
        return 0;                    // disabled → 不开启定时检测
    }

    bool AppConfig::autoUpdateEnabled() const
    {
        return d->cfg ? d->cfg->value("autoUpdateEnabled", false).toBool() : false;
    }

    void AppConfig::setCheckIntervalUnit(const QString& unit)
    {
        // 写入端同样归一化：非法值一律按 disabled 落盘，避免脏值污染后续读取
        const QString normalized = (unit == QLatin1String("hour") || unit == QLatin1String("day") ||
                                    unit == QLatin1String("month"))
                                       ? unit
                                       : QStringLiteral("disabled");
        if (d->cfg)
            d->cfg->setValue("checkIntervalUnit", normalized);
    }

    void AppConfig::setCheckIntervalValue(int value)
    {
        if (d->cfg)
            d->cfg->setValue("checkIntervalValue", qMax(1, value));
    }

    void AppConfig::setAutoUpdateEnabled(bool enabled)
    {
        if (d->cfg)
            d->cfg->setValue("autoUpdateEnabled", enabled);
    }

    bool AppConfig::showSecurityAdvisory() const
    {
        return boolOption(QStringLiteral("ShowSecurityAdvisory"), true);
    }

    bool AppConfig::fetchUpstreamAdvisories() const
    {
        // 默认关闭上游安全公告预取（网络最小化、隐私优先），需用户在 backend.conf/DConfig
        // 显式开启 FetchUpstreamAdvisories=true。与 AGENTS.md「m_fetchUpstream 默认 false」一致。
        return boolOption(QStringLiteral("FetchUpstreamAdvisories"), false);
    }

    QString AppConfig::preferredBackend() const
    {
        return d->effectiveBackendId;
    }

    bool AppConfig::boolOption(const QString& key, bool dconfigDefault) const
    {
        // 优先级：backend.conf 后端段 > backend.conf 全局段 > DConfig > 发行版预设
        QVariantMap fileOpts = d->backendConfig->optionsFor(d->effectiveBackendId);
        if (fileOpts.contains(key) && !fileOpts[key].isNull())
            return fileOpts[key].toBool();

        if (d->cfg)
        {
            // DConfig schema 键均为小写（backend.conf 键保留原始大小写、iniparser 查询
            // 大小写不敏感）；若直接按原 key 查询，keyList().contains 永不命中，DConfig
            // 层的布尔开关将全部静默失效回落到默认值。
            const QString dkey = key.toLower();
            if (d->cfg->keyList().contains(dkey))
                return d->cfg->value(dkey, dconfigDefault).toBool();
        }

        QVariantMap preset = PresetConfig::defaultOptionsFor(d->effectiveBackendId);
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
        out += QStringLiteral("CheckIntervalUnit = ") + checkIntervalUnit() + QLatin1Char('\n');
        out += QStringLiteral("CheckIntervalValue = ") + QString::number(checkIntervalValue()) +
               QLatin1Char('\n');
        out += QStringLiteral("AutoUpdateEnabled = ") + (autoUpdateEnabled() ? "true" : "false") +
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
