#pragma once

#include <QObject>
#include <QPointer>

#include "common/appconfig.h"
#include "core/monitor/updatemonitor.h"
#include "core/package/packagebackend.h"
#include "core/security/securityadvisor.h"

namespace DtkUpdate
{

    /**
     * @brief 跨发行版更新指示器核心（与桌面环境解耦）
     *
     * 抽离出 tray 前端共有的监控/安全/玲珑逻辑，供具体前端继承：
     *   - DtkUpdatePlugin  : dde-tray-loader（deepin/UOS，依赖 dde-dock SDK）
     *   - GenericIndicator : 标准 freedesktop 状态栏（QSystemTrayIcon，通用发行版）
     *
     * 具体前端只需实现少量钩子（状态变化/不可用提示/安全确认/后检），
     * 不必重复构建 AppConfig / UpdateMonitor / SecurityAdvisor / linyaps 接入。
     *
     * 构造时创建独立的 UpdateMonitor 实例（每个托盘前端实例各持一份），
     * 用于聚合后端状态；若 dtk-update-daemon 已在运行，可经 D-Bus 复用其调度，
     * 否则本指示器自建 Monitor 独立工作（非 daemon 单例，而是 per-indicator 实例）。
     */
    class UpdateIndicator : public QObject
    {
        Q_OBJECT
      public:
        explicit UpdateIndicator(QObject* parent = nullptr);
        ~UpdateIndicator() override;

        AppConfig* config() const { return m_config; }
        UpdateMonitor* monitor() const { return m_monitor; }
        SecurityAdvisor* advisor() const { return m_advisor; }
        PackageBackend* backend() const { return m_backend; }
        bool hasUpdates() const { return m_state == UpdateMonitor::State::HasUpdates; }

      protected:
        // 前端钩子：由子类实现具体 UI 表现
        // state 透传 UpdateMonitor::State，便于托盘区分 Checking/Updating/Error 等状态；
        // hasUpdates() 便捷方法可替代旧 (bool has) 语义。
        virtual void onStateChanged(UpdateMonitor::State state, int count) = 0;
        virtual void onBackendUnavailable(const QString& backendId, const QString& reason) = 0;
        virtual void onSecurityPrompt(const QString& severity,
                                      const QList<SecurityAdvisor::Advisory>& advs,
                                      const PreCheckReport& pre) = 0;
        virtual void onPostCheck(const PostCheckReport& report) = 0;
        virtual void onDistroNotices(const QList<SecurityAdvisor::Notice>& notices) = 0;

        void setupBackend();

      private:
        PackageBackend* m_backend = nullptr;
        AppConfig* m_config = nullptr;
        SecurityAdvisor* m_advisor = nullptr;
        UpdateMonitor* m_monitor = nullptr;
        UpdateMonitor::State m_state = UpdateMonitor::State::Idle;
    };

} // namespace DtkUpdate
