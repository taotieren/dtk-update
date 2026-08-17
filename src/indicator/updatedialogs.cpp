// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "updatedialogs.h"

#include <DDialog>
#include <DLog>
#include <QIcon>

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

} // namespace DtkUpdate
