#pragma once

#include <QList>
#include <QString>

#include "core/package/packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 升级前预检报告（借鉴 arch-update 的 pre-update 阶段）
     *
     * 仅探测、不修改系统。所有项都只是"建议"，最终由用户决定。
     * 这里是软性提示（重启、服务、配置审阅）。
     */
    struct PreCheckReport
    {
        bool rebootRequired = false;     // 内核/底层库更新需重启
        QStringList servicesNeedRestart; // 升级后需重启的服务
        QStringList configFilesToReview; // 待审阅配置文件（.dpkg-new 等）
        QStringList failedUnits;         // 处于 failed 状态的 systemd units
        QString error;                   // 探测过程中的非致命错误（仅记录）

        bool hasAnything() const
        {
            return rebootRequired || !servicesNeedRestart.isEmpty() ||
                   !configFilesToReview.isEmpty() || !failedUnits.isEmpty();
        }
    };

    /**
     * @brief 预检聚合器：在升级前调用，收集各项提示。
     *
     * 设计为纯函数式探针，不持有后端生命周期；调用方传入 backend。
     */
    class PreUpdateCheck
    {
      public:
        // 运行全部预检探针（内核待重启/服务待重启/配置待审阅），返回结构化报告。
        // 任何探针失败仅记录到 report.error，不阻断升级流程本身。
        static PreCheckReport run(PackageBackend* backend);
    };

} // namespace DtkUpdate
