#include "dtkupdateplugin.h"

#include <QCoreApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QTranslator>
#include <QVariant>
#include <QVariantMap>
#include <QWidget>

#include "common/translator.h"
#include "core/package/backendfactory.h"
#include "core/security/securityadvisor.h"
#include "indicator/updatedialogs.h"
#include "logger.h"
#include "traypopup.h"
#include "traywidget.h"

namespace DtkUpdate
{

    DtkUpdatePlugin::DtkUpdatePlugin(QObject* parent) : UpdateIndicator(parent) {}

    DtkUpdatePlugin::~DtkUpdatePlugin()
    {
        delete m_translator; // 持有而非跟随 app 实例，销毁时一并释放
    }

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

        // 确保 resources.qrc（含 /dsg/built-in-icons/ 图标）被链接进本插件 .so。
        // 该 qrc 编入 dtk-update-indicator 静态库，静态链接时链接器可能丢弃未被
        // 直接引用的资源目标文件，显式初始化可保证 icon() 在运行时能取到资源图标。
        Q_INIT_RESOURCE(resources);

        // 持有 QTranslator 并在 init() 注册（dde-tray-loader 不加载第三方翻译，
        // 必须用实例成员持有，避免临时对象离开 init() 后被销毁导致翻译失效）
        m_translator = new QTranslator(this);
        const QStringList dirs = {QCoreApplication::applicationDirPath(),
                                  QStringLiteral("/usr/share/dtk-update/translations"),
                                  QStringLiteral("/usr/local/share/dtk-update/translations")};
        for (const auto& dir : dirs)
        {
            if (m_translator->load(QLocale::system(), QStringLiteral("dtk-update"),
                                   QStringLiteral("_"), dir))
            {
                QCoreApplication::installTranslator(m_translator);
                break;
            }
        }

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
        return m_trayWidget.data();
    }

    Dock::PluginFlags DtkUpdatePlugin::flags() const
    {
        return Dock::Type_Tray | Dock::Attribute_CanSetting;
    }

    QIcon DtkUpdatePlugin::icon(Dock::IconType iconType, Dock::ThemeType themeType) const
    {
        Q_UNUSED(iconType);
        // 返回安装到资源内的 dci/svg 图标（控制中心按 dcc-setting 读取同名 dci）。
        // 资源前缀 "/dsg/built-in-icons/" 与 dcc-dtk-update.dci 同源，亮/暗各一套。
        const bool updatable = monitor() && !monitor()->upgradable().isEmpty();
        const QString base =
            updatable ? QStringLiteral("dtk-update-update") : QStringLiteral("dtk-update");
        const QString suffix =
            themeType == Dock::ThemeType_Dark ? QStringLiteral("-dark") : QString();
        return QIcon(QStringLiteral(":/dsg/built-in-icons/%1%2.svg").arg(base, suffix));
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

        // 「自动更新」可勾选菜单项：状态来自配置（默认关闭，需用户显式开启）
        auto makeCheckable =
            [&](const QString& id, const QString& text, bool checked, bool active = true)
        {
            QVariantMap m;
            m[QStringLiteral("itemId")] = id;
            m[QStringLiteral("itemText")] = text;
            m[QStringLiteral("isCheckable")] = true;
            m[QStringLiteral("isActive")] = active;
            m[QStringLiteral("isSeparator")] = false;
            m[QStringLiteral("checked")] = checked;
            items.append(m);
        };
        make(QStringLiteral("periodic"), tr("Periodic Check…"));
        makeCheckable(QStringLiteral("auto_update"), tr("Auto Update"),
                      config() && config()->autoUpdateEnabled());

        make(QString(), QString(), true); // 分隔符
        make(QStringLiteral("settings"), tr("Settings"));
        make(QStringLiteral("about"), tr("About"));

        QVariantMap root;
        root[QStringLiteral("items")] = items;
        // 菜单含可勾选项（Auto Update）：必须开启 checkableMenu，dde-dock 才会渲染
        // isCheckable/checked 并把勾选状态经 invokedMenuItem 的 checked 参数回传。
        root[QStringLiteral("checkableMenu")] = true;
        root[QStringLiteral("singleCheck")] = false; // 允许多个独立勾选项
        return QString::fromUtf8(QJsonDocument::fromVariant(root).toJson());
    }

    void DtkUpdatePlugin::invokedMenuItem(const QString& itemKey, const QString& menuId,
                                          const bool checked)
    {
        Q_UNUSED(itemKey);
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
        else if (menuId == QStringLiteral("periodic"))
        {
            // 定时检测设置：不开启/按小时/按天/按月（默认不开启）
            UpdateDialogs::showScheduleSettings(config());
        }
        else if (menuId == QStringLiteral("auto_update"))
        {
            // 自动更新开关：dde-dock 回传勾选状态；默认关闭，需用户显式开启
            if (config())
                config()->setAutoUpdateEnabled(checked);
        }
        else if (menuId == QStringLiteral("settings"))
        {
            // 通过 dde-am 打开控制中心（Wayland 下正确激活窗口）
            QStringList args{QStringLiteral("--by-user"),
                             QStringLiteral("org.deepin.dde.control-center"), QStringLiteral("--"),
                             QStringLiteral("-p"), QStringLiteral("update")};
            QProcess::startDetached(QStringLiteral("dde-am"), args);
        }
        // "about" 由 dde-dock 框架统一拦截并弹出系统关于页，本插件无需单独处理。
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

    void DtkUpdatePlugin::onStateChanged(UpdateMonitor::State state, int count)
    {
        Q_UNUSED(state)
        // 更新托盘控件状态（红点/角标依赖此调用，否则永远灰色）
        if (m_trayWidget)
            m_trayWidget->setState(count);
        // 状态变化刷新图标（控制中心/任务栏可能缓存）
        if (m_proxyInter)
            m_proxyInter->itemUpdate(this, pluginName());
    }

    bool DtkUpdatePlugin::pluginIsAllowDisable()
    {
        // 允许用户在控制中心禁用本插件
        return true;
    }

    bool DtkUpdatePlugin::pluginIsDisable()
    {
        // 默认启用；禁用状态由 proxy 持久化（键 "enable"）
        if (!m_proxyInter)
            return false;
        return !m_proxyInter->getValue(this, QStringLiteral("enable"), true).toBool();
    }

    void DtkUpdatePlugin::pluginStateSwitched()
    {
        if (!m_proxyInter)
            return;
        const bool disable = pluginIsDisable();
        if (disable)
            m_proxyInter->itemRemoved(this, pluginName());
        else
            m_proxyInter->itemAdded(this, pluginName());
    }

    void DtkUpdatePlugin::refreshIcon(const QString& itemKey)
    {
        Q_UNUSED(itemKey)
        // 图标主题（亮/暗）切换时由框架调用，转发刷新
        if (m_proxyInter)
            m_proxyInter->itemUpdate(this, pluginName());
    }

    void DtkUpdatePlugin::onBackendUnavailable(const QString& backendId, const QString& reason)
    {
        // 沙箱后端可选且多实例共存，任一环境异常均提示；系统后端不可用亦走此路径。
        UpdateDialogs::showSandboxUnavailable(backendId, reason);
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

    void DtkUpdatePlugin::onDistroNotices(const QList<SecurityAdvisor::Notice>& notices)
    {
        if (notices.isEmpty())
            return;
        UpdateDialogs::showDistroNotices(notices);
    }

} // namespace DtkUpdate
