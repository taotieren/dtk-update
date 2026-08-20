// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatedialogs.h"

#include <DDialog>
#include <DLog>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include "common/appconfig.h"

using Dtk::Widget::DDialog;

namespace DtkUpdate
{

    namespace
    {
        // 构造一个带标题与图标的 DDialog 基础实例（纯文本托盘提示风格）
        DDialog* buildBaseDialog(const QString& title, const QString& iconName)
        {
            auto* dlg = new DDialog();
            dlg->setTitle(title);
            dlg->setIcon(QIcon::fromTheme(iconName));
            return dlg;
        }
    } // namespace

    void UpdateDialogs::showSandboxUnavailable(const QString& backendId, const QString& reason)
    {
        // 通用沙箱后端不可用提示：linyaps/snap/flatpak 均适用，按 backendId 显示标题，
        // 不再硬编码 linyaps。沙箱后端可选且多实例共存，任一环境异常都提示用户。
        const QString title = tr("%1 Environment Issue").arg(backendId);
        const QString msg =
            reason.isEmpty()
                ? tr("The %1 runtime environment is abnormal; sandbox application updates via this "
                     "backend are unavailable. Please check the backend's installation and "
                     "runtime.")
                      .arg(backendId)
                : reason;
        DDialog* dlg = buildBaseDialog(title, QStringLiteral("dialog-warning"));
        dlg->setMessage(msg);
        dlg->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        dlg->exec();
        dlg->deleteLater();
    }

    bool UpdateDialogs::showSecurityPrompt(const QString& severity,
                                           const QList<SecurityAdvisor::Advisory>& advisories,
                                           const PreCheckReport& pre)
    {
        DDialog* dlg = buildBaseDialog(tr("Security advisory before update"),
                                       QStringLiteral("dialog-warning"));

        QString body;
        body += tr("The following packages have security-relevant updates "
                   "(overall severity: %1). Review the details and decide whether to proceed.")
                    .arg(severity) +
                QStringLiteral("\n\n");
        for (const auto& a : advisories)
        {
            body += QStringLiteral("• %1  [%2]  %3\n").arg(a.package, a.severity, a.title);
            if (!a.url.isEmpty())
                body += QStringLiteral("   %1\n").arg(a.url); // 官方公告链接，供用户自行核实
            if (!a.description.isEmpty())
                body += QStringLiteral("   %1\n").arg(a.description);
        }
        // 预检建议（仅信息，不自动执行）
        if (pre.rebootRequired)
            body += QLatin1Char('\n') + tr("A system reboot will be required after this update.");
        for (const QString& s : pre.servicesNeedRestart)
            body += QLatin1Char('\n') + tr("Service needs restart: ") + s;
        for (const QString& c : pre.configFilesToReview)
            body += QLatin1Char('\n') + tr("Config file to review: ") + c;

        body +=
            QStringLiteral("\n\n") + tr("No changes will be made unless you choose to continue.");
        dlg->setMessage(body);

        // 默认聚焦：取消（不主动替用户决定，硬约束 3）；ButtonRecommend 与 MainWindow 一致。
        dlg->addButton(tr("Cancel"), true, DDialog::ButtonRecommend);
        dlg->addButton(tr("Update Anyway"), false, DDialog::ButtonWarning);

        const int ret = dlg->exec();
        const bool proceed = (ret == DDialog::Accepted);
        dlg->deleteLater();
        return proceed;
    }

    void UpdateDialogs::showPostCheck(const PostCheckReport& report)
    {
        DDialog* dlg = buildBaseDialog(tr("Update completed — attention required"),
                                       QStringLiteral("dialog-information"));
        QString body;
        if (report.rebootRequired)
            body += tr("A system reboot is recommended (kernel or base library updated).") +
                    QLatin1Char('\n');
        for (const QString& s : report.servicesNeedRestart)
            body += tr("Service needs restart: ") + s + QLatin1Char('\n');
        for (const QString& c : report.configFilesToReview)
            body += tr("Config file to review: ") + c + QLatin1Char('\n');
        for (const QString& u : report.failedUnits)
            body += tr("Failed service unit: ") + u + QLatin1Char('\n');
        for (const QString& p : report.residualPackages)
            body += tr("Residual / orphan package: ") + p + QLatin1Char('\n');
        if (report.cleanableCacheBytes > 0)
            body += tr("Download cache can be cleaned: %1 MB")
                        .arg(report.cleanableCacheBytes / (1024 * 1024)) +
                    QLatin1Char('\n');
        dlg->setMessage(body);
        dlg->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        dlg->exec();
        dlg->deleteLater();
    }

    void UpdateDialogs::showDistroNotices(const QList<SecurityAdvisor::Notice>& notices)
    {
        if (notices.isEmpty())
            return; // 无通知（离线/无源）时静默，不弹窗打扰
        DDialog* dlg = buildBaseDialog(tr("Recent release notes & notices"),
                                       QStringLiteral("dialog-information"));
        // 仅展示最近若干条，避免过长
        const int shown = qMin(notices.size(), 8);
        QString body;
        for (int i = 0; i < shown; ++i)
        {
            const auto& n = notices.at(i);
            body += QStringLiteral("• %1").arg(n.title);
            if (!n.date.isEmpty())
                body += QStringLiteral("  (%1)").arg(n.date);
            body += QLatin1Char('\n');
            if (!n.url.isEmpty())
                body += QStringLiteral("   %1\n").arg(n.url);
        }
        if (notices.size() > shown)
            body += QStringLiteral("\n… (%1 more)").arg(notices.size() - shown);
        dlg->setMessage(body);
        dlg->addButton(tr("OK"), true, DDialog::ButtonRecommend);
        dlg->exec();
        dlg->deleteLater();
    }

    void UpdateDialogs::showScheduleSettings(AppConfig* config)
    {
        if (!config)
            return;
        DDialog* dlg = buildBaseDialog(tr("Update Settings"), QStringLiteral("preferences-system"));
        auto* content = new QWidget(dlg);
        auto* layout = new QVBoxLayout(content);

        // —— 定时检测（默认关闭，需用户显式开启）——
        auto* schedTitle = new QLabel(tr("Periodic update check"), content);
        schedTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
        layout->addWidget(schedTitle);

        auto* offBtn = new QRadioButton(tr("Off — check only when I ask or on events"), content);
        auto* hourlyBtn = new QRadioButton(tr("Every hour"), content);
        auto* dailyBtn = new QRadioButton(tr("Every day"), content);
        auto* monthlyBtn = new QRadioButton(tr("Every month"), content);
        layout->addWidget(offBtn);
        layout->addWidget(hourlyBtn);
        layout->addWidget(dailyBtn);
        layout->addWidget(monthlyBtn);

        auto* valueRow = new QHBoxLayout;
        auto* valueLabel = new QLabel(tr("Check every"), content);
        auto* valueSpin = new QSpinBox(content);
        valueSpin->setRange(1, 999);
        valueSpin->setValue(config->checkIntervalValue());
        auto* unitLabel = new QLabel(content);
        valueRow->addWidget(valueLabel);
        valueRow->addWidget(valueSpin);
        valueRow->addWidget(unitLabel);
        valueRow->addStretch();
        layout->addLayout(valueRow);

        const QString cur = config->checkIntervalUnit();
        QRadioButton* curBtn = offBtn;
        if (cur == QLatin1String("hour"))
            curBtn = hourlyBtn;
        else if (cur == QLatin1String("day"))
            curBtn = dailyBtn;
        else if (cur == QLatin1String("month"))
            curBtn = monthlyBtn;
        curBtn->setChecked(true);

        auto updateUnitText = [=]()
        {
            const QString u = hourlyBtn->isChecked()    ? tr("hour(s)")
                              : dailyBtn->isChecked()   ? tr("day(s)")
                              : monthlyBtn->isChecked() ? tr("month(s)")
                                                        : QString();
            unitLabel->setText(u);
            valueSpin->setEnabled(!offBtn->isChecked());
        };
        connect(offBtn, &QRadioButton::toggled, content, updateUnitText);
        connect(hourlyBtn, &QRadioButton::toggled, content, updateUnitText);
        connect(dailyBtn, &QRadioButton::toggled, content, updateUnitText);
        connect(monthlyBtn, &QRadioButton::toggled, content, updateUnitText);
        updateUnitText();

        // —— 自动更新（默认关闭，需用户显式开启）——
        auto* autoTitle = new QLabel(tr("Automatic update"), content);
        autoTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
        layout->addWidget(autoTitle);
        auto* autoCheck =
            new QCheckBox(tr("Automatically install updates found by periodic checks"), content);
        autoCheck->setChecked(config->autoUpdateEnabled());
        layout->addWidget(autoCheck);
        auto* autoHint = new QLabel(
            tr("Disabled by default. When enabled, updates are installed automatically; if a "
               "security advisory or pre-update check recommends attention, your explicit "
               "confirmation is still required before any change is made."),
            content);
        autoHint->setWordWrap(true);
        autoHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        layout->addWidget(autoHint);
        layout->addStretch();

        dlg->addContent(content);
        dlg->addButton(tr("Cancel"), true, DDialog::ButtonRecommend);
        dlg->addButton(tr("OK"), false, DDialog::ButtonNormal);
        // DDialog::exec() 返回点击按钮的 0-based index；Accepted(1) = 点击第二个按钮(OK)。
        // 与 showSecurityPrompt 中 `ret == DDialog::Accepted` 的既有语义保持一致。
        if (dlg->exec() == DDialog::Accepted)
        {
            QString newUnit = QStringLiteral("disabled");
            if (hourlyBtn->isChecked())
                newUnit = QStringLiteral("hour");
            else if (dailyBtn->isChecked())
                newUnit = QStringLiteral("day");
            else if (monthlyBtn->isChecked())
                newUnit = QStringLiteral("month");
            config->setCheckIntervalUnit(newUnit);
            if (newUnit != QStringLiteral("disabled"))
                config->setCheckIntervalValue(valueSpin->value()); // 关闭定时时无需写间隔值
            config->setAutoUpdateEnabled(autoCheck->isChecked());
        }
        dlg->deleteLater();
    }

} // namespace DtkUpdate
