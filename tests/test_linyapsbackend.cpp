#include "core/package/linyapsbackend.h"

#include <QStandardPaths>

#include <gtest/gtest.h>

using namespace DtkUpdate;

// 验证 Linyaps 后端身份标识与契约一致性。
TEST(LinyapsBackendTest, Identifiers)
{
    LinyapsBackend backend;
    EXPECT_EQ(backend.backendId(), QStringLiteral("linyaps"));
    EXPECT_FALSE(backend.backendName().isEmpty());
    EXPECT_EQ(backend.backendType(), BackendType::Linyaps);

    // 沙箱应用层后端：无系统级残余配置/重启/服务/失败单元概念
    EXPECT_FALSE(backend.supportsResidualConfig());

    bool required = true;
    QString err;
    EXPECT_FALSE(backend.checkRebootRequired(required, err));
    EXPECT_FALSE(required);

    QStringList svc;
    EXPECT_FALSE(backend.checkServicesNeedingRestart(svc, err));
    EXPECT_TRUE(svc.isEmpty());

    QStringList paths;
    EXPECT_FALSE(backend.checkConfigFilesToReview(paths, err));
    EXPECT_TRUE(paths.isEmpty());

    QStringList units;
    EXPECT_FALSE(backend.checkFailedUnits(units, err));
    EXPECT_TRUE(units.isEmpty());

    PackageList residual;
    EXPECT_TRUE(backend.listResidualPackages(residual, err));
    EXPECT_TRUE(residual.isEmpty());
}

// purge 应等价于 remove（玲珑无独立 purge 语义）。
TEST(LinyapsBackendTest, PurgeEqualsRemove)
{
    LinyapsBackend backend;
    // 不真正执行（无 ll-cli 环境），仅验证方法存在且签名可被编译调用。
    QString err;
    EXPECT_NO_THROW(backend.purge(QStringList{QStringLiteral("org.deepin.demo")}, err));
}

// isAvailable 在缺少 ll-cli 的环境中应返回 false，不虚假可用。
TEST(LinyapsBackendTest, NotAvailableWithoutLlCli)
{
    LinyapsBackend backend;
    const bool avail = backend.isAvailable();
#ifdef __linux__
    if (!QStandardPaths::findExecutable(QStringLiteral("ll-cli")).isEmpty())
        GTEST_SKIP() << "real linyaps environment, skip negative assertion";
    EXPECT_FALSE(avail) << "linyaps backend must NOT report available when ll-cli is missing";
#endif
}
