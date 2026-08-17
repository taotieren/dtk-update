#pragma once

#include <QObject>
#include <QVariantMap>

namespace DtkUpdate
{

    /**
     * @brief 软件包安全公告 / 发行版官方通知聚合器
     *
     * 安全信息来源（按优先级，逐级降级，任何一级失败都不阻塞更新）：
     *   1. deepin 安全中心 D-Bus（仅 deepin 环境）
     *   2. 发行版官方安全公告源（Debian DSA / Ubuntu USN / openSUSE / Arch 等），可选，
     *      通过 setFetchUpstream(true) 开启；网络失败/超时静默跳过
     *   3. 本地离线启发式（包名敏感度兜底）
     *
     * 发行版「最近新闻 / 通知」与之分离（fetchDistroNotices），与具体包名无关，
     * 仅展示给用户，不参与是否更新的决策。
     *
     * 重要约束：
     *   - 本类只「收集并展示信息」，绝不替用户决定、绝不自动触发更新。
     *   - 所有网络访问均为异步（信号槽），绝不 QEventLoop 阻塞调用线程（避免冻结桌面）。
     *   - 是否获取上游公告 / 发行版通知、是否展示，最终决定权在用户（见配置项）。
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

        // 发行版官方「最近新闻 / 通知」条目（与包名无关，仅展示）
        struct Notice
        {
            QString title;
            QString url;
            QString date;    // 发布时间（原始字符串，尽量保持源格式）
            QString summary; // 摘要（可空）
            QString source;  // 来源发行系名
        };

        // 跨线程信号（upstreamAdvisoriesReady / distroNoticesReady）携带 QList<自定义结构体>，
        // 必须注册元类型，否则 queued 连接会在运行时静默失败、QSignalSpy 取 value<>() 为空。
        // 此处声明，定义在 securityadvisor.cpp 顶部 qRegisterMetaType 完成注册。
        using AdvisoryList = QList<Advisory>;
        using NoticeList = QList<Notice>;

        // 是否到上游官方源获取公告（默认 false，需用户显式开启）
        void setFetchUpstream(bool on) { m_fetchUpstream = on; }
        bool fetchUpstream() const { return m_fetchUpstream; }

        // 同步聚合指定包的安全公告（快路径：安全中心 D-Bus + 离线兜底 + 已预取的上游缓存）。
        // 不发起任何网络请求，绝不阻塞调用线程。返回 true 表示正常（含降级）。
        bool fetchAdvisories(const QStringList& packages, QList<Advisory>& out);

        // 异步预取上游官方安全公告，结果缓存并在 upstreamAdvisoriesReady 信号返回。
        // 必须在 checkNow 拿到可升级包名后调用；applyUpdates 时 fetchAdvisories 会合并缓存。
        // 未开启 m_fetchUpstream 或无可机读源时直接发空信号。
        void prefetchUpstream(const QString& distroId, const QStringList& packages);

        // 异步拉取发行版官方「最近新闻 / 通知」（与包名无关）。结果经 distroNoticesReady 返回。
        void fetchDistroNotices(const QString& distroId);

        // 整体安全等级评估（用于托盘图标/提示着色）
        QString overallSeverity(const QList<Advisory>& advs) const;

        // 离线兜底：返回单包严重度（none/low/medium/high/critical），供单测与降级
        QString severityOfPackage(const QString& pkg) const;

      signals:
        // 上游公告预取完成（异步，可能为空列表）
        void upstreamAdvisoriesReady(const AdvisoryList& advs);
        // 发行版最近新闻/通知拉取完成（异步，可能为空列表）
        void distroNoticesReady(const NoticeList& notices);

      private:
        // 返回某发行版对应的上游官方安全公告索引 URL（无则空）。URL 均经核实可机读。
        static QString upstreamFeedUrl(const QString& distroId);
        // 返回某发行版对应的官方「最近新闻 / 通知」feed URL（无则空）
        static QString distroNoticeUrl(const QString& distroId);

        bool m_fetchUpstream = false;    // 是否获取上游官方源
        QList<Advisory> m_upstreamCache; // 最近一次异步预取的上游公告（供 fetchAdvisories 合并）
    };

} // namespace DtkUpdate
