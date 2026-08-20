#include "packagebackend.h"

#include <QDir>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QtConcurrent>
#include <functional>
#include <utility>

#include "common/appconfig.h"

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

    // 纯执行体（static 成员函数，无 this 依赖）：在调用线程同步取 prefix 后，可安全在后台线程执行，
    // 避免 runWriteOperation 的 lambda 捕获 this 导致对象析构后异步访问悬空指针（segfault）。
    bool PackageBackend::runPrivilegedExec(const QStringList& prefix, const QStringList& args,
                                           QString& output, int timeoutMs, bool* cancelled)
    {
        QStringList full = prefix;
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
            output = QStringLiteral("failed to start privileged command: ") + full.join(QChar(' '));
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

    bool PackageBackend::runPrivileged(const QStringList& args, QString& output, int timeoutMs,
                                       bool* cancelled) const
    {
        return runPrivilegedExec(privilegedPrefix(), args, output, timeoutMs, cancelled);
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
            std::function<void(const QDir&, int)> walk = [&](const QDir& d, int depth)
            {
                if (depth > maxDepth)
                    return;
                for (const QFileInfo& fi : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot))
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

    QStringList PackageBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        // 未覆盖 Upgrade 的子系统后端（apt/dnf/pacman/zypper）install 含 upgrade 语义，
        // 回落到子类实现的 Install 分支；沙箱后端（snap/flatpak/linyaps）应在各自
        // operationArgs 的 switch 中显式覆盖 Op::Upgrade 为 refresh/update/upgrade。
        if (op == Op::Upgrade)
            return operationArgs(Op::Install, packages, error);
        return {}; // 默认不支持其它写操作
    }

    bool PackageBackend::install(const QStringList& packages, QString& error)
    {
        return runWriteOperation(Op::Install, packages, error);
    }

    bool PackageBackend::upgrade(const QStringList& packages, QString& error)
    {
        return runWriteOperation(Op::Upgrade, packages, error);
    }

    bool PackageBackend::remove(const QStringList& packages, QString& error)
    {
        return runWriteOperation(Op::Remove, packages, error);
    }

    bool PackageBackend::purge(const QStringList& packages, QString& error)
    {
        return runWriteOperation(Op::Purge, packages, error);
    }

    bool PackageBackend::autoremove(QString& error)
    {
        return runWriteOperation(Op::Autoremove, {}, error);
    }

    bool PackageBackend::cleanCache(QString& error)
    {
        return runWriteOperation(Op::CleanCache, {}, error);
    }

    bool PackageBackend::runWriteOperation(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        if (op != Op::Autoremove && op != Op::CleanCache && packages.isEmpty())
        {
            // 无目标包，视为已完成——仍回传操作结果（契约必达），避免调用方只收进度
            // 而无回调而卡住（与下方 args 为空的 skipped 回传同一约束）。
            emit operationProgress(tr("Working"), 100);
            emit operationFinished(true, QStringLiteral("no packages to operate, skipped"));
            return true;
        }
        // 进度文案按操作语义映射（与历史行为一致）
        static const QHash<Op, QString> stage = {
            {Op::Install, tr("Installing")},
            {Op::Upgrade, tr("Upgrading")},
            {Op::Remove, tr("Removing")},
            {Op::Purge, tr("Purging")},
            {Op::Autoremove, tr("Removing orphans")},
            {Op::CleanCache, tr("Cleaning cache")},
        };
        emit operationProgress(stage.value(op, tr("Working")), 0);
        const QStringList args = operationArgs(op, packages, error);
        if (args.isEmpty())
        {
            // 后端不支持该操作：以"成功且无事可做"回传，维持 operationFinished 必达契约，
            // 避免调用方（如 monitor 升级后清理）只收到进度而无结果回调而卡住。
            emit operationProgress(stage.value(op, tr("Working")), 100);
            emit operationFinished(true, QStringLiteral("operation not supported by backend, "
                                                        "skipped"));
            return true;
        }
        // 后台线程执行，避免阻塞 UI/tray 主线程；结果经 operationFinished 回传。
        // 同步取出 prefix（此时 this 有效），异步任务仅依赖值捕获，避免对象析构后 this
        // 悬空（segfault）。
        const QStringList prefix = privilegedPrefix();
        runPrivilegedAsync([prefix, args](QString& out, QString&)
                           { return runPrivilegedExec(prefix, args, out, 600000, nullptr); });
        return true; // 已启动异步任务
    }

    QStringList PackageBackend::parseServiceList(const QString& raw)
    {
        QStringList services;
        QString copy = raw;
        QTextStream stream(&copy);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString s = line.trimmed();
            if (s.isEmpty())
                continue;
            // 输出形如 "systemd-manager" 或 "systemd-manager.service"；统一去 .service 后缀
            services.append(s.endsWith(QStringLiteral(".service")) ? s.chopped(8) : s);
        }
        return services;
    }

    QStringList PackageBackend::parseFailedUnits(const QString& raw)
    {
        QStringList units;
        QString copy = raw;
        QTextStream stream(&copy);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString s = line.trimmed();
            if (s.isEmpty())
                continue;
            // 形如 "foo.service  loaded  failed  failed  Foo description"
            const QString unit = s.split(QStringLiteral(" "), Qt::SkipEmptyParts).first();
            if (!unit.isEmpty())
                units.append(unit);
        }
        return units;
    }

    QVariantMap PackageBackend::defaultBackendOptions() const
    {
        QVariantMap opts;
        if (m_config)
        {
            opts.insert(QStringLiteral("noInstallRecommends"), m_config->noInstallRecommends());
            opts.insert(QStringLiteral("autoRemoveOrphans"), m_config->autoRemoveOrphans());
            opts.insert(QStringLiteral("autoCleanCache"), m_config->autoCleanCache());
        }
        return opts;
    }

    QVariantMap PackageBackend::backendOptions() const
    {
        return defaultBackendOptions();
    }

} // namespace DtkUpdate
