#include <QStandardPaths>

#include "core/package/pacmanbackend.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(PacmanBackendTest, Identifiers)
{
    PacmanBackend backend;
    EXPECT_EQ(backend.backendId(), QStringLiteral("pacman"));
    EXPECT_FALSE(backend.backendName().isEmpty());
    EXPECT_EQ(backend.backendType(), BackendType::Pacman);
    // pacman 无 rc 概念
    EXPECT_FALSE(backend.supportsResidualConfig());
    EXPECT_NO_THROW(backend.isAvailable());
    EXPECT_NO_THROW(backend.backendOptions());
}

// 回归测试：缺 pacman 命令时 isAvailable 必须返回 false，不能虚假报告可用。
TEST(PacmanBackendTest, NotAvailableWhenPacmanMissing)
{
    PacmanBackend backend;
    const bool avail = backend.isAvailable();
#ifdef __linux__
    if (!QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty())
    {
        GTEST_SKIP() << "real pacman environment, skip negative assertion";
    }
    EXPECT_FALSE(avail) << "pacman backend must NOT report available when "
                           "pacman is missing (avoids false-positive)";
#endif
}
