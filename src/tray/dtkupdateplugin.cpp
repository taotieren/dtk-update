#include "dtkupdateplugin.h"
#include "traywidget.h"
#include "traypopup.h"
#include "logger.h"
#include "common/translator.h"
#include "core/security/securityadvisor.h"

#include <DDialog>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QVariantMap>

namespace DtkUpdate {

DtkUpdatePlugin::DtkUpdatePlugin(QObject *parent)
    : QObject(parent)
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

void DtkUpdatePlugin::init(PluginProxyInterface *proxyInter)
{
    m_proxyInter = proxyInter;
    buildBackend();

    m_trayWidget = new TrayWidget;
    connect(m_trayWidget, &TrayWidget::clicked, this, [this] {
        // 左键切换弹出面板（dde-tray-loader 负责定位）
        if (m_proxyInter)
            m_proxyInter->requestSetAppletVisible(this, pluginName(), true);
    });

    if (m_proxyInter)
        m_proxyInter->itemAdded(this, pluginName());
}

QWidget *DtkUpdatePlugin::itemWidget(const QString &itemKey)
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
    Q_UNUSED(iconType)
    // 按主题返回 dci/svg 图标；无更新使用普通态，有更新使用带角标态
    const bool updatable = m_monitor && !m_monitor->upgradable().isEmpty();
    QString name = updatable ? QStringLiteral("dtk-update-update")
                             : QStringLiteral("dtk-update");
    Q_UNUSED(themeType)
    return QIcon::fromTheme(name);
}

const QString DtkUpdatePlugin::itemContextMenu(const QString &itemKey)
{
    Q_UNUSED(itemKey)
    QList<QVariant> items;

    auto make = [&](const QString &id, const QString &text, bool separator = false,
                    bool active = true) {
        QVariantMap m;
        m[QStringLiteral("itemId")] = id;
        m[QStringLiteral("itemText")] = text;
        m[QStringLiteral("isCheckable")] = false;
        m[QStringLiteral("isActive")] = active;
        m[QStringLiteral("isSeparator")] = separator;
        m[QStringLiteral("checked")] = false;
        items.append(m);
    };

    const bool hasUpdates = m_monitor && m_monitor->state() == UpdateMonitor::State::HasUpdates;
    make(QStringLiteral("check"), tr("Check for Updates"));
    make(QStringLiteral("update"), tr("Update Now"), false, hasUpdates);
    make(QStringLiteral("open"), tr("Open Update Manager"));
    make(QString(), QString(), true);  // 分隔符
    make(QStringLiteral("settings"), tr("Settings"));
    make(QStringLiteral("about"), tr("About"));

    QVariantMap root;
    root[QStringLiteral("items")] = items;
    root[QStringLiteral("checkableMenu")] = false;
    root[QStringLiteral("singleCheck")] = false;
    return QString::fromUtf8(QJsonDocument::fromVariant(root).toJson());
}

void DtkUpdatePlugin::invokedMenuItem(const QString &itemKey,
                                         const QString &menuId, const bool checked)
{
    Q_UNUSED(itemKey); Q_UNUSED(checked)
    if (menuId == QStringLiteral("check") && m_monitor) {
        m_monitor->checkNow();
    } else if (menuId == QStringLiteral("update") && m_monitor) {
        m_monitor->applyUpdates();
    } else if (menuId == QStringLiteral("open")) {
        QProcess::startDetached(QStringLiteral("dtk-update-gui"), {});
    } else if (menuId == QStringLiteral("settings")) {
        // 通过 dde-am 打开控制中心（Wayland 下正确激活窗口）
        QStringList args{QStringLiteral("--by-user"),
                         QStringLiteral("org.deepin.dde.control-center"),
                         QStringLiteral("--"), QStringLiteral("-p"),
                         QStringLiteral("update")};
        QProcess::startDetached(QStringLiteral("dde-am"), args);
    }
    // "about" 暂由系统处理，可后续扩展
}

QWidget *DtkUpdatePlugin::itemPopupApplet(const QString &itemKey)
{
    Q_UNUSED(itemKey)
    if (!m_popup) {
        m_popup = new TrayPopup(m_monitor);
    }
    return m_popup;
}

void DtkUpdatePlugin::buildBackend()
{
    m_config = new AppConfig(this);
    m_backend = BackendFactory::createBackend(this, m_config->preferredBackend());
    if (m_backend)
        m_backend->setConfig(m_config);
    m_monitor = new UpdateMonitor(m_backend, m_config, this);
    // 安全提示器：是否到上游官方源获取公告完全由用户配置决定（默认开启，用户可关）
    m_advisor = new SecurityAdvisor(this);
    m_advisor->setFetchUpstream(m_config->fetchUpstreamAdvisories());
    m_monitor->setSecurityAdvisor(m_advisor);
    // 托盘作为后台小程序，直接执行升级（不弹确认；确认交互由主窗口负责）
    connect(m_monitor, &UpdateMonitor::updatesAvailable, this,
            [this](const PackageList &pkgs) { updateTrayState(); Q_UNUSED(pkgs) });
    connect(m_monitor, &UpdateMonitor::securityPrompt, this,
            [this](const QString &sev, const QList<SecurityAdvisor::Advisory> &advs,
                   const PreCheckReport &pre) {
                showSecurityConfirm(sev, advs, pre);
            });
    connect(m_monitor, &UpdateMonitor::postCheck, this, &DtkUpdatePlugin::showPostCheck);
    connect(m_monitor, &UpdateMonitor::stateChanged, this, [this] {
        // 状态变化刷新图标（控制中心/任务栏可能缓存）
        if (m_proxyInter)
            m_proxyInter->itemUpdated(this, pluginName());
    });
    m_monitor->start();
}

void DtkUpdatePlugin::updateTrayState()
{
    if (m_trayWidget && m_monitor)
        m_trayWidget->setState(m_monitor->upgradable().size());
    if (m_proxyInter)
        m_proxyInter->itemUpdated(this, pluginName());
}

void DtkUpdatePlugin::showSecurityConfirm(const QString &severity,
                                          const QList<SecurityAdvisor::Advisory> &advs,
                                          const PreCheckReport &pre)
{
    if (!m_monitor)
        return;
    DDialog dlg;
    dlg.setTitle(tr("Security advisory before update"));
    dlg.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));

    QString body;
    body += tr("The following packages have security-relevant updates "
               "(overall severity: %1). Review the details and decide whether to proceed.")
                .arg(severity)
            + QLatin1Char('\n') + QLatin1Char('\n');
    for (const auto &a : advs) {
        body += QStringLiteral("• %1  [%2]  %3\n").arg(a.package, a.severity, a.title);
        if (!a.url.isEmpty())
            body += QStringLiteral("   %1\n").arg(a.url);  // 官方公告链接，供用户自行核实
        if (!a.description.isEmpty())
            body += QStringLiteral("   %1\n").arg(a.description);
    }
    // 预检建议（仅信息，不自动执行）
    if (pre.rebootRequired)
        body += QLatin1Char('\n') + tr("A system reboot will be required after this update.");
    for (const QString &s : pre.servicesNeedRestart)
        body += QLatin1Char('\n') + tr("Service needs restart: ") + s;
    for (const QString &c : pre.configFilesToReview)
        body += QLatin1Char('\n') + tr("Config file to review: ") + c;

    body += QLatin1Char('\n') + QLatin1Char('\n') + tr("No changes will be made unless you choose to continue.");
    dlg.setMessage(body);

    dlg.addButton(tr("Cancel"), true, DDialog::ButtonNormal);  // 默认聚焦：取消（不主动替用户决定）
    dlg.addButton(tr("Update Anyway"), false, DDialog::ButtonWarning);

    // 用户必须显式选择。关闭对话框（ESC/点 X）一律视为取消。
    const int ret = dlg.exec();
    if (ret == DDialog::Accepted)  // 仅"Update Anyway"返回 Accepted
        m_monitor->proceedUpdate();
    else
        m_monitor->cancelUpdate();
}

void DtkUpdatePlugin::showPostCheck(const PostCheckReport &report)
{
    if (!report.hasAnything())
        return;
    DDialog dlg;
    dlg.setTitle(tr("Update completed — attention required"));
    dlg.setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
    QString body;
    if (report.rebootRequired)
        body += tr("A system reboot is recommended (kernel or base library updated).")
                + QLatin1Char('\n');
    for (const QString &s : report.servicesNeedRestart)
        body += tr("Service needs restart: ") + s + QLatin1Char('\n');
    for (const QString &c : report.configFilesToReview)
        body += tr("Config file to review: ") + c + QLatin1Char('\n');
    for (const QString &u : report.failedUnits)
        body += tr("Failed service unit: ") + u + QLatin1Char('\n');
    for (const QString &p : report.residualPackages)
        body += tr("Residual / orphan package: ") + p + QLatin1Char('\n');
    if (report.cleanableCacheBytes > 0)
        body += tr("Download cache can be cleaned: %1 MB")
                        .arg(report.cleanableCacheBytes / (1024 * 1024))
                + QLatin1Char('\n');
    dlg.setMessage(body);
    dlg.addButton(tr("OK"), true, DDialog::ButtonRecommend);
    dlg.exec();
}

}  // namespace DtkUpdate
