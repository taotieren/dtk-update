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
    // pacman 无 recommends 概念，但有 .pacnew 残留配置需审阅
    EXPECT_TRUE(backend.supportsResidualConfig());
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

// 回归测试：real pacman 环境（如 Arch/Manjaro）须能扫描 /etc 下的 .pacnew/.pacsave
// 残留配置并报告 support=true。CI（ubuntu:devel 无 pacman）无此残留，故 SKIP 而非伪通过。
// 锁定 commit 4646676 之后补齐的 Arch 配置审阅能力，防后续回退为永远不扫。
TEST(PacmanBackendTest, ConfigFilesToReviewScansPacnewOnArch)
{
#ifdef __linux__
    if (QStandardPaths::findExecutable(QStringLiteral("pacman")).isEmpty())
        GTEST_SKIP() << "no pacman, skip Arch-specific residual config probe";
    PacmanBackend backend;
    QStringList paths;
    QString err;
    ASSERT_TRUE(backend.checkConfigFilesToReview(paths, err))
        << "pacman checkConfigFilesToReview must report support=true on Arch";
    // 不强制要求一定有残留（纯净系统为空属正常），仅断言接口可用且返回路径列表类型正确
    EXPECT_TRUE(paths.isEmpty() || !paths.first().isEmpty());
#else
    GTEST_SKIP() << "non-linux, skip";
#endif
}
