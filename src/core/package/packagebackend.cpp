#include "packagebackend.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <utility>

namespace DtkUpdate {

/**
 * @brief 在后台线程执行耗时的提权写任务，完成后经 operationFinished 信号回传主线程。
 *
 * 写操作（install/remove/clean 等）原本同步调用 runPrivileged 的
 * QProcess::waitForFinished(-1)，会在 UI/tray 主线程阻塞数分钟，冻结整个桌面。
 * 改为 QtConcurrent 后台线程执行：install/remove/... 立即返回表示"已启动"，
 * 真正的成功/失败通过 operationFinished 信号异步送达主线程，由 UpdateMonitor
 * 统一处理（释放并发锁、刷新状态、后检）。
 */
void PackageBackend::runPrivilegedAsync(std::function<bool(QString &, QString &)> task)
{
    auto *watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
            [this, watcher]() {
                const auto res = watcher->result();
                watcher->deleteLater();
                emit operationFinished(res.first, res.second);
            });
    watcher->setFuture(QtConcurrent::run([task = std::move(task)]() {
        QString out;
        QString err;
        const bool ok = task(out, err);
        Q_UNUSED(err);
        return std::make_pair(ok, out);
    }));
}

}  // namespace DtkUpdate
