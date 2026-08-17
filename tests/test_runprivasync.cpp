#include <QCoreApplication>
#include <QSignalSpy>
#include <QThreadPool>
#include <QtConcurrent>
#include <chrono>
#include <thread>

#include "core/package/aptbackend.h"
#include "core/package/packagebackend.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

// 进程级唯一的 QCoreApplication 实例（Qt 要求全局仅一个）。它必须由自定义 main()
// 在 RUN_ALL_TESTS() 之前构造、之后按受控顺序析构，绝不能当作「静态全局对象」让
// 编译器在 main 返回后随其他全局静态一起无序析构——否则 Qt 全局静态（尤其是
// QtConcurrent 使用的全局 QThreadPool）与 app 析构顺序错位，会在 teardown 阶段
// 触发 use-after-free 段错误（CI 容器必现、本地偶发）。
QCoreApplication* g_appInstance = nullptr;

// 暴露为普通函数（非 static），供多个测试 TU 共享同一进程级单例。app 由 main() 最早
// 构造，此函数仅作存在性保证（避免个别用例在 app 之前访问 Qt 事件循环）。
void ensureApp()
{
    // g_appInstance 由 main() 构造；若因特殊链接顺序尚未构造，此处惰性兜底构造。
    if (!g_appInstance)
    {
        static int argc = 1;
        static char arg0[] = "test-core";
        static char* argv[] = {arg0, nullptr};
        g_appInstance = new QCoreApplication(argc, argv);
    }
}

// runPrivilegedAsync 是 PackageBackend 的 protected 方法；子类化暴露以便测试。
class AsyncHarness : public AptBackend
{
    Q_OBJECT
  public:
    using AptBackend::AptBackend;
    void runAsync(std::function<bool(QString&, QString&)> task)
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
    h.runAsync(
        [&ran](QString&, QString&)
        {
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
    h.runAsync(
        [](QString& out, QString&)
        {
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
    h.runAsync(
        [](QString& out, QString&)
        {
            out = QStringLiteral("boom");
            return false;
        });
    ASSERT_TRUE(spy.wait(2000));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_FALSE(spy.first().at(0).toBool());
    EXPECT_TRUE(spy.first().at(1).toString().contains(QStringLiteral("boom")));
}

#include "test_runprivasync.moc"

// 自定义 main()：替代 gtest_main，以受控顺序管理 QCoreApplication 生命周期。
// 关键点：RUN_ALL_TESTS() 返回后，先耗尽全局线程池（QtConcurrent 后台任务可能仍在跑）、
// 再处理遗留事件、最后才 delete QCoreApplication。这样 Qt 全局静态对象（含全局
// QThreadPool）始终在 app 存活期间存在，避免 teardown 期 use-after-free 段错误。
int main(int argc, char** argv)
{
    // 初始化 gtest（必须在使用 RUN_ALL_TESTS 前调用，否则新版 gtest 会强制报错）。
    testing::InitGoogleTest(&argc, argv);

    // 构造唯一的 QCoreApplication（进程级单例，供所有 TU 的 ensureApp() 引用）。
    QCoreApplication app(argc, argv);
    g_appInstance = &app;

    const int result = RUN_ALL_TESTS();

    // 受控 teardown：先等全局线程池里的 QtConcurrent 任务全部结束，
    // 再排空事件队列，最后 app 随 main 栈帧自然析构（晚于所有全局静态）。
    if (QThreadPool::globalInstance())
        QThreadPool::globalInstance()->waitForDone();
    app.processEvents();
    app.sendPostedEvents(nullptr, 0);

    g_appInstance = nullptr;
    return result;
}
