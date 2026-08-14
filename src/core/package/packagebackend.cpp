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

    QStringList PackageBackend::operationArgs(Op, const QStringList&, QString&)
    {
        return {}; // 默认不支持任何写操作
    }

    bool PackageBackend::install(const QStringList& packages, QString& error)
    {
        return runWriteOperation(Op::Install, packages, error);
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
            return true; // 无目标包，视为已完成
        // 进度文案按操作语义映射（与历史行为一致）
        static const QHash<Op, QString> stage = {
            {Op::Install, tr("Installing")},
            {Op::Remove, tr("Removing")},
            {Op::Purge, tr("Purging")},
            {Op::Autoremove, tr("Removing orphans")},
            {Op::CleanCache, tr("Cleaning cache")},
        };
        emit operationProgress(stage.value(op, tr("Working")), 0);
        const QStringList args = operationArgs(op, packages, error);
        if (args.isEmpty())
            return true; // 后端不支持该操作（如 linyaps 未覆盖）
        // 后台线程执行，避免阻塞 UI/tray 主线程；结果经 operationFinished 回传。
        runPrivilegedAsync([this, args](QString& out, QString& err)
                           { return runPrivileged(args, out, err); });
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
