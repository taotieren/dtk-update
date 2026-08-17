#include <QDir>
#include <QFileInfo>
#include <QSysInfo>
#include <algorithm>

#include "common/systeminfo.h"
#include "core/healthcheck/postupdatecheck.h"
#include "core/healthcheck/preupdatecheck.h"
#include "core/package/aptbackend.h"
#include "core/package/dnfbackend.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

// AptBackend 的预检方法不执行写操作，仅探测；此处验证返回语义与稳定性。
TEST(HealthCheckTest, AptRebootRequiredReadsFlagFile)
{
    AptBackend be;
    bool required = false;
    QString err;
    ASSERT_TRUE(be.checkRebootRequired(required, err));
    // 本机通常无 /run/reboot-required，期望 false（不崩溃、不误报）
    EXPECT_FALSE(required);
}

TEST(HealthCheckTest, AptServicesNeedsRestartProbeStable)
{
    AptBackend be;
    QStringList svcs;
    QString err;
    // 命令不存在时 support=false，不崩溃；存在时返回列表
    const bool supported = be.checkServicesNeedingRestart(svcs, err);
    EXPECT_TRUE(supported || svcs.isEmpty());
    SUCCEED();
}

TEST(HealthCheckTest, AptConfigFilesToReviewNoCrash)
{
    AptBackend be;
    QStringList paths;
    QString err;
    ASSERT_TRUE(be.checkConfigFilesToReview(paths, err));
    SUCCEED(); // 不保证有 .dpkg-new，仅验证调用稳定
}

TEST(HealthCheckTest, PreCheckAggNoBackendSafe)
{
    PreCheckReport r = PreUpdateCheck::run(nullptr);
    EXPECT_FALSE(r.hasAnything());
    EXPECT_FALSE(r.rebootRequired);
}

TEST(HealthCheckTest, PostCheckAggNoBackendSafe)
{
    PostCheckReport r = PostUpdateCheck::run(nullptr);
    EXPECT_FALSE(r.hasAnything());
    EXPECT_FALSE(r.rebootRequired);
}

// 确定性假后端：覆盖四个预检探针返回 false/空，避免测试受真实宿主 failed unit
// 或待审配置影响（见 AGENTS.md「测试坑 · MonitorFakeBackend 需覆盖预检探针」）。
class HealthCheckFakeBackend : public PackageBackend
{
    Q_OBJECT
  public:
    explicit HealthCheckFakeBackend(QObject* parent = nullptr) : PackageBackend(parent) {}
    bool isAvailable() const override { return true; }
    QString availabilityError() const override { return QString(); }
    QStringList privilegedPrefix() const override { return {QStringLiteral("echo")}; }
    BackendType backendType() const override { return BackendType::Apt; }
    QString backendId() const override { return QStringLiteral("fake"); }
    QString backendName() const override { return QStringLiteral("Fake"); }
    bool supportsResidualConfig() const override { return false; }
    bool fetchUpgradable(PackageList&, QString&) override { return true; }
    bool listInstalled(PackageList&, const QString&, QString&) override { return true; }
    bool simulateInstall(const QString&, QString&, QString&) override { return true; }
    bool listResidualPackages(PackageList&, QString&) override { return true; }
    QStringList cacheDirectories() const override { return {}; }
    bool checkRebootRequired(bool& required, QString&) override
    {
        required = false;
        return false;
    }
    bool checkServicesNeedingRestart(QStringList& svcs, QString&) override
    {
        svcs.clear();
        return false;
    }
    bool checkConfigFilesToReview(QStringList& cfgs, QString&) override
    {
        cfgs.clear();
        return false;
    }
    bool checkFailedUnits(QStringList& units, QString&) override
    {
        units.clear();
        return false;
    }
};

TEST(HealthCheckTest, PreCheckAggregatesFromAptBackend)
{
    HealthCheckFakeBackend be;
    PreCheckReport r = PreUpdateCheck::run(&be);
    // 确定性后端：聚合逻辑稳定、不崩溃、结构有效，且不带任何环境相关需确认项。
    EXPECT_FALSE(r.hasAnything());
}

TEST(HealthCheckTest, PostCheckAggregatesFromAptBackend)
{
    HealthCheckFakeBackend be;
    PostCheckReport r = PostUpdateCheck::run(&be);
    EXPECT_FALSE(r.hasAnything());
}

// 新增：失败的 systemd units 探针——本机无 systemd 时应安全返回 support=false。
TEST(HealthCheckTest, AptFailedUnitsProbeStable)
{
    AptBackend be;
    QStringList units;
    QString err;
    const bool supported = be.checkFailedUnits(units, err);
    EXPECT_TRUE(supported || units.isEmpty());
    SUCCEED();
}

// 新增：容器环境跳过内核重启误报（容器内不应判需重启）。
TEST(HealthCheckTest, ContainerSkipsRebootFalsePositive)
{
    if (!SystemInfo::isContainer())
        GTEST_SKIP() << "not running in a container";
    AptBackend be;
    bool required = true;
    QString err;
    ASSERT_TRUE(be.checkRebootRequired(required, err));
    EXPECT_FALSE(required) << "container must not report reboot-required (avoids false positive)";
}

// 新增：DnfBackend 探针在任意环境都必须优雅（不崩溃、返回合法语义）。
// 本机是 Arch（非容器、/usr/lib/modules 存在），dnf reboot 探针会走内核比对分支
// 返回 support=true；服务/失败 units 在无对应命令时返回 support=false。重点是稳定。
TEST(HealthCheckTest, DnfProbesGraceful)
{
    DnfBackend be;
    bool required = false;
    QStringList svcs, cfgs, units;
    QString err;
    const bool rebootSupported = be.checkRebootRequired(required, err);
    EXPECT_TRUE(rebootSupported); // 本机有 /usr/lib/modules，内核比对分支必支持
    const bool svcSupported = be.checkServicesNeedingRestart(svcs, err);
    EXPECT_TRUE(svcSupported || svcs.isEmpty());
    ASSERT_TRUE(be.checkConfigFilesToReview(cfgs, err)); // 扫描 /etc 总是可做的
    const bool unitsSupported = be.checkFailedUnits(units, err);
    EXPECT_TRUE(unitsSupported || units.isEmpty());
}

// 回归：DnfBackend 重启探测必须按"运行内核 ≠ 已安装最新内核"判定需重启，
// 而非静默 false。本测试在"本机无 needs-restarting"时验证内核比对回退分支的
// 一致性；needs-restarting 退出码（1=需重启）语义由 RunProbeTest 直接锚定。
TEST(HealthCheckTest, DnfRebootProbeFallsBackToKernelCompare)
{
    // 本机若无 needs-restarting，则走内核比对回退；本测试仅验证"探测不会把
    // 一个明确需重启的信号误判为不需重启"的契约：通过内核比对分支的一致性保证。
    DnfBackend be;
    bool required = false;
    QString err;
    ASSERT_TRUE(be.checkRebootRequired(required, err));
    // running 内核与 /usr/lib/modules 最新内核若不一致，必须判需重启（不应静默 false）
    const QString running = QSysInfo::kernelVersion();
    QDir modules(QStringLiteral("/usr/lib/modules"));
    if (modules.exists())
    {
        QStringList kernels;
        for (const QFileInfo& fi : modules.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            kernels.append(fi.fileName());
        if (!kernels.isEmpty())
        {
            std::sort(kernels.begin(), kernels.end());
            const QString latest = kernels.last();
            EXPECT_EQ(required, (running != latest))
                << "rebootRequired must equal (running kernel != latest installed kernel)";
        }
    }
}

// 新增：PostCheckReport 新字段（残留包/可清理缓存）结构稳定且默认无内容。
TEST(HealthCheckTest, PostCheckReportNewFieldsDefault)
{
    PostCheckReport r;
    EXPECT_TRUE(r.residualPackages.isEmpty());
    EXPECT_EQ(r.cleanableCacheBytes, 0);
    EXPECT_FALSE(r.hasAnything());
}

#include "test_healthcheck.moc"
