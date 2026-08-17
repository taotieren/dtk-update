#include "securityadvisor.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <functional>

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

        constexpr int kUpstreamTimeoutMs = 5000; // 上游网络访问超时，避免长期挂起

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

        // 去除常见 XML 实体，便于文本匹配
        QString decodeEntities(QString text)
        {
            return text.replace(QStringLiteral("&lt;"), QStringLiteral("<"))
                .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
                .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
                .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
                .replace(QStringLiteral("&#39;"), QStringLiteral("'"));
        }

        // 把一个 RSS <item>/Atom <entry> 片段里的字段取出来
        struct FeedItem
        {
            QString title;
            QString link;
            QString desc; // description 或 summary
            QString date; // pubDate / updated
        };

        FeedItem grabItem(const QString& raw)
        {
            FeedItem it;
            const QString item = raw.section(QStringLiteral("</item>"), 0, 0)
                                     .section(QStringLiteral("</entry>"), 0, 0);
            const auto grab = [&](const QString& tag) -> QString
            {
                const QRegularExpression re(tag + QStringLiteral(">([^<]*)</") + tag);
                const QRegularExpressionMatch m = re.match(item);
                return m.hasMatch() ? m.captured(1).trimmed() : QString();
            };
            it.title = grab(QStringLiteral("title"));
            it.desc = grab(QStringLiteral("description"));
            if (it.desc.isEmpty())
                it.desc = grab(QStringLiteral("summary"));
            it.date = grab(QStringLiteral("pubDate"));
            if (it.date.isEmpty())
                it.date = grab(QStringLiteral("updated"));
            it.link = grab(QStringLiteral("link"));
            if (it.link.isEmpty())
            {
                // Atom 形式：<link href="..."/>
                const QRegularExpression reLink(QStringLiteral("<link[^>]*href=\"([^\"]+)\""));
                const QRegularExpressionMatch m = reLink.match(item);
                if (m.hasMatch())
                    it.link = m.captured(1);
            }
            return it;
        }

        // 把整段流切成 item/entry 片段列表
        QStringList splitItems(const QString& text)
        {
            QStringList items =
                text.split(QRegularExpression(QStringLiteral("<item[ >]")), Qt::SkipEmptyParts);
            // 去掉第一段里 channel 之前的无关内容
            if (!items.isEmpty())
                items.removeFirst();
            const QStringList entries =
                text.split(QRegularExpression(QStringLiteral("<entry[ >]")), Qt::SkipEmptyParts);
            for (const QString& e : entries)
            {
                if (!e.isEmpty())
                    items.append(e);
            }
            return items;
        }

        // 解析上游安全公告流（命中 packages 的条目）
        QList<SecurityAdvisor::Advisory> parseAdvisoryFeed(const QByteArray& data,
                                                           const QStringList& packages)
        {
            QList<SecurityAdvisor::Advisory> result;
            const QString text = decodeEntities(QString::fromUtf8(data));
            for (const QString& raw : splitItems(text))
            {
                const FeedItem it = grabItem(raw);
                for (const QString& pkg : packages)
                {
                    if (!pkg.isEmpty() && (it.title.contains(pkg, Qt::CaseInsensitive) ||
                                           it.desc.contains(pkg, Qt::CaseInsensitive)))
                    {
                        SecurityAdvisor::Advisory a;
                        a.package = pkg;
                        a.severity = QStringLiteral("high"); // 上游公告默认高关注度
                        a.title = it.title.isEmpty() ? pkg : it.title;
                        a.url = it.link;
                        a.description = it.desc;
                        a.source = QStringLiteral("upstream");
                        result.append(a);
                        break;
                    }
                }
            }
            return result;
        }

        // 解析发行版「最近新闻 / 通知」流（与包名无关）
        QList<SecurityAdvisor::Notice> parseNoticeFeed(const QByteArray& data,
                                                       const QString& source)
        {
            QList<SecurityAdvisor::Notice> result;
            const QString text = decodeEntities(QString::fromUtf8(data));
            for (const QString& raw : splitItems(text))
            {
                const FeedItem it = grabItem(raw);
                if (it.title.isEmpty())
                    continue;
                SecurityAdvisor::Notice n;
                n.title = it.title;
                n.url = it.link;
                n.date = it.date;
                n.summary = it.desc;
                n.source = source;
                result.append(n);
            }
            return result;
        }

        // 异步 GET（带超时）：完成/超时后 safeDelete，回调通过 functor 投递
        // owner 必须传入 SecurityAdvisor 自身：nam/timer/reply 全部挂到 owner 下，
        // 一旦 SecurityAdvisor 先于异步完成被析构，整条异步链随父对象一起销毁，
        // 杜绝「对象已亡、定时器却在进程退出时才触发 [this] 回调」的 use-after-free 段错误。
        void asyncGet(QObject* owner, const QString& url, int timeoutMs,
                      std::function<void(const QByteArray&, bool ok)> onDone)
        {
            auto* nam = new QNetworkAccessManager(owner);
            QNetworkRequest req{QUrl(url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
            req.setHeader(QNetworkRequest::UserAgentHeader,
                          QStringLiteral("dtk-update/1.0 (advisory-fetch)"));
            // 请求稳定语言（英文），避免服务端按 Accept-Language 返回本地化描述导致
            // parseAdvisoryFeed 的包名匹配漏判；等价于本地解析前注入 LC_ALL=C 的区域稳定纪律。
            req.setRawHeader("Accept-Language", "en");
            QNetworkReply* reply = nam->get(req);

            auto* timer = new QTimer(nam);
            timer->setSingleShot(true);
            QObject::connect(timer, &QTimer::timeout, nam,
                             [reply]()
                             {
                                 if (!reply->isFinished())
                                     reply->abort();
                             });
            QObject::connect(reply, &QNetworkReply::finished, nam,
                             [=]() mutable
                             {
                                 timer->stop();
                                 const bool ok = reply->error() == QNetworkReply::NoError;
                                 const QByteArray data = ok ? reply->readAll() : QByteArray();
                                 if (!ok)
                                     qCWarning(dtkUpdateCore)
                                         << "advisory fetch failed:" << url << reply->errorString();
                                 reply->deleteLater();
                                 nam->deleteLater();
                                 onDone(data, ok);
                             });
            timer->start(timeoutMs);
        }
    } // namespace

    SecurityAdvisor::SecurityAdvisor(QObject* parent) : QObject(parent)
    {
        if (!QDBusConnection::systemBus().interface() ||
            !QDBusConnection::systemBus().interface()->isServiceRegistered(kService))
            qCInfo(dtkUpdateCore)
                << "deepin security center D-Bus unavailable, fallback to offline heuristic";
    }

    // 各发行系官方可机读安全公告源（均已核实可访问，返回 RSS/RDF/Atom）。
    // 若无稳定官方源则留空（静默跳过），绝不指向无关或失效地址。
    QString SecurityAdvisor::upstreamFeedUrl(const QString& distroId)
    {
        const DistroProbe::Family fam = DistroProbe::detectFamily();
        Q_UNUSED(distroId)
        switch (fam)
        {
        case DistroProbe::Family::Debian: // 含 Ubuntu / deepin / UOS 等衍生
            // Ubuntu 用 USN RSS；其余 Debian 系用 DSA RDF
            if (DistroProbe::detectId() == QStringLiteral("ubuntu"))
                return QStringLiteral("https://ubuntu.com/security/notices/rss.xml");
            return QStringLiteral("https://www.debian.org/security/dsa.rdf");
        case DistroProbe::Family::Suse:
            return QStringLiteral("https://lists.opensuse.org/archives/list/"
                                  "security-announce@lists.opensuse.org/feed");
        case DistroProbe::Family::Arch:
            // Arch 无官方安全公告 RSS；取官方新闻流作最近通知/手动干预提示
            return QStringLiteral("https://archlinux.org/feeds/news/");
        case DistroProbe::Family::Fedora:
            // Fedora 无稳定官方安全公告 RSS，留空跳过（不指向失效地址）
            return QString();
        case DistroProbe::Family::Unknown:
        default:
            return QString();
        }
    }

    // 各发行系官方「最近新闻 / 通知」feed（与包名无关，仅展示）
    QString SecurityAdvisor::distroNoticeUrl(const QString& distroId)
    {
        const DistroProbe::Family fam = DistroProbe::detectFamily();
        Q_UNUSED(distroId)
        switch (fam)
        {
        case DistroProbe::Family::Debian:
            // Debian/Ubuntu 无可靠官方「最近新闻」RSS：Debian 官网新闻页返回 HTML（非标准
            // feed），Ubuntu 官方 blog RSS 已失效（404）。按约束保持返回空、不指向失效地址。
            return QString();
        case DistroProbe::Family::Suse:
            return QStringLiteral("https://news.opensuse.org/feed/");
        case DistroProbe::Family::Arch:
            return QStringLiteral("https://archlinux.org/feeds/news/");
        case DistroProbe::Family::Fedora:
            return QStringLiteral("https://fedoramagazine.org/feed/");
        case DistroProbe::Family::Unknown:
        default:
            return QString();
        }
    }

    void SecurityAdvisor::prefetchUpstream(const QString& distroId, const QStringList& packages)
    {
        m_upstreamCache.clear();
        if (!m_fetchUpstream || packages.isEmpty())
        {
            emit upstreamAdvisoriesReady(m_upstreamCache);
            return;
        }
        const QString feedUrl = upstreamFeedUrl(distroId);
        if (feedUrl.isEmpty())
        {
            qCInfo(dtkUpdateCore) << "no upstream advisory feed for distro, skip";
            emit upstreamAdvisoriesReady(m_upstreamCache);
            return;
        }
        asyncGet(this, feedUrl, kUpstreamTimeoutMs,
                 [this, packages](const QByteArray& data, bool ok)
                 {
                     if (ok)
                         m_upstreamCache = parseAdvisoryFeed(data, packages);
                     emit upstreamAdvisoriesReady(m_upstreamCache);
                 });
    }

    void SecurityAdvisor::fetchDistroNotices(const QString& distroId)
    {
        const QString feedUrl = distroNoticeUrl(distroId);
        if (feedUrl.isEmpty())
        {
            emit distroNoticesReady(QList<Notice>());
            return;
        }
        const QString source = DistroProbe::familyName(DistroProbe::detectFamily());
        asyncGet(
            this, feedUrl, kUpstreamTimeoutMs, [this, source](const QByteArray& data, bool ok)
            { emit distroNoticesReady(ok ? parseNoticeFeed(data, source) : QList<Notice>()); });
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

        // 2) 合并已异步预取的上游公告缓存（若开启且已就绪）
        for (const auto& a : m_upstreamCache)
            out.append(a);

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
