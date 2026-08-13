#include "backendconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include "iniparser.h"
#include "logger.h"

namespace DtkUpdate
{

    BackendConfig::BackendConfig(QObject* parent) : QObject(parent) {}

    QStringList BackendConfig::systemPaths() const
    {
        return {QStringLiteral("/etc/dtk-update/backend.conf")};
    }

    QStringList BackendConfig::userPaths() const
    {
        const QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
        return {cfgDir + QStringLiteral("/dtk-update/backend.conf")};
    }

    bool BackendConfig::load()
    {
        QStringList paths = systemPaths();
        paths.append(userPaths());
        return loadFrom(paths);
    }

    bool BackendConfig::loadFrom(const QStringList& paths)
    {
        m_preferred.clear();
        m_globals.clear();
        m_sections.clear();

        IniParser parser;
        bool any = false;
        for (const QString& p : paths)
        {
            if (QFile::exists(p))
            {
                QString err;
                if (parser.parseFile(p, &err))
                {
                    merge(parser);
                    any = true;
                }
                else
                {
                    qCWarning(dtkUpdateCore) << "skip backend.conf:" << err;
                }
            }
        }
        m_preferred = parser.globalValue(QStringLiteral("PreferredBackend")).trimmed();
        return any;
    }

    void BackendConfig::merge(const IniParser& parser)
    {
        const QString pref = parser.globalValue(QStringLiteral("PreferredBackend")).trimmed();
        if (!pref.isEmpty())
            m_preferred = pref;
        const auto globals = parser.globals();
        for (auto it = globals.constBegin(); it != globals.constEnd(); ++it)
            m_globals.insert(it.key(), it.value());
        const auto sections = parser.sections();
        for (auto sit = sections.constBegin(); sit != sections.constEnd(); ++sit)
        {
            auto& dst = m_sections[sit.key()];
            const auto& src = sit.value();
            for (auto kit = src.constBegin(); kit != src.constEnd(); ++kit)
                dst.insert(kit.key(), kit.value());
        }
    }

    QString BackendConfig::preferredBackend() const
    {
        return m_preferred;
    }

    QVariantMap BackendConfig::globalOptions() const
    {
        return toVariantMap(m_globals);
    }

    QVariantMap BackendConfig::optionsFor(const QString& backendId) const
    {
        QVariantMap result = toVariantMap(m_globals);
        if (!backendId.isEmpty())
        {
            const auto sit = m_sections.constFind(backendId.toLower());
            if (sit != m_sections.constEnd())
            {
                const QVariantMap sec = toVariantMap(sit.value());
                for (auto it = sec.constBegin(); it != sec.constEnd(); ++it)
                    result.insert(it.key(), it.value());
            }
        }
        return result;
    }

    QString BackendConfig::effectiveBackend(const QStringList& registered) const
    {
        if (m_preferred.isEmpty())
            return QString(); // 未配置 -> 交由调用方按发行版预设决定
        // 配置了有效后端 -> 使用
        for (const auto& r : registered)
            if (r.compare(m_preferred, Qt::CaseInsensitive) == 0)
                return r;
        // 配置了无效后端 -> 警告并回退到首个已注册（不静默使用错误后端）
        qCWarning(dtkUpdateCore) << "PreferredBackend not registered, fallback to first registered:"
                                 << m_preferred;
        return registered.isEmpty() ? QString() : registered.first();
    }

    QVariantMap BackendConfig::toVariantMap(const IniParser::SectionMap& src) const
    {
        QVariantMap out;
        for (auto it = src.constBegin(); it != src.constEnd(); ++it)
        {
            const QString& raw = it.value();
            const QString lowKey = it.key();
            // 布尔识别（仅当值显式为 true/false，区分大小写）
            if (raw == QStringLiteral("true"))
                out.insert(lowKey, true);
            else if (raw == QStringLiteral("false"))
                out.insert(lowKey, false);
            else
                out.insert(lowKey, raw);
        }
        return out;
    }

} // namespace DtkUpdate
