#pragma once

#include <QString>
#include <QVariantMap>

#include "distroprobe.h"

namespace DtkUpdate
{

    /**
     * @brief 发行版预设配置（代码内置，随二进制发布）
     *
     * 提供两层信息：
     *   1. 发行系 -> 默认后端 id（如 Debian 系默认 apt，Fedora 系默认 dnf）
     *   2. 各后端在该发行系下的默认选项（可在配置文件中被覆盖）
     *
     * 解析优先级（高 -> 低）：
     *   用户配置文件(.config) > 系统配置文件(/etc) > DConfig(运行时) > 本预设(兜底)
     */
    class PresetConfig
    {
      public:
        // 返回指定发行系默认后端 id（未知家族返回空串）
        static QString defaultBackendFor(DistroProbe::Family family);

        // 返回指定后端在当前发行系下的默认选项（扁平点号键，如 "NoInstallRecommends"）
        static QVariantMap defaultOptionsFor(const QString& backendId, DistroProbe::Family family);

        // 已知后端列表（用于配置文件校验：用户只能选已注册的后端，见 BackendConfig）
        static QStringList knownBackendIds();
    };

} // namespace DtkUpdate
