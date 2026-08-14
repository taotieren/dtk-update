#pragma once

#include <QObject>
#include <QPointer>
#include <QSystemTrayIcon>

#include "indicator/updateindicator.h"

namespace DtkUpdate
{

    /**
     * @brief 通用 freedesktop 状态栏指示器（跨发行版备用方案）
     *
     * 当 dde-dock SDK 不可用（非 deepin/UOS 发行版）时启用，不依赖任何桌面私有接口，
     * 仅用 Qt6 原生 QSystemTrayIcon + D-Bus 激活 dtk-update-gui。
     * 复用 UpdateIndicator 的监控/安全/玲珑逻辑，自身只负责托盘表现。
     */
    class GenericIndicator : public UpdateIndicator
    {
        Q_OBJECT
      public:
        explicit GenericIndicator(QObject* parent = nullptr);
        ~GenericIndicator() override;

        void show();

      protected:
        void onStateChanged(bool hasUpdates, int count) override;
        void onBackendUnavailable(const QString& backendId, const QString& reason) override;
        void onSecurityPrompt(const QString& severity, const QList<SecurityAdvisor::Advisory>& advs,
                              const PreCheckReport& pre) override;
        void onPostCheck(const PostCheckReport& report) override;
        void onDistroNotices(const QList<SecurityAdvisor::Notice>& notices) override;

      private:
        void buildMenu();
        void updateIcon(bool hasUpdates);

        QPointer<QSystemTrayIcon> m_tray;
    };

} // namespace DtkUpdate
