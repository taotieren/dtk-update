#pragma once

#include <QDateTime>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "common/appconfig.h"
#include "core/healthcheck/postupdatecheck.h"
#include "core/healthcheck/preupdatecheck.h"
#include "core/package/packageinfo.h"
#include "core/security/securityadvisor.h"

namespace DtkUpdate
{

    class PackageBackend;

    /**
     * @brief 更新监控调度
     *
     * 按 DConfig 间隔定时检查；监听网络恢复（NetworkManager）与会话就绪信号触发检查。
     * 维护状态机：Idle / Checking / HasUpdates / Updating / Error。
     * 仅负责调度与状态聚合；检查委托 PackageBackend，升级前经 SecurityAdvisor 评估。
     */
    class UpdateMonitor : public QObject
    {
        Q_OBJECT
      public:
        enum class State
        {
            Idle,
            Checking,
            HasUpdates,
            Updating,
            Error
        };
        Q_ENUM(State)

        explicit UpdateMonitor(PackageBackend* backend, AppConfig* config,
                               QObject* parent = nullptr);
        ~UpdateMonitor() override;

        State state() const { return m_state; }
        const PackageList& upgradable() const { return m_upgradable; }
        QDateTime lastCheck() const { return m_lastCheck; }

        // 设置安全提示器（可选；为空则跳过升级前提示）
        void setSecurityAdvisor(SecurityAdvisor* advisor) { m_advisor = advisor; }

        // 设置可选的沙箱应用商店后端（linyaps/snap/flatpak 等），用于跨发行版聚合沙箱应用更新。
        // 多个沙箱后端可同时存在，与系统包管理(apt/dnf)正交互不干扰。
        // 设为 nullptr 可移除已接入的沙箱后端（如运行环境异常时由调用方清理）。
        void setSandboxBackend(PackageBackend* backend);

        // 兼容封装：接入玲珑(linyaps)后端（等价 setSandboxBackend）。新代码建议直接传对应后端。
        void setLinyapsBackend(PackageBackend* backend);

      public slots:
        void start();
        void stop();
        void checkNow(); // 手动/事件触发检查
        // 执行升级（全部可升级包）。autoTriggered=true 表示由定时器触发的自动更新发起：
        // 此时即使 showSecurityAdvisory 被关闭，存在安全公告/预检建议仍必须征求确认（绝不静默）。
        void applyUpdates(bool autoTriggered = false);
        void proceedUpdate(); // 安全提示确认后继续（由 UI 调用）
        void cancelUpdate();  // 用户取消升级（由 UI 调用）

      signals:
        void stateChanged(State state);
        void updatesAvailable(const PackageList& packages);
        void checkFailed(const QString& error);

        // 升级前安全提示（UI 据此弹窗确认，确认后调用 proceedUpdate）。
        // pre：升级前预检报告（内核待重启/服务/配置审阅建议），仅展示不自动执行。
        void securityPrompt(const QString& overallSeverity,
                            const QList<SecurityAdvisor::Advisory>& advs,
                            const PreCheckReport& pre);
        void upgradeProgress(const QString& stage, int percent);
        void upgradeFinished(bool success, const QString& detail);

        // 升级被用户取消
        void upgradeCancelled();

        // 升级后后检报告（内核/服务/配置审阅建议），供 UI 提示用户，绝不自动执行
        void postCheck(const PostCheckReport& report);

        // 某后端运行环境异常/不可用（如 linyaps 的 ll-cli 存在但环境损坏），
        // 供 UI 提示用户处理。available=false 表示不可用，reason 为诊断信息。
        void backendUnavailable(const QString& backendId, const QString& reason);

      private slots:
        void onTimeout();
        void onConfigChanged();
        void onPrepareForSleep(bool sleeping); // logind 唤醒后检查
        void onNmStateChanged(uint state);     // NetworkManager 连通后检查
        void onBackendProgress(const QString& stage, int percent);
        void onBackendFinished(bool success, const QString& detail);

      private:
        void setState(State s);
        void applyConfigInterval();
        // fromTimer=true：定时器触发，允许自动更新（自动更新仅在该路径生效）；
        // false：手动/事件（唤醒、联网）触发，只检查不自动更新。
        void checkNowImpl(bool fromTimer);

        PackageBackend* m_backend;
        // 可选：跨发行版沙箱应用商店后端列表（linyaps/snap/flatpak 等），与系统包管理人正交。
        // 用 QPointer 持有，父对象（通常为 UI/tray）删除时自动置空，monitor 不持有所有权。
        QList<QPointer<PackageBackend>> m_sandboxBackends;
        AppConfig* m_config;
        SecurityAdvisor* m_advisor = nullptr;
        QTimer* m_timer;
        State m_state = State::Idle;
        PackageList m_upgradable;
        QDateTime m_lastCheck;
        QLockFile m_lock; // 进程级并发锁，防止 gui+tray 同时写系统（值成员，析构自动释放）
        // 用户已取消升级标志：install 为异步后台执行，cancelUpdate 后子线程仍可能 emit
        // operationFinished，此时必须忽略（不再弹后检/重查），否则会与"已取消"矛盾。
        bool m_cancelled = false;
        // 本轮升级发起的异步写操作计数：系统后端 + 各可用沙箱后端各计 1。
        // onBackendFinished 每完成一个减 1，全部完成（=0）才统一收尾并 emit upgradeFinished，
        // 避免多后端并存时各后端独立 emit 导致重复收尾/重复解锁/重复后检的竞态。
        int m_pendingOps = 0;
    };

} // namespace DtkUpdate
