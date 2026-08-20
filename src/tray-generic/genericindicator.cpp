#include "genericindicator.h"

#include <DGuiApplicationHelper>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QIcon>
#include <QList>
#include <QMenu>
#include <QPair>
#include <QProcess>
#include <QStandardPaths>

#include "indicator/updatedialogs.h"
#include "logger.h"

DGUI_USE_NAMESPACE

namespace DtkUpdate
{

    GenericIndicator::GenericIndicator(QObject* parent) : UpdateIndicator(parent)
    {
        m_tray = new QSystemTrayIcon(this);
        // QMenu 构造要求 QWidget* 父对象，本类为 QObject，故无父对象、析构手动释放
        m_menu = new QMenu;
        rebuildMenu();
        m_tray->setContextMenu(m_menu);
        // 每次弹出前重建菜单，使「定时检测 / 自动更新」勾选状态与配置实时一致
        // （配置可能经 GUI 设置对话框等其他途径修改）。
        connect(m_menu, &QMenu::aboutToShow, this, &GenericIndicator::rebuildMenu);
        updateIcon(hasUpdates());
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason)
                {
                    if (reason == QSystemTrayIcon::Trigger)
                        QProcess::startDetached(QStringLiteral("dtk-update-gui"), {});
                });
        // 亮/暗主题切换时刷新图标（DGui 提供跨 DTK 主题能力）
        connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this,
                [this] { updateIcon(hasUpdates()); });
    }

    GenericIndicator::~GenericIndicator()
    {
        // 托盘菜单（QMenu）无 QObject 父对象托管，必须手动释放防泄漏；
        // QPointer 成员在对象删除后自动置空，无悬垂风险。
        delete m_menu;
        delete m_unitGroup;
    }

    void GenericIndicator::show()
    {
        if (m_tray)
        {
            if (!QSystemTrayIcon::isSystemTrayAvailable())
                qCWarning(dtkUpdateTray) << "system tray unavailable; icon will not show";
            m_tray->show();
        }
    }

    void GenericIndicator::rebuildMenu()
    {
        if (!m_menu)
            return;
        // 先释放上次重建遗留的 QActionGroup（QMenu::clear 只删 QAction，不删 QObject 子对象）
        if (m_unitGroup)
            delete m_unitGroup;
        m_menu->clear(); // 清空旧 action（QMenu 拥有并删除），保证重建后状态一致
        auto* check = m_menu->addAction(tr("Check for Updates"));
        connect(check, &QAction::triggered, this,
                [this]
                {
                    if (monitor())
                        monitor()->checkNow();
                });
        auto* open = m_menu->addAction(tr("Open Update Manager"));
        connect(open, &QAction::triggered, this,
                [] { QProcess::startDetached(QStringLiteral("dtk-update-gui"), {}); });
        m_menu->addSeparator();

        // —— 定时检测（默认不开启，需用户显式开启；单选子菜单）——
        auto* periodicMenu = m_menu->addMenu(tr("Periodic Check"));
        m_unitGroup = new QActionGroup(m_menu);
        m_unitGroup->setExclusive(true);
        const QString curUnit =
            config() ? config()->checkIntervalUnit() : QStringLiteral("disabled");
        const QList<QPair<QString, QString>> units = {
            {QStringLiteral("disabled"), tr("Off")},
            {QStringLiteral("hour"), tr("Every hour")},
            {QStringLiteral("day"), tr("Every day")},
            {QStringLiteral("month"), tr("Every month")},
        };
        for (const auto& u : units)
        {
            auto* act = periodicMenu->addAction(u.second);
            act->setCheckable(true);
            act->setChecked(u.first == curUnit);
            m_unitGroup->addAction(act);
            connect(act, &QAction::triggered, this,
                    [this, u]
                    {
                        if (config())
                            config()->setCheckIntervalUnit(u.first);
                    });
        }

        // —— 自动更新（默认关闭，需用户显式开启；可勾选）——
        auto* autoAct = m_menu->addAction(tr("Auto Update"));
        autoAct->setCheckable(true);
        // 重建时同步勾选状态，但不触发写配置（triggered 仅用户点击才发，避免每次弹菜单冗余写
        // DConfig）
        autoAct->setChecked(config() && config()->autoUpdateEnabled());
        connect(autoAct, &QAction::triggered, this,
                [this](bool on)
                {
                    if (config())
                        config()->setAutoUpdateEnabled(on);
                });

        m_menu->addSeparator();
        auto* quit = m_menu->addAction(tr("Quit"));
        connect(quit, &QAction::triggered, &QApplication::quit);
    }

    void GenericIndicator::updateIcon(bool hasUpdates)
    {
        const QString name =
            hasUpdates ? QStringLiteral("dtk-update-update") : QStringLiteral("dtk-update");
        // 主题图标不存在时回退到内置资源（resources.qrc 的 /icons 前缀），避免空图标
        QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            icon = QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
        if (m_tray)
        {
            if (icon.isNull())
                qCWarning(dtkUpdateTray) << "icon not found:" << name;
            else
                m_tray->setIcon(icon);
        }
    }

    void GenericIndicator::onStateChanged(UpdateMonitor::State state, int count)
    {
        const bool has = (state == UpdateMonitor::State::HasUpdates);
        updateIcon(has);
        if (m_tray)
        {
            m_tray->setToolTip(has ? tr("%1 update(s) available").arg(count)
                                   : tr("System up to date"));
            if (has)
            {
                m_tray->showMessage(tr("Updates available"),
                                    tr("%1 package(s) can be updated.").arg(count),
                                    QSystemTrayIcon::Information, 5000);
            }
        }
    }

    void GenericIndicator::onBackendUnavailable(const QString& backendId, const QString& reason)
    {
        // 沙箱后端可选且多实例共存，任一环境异常均提示；系统后端不可用亦走此路径。
        UpdateDialogs::showSandboxUnavailable(backendId, reason);
    }

    void GenericIndicator::onSecurityPrompt(const QString& severity,
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

    void GenericIndicator::onPostCheck(const PostCheckReport& report)
    {
        if (!report.hasAnything())
            return;
        UpdateDialogs::showPostCheck(report);
    }

    void GenericIndicator::onDistroNotices(const QList<SecurityAdvisor::Notice>& notices)
    {
        if (notices.isEmpty())
            return;
        UpdateDialogs::showDistroNotices(notices);
    }

} // namespace DtkUpdate
