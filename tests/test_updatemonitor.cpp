#include "core/monitor/updatemonitor.h"
#include "core/package/packagebackend.h"
#include "core/security/securityadvisor.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QTest>
#include <gtest/gtest.h>

using namespace DtkUpdate;

// 进程级 QCoreApplication 单例由 test_runprivasync.cpp 定义（同一 Qt::Test 条件块内），
// 此处仅声明引用，避免多 TU 重复构造 QCoreApplication 导致崩溃。
extern void ensureApp();

// 伪后端：可控返回，记录写操作调用
class MonitorFakeBackend : public PackageBackend
{
    Q_OBJECT
  public:
    explicit MonitorFakeBackend(QObject* parent = nullptr) : PackageBackend(parent) {}
    BackendType backendType() const override { return BackendType::Apt; }
    QString backendId() const override { return QStringLiteral("fake"); }
    QString backendName() const override { return QStringLiteral("Fake"); }
    bool isAvailable() const override { return true; }
    bool supportsResidualConfig() const override { return true; }
    QVariantMap backendOptions() const override { return {}; }

    bool fetchUpgradable(PackageList& out, QString&) override
    {
        out = m_upgradable;
        return m_fetchOk;
    }
    bool listInstalled(PackageList&, const QString&, QString&) override { return true; }
    bool simulateInstall(const QString&, QString&, QString&) override { return true; }
    bool listResidualPackages(PackageList&, QString&) override { return true; }
    QStringList cacheDirectories() const override { return {}; }

    // 假后端不触发任何真实系统探测（避免宿主有 failed units / 需重启服务时
    // 干扰"无前检确认即直装"类断言），让测试确定性通过。
    bool checkRebootRequired(bool& required, QString&) override
    {
        required = false;
        return false;
    }
    bool checkServicesNeedingRestart(QStringList& services, QString&) override
    {
        services.clear();
        return false;
    }
    bool checkConfigFilesToReview(QStringList& paths, QString&) override
    {
        paths.clear();
        return false;
    }
    bool checkFailedUnits(QStringList& units, QString&) override
    {
        units.clear();
        return false;
    }

    bool install(const QStringList& packages, QString&) override
    {
        m_installed = packages;
        emit operationFinished(true, QStringLiteral("ok"));
        return true;
    }
    bool upgrade(const QStringList& packages, QString&) override
    {
        m_installed = packages;
        emit operationFinished(true, QStringLiteral("ok"));
        return true;
    }
    bool remove(const QStringList&, QString&) override { return true; }
    bool purge(const QStringList&, QString&) override { return true; }
    bool autoremove(QString&) override { return true; }
    bool cleanCache(QString&) override { return true; }
    QStringList privilegedPrefix() const override { return {}; }

    PackageList m_upgradable;
    bool m_fetchOk = true;
    QStringList m_installed;
};

// 伪配置：固定间隔，避免依赖 DConfig 实现
class FakeConfig : public AppConfig
{
  public:
    explicit FakeConfig(QObject* p = nullptr) : AppConfig(p) {}
};

TEST(UpdateMonitorTest, CheckNowSetsHasUpdates)
{
    MonitorFakeBackend backend;
    PackageInfo pi;
    pi.name = QStringLiteral("foo");
    pi.isUpgradable = true;
    backend.m_upgradable = {pi};

    FakeConfig config;
    UpdateMonitor monitor(&backend, &config);
    QSignalSpy spyState(&monitor, &UpdateMonitor::stateChanged);
    QSignalSpy spyAvail(&monitor, &UpdateMonitor::updatesAvailable);

    monitor.checkNow();
    EXPECT_EQ(monitor.state(), UpdateMonitor::State::HasUpdates);
    EXPECT_EQ(monitor.upgradable().size(), 1);
    EXPECT_EQ(spyAvail.count(), 1);
}

TEST(UpdateMonitorTest, CheckNowNoUpdatesIdle)
{
    MonitorFakeBackend backend; // 空列表
    FakeConfig config;
    UpdateMonitor monitor(&backend, &config);
    monitor.checkNow();
    EXPECT_EQ(monitor.state(), UpdateMonitor::State::Idle);
}

TEST(UpdateMonitorTest, CheckNowFailureSetsError)
{
    MonitorFakeBackend backend;
    backend.m_fetchOk = false;
    FakeConfig config;
    UpdateMonitor monitor(&backend, &config);
    QSignalSpy spyFail(&monitor, &UpdateMonitor::checkFailed);
    monitor.checkNow();
    EXPECT_EQ(monitor.state(), UpdateMonitor::State::Error);
    EXPECT_EQ(spyFail.count(), 1);
}

TEST(UpdateMonitorTest, ApplyUpdatesEmitsSecurityPromptThenProceed)
{
    ensureApp();
    MonitorFakeBackend backend;
    PackageInfo pi;
    pi.name = QStringLiteral("systemd");
    pi.isUpgradable = true; // critical 组件
    backend.m_upgradable = {pi};

    FakeConfig config;
    SecurityAdvisor advisor;
    UpdateMonitor monitor(&backend, &config);
    monitor.setSecurityAdvisor(&advisor);

    // 真实流程：checkNow 同步填充 m_upgradable（升级目标来自已发现的可升级包）
    QSignalSpy spyAvail(&monitor, &UpdateMonitor::updatesAvailable);
    monitor.checkNow();
    ASSERT_EQ(spyAvail.count(), 1);
    ASSERT_EQ(monitor.state(), UpdateMonitor::State::HasUpdates);

    QSignalSpy spyPrompt(&monitor, &UpdateMonitor::securityPrompt);
    QSignalSpy spyFinished(&monitor, &UpdateMonitor::upgradeFinished);

    monitor.applyUpdates();
    // 有 advisor 时应触发安全提示并暂停，不立即安装
    EXPECT_EQ(spyPrompt.count(), 1);
    EXPECT_TRUE(backend.m_installed.isEmpty());

    // 用户确认后继续
    monitor.proceedUpdate();
    EXPECT_TRUE(backend.m_installed.contains(QStringLiteral("systemd")));
    EXPECT_EQ(spyFinished.count(), 1);
}

TEST(UpdateMonitorTest, ApplyUpdatesWithoutAdvisorInstallsDirectly)
{
    ensureApp();
    MonitorFakeBackend backend;
    PackageInfo pi;
    pi.name = QStringLiteral("firefox");
    pi.isUpgradable = true;
    backend.m_upgradable = {pi};

    FakeConfig config;
    UpdateMonitor monitor(&backend, &config); // 无 advisor

    QSignalSpy spyAvail(&monitor, &UpdateMonitor::updatesAvailable);
    monitor.checkNow();
    ASSERT_EQ(spyAvail.count(), 1);
    ASSERT_EQ(monitor.state(), UpdateMonitor::State::HasUpdates);

    QSignalSpy spyPrompt(&monitor, &UpdateMonitor::securityPrompt);
    monitor.applyUpdates();
    // 无 advisor 时直接安装，不发安全提示
    EXPECT_EQ(spyPrompt.count(), 0);
    EXPECT_TRUE(backend.m_installed.contains(QStringLiteral("firefox")));
}

TEST(UpdateMonitorTest, ApplyUpdatesEmptyNoOp)
{
    MonitorFakeBackend backend; // 无可升级
    FakeConfig config;
    UpdateMonitor monitor(&backend, &config);
    monitor.applyUpdates();
    EXPECT_TRUE(backend.m_installed.isEmpty());
}

// 多后端聚合：系统后端 + 跨发行系玲珑(linyaps) 后端。
// checkNow 应把两者可升级项合并进 m_upgradable，且每项带正确的 backendId，
// 供 UI 区分展示；proceedUpdate 应按 backendId 分流到对应后端安装。
class MonitorFakeLinyaps : public PackageBackend
{
    Q_OBJECT
  public:
    explicit MonitorFakeLinyaps(QObject* parent = nullptr) : PackageBackend(parent) {}
    BackendType backendType() const override { return BackendType::Linyaps; }
    QString backendId() const override { return QStringLiteral("linyaps"); }
    QString backendName() const override { return QStringLiteral("Linyaps"); }
    bool isAvailable() const override { return true; }
    bool supportsResidualConfig() const override { return false; }
    QVariantMap backendOptions() const override { return {}; }

    bool fetchUpgradable(PackageList& out, QString&) override
    {
        out = m_upgradable;
        return true;
    }
    bool listInstalled(PackageList&, const QString&, QString&) override { return true; }
    bool simulateInstall(const QString&, QString&, QString&) override { return true; }
    bool listResidualPackages(PackageList&, QString&) override { return true; }
    QStringList cacheDirectories() const override { return {}; }

    // 假后端不触发任何真实系统探测（避免宿主有 failed units / 需重启服务时
    // 干扰"无前检确认即直装"类断言），让测试确定性通过。
    bool checkRebootRequired(bool& required, QString&) override
    {
        required = false;
        return false;
    }
    bool checkServicesNeedingRestart(QStringList& services, QString&) override
    {
        services.clear();
        return false;
    }
    bool checkConfigFilesToReview(QStringList& paths, QString&) override
    {
        paths.clear();
        return false;
    }
    bool checkFailedUnits(QStringList& units, QString&) override
    {
        units.clear();
        return false;
    }
    bool install(const QStringList& packages, QString&) override
    {
        m_installed = packages;
        emit operationFinished(true, QStringLiteral("ok"));
        return true;
    }
    bool upgrade(const QStringList& packages, QString&) override
    {
        m_installed = packages;
        emit operationFinished(true, QStringLiteral("ok"));
        return true;
    }
    bool remove(const QStringList&, QString&) override { return true; }
    bool purge(const QStringList&, QString&) override { return true; }
    bool autoremove(QString&) override { return true; }
    bool cleanCache(QString&) override { return true; }
    QStringList privilegedPrefix() const override { return {}; }

    PackageList m_upgradable;
    QStringList m_installed;
};

TEST(UpdateMonitorTest, MultiBackendAggregatesAndRoutes)
{
    ensureApp();
    MonitorFakeBackend sysBackend;
    PackageInfo sysPkg;
    sysPkg.name = QStringLiteral("systemd");
    sysPkg.isUpgradable = true;
    sysBackend.m_upgradable = {sysPkg};

    MonitorFakeLinyaps llBackend;
    PackageInfo llPkg;
    llPkg.name = QStringLiteral("org.deepin.demo");
    llPkg.isUpgradable = true;
    llPkg.backendId = QStringLiteral("linyaps"); // 真实 fetchUpgradable 会标记来源后端
    llBackend.m_upgradable = {llPkg};

    FakeConfig config;
    UpdateMonitor monitor(&sysBackend, &config);
    monitor.setLinyapsBackend(&llBackend);

    QSignalSpy spyAvail(&monitor, &UpdateMonitor::updatesAvailable);
    monitor.checkNow();
    ASSERT_EQ(spyAvail.count(), 1);
    // 两项可升级包被合并（系统 + 玲珑）
    ASSERT_EQ(monitor.upgradable().size(), 2);
    bool hasSys = false, hasLl = false;
    for (const auto& p : monitor.upgradable())
    {
        if (p.name == QStringLiteral("systemd") && p.backendId.isEmpty())
            hasSys = true;
        if (p.name == QStringLiteral("org.deepin.demo") && p.backendId == QStringLiteral("linyaps"))
            hasLl = true;
    }
    EXPECT_TRUE(hasSys);
    EXPECT_TRUE(hasLl);

    // 升级时按 backendId 分流：系统包交给系统后端，玲珑包交给玲珑后端
    monitor.applyUpdates(); // 无 advisor，直接安装
    EXPECT_TRUE(sysBackend.m_installed.contains(QStringLiteral("systemd")));
    EXPECT_TRUE(llBackend.m_installed.contains(QStringLiteral("org.deepin.demo")));
    EXPECT_FALSE(sysBackend.m_installed.contains(QStringLiteral("org.deepin.demo")));
    EXPECT_FALSE(llBackend.m_installed.contains(QStringLiteral("systemd")));
}

// 验证 commit 6f56292 的聚合兜底：沙箱后端 fetchUpgradable 漏填 backendId 时，
// checkNow 聚合期必须强制回填 sb->backendId()，否则 proceedUpdate 按 p.backendId 路由
// 会误把沙箱包归入系统后端 sysPkgs（AGENTS.md「沙箱后端按 backendId 分组路由」坑）。
class MonitorFakeLinyapsMissingId : public MonitorFakeLinyaps
{
  public:
    explicit MonitorFakeLinyapsMissingId(QObject* parent = nullptr) : MonitorFakeLinyaps(parent) {}
    bool fetchUpgradable(PackageList& out, QString&) override
    {
        // 故意不清空上游返回的 backendId 字段，模拟漏填来源后端
        PackageList raw = m_upgradable;
        for (PackageInfo& p : raw)
            p.backendId.clear();
        out = raw;
        return true;
    }
};

TEST(UpdateMonitorTest, BackendIdBackfilledWhenMissing)
{
    ensureApp();
    MonitorFakeBackend sysBackend;
    PackageInfo sysPkg;
    sysPkg.name = QStringLiteral("systemd");
    sysPkg.isUpgradable = true;
    sysBackend.m_upgradable = {sysPkg};

    MonitorFakeLinyapsMissingId llBackend;
    PackageInfo llPkg;
    llPkg.name = QStringLiteral("org.deepin.demo");
    llPkg.isUpgradable = true;
    // 注意：此处不预设 backendId，交由 MonitorFakeLinyapsMissingId 强制清空，
    // 以验证聚合兜底路径（非依赖后端自觉）。
    llBackend.m_upgradable = {llPkg};

    FakeConfig config;
    UpdateMonitor monitor(&sysBackend, &config);
    monitor.setLinyapsBackend(&llBackend);

    QSignalSpy spyAvail(&monitor, &UpdateMonitor::updatesAvailable);
    monitor.checkNow();
    ASSERT_EQ(spyAvail.count(), 1);
    ASSERT_EQ(monitor.upgradable().size(), 2);

    // 聚合后沙箱包 backendId 必须被兜底为 "linyaps"，而非空（空则路由失败）
    bool backfilled = false;
    for (const auto& p : monitor.upgradable())
        if (p.name == QStringLiteral("org.deepin.demo") && p.backendId == QStringLiteral("linyaps"))
            backfilled = true;
    EXPECT_TRUE(backfilled) << "聚合期未兜底回填沙箱包 backendId（commit 6f56292 回归）";

    // 路由正确性：沙箱包仍交由玲珑后端安装，不污染系统后端
    monitor.applyUpdates();
    EXPECT_TRUE(llBackend.m_installed.contains(QStringLiteral("org.deepin.demo")));
    EXPECT_FALSE(sysBackend.m_installed.contains(QStringLiteral("org.deepin.demo")));
}

#include "test_updatemonitor.moc"
