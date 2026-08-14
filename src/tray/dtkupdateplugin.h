#pragma once

#include <QObject>
#include <QPointer>
#include <QTranslator>

#include "indicator/updateindicator.h"

#include <pluginsiteminterface.h>
#include <pluginsiteminterface_v2.h>

class TrayWidget;
class QMenu;

namespace DtkUpdate
{

    /**
     * @brief dde-tray-loader 托盘插件（V2 接口，deepin/UOS）
     *
     * 继承 UpdateIndicator 复用监控/安全/玲珑逻辑，仅负责把钩子接到 Dock 表现。
     * flags: Type_Tray | Attribute_CanSetting
     * 提供：托盘图标、左键更新概览面板、右键菜单。
     * 依赖 dde-dock SDK（pluginsiteminterface.h），缺失时整个 target 被跳过。
     */
    class DtkUpdatePlugin : public UpdateIndicator, public PluginsItemInterfaceV2
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

        // 控制中心显隐（Attribute_CanSetting 必须实现）
        bool pluginIsAllowDisable() override;
        bool pluginIsDisable() override;
        void pluginStateSwitched() override;

        // 图标主题变化时刷新（亮/暗切换）
        void refreshIcon(const QString& itemKey) override;

      protected:
        void onStateChanged(bool hasUpdates, int count) override;
        void onBackendUnavailable(const QString& backendId, const QString& reason) override;
        void onSecurityPrompt(const QString& severity, const QList<SecurityAdvisor::Advisory>& advs,
                              const PreCheckReport& pre) override;
        void onPostCheck(const PostCheckReport& report) override;
        void onDistroNotices(const QList<SecurityAdvisor::Notice>& notices) override;

      private:
        PluginProxyInterface* m_proxyInter = nullptr;
        QPointer<TrayWidget> m_trayWidget;
        QPointer<QWidget> m_popup;
        QTranslator* m_translator = nullptr;
    };

} // namespace DtkUpdate
