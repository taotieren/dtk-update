#pragma once

#include <QObject>
#include <QString>

#include "backendconfig.h"

namespace DtkUpdate
{

    /**
     * @brief 应用配置封装（三级合并：配置文件 > DConfig > 发行版预设）
     *
     * 优先级（高 -> 低）：
     *   1. 用户/系统后端配置文件（conf / INI 风格，可手编）
     *   2. DConfig 运行时配置（控制中心/命令行）
     *   3. 发行版内置预设（PresetConfig，按 /etc/os-release 选择默认后端与默认选项）
     *
     * 所有可配置行为均经此暴露，保持配置透明、可调试。
     */
    class AppConfig : public QObject
    {
        Q_OBJECT
      public:
        explicit AppConfig(QObject* parent = nullptr);

        // 检查间隔（分钟），0 表示仅手动检查
        int checkIntervalMinutes() const;

        // 显示安全提示
        bool showSecurityAdvisory() const;

        // 是否到对应发行版/软件包的上游官方安全公告源获取更新注意信息（仅展示，失败静默降级）
        bool fetchUpstreamAdvisories() const;

        // 配置透明化：导出当前生效的最终配置（合并文件/DConfig/预设后），供 CLI --show-config 与 UI
        // 展示
        QString showConfig() const;

        // 首选包管理器后端（解析后的值：配置文件优先，否则 DConfig，否则发行版预设）
        QString preferredBackend() const;

        // 安装时是否跳过可选依赖（Recommends）
        bool noInstallRecommends() const;

        // 自动移除孤儿依赖（autoremove）
        bool autoRemoveOrphans() const;

        // 自动清理下载缓存
        bool autoCleanCache() const;

        // 当前生效后端所属发行系（用于 UI 展示）
        DistroProbe::Family distroFamily() const;

      signals:
        void configChanged();

      private:
        bool boolOption(const QString& key, bool dconfigDefault) const;
        void loadConfigFile();

        class Private;
        Private* d;
    };

} // namespace DtkUpdate
