#include "genericindicator.h"

#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QProcess>

#include "common/translator.h"
#include "indicator/updatedialogs.h"
#include "logger.h"

namespace DtkUpdate
{

    GenericIndicator::GenericIndicator(QObject* parent) : UpdateIndicator(parent)
    {
        DtkUpdate::loadTranslator(QStringLiteral("dtk-update"));
        m_tray = new QSystemTrayIcon(this);
        buildMenu();
        updateIcon(false);
        connect(m_tray, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason)
                {
                    if (reason == QSystemTrayIcon::Trigger)
                        QProcess::startDetached(QStringLiteral("dtk-update-gui"), {});
                });
    }

    GenericIndicator::~GenericIndicator() = default;

    void GenericIndicator::show()
    {
        if (m_tray)
            m_tray->show();
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
        if (m_tray)
            m_tray->setIcon(QIcon::fromTheme(name));
    }

    void GenericIndicator::onStateChanged(bool hasUpdates, int count)
    {
        updateIcon(hasUpdates);
        if (m_tray)
        {
            m_tray->setToolTip(hasUpdates ? tr("%1 update(s) available").arg(count)
                                          : tr("System up to date"));
            if (hasUpdates)
            {
                m_tray->showMessage(tr("Updates available"),
                                    tr("%1 package(s) can be updated.").arg(count),
                                    QSystemTrayIcon::Information, 5000);
            }
        }
    }

    void GenericIndicator::onBackendUnavailable(const QString& backendId, const QString& reason)
    {
        if (backendId != QStringLiteral("linyaps"))
            return;
        UpdateDialogs::showLinyapsUnavailable(reason);
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

} // namespace DtkUpdate
