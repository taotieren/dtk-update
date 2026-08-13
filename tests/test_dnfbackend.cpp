#include <QStandardPaths>

#include "core/package/dnfbackend.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(DnfBackendTest, Identifiers)
{
    DnfBackend backend;
    EXPECT_EQ(backend.backendId(), QStringLiteral("dnf"));
    EXPECT_FALSE(backend.backendName().isEmpty());
    EXPECT_EQ(backend.backendType(), BackendType::Dnf);
    // dnf/rpm 没有 rc 概念
    EXPECT_FALSE(backend.supportsResidualConfig());
    EXPECT_NO_THROW(backend.isAvailable());
    EXPECT_NO_THROW(backend.backendOptions());
}

// 回归测试：缺 rpm（仅存在 dnf 占位脚本）时 isAvailable 必须返回 false，
// 不能虚假报告可用。
TEST(DnfBackendTest, NotAvailableWhenRpmMissing)
{
    DnfBackend backend;
    const bool avail = backend.isAvailable();
#ifdef __linux__
    if (!QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty())
    {
        GTEST_SKIP() << "real dnf/rpm environment, skip negative assertion";
    }
    EXPECT_FALSE(avail) << "dnf backend must NOT report available when "
                           "rpm is missing (avoids false-positive)";
#endif
}
