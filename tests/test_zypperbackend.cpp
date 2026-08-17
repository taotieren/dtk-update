#include <QStandardPaths>

#include "core/package/zypperbackend.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(ZypperBackendTest, Identifiers)
{
    ZypperBackend backend;
    EXPECT_EQ(backend.backendId(), QStringLiteral("zypper"));
    EXPECT_FALSE(backend.backendName().isEmpty());
    EXPECT_EQ(backend.backendType(), BackendType::Zypper);
    // zypper/rpm 无 dpkg rc 概念
    EXPECT_FALSE(backend.supportsResidualConfig());
    EXPECT_NO_THROW(backend.isAvailable());
    EXPECT_NO_THROW(backend.backendOptions());
}

// 回归测试：缺 zypper（或 rpm）命令时 isAvailable 必须返回 false，不能虚假报告可用。
TEST(ZypperBackendTest, NotAvailableWhenZypperMissing)
{
    ZypperBackend backend;
    const bool avail = backend.isAvailable();
#ifdef __linux__
    if (!QStandardPaths::findExecutable(QStringLiteral("zypper")).isEmpty() &&
        !QStandardPaths::findExecutable(QStringLiteral("rpm")).isEmpty())
    {
        GTEST_SKIP() << "real zypper/rpm environment, skip negative assertion";
    }
    EXPECT_FALSE(avail) << "zypper backend must NOT report available when "
                           "zypper/rpm is missing (avoids false-positive)";
#endif
}
