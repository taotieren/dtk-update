#include "mainwindow.h"

#include <DDBusSender>
#include <DDialog>
#include <DGuiApplicationHelper>
#include <DListView>
#include <DProgressBar>
#include <DSpinner>
#include <DTitlebar>
#include <QCloseEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QStandardItemModel>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "core/dependency/dependencyresolver.h"
#include "core/package/backendfactory.h"
#include "indicator/updatedialogs.h"
#include "logger.h"

DWIDGET_USE_NAMESPACE
DGUI_USE_NAMESPACE

namespace DtkUpdate
{

    // 内部 UI 结构（Pimpl 风格，降低头文件耦合）
    class MainWindow::Private
    {
      public:
        DListView* updateList = nullptr;
        QStandardItemModel* updateModel = nullptr;
        QPushButton* refreshBtn = nullptr;
        QPushButton* updateBtn = nullptr;
        QPushButton* inspectBtn = nullptr;
        QPushButton* settingsBtn = nullptr;
        DProgressBar* progress = nullptr;
        QLabel* summary = nullptr;
        QLabel* sevLabel = nullptr;
        QTextBrowser* depView = nullptr;
        DSpinner* spinner = nullptr;
        QStringList selectedPackages;
    };

    MainWindow::MainWindow(PackageBackend* backend, AppConfig* config, SecurityAdvisor* advisor,
                           QWidget* parent)
        : DMainWindow(parent), m_backend(backend), m_config(config), m_advisor(advisor)
    {
        m_monitor = new UpdateMonitor(m_backend, m_config, this);
        if (m_advisor)
            m_monitor->setSecurityAdvisor(m_advisor);

        // 跨发行系接入可选的玲珑(linyaps)后端：由 BackendFactory 统一探测接入。
        // 系统后端（apt/dnf）不可用时仍经 backendUnavailable 提示用户。
        BackendFactory::attachSandboxBackends(m_monitor, m_config, this);

        connect(m_monitor, &UpdateMonitor::backendUnavailable, this,
                &MainWindow::onBackendUnavailable);

        d = new Private;
        buildUI();
        setMinimumSize(820, 560);
        titlebar()->setTitle(tr("Dtk Update"));

        // 显示当前后端信息
        if (m_backend)
        {
            const QString info = tr("Backend: %1").arg(m_backend->backendName());
            d->summary->setText(info);
            connect(m_backend, &PackageBackend::operationFinished, this,
                    [this](bool ok, const QString& detail)
                    {
                        Q_UNUSED(ok)
                        Q_UNUSED(detail)
                    });
        }

        connect(m_monitor, &UpdateMonitor::stateChanged, this, &MainWindow::onStateChanged);
        connect(m_monitor, &UpdateMonitor::updatesAvailable, this, &MainWindow::onUpdatesAvailable);
        connect(m_monitor, &UpdateMonitor::securityPrompt, this, &MainWindow::onSecurityPrompt);
        connect(m_monitor, &UpdateMonitor::postCheck, this, &MainWindow::onPostCheck);
        if (m_advisor)
            connect(m_advisor, &SecurityAdvisor::distroNoticesReady, this,
                    [](const QList<SecurityAdvisor::Notice>& notices)
                    { UpdateDialogs::showDistroNotices(notices); });
        connect(m_monitor, &UpdateMonitor::upgradeProgress, this, &MainWindow::onUpgradeProgress);
        connect(m_monitor, &UpdateMonitor::upgradeFinished, this, &MainWindow::onUpgradeFinished);

        m_monitor->start();
    }

    MainWindow::~MainWindow()
    {
        delete d;
    }

    void MainWindow::buildUI()
    {
        auto* central = new QWidget(this);
        setCentralWidget(central);
        auto* root = new QVBoxLayout(central);
        root->setContentsMargins(16, 16, 16, 16);
        root->setSpacing(10);

        // 顶部：摘要 + 操作按钮
        auto* top = new QHBoxLayout;
        d->summary = new QLabel(tr("Checking for updates…"));
        d->summary->setWordWrap(true);
        top->addWidget(d->summary, 1);

        d->sevLabel = new QLabel;
        d->sevLabel->setVisible(false);
        top->addWidget(d->sevLabel);

        d->refreshBtn = new QPushButton(tr("Check"));
        d->inspectBtn = new QPushButton(tr("Dependency"));
        d->inspectBtn->setEnabled(false);
        d->updateBtn = new QPushButton(tr("Update Now"));
        d->updateBtn->setEnabled(false);
        d->settingsBtn = new QPushButton(tr("Settings"));
        top->addWidget(d->refreshBtn);
        top->addWidget(d->inspectBtn);
        top->addWidget(d->updateBtn);
        top->addWidget(d->settingsBtn);
        root->addLayout(top);

        connect(d->refreshBtn, &QPushButton::clicked, this, &MainWindow::refresh);
        connect(d->updateBtn, &QPushButton::clicked, this, &MainWindow::applySelected);
        connect(d->inspectBtn, &QPushButton::clicked, this, &MainWindow::onInspectClicked);
        connect(d->settingsBtn, &QPushButton::clicked, this, &MainWindow::onOpenSettings);

        // 进度条（升级时显示）
        d->progress = new DProgressBar;
        d->progress->setVisible(false);
        d->progress->setValue(0);
        root->addWidget(d->progress);

        // 中部：左更新列表 / 右依赖详情
        auto* mid = new QHBoxLayout;
        d->updateList = new DListView;
        d->updateList->setEditTriggers(QAbstractItemView::NoEditTriggers);
        d->updateList->setSelectionMode(QAbstractItemView::MultiSelection);
        d->updateList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        d->updateModel = new QStandardItemModel(d->updateList);
        d->updateList->setModel(d->updateModel);
        mid->addWidget(d->updateList, 1);

        d->depView = new QTextBrowser;
        d->depView->setOpenExternalLinks(false);
        d->depView->setPlaceholderText(tr("Select a package and click 'Dependency' to analyze."));
        mid->addWidget(d->depView, 1);
        root->addLayout(mid, 1);

        connect(d->updateList->selectionModel(), &QItemSelectionModel::selectionChanged, this,
                &MainWindow::onSelectionChanged);
    }

    void MainWindow::refresh()
    {
        m_monitor->checkNow();
    }

    void MainWindow::onStateChanged(UpdateMonitor::State state)
    {
        switch (state)
        {
        case UpdateMonitor::State::Checking:
            d->summary->setText(tr("Checking for updates…"));
            d->updateBtn->setEnabled(false);
            if (!d->spinner)
            {
                d->spinner = new DSpinner(d->summary);
                d->spinner->setFixedSize(16, 16);
            }
            d->spinner->start();
            break;
        case UpdateMonitor::State::Updating:
            d->summary->setText(tr("Updating…"));
            d->updateBtn->setEnabled(false);
            break;
        case UpdateMonitor::State::Error:
            d->summary->setText(tr("Last check failed. Click 'Check' to retry."));
            d->updateBtn->setEnabled(false);
            if (d->spinner)
            {
                d->spinner->stop();
                d->spinner->deleteLater();
                d->spinner = nullptr;
            }
            break;
        case UpdateMonitor::State::HasUpdates:
            d->updateBtn->setEnabled(true);
            if (d->spinner)
            {
                d->spinner->stop();
                d->spinner->deleteLater();
                d->spinner = nullptr;
            }
            break;
        case UpdateMonitor::State::Idle:
        default:
            d->summary->setText(tr("System up to date"));
            d->updateBtn->setEnabled(false);
            if (d->spinner)
            {
                d->spinner->stop();
                d->spinner->deleteLater();
                d->spinner = nullptr;
            }
            break;
        }
    }

    void MainWindow::onUpdatesAvailable(const PackageList& packages)
    {
        d->updateModel->clear();
        d->selectedPackages.clear();
        d->inspectBtn->setEnabled(false);
        d->depView->clear();

        const int n = packages.size();
        for (const auto& p : packages)
        {
            auto* item = new QStandardItem(
                QStringLiteral("%1  %2 → %3").arg(p.name, p.currentVersion, p.candidateVersion));
            item->setData(p.name, Qt::UserRole);
            d->updateModel->appendRow(item);
        }
        if (n == 0)
            d->summary->setText(tr("System up to date"));
        else
            d->summary->setText(tr("%1 updates available").arg(n));
    }

    void MainWindow::onSelectionChanged()
    {
        d->selectedPackages.clear();
        const auto idxs = d->updateList->selectionModel()->selectedIndexes();
        for (const auto& idx : idxs)
        {
            const auto* item = d->updateModel->itemFromIndex(idx);
            if (item)
                d->selectedPackages.append(item->data(Qt::UserRole).toString());
        }
        d->inspectBtn->setEnabled(d->selectedPackages.size() == 1);
    }

    void MainWindow::onInspectClicked()
    {
        if (d->selectedPackages.size() != 1)
            return;
        inspectDependency(d->selectedPackages.first());
    }

    void MainWindow::inspectDependency(const QString& pkg)
    {
        d->depView->setHtml(tr("Analyzing dependencies for %1 …").arg(pkg));
        DependencyResolver resolver;
        resolver.setBackend(m_backend);
        QString error;
        if (!resolver.resolve(pkg, error))
        {
            d->depView->setHtml(QStringLiteral("<font color='red'>%1</font>")
                                    .arg(tr("Failed to resolve: %1").arg(error.toHtmlEscaped())));
            return;
        }
        const QStringList toInstall = resolver.toInstall();
        const QStringList toRemove = resolver.toRemove();
        QString html;
        html += QStringLiteral("<h4>%1</h4>").arg(pkg);
        html += QStringLiteral("<p><b>%1</b></p>").arg(tr("Packages to be installed:"));
        if (toInstall.isEmpty())
            html += QStringLiteral("<p>%1</p>").arg(tr("None"));
        else
            for (const auto& p : toInstall)
                html += QStringLiteral("<li>%1</li>").arg(p.toHtmlEscaped());
        if (!toRemove.isEmpty())
        {
            html += QStringLiteral("<p><b><font color='red'>%1</font></b></p>")
                        .arg(tr("Packages to be removed:"));
            for (const auto& p : toRemove)
                html += QStringLiteral("<li>%1</li>").arg(p.toHtmlEscaped());
        }
        html += QStringLiteral("</ul>");
        d->depView->setHtml(html);
    }

    void MainWindow::applySelected()
    {
        if (m_monitor->upgradable().isEmpty())
            return;
        // advisor 会在 applyUpdates 内发 securityPrompt，这里再做最终确认
        m_monitor->applyUpdates();
    }

    void MainWindow::onSecurityPrompt(const QString& sev,
                                      const QList<SecurityAdvisor::Advisory>& advs,
                                      const PreCheckReport& pre)
    {
        Q_UNUSED(sev)
        // 升级前安全提示 + 预检：弹出确认对话框，展示风险等级、公告与预检建议。
        // 只有用户明确点击「Update」才继续；任何其它情况（取消/ESC/关闭）都取消。
        if (confirmApply(advs, pre))
        {
            m_monitor->proceedUpdate();
        }
        else
        {
            qCInfo(dtkUpdateCore) << "user cancelled update due to security prompt";
            m_monitor->cancelUpdate();
        }
    }

    bool MainWindow::confirmApply(const QList<SecurityAdvisor::Advisory>& advs,
                                  const PreCheckReport& pre)
    {
        DDialog dlg(this);
        dlg.setTitle(tr("Confirm System Update"));
        dlg.setIcon(QIcon::fromTheme(QStringLiteral("dialog-warning")));

        QString text = tr("The following packages will be upgraded. This action modifies "
                          "the system and may affect dependencies. Continue?");
        text += QStringLiteral("<br><span style=\"color:#888\">%1</span>")
                    .arg(tr("No changes are made unless you choose to continue. "
                            "Optional dependencies and orphan removal follow your settings."));

        if (!advs.isEmpty())
        {
            text += QStringLiteral("<br><br><b>%1</b>").arg(tr("Security advisories:"));
            for (const auto& a : advs)
            {
                text += QStringLiteral("<br>• [%1] %2").arg(a.severity, a.title.toHtmlEscaped());
                if (!a.url.isEmpty()) // 官方公告链接，供用户自行核实
                    text += QStringLiteral(" <a href=\"%1\">%2</a>")
                                .arg(a.url.toHtmlEscaped(), tr("details"));
                if (!a.description.isEmpty())
                    text += QStringLiteral("<br>&nbsp;&nbsp;%1").arg(a.description.toHtmlEscaped());
            }
        }

        // 预检建议（仅信息，不自动执行）
        QString preText;
        if (pre.rebootRequired)
            preText += QStringLiteral("<br>• <b>%1</b>")
                           .arg(tr("A system reboot will be required after this update "
                                   "(kernel or base library changed)."));
        for (const QString& s : pre.servicesNeedRestart)
            preText +=
                QStringLiteral("<br>• %1: %2").arg(tr("Service needs restart"), s.toHtmlEscaped());
        for (const QString& c : pre.configFilesToReview)
            preText +=
                QStringLiteral("<br>• %1: %2").arg(tr("Config file to review"), c.toHtmlEscaped());
        for (const QString& u : pre.failedUnits)
            preText +=
                QStringLiteral("<br>• %1: %2").arg(tr("Failed service unit"), u.toHtmlEscaped());
        if (!preText.isEmpty())
            text += QStringLiteral("<br><br><b>%1</b>").arg(tr("Pre-update checks:")) + preText;

        dlg.setMessage(text);
        dlg.addButton(QStringLiteral("Cancel"), false, DDialog::ButtonNormal);
        const int updateId =
            dlg.addButton(QStringLiteral("Update"), true, DDialog::ButtonRecommend);
        return dlg.exec() == updateId;
    }

    void MainWindow::onPostCheck(const PostCheckReport& report)
    {
        // 更新后后检：提示用户需重启/审阅配置，绝不自动执行
        if (!report.hasAnything())
            return;
        showPostCheckHint(report);
    }

    void MainWindow::showPostCheckHint(const PostCheckReport& report)
    {
        DDialog dlg(this);
        dlg.setTitle(tr("Update completed — attention required"));
        dlg.setIcon(QIcon::fromTheme(QStringLiteral("dialog-information")));
        QString text;
        if (report.rebootRequired)
            text +=
                QStringLiteral("<br>• <b>%1</b>")
                    .arg(tr("A system reboot is recommended (kernel or base library updated)."));
        for (const QString& s : report.servicesNeedRestart)
            text +=
                QStringLiteral("<br>• %1: %2").arg(tr("Service needs restart"), s.toHtmlEscaped());
        for (const QString& c : report.configFilesToReview)
            text +=
                QStringLiteral("<br>• %1: %2").arg(tr("Config file to review"), c.toHtmlEscaped());
        for (const QString& u : report.failedUnits)
            text +=
                QStringLiteral("<br>• %1: %2").arg(tr("Failed service unit"), u.toHtmlEscaped());
        for (const QString& p : report.residualPackages)
            text += QStringLiteral("<br>• %1: %2")
                        .arg(tr("Residual / orphan package"), p.toHtmlEscaped());
        if (report.cleanableCacheBytes > 0)
        {
            text += QStringLiteral("<br>• %1")
                        .arg(tr("Download cache can be cleaned: %1 MB")
                                 .arg(report.cleanableCacheBytes / (1024 * 1024)));
        }
        if (!report.error.isEmpty())
            text += QStringLiteral("<br><span style=\"color:#888\">%1</span>")
                        .arg(report.error.toHtmlEscaped());
        dlg.setMessage(text);
        dlg.addButton(QStringLiteral("OK"), true, DDialog::ButtonRecommend);
        dlg.exec();
    }

    void MainWindow::onUpgradeProgress(const QString& stage, int percent)
    {
        d->progress->setVisible(true);
        d->progress->setValue(percent);
        d->summary->setText(tr("Updating… %1").arg(stage));
    }

    void MainWindow::onUpgradeFinished(bool success, const QString& detail)
    {
        d->progress->setVisible(false);
        d->progress->setValue(0);
        d->updateModel->clear();
        d->depView->clear();
        d->updateBtn->setEnabled(false);
        d->inspectBtn->setEnabled(false);
        DDialog dlg(this);
        dlg.setTitle(success ? tr("Update Completed") : tr("Update Failed"));
        dlg.setMessage(success ? tr("System packages have been updated.")
                               : tr("Update failed: %1").arg(detail.toHtmlEscaped()));
        dlg.addButton(QStringLiteral("OK"), true, DDialog::ButtonRecommend);
        dlg.exec();
    }

    void MainWindow::onOpenSettings()
    {
        // 通过 dde-am 打开控制中心更新模块（Wayland 下正确激活窗口）
        QStringList args{QStringLiteral("--by-user"),
                         QStringLiteral("org.deepin.dde.control-center"), QStringLiteral("--"),
                         QStringLiteral("-p"), QStringLiteral("update")};
        QProcess::startDetached(QStringLiteral("dde-am"), args);
    }

    void MainWindow::onBackendUnavailable(const QString& backendId, const QString& reason)
    {
        // 沙箱式应用商店后端（linyaps/snap/flatpak …）均为可选、且与系统后端正交：
        // 一台机器可能一个都没有、也可能多个并存。任一沙箱后端环境异常都需提示用户，
        // 而非只对 linyaps 处理、其余静默忽略。系统后端(apt/dnf)不可用亦走此提示。
        const QString title = tr("%1 Environment Issue").arg(backendId);
        const QString msg =
            reason.isEmpty()
                ? tr("The %1 runtime environment is abnormal; sandbox application updates via this "
                     "backend are unavailable. Please check the backend's installation and "
                     "runtime.")
                      .arg(backendId)
                : reason;
        DDialog dlg(this);
        dlg.setTitle(title);
        dlg.setMessage(msg);
        dlg.addButton(QStringLiteral("OK"), true, DDialog::ButtonRecommend);
        dlg.exec();
    }

} // namespace DtkUpdate
