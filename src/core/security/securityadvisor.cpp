#include "securityadvisor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QEventLoop>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include "distroprobe.h"
#include "logger.h"

namespace DtkUpdate
{

    namespace
    {
        // deepin 安全中心 D-Bus（服务名/路径在不同版本可能微调，调用失败即降级）
        const QString kService = QStringLiteral("com.deepin.SecurityCenter");
        const QString kPath = QStringLiteral("/com/deepin/SecurityCenter");
        const QString kIface = QStringLiteral("com.deepin.SecurityCenter");

        constexpr int kUpstreamTimeoutMs = 3000; // 上游网络访问超时，避免阻塞更新流程

        int severityWeight(const QString& sev)
        {
            if (sev == QStringLiteral("critical"))
                return 4;
            if (sev == QStringLiteral("high"))
                return 3;
            if (sev == QStringLiteral("medium"))
                return 2;
            if (sev == QStringLiteral("low"))
                return 1;
            return 0;
        }

        // 离线兜底：对核心安全敏感组件给出基础提示（无安全中心时仍提示风险）
        QString offlineSeverity(const QString& pkg)
        {
            static const QStringList critical = {
                QStringLiteral("linux-image"), QStringLiteral("linux-generic"),
                QStringLiteral("systemd"),     QStringLiteral("libc6"),
                QStringLiteral("openssl"),     QStringLiteral("libssl"),
                QStringLiteral("sudo"),        QStringLiteral("polkitd"),
                QStringLiteral("policykit"),   QStringLiteral("dbus"),
                QStringLiteral("shadow"),      QStringLiteral("passwd"),
            };
            static const QStringList high = {
                QStringLiteral("apt"),        QStringLiteral("dpkg"),
                QStringLiteral("bash"),       QStringLiteral("coreutils"),
                QStringLiteral("glibc"),      QStringLiteral("gnutls"),
                QStringLiteral("libxml2"),    QStringLiteral("curl"),
                QStringLiteral("wget"),       QStringLiteral("firefox"),
                QStringLiteral("chromium"),   QStringLiteral("openssh"),
                QStringLiteral("ssh"),        QStringLiteral("cryptsetup"),
                QStringLiteral("dde-daemon"), QStringLiteral("deepin-daemon"),
            };
            for (const auto& c : critical)
                if (pkg.startsWith(c))
                    return QStringLiteral("critical");
            for (const auto& h : high)
                if (pkg.startsWith(h))
                    return QStringLiteral("high");
            return QStringLiteral("none");
        }

        // 解析上游 RSS/Atom 条目里出现的包名（宽松匹配），返回命中的 (包, 标题, 链接, 描述)
        QList<SecurityAdvisor::Advisory> parseUpstreamFeed(const QByteArray& data,
                                                           const QStringList& packages)
        {
            QList<SecurityAdvisor::Advisory> result;
            // 用简单文本扫描：匹配 <title>...</title> 与 <link>...</link>（兼容 RSS/Atom）
            QString text = QString::fromUtf8(data);
            // 转义处理（去除常见 XML 实体）
            text.replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
                .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                .replace(QStringLiteral("&#39;"), QStringLiteral("'"));

            const QStringList items =
                text.split(QRegularExpression(QStringLiteral("<item[ >]")), Qt::SkipEmptyParts);
            for (const QString& rawItem : items)
            {
                const QString item = rawItem.section(QStringLiteral("</item>"), 0, 0);
                const auto grab = [&](const QString& tag) -> QString
                {
                    const QRegularExpression re(tag + QStringLiteral(">([^<]*)</") + tag);
                    const QRegularExpressionMatch m = re.match(item);
                    return m.hasMatch() ? m.captured(1).trimmed() : QString();
                };
                // Atom 用 <entry>/<title>/<link href="...">
                const QString title = grab(QStringLiteral("title"));
                QString link = grab(QStringLiteral("link"));
                if (link.isEmpty())
                {
                    const QRegularExpression reLink(QStringLiteral("<link[^>]*href=\"([^\"]+)\""));
                    const QRegularExpressionMatch m = reLink.match(item);
                    if (m.hasMatch())
                        link = m.captured(1);
                }
                const QString desc = grab(QStringLiteral("description"));

                // 命中规则：标题或描述包含某个待查包名
                for (const QString& pkg : packages)
                {
                    if (!pkg.isEmpty() && (title.contains(pkg, Qt::CaseInsensitive) ||
                                           desc.contains(pkg, Qt::CaseInsensitive)))
                    {
                        SecurityAdvisor::Advisory a;
                        a.package = pkg;
                        a.severity = QStringLiteral("high"); // 上游公告默认高关注度
                        a.title = title.isEmpty() ? pkg : title;
                        a.url = link;
                        a.description = desc;
                        a.source = QStringLiteral("upstream");
                        result.append(a);
                        break;
                    }
                }
            }
            return result;
        }
    } // namespace

    SecurityAdvisor::SecurityAdvisor(QObject* parent) : QObject(parent)
    {
        if (!QDBusConnection::systemBus().interface() ||
            !QDBusConnection::systemBus().interface()->isServiceRegistered(kService))
            qCInfo(dtkUpdateCore)
                << "deepin security center D-Bus unavailable, fallback to offline heuristic";
    }

    QString SecurityAdvisor::upstreamFeedUrl(const QString& distroId)
    {
        // 仅列出有明确公开、稳定安全公告 RSS/页面源的发行版；不存在则返回空（不抓取）。
        // Fedora 没有稳定可机读的官方安全公告源，故不列出（避免指向无关的通用邮件列表）。
        static const QMap<QString, QString> feeds = {
            // Debian 安全公告（DSA）RSS
            {QStringLiteral("debian"), QStringLiteral("https://www.debian.org/security/dsa")},
            // Ubuntu 安全公告（USN）RSS
            {QStringLiteral("ubuntu"), QStringLiteral("https://ubuntu.com/security/notices")},
        };
        return feeds.value(distroId.toLower());
    }

    QList<SecurityAdvisor::Advisory> SecurityAdvisor::fetchUpstreamFor(const QString& distroId,
                                                                       const QStringList& packages)
    {
        QList<Advisory> result;
        if (!m_fetchUpstream || packages.isEmpty())
            return result;

        const QString feedUrl = upstreamFeedUrl(distroId);
        if (feedUrl.isEmpty())
        {
            qCInfo(dtkUpdateCore) << "no upstream advisory feed for distro:" << distroId
                                  << ", skip";
            return result;
        }

        // 同步网络访问（带超时）：仅在 daemon/非交互密集场景调用，且超时很短。
        QNetworkAccessManager nam;
        QNetworkRequest req{QUrl(feedUrl)};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("dtk-update/1.0 (advisory-fetch)"));

        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        QNetworkReply* reply = nam.get(req);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

        timer.start(kUpstreamTimeoutMs);
        loop.exec();
        if (!reply->isFinished())
        {
            reply->abort();
            qCWarning(dtkUpdateCore) << "upstream advisory fetch timeout:" << distroId;
            reply->deleteLater();
            return result; // 静默降级
        }
        if (reply->error() != QNetworkReply::NoError)
        {
            qCWarning(dtkUpdateCore) << "upstream advisory fetch failed:" << reply->errorString();
            reply->deleteLater();
            return result; // 静默降级
        }
        const QByteArray data = reply->readAll();
        reply->deleteLater();
        result = parseUpstreamFeed(data, packages);
        return result;
    }

    bool SecurityAdvisor::fetchAdvisories(const QStringList& packages, QList<Advisory>& out)
    {
        out.clear();
        if (packages.isEmpty())
            return true;

        // 1) 尝试从安全中心 D-Bus 拉取（失败则降级）
        if (QDBusConnection::systemBus().interface() &&
            QDBusConnection::systemBus().interface()->isServiceRegistered(kService))
        {
            QDBusInterface iface(kService, kPath, kIface, QDBusConnection::systemBus());
            if (iface.isValid())
            {
                QDBusReply<QVariantList> reply =
                    iface.call(QStringLiteral("QueryAdvisory"), packages);
                if (reply.isValid())
                {
                    for (const auto& v : reply.value())
                    {
                        const QVariantList row = v.toList();
                        if (row.size() < 5)
                            continue;
                        Advisory a;
                        a.package = row[0].toString();
                        a.severity = row[1].toString();
                        a.title = row[2].toString();
                        a.url = row[3].toString();
                        a.description = row[4].toString();
                        a.source = QStringLiteral("deepin-center");
                        out.append(a);
                    }
                    return true;
                }
                qCWarning(dtkUpdateCore) << "QueryAdvisory failed, fall back to offline heuristic";
            }
        }

        // 2) 上游官方公告（可选，开启且可用才抓取；失败静默降级）
        if (m_fetchUpstream)
        {
            const QString distro = DistroProbe::detectId();
            const QList<Advisory> ups = fetchUpstreamFor(distro, packages);
            for (const auto& a : ups)
                out.append(a);
        }

        // 3) 离线兜底：基于包名启发式给出基础风险提示（始终可用）
        for (const auto& pkg : packages)
        {
            const QString sev = offlineSeverity(pkg);
            if (sev == QStringLiteral("none"))
                continue;
            Advisory a;
            a.package = pkg;
            a.severity = sev;
            a.title = tr("Security-sensitive package update");
            a.description =
                tr("This package is security-sensitive. Review the changelog before updating.");
            a.source = QStringLiteral("offline");
            out.append(a);
        }
        return true; // 降级不报错，不阻塞更新流程
    }

    QString SecurityAdvisor::overallSeverity(const QList<Advisory>& advs) const
    {
        int maxW = 0;
        for (const auto& a : advs)
        {
            const int w = severityWeight(a.severity);
            if (w > maxW)
                maxW = w;
        }
        switch (maxW)
        {
        case 4:
            return QStringLiteral("critical");
        case 3:
            return QStringLiteral("high");
        case 2:
            return QStringLiteral("medium");
        case 1:
            return QStringLiteral("low");
        default:
            return QStringLiteral("none");
        }
    }

    QString SecurityAdvisor::severityOfPackage(const QString& pkg) const
    {
        return offlineSeverity(pkg);
    }

} // namespace DtkUpdate
