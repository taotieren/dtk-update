#include <gtest/gtest.h>

#include "core/package/backendfactory.h"

using namespace DtkUpdate;

TEST(BackendFactoryTest, CreateById)
{
    // 按 id 强制创建后端（不依赖系统是否真实存在）
    PackageBackend *apt = BackendFactory::createById(QStringLiteral("apt"));
    ASSERT_NE(apt, nullptr);
    EXPECT_EQ(apt->backendId(), QStringLiteral("apt"));
    delete apt;

    PackageBackend *dnf = BackendFactory::createById(QStringLiteral("dnf"));
    ASSERT_NE(dnf, nullptr);
    EXPECT_EQ(dnf->backendId(), QStringLiteral("dnf"));
    delete dnf;

    // 未知 id 返回空
    EXPECT_EQ(BackendFactory::createById(QStringLiteral("nonexistent")), nullptr);
}

TEST(BackendFactoryTest, AvailableIdsDoesNotThrow)
{
    QStringList ids;
    EXPECT_NO_THROW(ids = BackendFactory::availableBackendIds());
    // 在 apt 系统上通常包含 "apt"
    if (ids.contains(QStringLiteral("apt")))
        SUCCEED();
    else
        GTEST_SKIP() << "apt backend not available on this system";
}

// 回归测试：对于尚无后端实现的发行系（如 Arch，预设为 pacman 但未实现），
// 工厂绝不能静默回退到 apt/dnf，应如实返回 nullptr。
TEST(BackendFactoryTest, NoSilentFallbackOnUnsupportedFamily)
{
    // Arch 系本机缺 apt-get/dpkg-query 与 rpm，任何后端都不可用
    PackageBackend *b = BackendFactory::createBackend(DistroProbe::Family::Arch, nullptr);
    EXPECT_EQ(b, nullptr) << "factory must not silently pick an unrelated backend "
                             "on families without an implemented backend";
    delete b;
}

// 回归测试：未配置首选后端时，仍按发行系预设探测，不默认落到 apt。
TEST(BackendFactoryTest, AutoDetectRespectsFamily)
{
    // 在 Arch 上自动探测应返回 nullptr（无可用后端）
    PackageBackend *b = BackendFactory::createBackend(DistroProbe::Family::Arch, nullptr,
                                                      QString());
    EXPECT_EQ(b, nullptr);
    delete b;
}
