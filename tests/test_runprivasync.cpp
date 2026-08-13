#include <gtest/gtest.h>

#include "core/package/packagebackend.h"
#include "core/package/aptbackend.h"

#include <QSignalSpy>
#include <QCoreApplication>
#include <QtConcurrent>

#include <chrono>
#include <thread>

using namespace DtkUpdate;

// gtest 主函数不含 Qt 事件循环；QFutureWatcher 的跨线程 finished 信号需要
// QCoreApplication 实例才能分发。首次调用时构造单例 QCoreApplication。
// 定义为普通函数（非 static），供多个测试 TU 共享同一进程级单例。
void ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test-runprivasync";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
    Q_UNUSED(app);
}

// runPrivilegedAsync 是 PackageBackend 的 protected 方法；子类化暴露以便测试。
class AsyncHarness : public AptBackend {
    Q_OBJECT
public:
    using AptBackend::AptBackend;
    void runAsync(std::function<bool(QString &, QString &)> task)
    {
        runPrivilegedAsync(std::move(task));
    }
};

// 回归：写操作必须异步执行，不能同步阻塞调用线程（否则 GUI/tray 主线程冻结数分钟）。
// 模拟一个耗时任务，验证调用 runAsync 后立即返回（不等待任务完成）。
TEST(RunPrivilegedAsyncTest, DoesNotBlockCaller)
{
    ensureApp();
    AsyncHarness h;
    // 任务睡眠 200ms 模拟升级耗时
    bool ran = false;
    auto start = std::chrono::steady_clock::now();
    h.runAsync([&ran](QString &, QString &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        ran = true;
        return true;
    });
    auto elapsed = std::chrono::steady_clock::now() - start;
    // 若同步阻塞，elapsed 应 >= 200ms；异步则远小于此。
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 100)
        << "runPrivilegedAsync must return immediately (caller thread not blocked)";
    EXPECT_FALSE(ran) << "task must not have run synchronously within the call";

    // 等待异步任务完成（不依赖真实 apt 命令）
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    EXPECT_TRUE(ran);
}

// 回归：异步任务的结果必须经由 operationFinished 信号回传主线程。
TEST(RunPrivilegedAsyncTest, ResultDeliveredViaSignal)
{
    ensureApp();
    AsyncHarness h;
    QSignalSpy spy(&h, &PackageBackend::operationFinished);
    h.runAsync([](QString &out, QString &) {
        out = QStringLiteral("done-123");
        return true;
    });
    ASSERT_TRUE(spy.wait(2000));
    ASSERT_EQ(spy.count(), 1);
    // 参数：(bool success, QString detail)
    EXPECT_TRUE(spy.first().at(0).toBool());
    EXPECT_TRUE(spy.first().at(1).toString().contains(QStringLiteral("done-123")));
}

// 回归：失败时 success=false 仍经信号正确送达。
TEST(RunPrivilegedAsyncTest, FailureDeliveredViaSignal)
{
    ensureApp();
    AsyncHarness h;
    QSignalSpy spy(&h, &PackageBackend::operationFinished);
    h.runAsync([](QString &out, QString &) {
        out = QStringLiteral("boom");
        return false;
    });
    ASSERT_TRUE(spy.wait(2000));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.first().at(0).toBool());
    EXPECT_TRUE(spy.first().at(1).toString().contains(QStringLiteral("boom")));
}

#include "test_runprivasync.moc"
