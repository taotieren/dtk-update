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
     * 后端轮询由 dtk-update-daemon 统一负责；本指示器若 daemon 未运行则自建
     * UpdateMonitor 作为兜底，保证无 daemon 时也能工作（单实例避免重复探测）。
     */
    class UpdateIndicator : public QObject
    {
        Q_OBJECT
      public:
        explicit UpdateIndicator(QObject* parent = nullptr);
        ~UpdateIndicator() override;

        AppConfig* config() const { return m_config; }
        UpdateMonitor* monitor() const { return m_monitor; }

      signals:
        // 供前端刷新图标的轻量通知
        void stateChanged();
        // 发行版官方「最近新闻 / 通知」拉取完成（与包名无关，供前端弹出通知）
        void distroNoticesReady(const QList<SecurityAdvisor::Notice>& notices);

      protected:
        // 前端钩子：由子类实现具体 UI 表现
        virtual void onStateChanged(bool hasUpdates, int count) = 0;
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
    };

} // namespace DtkUpdate
