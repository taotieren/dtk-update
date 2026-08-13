#include <QStandardPaths>

#include "core/package/aptbackend.h"
#include "core/package/packageparser.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(AptBackendTest, Identifiers)
{
    AptBackend backend;
    EXPECT_EQ(backend.backendId(), QStringLiteral("apt"));
    EXPECT_FALSE(backend.backendName().isEmpty());
    EXPECT_EQ(backend.backendType(), BackendType::Apt);
    // deb/apt 系支持残留配置（rc 状态）
    EXPECT_TRUE(backend.supportsResidualConfig());
    // isAvailable 不崩溃（容器/无 apt 环境下应为 false，不抛异常）
    EXPECT_NO_THROW(backend.isAvailable());
}

TEST(AptBackendTest, OptionsReflectConfig)
{
    AptBackend backend;
    EXPECT_NO_THROW(backend.backendOptions());
}

TEST(AptBackendTest, ParseUpgradableLine)
{
    PackageInfo info;
    const QString line = QStringLiteral("firefox/stable 120.0.1 amd64 [upgradable from: 119.0]");
    ASSERT_TRUE(PackageParser::parseUpgradableLine(line, info));
    EXPECT_EQ(info.name, QStringLiteral("firefox"));
    EXPECT_EQ(info.candidateVersion, QStringLiteral("120.0.1"));
    EXPECT_EQ(info.currentVersion, QStringLiteral("119.0"));
}

// 回归测试：当关键命令缺失（如仅存在 apt 占位脚本、缺 apt-get/dpkg-query）时，
// isAvailable 必须返回 false，绝不能虚假报告可用。
TEST(AptBackendTest, NotAvailableWhenKeyCommandsMissing)
{
    // 本机（Arch 仿真环境）无 apt-get/dpkg-query，apt 只是 tinyget 占位脚本
    AptBackend backend;
    const bool avail = backend.isAvailable();
#ifdef __linux__
    // 仅在确认缺失关键命令的平台上断言为 false，避免在有真实 apt 的 CI 上误判
    if (!QStandardPaths::findExecutable(QStringLiteral("apt-get")).isEmpty() &&
        !QStandardPaths::findExecutable(QStringLiteral("dpkg-query")).isEmpty())
    {
        GTEST_SKIP() << "real apt environment, skip negative assertion";
    }
    EXPECT_FALSE(avail) << "apt backend must NOT report available when "
                           "apt-get/dpkg-query are missing (avoids false-positive)";
#endif
}
