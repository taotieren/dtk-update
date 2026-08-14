#pragma once

#include <DMainWindow>

#include "common/appconfig.h"
#include "core/monitor/updatemonitor.h"
#include "core/package/packagebackend.h"
#include "core/security/securityadvisor.h"

DWIDGET_USE_NAMESPACE

#include <QPointer>

namespace DtkUpdate
{

    /**
     * @brief 独立主窗口（DMainWindow）
     *
     * 展示可升级包列表、依赖解析结果、操作进度，并在执行升级/移除前弹出
     * 安全确认对话框（含 SecurityAdvisor 安全等级与预检提示）。
     * 与托盘插件共享同一 core 层实例（backend/monitor）。
     */
    class MainWindow : public DMainWindow
    {
        Q_OBJECT
      public:
        explicit MainWindow(PackageBackend* backend, AppConfig* config,
                            SecurityAdvisor* advisor = nullptr, QWidget* parent = nullptr);
        ~MainWindow() override;

      public slots:
        void refresh();
        void applySelected();

        // 打开依赖详情（针对单个包解析依赖树）
        void inspectDependency(const QString& pkg);

      private slots:
        void onStateChanged(UpdateMonitor::State state);
        void onUpdatesAvailable(const PackageList& packages);
        void onSecurityPrompt(const QString& sev, const QList<SecurityAdvisor::Advisory>& advs,
                              const PreCheckReport& pre);
        void onPostCheck(const PostCheckReport& report);
        void onUpgradeProgress(const QString& stage, int percent);
        void onUpgradeFinished(bool success, const QString& detail);

        void onSelectionChanged();
        void onInspectClicked();
        void onOpenSettings();

        // 玲珑(linyaps)运行环境异常时提示用户处理
        void onBackendUnavailable(const QString& backendId, const QString& reason);

      private:
        void buildUI();
        bool confirmApply(const QList<SecurityAdvisor::Advisory>& advs, const PreCheckReport& pre);
        void showPostCheckHint(const PostCheckReport& report);

        PackageBackend* m_backend;
        AppConfig* m_config;
        SecurityAdvisor* m_advisor;
        UpdateMonitor* m_monitor;

        // UI
        class Private;
        Private* d;
    };

} // namespace DtkUpdate
