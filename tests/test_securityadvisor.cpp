#include "core/security/securityadvisor.h"

#include <gtest/gtest.h>

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

TEST(SecurityAdvisorTest, UpstreamUnknownDistroReturnsEmpty)
{
    // 未知发行版没有映射的上游源 -> 应静默返回空列表，不崩溃/不阻塞
    SecurityAdvisor advisor;
    advisor.setFetchUpstream(true);
    QList<SecurityAdvisor::Advisory> ups =
        advisor.fetchUpstreamFor(QStringLiteral("unknown-distro-xyz"), {QStringLiteral("apt")});
    EXPECT_TRUE(ups.isEmpty());
}

TEST(SecurityAdvisorTest, AdvisoryHasSourceField)
{
    SecurityAdvisor advisor;
    QList<SecurityAdvisor::Advisory> out;
    ASSERT_TRUE(advisor.fetchAdvisories({QStringLiteral("systemd")}, out));
    ASSERT_EQ(out.size(), 1);
    EXPECT_EQ(out.first().source.toStdString(), "offline");
}
