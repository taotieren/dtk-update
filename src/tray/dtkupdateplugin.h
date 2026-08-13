#pragma once

#include <QObject>
#include <QPointer>

#include "common/appconfig.h"
#include "core/monitor/updatemonitor.h"
#include "core/package/packagebackend.h"
#include "core/security/securityadvisor.h"

#include <pluginsiteminterface.h>

class TrayWidget;
class QMenu;

namespace DtkUpdate
{

    /**
     * @brief dde-tray-loader 托盘插件（V2 接口）
     *
     * flags: Type_Tray | Attribute_CanSetting
     * 提供：托盘图标、左键更新概览面板、右键菜单。
     */
    class DtkUpdatePlugin : public QObject, public PluginsItemInterfaceV2
    {
        Q_OBJECT
        Q_INTERFACES(PluginsItemInterfaceV2)

      public:
        explicit DtkUpdatePlugin(QObject* parent = nullptr);
        ~DtkUpdatePlugin() override;

        // PluginsItemInterfaceV2
        const QString pluginName() const override;
        const QString pluginDisplayName() const override;
        void init(PluginProxyInterface* proxyInter) override;
        QWidget* itemWidget(const QString& itemKey) override;
        Dock::PluginFlags flags() const override;
        QIcon icon(Dock::IconType, Dock::ThemeType) const override;

        // 右键菜单
        const QString itemContextMenu(const QString& itemKey) override;
        void invokedMenuItem(const QString& itemKey, const QString& menuId,
                             const bool checked) override;

        // 左键弹出面板
        QWidget* itemPopupApplet(const QString& itemKey) override;

      private:
        void buildBackend();
        void updateTrayState();
        // 安全公告确认：展示公告（含上游官方链接），用户同意才继续，取消则放弃
        void showSecurityConfirm(const QString& severity,
                                 const QList<SecurityAdvisor::Advisory>& advs,
                                 const PreCheckReport& pre);
        void showPostCheck(const PostCheckReport& report);

        PluginProxyInterface* m_proxyInter = nullptr;
        QPointer<TrayWidget> m_trayWidget;
        QPointer<QWidget> m_popup;
        PackageBackend* m_backend = nullptr;
        AppConfig* m_config = nullptr;
        SecurityAdvisor* m_advisor = nullptr;
        UpdateMonitor* m_monitor = nullptr;
    };

} // namespace DtkUpdate
