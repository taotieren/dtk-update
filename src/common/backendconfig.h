#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "iniparser.h"
#include "presetconfig.h"

namespace DtkUpdate
{

    /**
     * @brief 用户/系统级后端配置文件（conf / INI 风格）
     *
     * 配置文件示例：
     *   PreferredBackend = apt          # 用户喜欢的后端（只能从已注册后端中选）
     *   NoInstallRecommends = true      # 全局覆盖（对所有后端生效）
     *   AutoRemoveOrphans = true
     *   AutoCleanCache = false
     *
     *   [apt]                           # 各后端独立配置段（段名即后端 id）
     *   NoInstallRecommends = false
     *
     *   [dnf]
     *   AutoCleanCache = true
     *
     * 加载路径（按此顺序读取，后者覆盖前者相同键）：
     *   1. 系统级 /etc/dtk-update/backend.conf
     *   2. 用户级 ~/.config/dtk-update/backend.conf
     *
     * 若 PreferredBackend 不在已注册后端列表中，则回落到发行版预设默认后端。
     */
    class BackendConfig : public QObject
    {
        Q_OBJECT
      public:
        explicit BackendConfig(QObject* parent = nullptr);

        // 从默认路径加载；返回是否至少成功读取一个文件
        bool load();
        // 从指定文件列表加载（用于测试/自定义）
        bool loadFrom(const QStringList& paths);

        QVariantMap optionsFor(const QString& backendId) const;

        // 解析出最终生效的后端 id：配置 > 发行版预设（预设由调用方在 PresetConfig 处理）
        QString effectiveBackend(const QStringList& registeredBackends) const;

      protected:
        // 可被子类覆写以自定义路径（测试用）
        virtual QStringList systemPaths() const;
        virtual QStringList userPaths() const;

      private:
        void merge(const IniParser& parser);
        QVariantMap toVariantMap(const IniParser::SectionMap& src) const;

        QString m_preferred;
        IniParser::SectionMap m_globals;                 // 全局选项
        QMap<QString, IniParser::SectionMap> m_sections; // 各后端独立段（小写键）
    };

} // namespace DtkUpdate
