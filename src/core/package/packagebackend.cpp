#include "packagebackend.h"

#include <QDir>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent>
#include <functional>
#include <utility>

namespace DtkUpdate
{

    bool PackageBackend::runProbe(const QStringList& args, QString& output, int& exitCode) const
    {
        if (args.isEmpty() || !commandExists(args.first()))
        {
            exitCode = -1;
            return false;
        }
        QProcess p;
        applyStableLocale(p);
        p.start(args.first(), args.mid(1));
        if (!p.waitForStarted(5000))
        {
            exitCode = -1;
            return false;
        }
        p.waitForFinished(-1);
        exitCode = p.exitCode();
        output = QString::fromLocal8Bit(p.readAllStandardOutput());
        return true;
    }

    bool PackageBackend::runQuery(const QString& command, const QStringList& args, QString& output,
                                  QString& error, int timeoutMs) const
    {
        if (!commandExists(command))
        {
            error = QStringLiteral("command not found: ") + command;
            return false;
        }
        QProcess p;
        applyStableLocale(p);
        p.start(command, args);
        if (!p.waitForStarted(5000))
        {
            error = QStringLiteral("failed to start: ") + command;
            return false;
        }
        if (!p.waitForFinished(timeoutMs))
        {
            error = QStringLiteral("timeout: ") + command;
            return false;
        }
        if (p.exitCode() != 0)
        {
            error = QString::fromLocal8Bit(p.readAllStandardError());
            return false;
        }
        output = QString::fromLocal8Bit(p.readAllStandardOutput());
        return true;
    }

    bool PackageBackend::commandExists(const QString& command)
    {
        return !QStandardPaths::findExecutable(command).isEmpty();
    }

    bool PackageBackend::runPrivileged(const QStringList& args, QString& output, int timeoutMs,
                                       bool* cancelled) const
    {
        QStringList full = privilegedPrefix();
        full.append(args);
        if (full.size() < 2)
        {
            output = QStringLiteral("incomplete privileged command");
            return false;
        }
        QProcess p;
        applyStableLocale(p);
        p.start(full.first(), full.mid(1));
        if (!p.waitForStarted(5000))
        {
            output =
                QStringLiteral("failed to start privileged command: ") + full.join(QChar(' '));
            return false;
        }
        // 取消竞态：外部置 *cancelled 时立即终止进程
        if (cancelled && *cancelled)
        {
            p.kill();
            p.waitForFinished(3000);
            output = QStringLiteral("cancelled");
            return false;
        }
        p.waitForFinished(timeoutMs);
        if (p.state() != QProcess::NotRunning)
        {
            p.kill();
            output = QStringLiteral("timeout");
            return false;
        }
        output = QString::fromLocal8Bit(p.readAllStandardOutput()) +
                 QString::fromLocal8Bit(p.readAllStandardError());
        return p.exitCode() == 0;
    }

    QStringList PackageBackend::collectConfigFiles(const QStringList& dirs,
                                                   const QStringList& suffixes, int maxDepth)
    {
        QStringList result;
        for (const QString& dir : dirs)
        {
            QDir base(dir);
            if (!base.exists())
                continue;
            std::function<void(const QDir&, int)> walk = [&](const QDir& d, int depth) {
                if (depth > maxDepth)
                    return;
                for (const QFileInfo& fi :
                     d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot))
                {
                    if (fi.isDir())
                    {
                        walk(QDir(fi.filePath()), depth + 1);
                    }
                    else
                    {
                        const QString name = fi.fileName();
                        for (const QString& suf : suffixes)
                            if (name.endsWith(suf))
                            {
                                result.append(fi.filePath());
                                break;
                            }
                    }
                }
            };
            walk(base, 0);
        }
        result.sort();
        return result;
    }

    /**
     * @brief 在后台线程执行耗时的提权写任务，完成后经 operationFinished 信号回传主线程。
     *
     * 写操作（install/remove/clean 等）原本同步调用 runPrivileged 的
     * QProcess::waitForFinished(-1)，会在 UI/tray 主线程阻塞数分钟，冻结整个桌面。
     * 改为 QtConcurrent 后台线程执行：install/remove/... 立即返回表示"已启动"，
     * 真正的成功/失败通过 operationFinished 信号异步送达主线程，由 UpdateMonitor
     * 统一处理（释放并发锁、刷新状态、后检）。
     */
    void PackageBackend::runPrivilegedAsync(std::function<bool(QString&, QString&)> task)
    {
        auto* watcher = new QFutureWatcher<std::pair<bool, QString>>(this);
        connect(watcher, &QFutureWatcher<std::pair<bool, QString>>::finished, this,
                [this, watcher]()
                {
                    const auto res = watcher->result();
                    watcher->deleteLater();
                    emit operationFinished(res.first, res.second);
                });
        watcher->setFuture(QtConcurrent::run(
            [task = std::move(task)]()
            {
                QString out;
                QString err;
                const bool ok = task(out, err);
                Q_UNUSED(err);
                return std::make_pair(ok, out);
            }));
    }

} // namespace DtkUpdate
