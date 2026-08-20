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

        // 定时检测更新单位："disabled" | "hour" | "day" | "month"。默认 "disabled"（不开启，
        // 仅手动/事件触发检查）。非法值一律按 disabled 处理，绝不解读成自动检测。
        virtual QString checkIntervalUnit() const;

        // 定时检测间隔数值（>=1），与 checkIntervalUnit 配合使用（如 2 + "day" = 每 2 天）
        virtual int checkIntervalValue() const;

        // 换算后的定时检测间隔（分钟）；disabled/未知单位 → 0（表示不开启定时检测）
        virtual int effectiveCheckIntervalMinutes() const;

        // 自动更新：默认关闭。开启后仅在「定时检测」发现更新且无安全公告/预检建议时
        // 自动安装；存在任何安全风险仍会先征求用户确认（never decide for the user）。
        virtual bool autoUpdateEnabled() const;

        // 写入接口（写 DConfig，configChanged 信号自动发出，monitor 据此热更新调度）
        void setCheckIntervalUnit(const QString& unit);
        void setCheckIntervalValue(int value);
        void setAutoUpdateEnabled(bool enabled);

        // 显示安全提示
        virtual bool showSecurityAdvisory() const;

        // 是否到对应发行版/软件包的上游官方安全公告源获取更新注意信息（仅展示，失败静默降级）
        bool fetchUpstreamAdvisories() const;

        // 配置透明化：导出当前生效的最终配置（合并文件/DConfig/预设后），供 CLI --show-config 与 UI
        // 展示
        QString showConfig() const;

        // 首选包管理器后端（解析后的值：配置文件优先，否则 DConfig，否则发行版预设）
        QString preferredBackend() const;

        // 安装时是否跳过可选依赖（Recommends）
        virtual bool noInstallRecommends() const;

        // 自动移除孤儿依赖（autoremove）。升级成功后按此开关执行后台清理（monitor 接线）。
        virtual bool autoRemoveOrphans() const;

        // 自动清理下载缓存。升级成功后按此开关执行后台清理（monitor 接线）。
        virtual bool autoCleanCache() const;

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
