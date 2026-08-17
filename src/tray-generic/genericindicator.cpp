#include "genericindicator.h"

#include <DGuiApplicationHelper>
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMenu>
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
        buildMenu();
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

    GenericIndicator::~GenericIndicator() = default;

    void GenericIndicator::show()
    {
        if (m_tray)
        {
            if (!QSystemTrayIcon::isSystemTrayAvailable())
                qCWarning(dtkUpdateTray) << "system tray unavailable; icon will not show";
            m_tray->show();
        }
    }

    void GenericIndicator::buildMenu()
    {
        auto* menu = new QMenu;
        auto* check = menu->addAction(tr("Check for Updates"));
        connect(check, &QAction::triggered, this,
                [this]
                {
                    if (monitor())
                        monitor()->checkNow();
                });
        auto* open = menu->addAction(tr("Open Update Manager"));
        connect(open, &QAction::triggered, this,
                [] { QProcess::startDetached(QStringLiteral("dtk-update-gui"), {}); });
        menu->addSeparator();
        auto* quit = menu->addAction(tr("Quit"));
        connect(quit, &QAction::triggered, &QApplication::quit);
        m_tray->setContextMenu(menu);
    }

    void GenericIndicator::updateIcon(bool hasUpdates)
    {
        const QString name =
            hasUpdates ? QStringLiteral("dtk-update-update") : QStringLiteral("dtk-update");
        // 主题图标不存在时回退到内置资源，避免空图标
        QIcon icon = QIcon::fromTheme(name);
        if (icon.isNull())
            icon = QIcon(QStringLiteral(":/resources/%1.svg").arg(name));
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
