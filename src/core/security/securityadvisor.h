#pragma once

#include <QObject>
#include <QVariantMap>

namespace DtkUpdate
{

    /**
     * @brief 软件包安全公告聚合器
     *
     * 信息来源（按优先级，逐级降级，任何一级失败都不阻塞更新）：
     *   1. deepin 安全中心 D-Bus（仅 deepin 环境）
     *   2. 上游官方安全公告源（Debian DSA / Ubuntu CVE / Fedora 等），可选，
     *      通过 setFetchUpstream(true) 开启；网络失败/超时静默跳过
     *   3. 本地离线启发式（包名敏感度兜底）
     *
     * 重要约束：
     *   - 本类只「收集并展示信息」，绝不替用户决定、绝不自动触发更新。
     *   - 所有网络访问带超时，失败即降级，不引入不可控阻塞。
     *   - 是否获取上游公告、是否展示公告，最终决定权在用户（见配置项）。
     */
    class SecurityAdvisor : public QObject
    {
        Q_OBJECT
      public:
        explicit SecurityAdvisor(QObject* parent = nullptr);

        struct Advisory
        {
            QString package;
            QString severity; // none / low / medium / high / critical
            QString title;
            QString url;
            QString description;
            QString source; // deepin-center / upstream / offline
        };

        // 是否到上游官方源获取公告（默认 false，需用户显式开启）
        void setFetchUpstream(bool on) { m_fetchUpstream = on; }
        bool fetchUpstream() const { return m_fetchUpstream; }

        // 拉取指定包的安全公告；任何来源失败都降级而非报错。
        // 返回 false 仅当 packages 为空等参数问题；正常始终返回 true。
        bool fetchAdvisories(const QStringList& packages, QList<Advisory>& out);

        // 针对指定发行版拉取上游官方公告（仅当 setFetchUpstream(true) 时生效）。
        // 网络不可用/超时/解析失败 -> 返回空列表且不抛错。
        QList<Advisory> fetchUpstreamFor(const QString& distroId, const QStringList& packages);

        // 整体安全等级评估（用于托盘图标/提示着色）
        QString overallSeverity(const QList<Advisory>& advs) const;

        // 离线兜底：返回单包严重度（none/low/medium/high/critical），供单测与降级
        QString severityOfPackage(const QString& pkg) const;

      private:
        // 返回某发行版对应的上游官方安全公告索引 URL（无则空）
        static QString upstreamFeedUrl(const QString& distroId);

        bool m_fetchUpstream = false; // 是否获取上游官方源
    };

} // namespace DtkUpdate
