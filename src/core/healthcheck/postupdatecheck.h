#pragma once

#include <QStringList>

#include "core/package/packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 升级后后检报告（借鉴 arch-update 的 post-update 阶段）
     *
     * 主更新完成后调用：检测是否需要重启内核/服务、是否有配置文件待审阅。
     * 仅提示，绝不自动重启或自动合并配置。
     */
    struct PostCheckReport
    {
        bool rebootRequired = false;
        QStringList servicesNeedRestart;
        QStringList configFilesToReview;
        QStringList failedUnits;        // 升级后处于 failed 状态的 units
        QStringList residualPackages;   // 残留配置(rc)/孤儿包，可清理
        qint64 cleanableCacheBytes = 0; // 可清理的下载缓存字节数
        QString error;

        bool hasAnything() const
        {
            return rebootRequired || !servicesNeedRestart.isEmpty() ||
                   !configFilesToReview.isEmpty() || !failedUnits.isEmpty() ||
                   !residualPackages.isEmpty() || cleanableCacheBytes > 0;
        }
    };

    class PostUpdateCheck
    {
      public:
        static PostCheckReport run(PackageBackend* backend);
    };

} // namespace DtkUpdate
