#include "dtkupdateplugin.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QVariantMap>

#include "common/translator.h"
#include "core/package/backendfactory.h"
#include "core/security/securityadvisor.h"
#include "indicator/updatedialogs.h"
#include "logger.h"
#include "traypopup.h"
#include "traywidget.h"

namespace DtkUpdate
{

    DtkUpdatePlugin::DtkUpdatePlugin(QObject* parent) : UpdateIndicator(parent)
    {
        DtkUpdate::loadTranslator(QStringLiteral("dtk-update"));
    }

    DtkUpdatePlugin::~DtkUpdatePlugin() = default;

    const QString DtkUpdatePlugin::pluginName() const
    {
        return QStringLiteral("dtk-update");
    }

    const QString DtkUpdatePlugin::pluginDisplayName() const
    {
        return tr("Dtk Update");
    }

    void DtkUpdatePlugin::init(PluginProxyInterface* proxyInter)
    {
        m_proxyInter = proxyInter;

        m_trayWidget = new TrayWidget;
        connect(m_trayWidget, &TrayWidget::clicked, this,
                [this]
                {
                    // 左键切换弹出面板（dde-tray-loader 负责定位）
                    if (m_proxyInter)
                        m_proxyInter->requestSetAppletVisible(this, pluginName(), true);
                });

        if (m_proxyInter)
            m_proxyInter->itemAdded(this, pluginName());
    }

    QWidget* DtkUpdatePlugin::itemWidget(const QString& itemKey)
    {
        Q_UNUSED(itemKey)
        return m_trayWidget;
    }

    Dock::PluginFlags DtkUpdatePlugin::flags() const
    {
        return Dock::Type_Tray | Dock::Attribute_CanSetting;
    }

    QIcon DtkUpdatePlugin::icon(Dock::IconType iconType, Dock::ThemeType themeType) const
    {
        Q_UNUSED(iconType);
        // 按主题返回 dci/svg 图标；无更新使用普通态，有更新使用带角标态
        const bool updatable = monitor() && !monitor()->upgradable().isEmpty();
        QString name =
            updatable ? QStringLiteral("dtk-update-update") : QStringLiteral("dtk-update");
        Q_UNUSED(themeType)
        return QIcon::fromTheme(name);
    }

    const QString DtkUpdatePlugin::itemContextMenu(const QString& itemKey)
    {
        Q_UNUSED(itemKey)
        QList<QVariant> items;

        auto make =
            [&](const QString& id, const QString& text, bool separator = false, bool active = true)
        {
            QVariantMap m;
            m[QStringLiteral("itemId")] = id;
            m[QStringLiteral("itemText")] = text;
            m[QStringLiteral("isCheckable")] = false;
            m[QStringLiteral("isActive")] = active;
            m[QStringLiteral("isSeparator")] = separator;
            m[QStringLiteral("checked")] = false;
            items.append(m);
        };

        const bool hasUpdates = monitor() && monitor()->state() == UpdateMonitor::State::HasUpdates;
        make(QStringLiteral("check"), tr("Check for Updates"));
        make(QStringLiteral("update"), tr("Update Now"), false, hasUpdates);
        make(QStringLiteral("open"), tr("Open Update Manager"));
        make(QString(), QString(), true); // 分隔符
        make(QStringLiteral("settings"), tr("Settings"));
        make(QStringLiteral("about"), tr("About"));

        QVariantMap root;
        root[QStringLiteral("items")] = items;
        root[QStringLiteral("checkableMenu")] = false;
        root[QStringLiteral("singleCheck")] = false;
        return QString::fromUtf8(QJsonDocument::fromVariant(root).toJson());
    }

    void DtkUpdatePlugin::invokedMenuItem(const QString& itemKey, const QString& menuId,
                                          const bool checked)
    {
        Q_UNUSED(itemKey);
        Q_UNUSED(checked)
        if (menuId == QStringLiteral("check") && monitor())
        {
            monitor()->checkNow();
        }
        else if (menuId == QStringLiteral("update") && monitor())
        {
            monitor()->applyUpdates();
        }
        else if (menuId == QStringLiteral("open"))
        {
            QProcess::startDetached(QStringLiteral("dtk-update-gui"), {});
        }
        else if (menuId == QStringLiteral("settings"))
        {
            // 通过 dde-am 打开控制中心（Wayland 下正确激活窗口）
            QStringList args{QStringLiteral("--by-user"),
                             QStringLiteral("org.deepin.dde.control-center"), QStringLiteral("--"),
                             QStringLiteral("-p"), QStringLiteral("update")};
            QProcess::startDetached(QStringLiteral("dde-am"), args);
        }
        // "about" 暂由系统处理，可后续扩展
    }

    QWidget* DtkUpdatePlugin::itemPopupApplet(const QString& itemKey)
    {
        Q_UNUSED(itemKey)
        if (!m_popup)
        {
            m_popup = new TrayPopup(monitor());
        }
        return m_popup;
    }

    void DtkUpdatePlugin::onStateChanged(bool /*hasUpdates*/, int /*count*/)
    {
        // 状态变化刷新图标（控制中心/任务栏可能缓存）
        if (m_proxyInter)
            m_proxyInter->itemUpdated(this, pluginName());
    }

    void DtkUpdatePlugin::onBackendUnavailable(const QString& backendId, const QString& reason)
    {
        if (backendId != QStringLiteral("linyaps"))
            return;
        UpdateDialogs::showLinyapsUnavailable(reason);
    }

    void DtkUpdatePlugin::onSecurityPrompt(const QString& severity,
                                           const QList<SecurityAdvisor::Advisory>& advs,
                                           const PreCheckReport& pre)
    {
        if (!monitor())
            return;
        // 用户必须显式选择；关闭对话框（ESC/点 X）一律视为取消。
        if (UpdateDialogs::showSecurityPrompt(severity, advs, pre))
            monitor()->proceedUpdate();
        else
            monitor()->cancelUpdate();
    }

    void DtkUpdatePlugin::onPostCheck(const PostCheckReport& report)
    {
        if (!report.hasAnything())
            return;
        UpdateDialogs::showPostCheck(report);
    }

} // namespace DtkUpdate
