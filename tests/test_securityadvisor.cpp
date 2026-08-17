#include <QSignalSpy>

#include "core/security/securityadvisor.h"

#include <gtest/gtest.h>

// ensureApp() 在 test_runprivasync.cpp 中定义：保证存在 QCoreApplication，
// 使 SecurityAdvisor 内部的 QNetworkAccessManager / QTimer 异步超时能在
// QSignalSpy::wait 期间被事件循环驱动。缺少它将导致异步网络请求永不回调
// （spy.wait 无法驱动事件循环），进而整个测试套件在 CI 中无限挂起。
extern void ensureApp();

using namespace DtkUpdate;

TEST(SecurityAdvisorTest, OfflineSeverityCritical)
{
    SecurityAdvisor advisor;
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("linux-image-6.6-amd64")),
              QStringLiteral("critical"));
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("systemd")), QStringLiteral("critical"));
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("openssl")), QStringLiteral("critical"));
}

TEST(SecurityAdvisorTest, OfflineSeverityHigh)
{
    SecurityAdvisor advisor;
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("apt")), QStringLiteral("high"));
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("firefox")), QStringLiteral("high"));
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("openssh-server")), QStringLiteral("high"));
}

TEST(SecurityAdvisorTest, OfflineSeverityNone)
{
    SecurityAdvisor advisor;
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("cowsay")), QStringLiteral("none"));
    EXPECT_EQ(advisor.severityOfPackage(QStringLiteral("fortune-mod")), QStringLiteral("none"));
}

TEST(SecurityAdvisorTest, OverallSeverityPicksMax)
{
    SecurityAdvisor advisor;
    SecurityAdvisor::Advisory low, high;
    low.package = QStringLiteral("a");
    low.severity = QStringLiteral("low");
    high.package = QStringLiteral("b");
    high.severity = QStringLiteral("high");
    QList<SecurityAdvisor::Advisory> list{low, high};
    EXPECT_EQ(advisor.overallSeverity(list), QStringLiteral("high"));
}

TEST(SecurityAdvisorTest, OverallSeverityEmpty)
{
    SecurityAdvisor advisor;
    EXPECT_EQ(advisor.overallSeverity({}), QStringLiteral("none"));
}

TEST(SecurityAdvisorTest, OverallSeverityCriticalWins)
{
    SecurityAdvisor advisor;
    SecurityAdvisor::Advisory a;
    a.package = QStringLiteral("linux-image-6.6");
    a.severity = QStringLiteral("critical");
    EXPECT_EQ(advisor.overallSeverity({a}), QStringLiteral("critical"));
}

TEST(SecurityAdvisorTest, UpstreamDisabledByDefault)
{
    SecurityAdvisor advisor;
    EXPECT_FALSE(advisor.fetchUpstream());
}

TEST(SecurityAdvisorTest, UpstreamDisabledNoSignal)
{
    // fetchUpstream 默认关闭时 prefetchUpstream 同步 emit 一次空缓存（不预取、不触网），
    // 且不崩溃、不阻塞。注意：禁用分支为同步 emit，故直接检查 spy.count() 而非 wait()。
    ensureApp();
    SecurityAdvisor advisor; // 默认 fetchUpstream=false
    QSignalSpy spy(&advisor, &SecurityAdvisor::upstreamAdvisoriesReady);
    advisor.prefetchUpstream(QStringLiteral("unknown-distro-xyz"), {QStringLiteral("apt")});
    EXPECT_EQ(spy.count(), 1); // 禁用时同步 emit 空缓存
    EXPECT_TRUE(spy.first().at(0).value<QList<SecurityAdvisor::Advisory>>().isEmpty());
}

TEST(SecurityAdvisorTest, UpstreamPrefetchAsyncSignals)
{
    // prefetchUpstream 是异步的：调用后应在超时内收到 upstreamAdvisoriesReady 信号，
    // 不崩溃、不阻塞调用线程。CI 无外网时走 QTimer 超时路径（abort 后 emit 空），
    // 有网时则真实抓取；二者都在 5s 内完成。依赖 ensureApp() 驱动 Qt 事件循环。
    ensureApp();
    SecurityAdvisor advisor;
    advisor.setFetchUpstream(true);
    QSignalSpy spy(&advisor, &SecurityAdvisor::upstreamAdvisoriesReady);
    advisor.prefetchUpstream(QStringLiteral("unknown-distro-xyz"), {QStringLiteral("apt")});
    EXPECT_TRUE(spy.wait(5000)); // 超时前必收到信号（空或含条目）
    EXPECT_EQ(spy.count(), 1);
}

TEST(SecurityAdvisorTest, AdvisoryHasSourceField)
{
    SecurityAdvisor advisor;
    QList<SecurityAdvisor::Advisory> out;
    ASSERT_TRUE(advisor.fetchAdvisories({QStringLiteral("systemd")}, out));
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.first().source.toStdString(), "offline");
}
