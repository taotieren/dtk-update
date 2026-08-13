#include "aptbackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QtConcurrent>

#include "common/appconfig.h"
#include "common/systeminfo.h"
#include "logger.h"
#include "packageparser.h"

namespace DtkUpdate
{

    AptBackend::AptBackend(QObject* parent) : PackageBackend(parent) {}

    bool AptBackend::commandExists(const QString& cmd)
    {
        return !QStandardPaths::findExecutable(cmd).isEmpty();
    }

    bool AptBackend::isAvailable() const
    {
        // 探测 apt 系关键命令是否齐全。部分环境（如 Arch 上的 tinyget 仿真）
        // 仅提供 `apt` 占位脚本，但 apt-get / dpkg-query 缺失，此时必须判为
        // 不可用，否则运行时会虚假可用、操作后崩溃。
        static const QStringList required = {
            QStringLiteral("apt"),
            QStringLiteral("apt-get"),
            QStringLiteral("dpkg-query"),
        };
        for (const QString& cmd : required)
        {
            if (!commandExists(cmd))
                return false;
        }
        // 关键命令齐全后，再做一次轻量冒烟：能真正执行 `apt list --upgradable`
        // 才视为可用，避免命令存在却运行即崩溃（如损坏的 apt 封装）。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("list"), QStringLiteral("--upgradable")}, out,
                      err))
            return false;
        return true;
    }

    bool AptBackend::runQuery(const QStringList& args, QString& output, QString& error) const
    {
        QProcess proc;
        applyStableLocale(proc);
        proc.start(args.first(), args.mid(1));
        if (!proc.waitForStarted(8000))
        {
            error = proc.errorString();
            return false;
        }
        proc.waitForFinished(-1);
        output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
        {
            error = QString::fromLocal8Bit(proc.readAllStandardError());
            return false;
        }
        return true;
    }

    bool AptBackend::runPrivileged(const QStringList& aptArgs, QString& output,
                                   QString& error) const
    {
        QStringList args{QStringLiteral("pkexec"), QStringLiteral("apt-get")};
        args.append(aptArgs);
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(QStringLiteral("pkexec"), args);
        if (!proc.waitForStarted(8000))
        {
            error = proc.errorString();
            return false;
        }
        proc.waitForFinished(-1);
        output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        const int code = proc.exitCode();
        if (proc.exitStatus() != QProcess::NormalExit || code != 0)
        {
            error = output;
            return false;
        }
        return true;
    }

    bool AptBackend::runProbe(const QStringList& args, QString& output, int& exitCode) const
    {
        exitCode = -1;
        QProcess proc;
        applyStableLocale(proc);
        proc.start(args.first(), args.mid(1));
        if (!proc.waitForStarted(8000))
        {
            exitCode = -1;
            return false;
        }
        proc.waitForFinished(-1);
        output = QString::fromLocal8Bit(proc.readAllStandardOutput());
        if (proc.exitStatus() == QProcess::NormalExit)
            exitCode = proc.exitCode();
        else
            exitCode = -1;
        return true; // 进程已正常结束（无论退出码），交由调用方解释语义
    }

    bool AptBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // apt list --upgradable 输出形如：
        //   firefox/stable 120.0.1 amd64 [upgradable from: 119.0]
        QString raw;
        if (!runQuery(
                {QStringLiteral("apt"), QStringLiteral("list"), QStringLiteral("--upgradable")},
                raw, error))
        {
            return false;
        }

        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            PackageInfo info;
            if (PackageParser::parseUpgradableLine(line, info))
                out.append(info);
        }
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable packages";
        return true;
    }

    bool AptBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        // dpkg-query 自定义格式，逐字段以 \t 分隔
        const QString format = QStringLiteral("${Package}\t${Version}\t${Architecture}\t"
                                              "${db:Status-Abbrev}\t${Section}\n");
        QString raw;
        if (!runQuery(
                {QStringLiteral("dpkg-query"), QStringLiteral("-W"), QStringLiteral("-f"), format},
                raw, error))
        {
            return false;
        }

        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            PackageInfo info;
            if (!PackageParser::parseDpkgLine(line, info))
                continue;
            if (!filter.isEmpty() && !info.name.contains(filter, Qt::CaseInsensitive))
                continue;
            out.append(info);
        }
        return true;
    }

    bool AptBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // 干跑解析依赖（不实际安装）
        return runQuery(
            {QStringLiteral("apt-get"), QStringLiteral("install"), QStringLiteral("-s"), pkg},
            resolution, error);
    }

    bool AptBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // 列出处于 "rc"（残留配置）状态的包：dpkg-query -l | grep '^rc'
        const QString format = QStringLiteral("${db:Status-Abbrev}\t${Package}\t"
                                              "${Version}\t${Architecture}\t${Section}\n");
        QString raw;
        if (!runQuery(
                {QStringLiteral("dpkg-query"), QStringLiteral("-W"), QStringLiteral("-f"), format},
                raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() < 2 || parts.at(0) != QStringLiteral("rc"))
                continue;
            PackageInfo info;
            info.name = parts.at(1);
            if (parts.size() > 2)
                info.currentVersion = parts.at(2);
            if (parts.size() > 3)
                info.architecture = parts.at(3);
            if (parts.size() > 4)
                info.section = parts.at(4);
            out.append(info);
        }
        return true;
    }

    bool AptBackend::install(const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        if (packages.isEmpty())
            return true;
        emit operationProgress(tr("Installing"), 0);
        QStringList args;
        args << QStringLiteral("install") << QStringLiteral("-y");
        // 读取全局配置：是否跳过可选依赖
        const bool noRec = m_config ? m_config->noInstallRecommends() : true;
        if (noRec)
            args << QStringLiteral("--no-install-recommends");
        args << packages;
        // 后台线程执行（避免阻塞 GUI/tray 主线程）；结果经 operationFinished 回传。
        runPrivilegedAsync([this, args](QString& out, QString& err)
                           { return runPrivileged(args, out, err); });
        return true; // 已启动异步任务
    }

    QVariantMap AptBackend::backendOptions() const
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

    bool AptBackend::remove(const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        if (packages.isEmpty())
            return true;
        QStringList args;
        args << QStringLiteral("remove") << QStringLiteral("-y") << packages;
        runPrivilegedAsync([this, args](QString& out, QString& err)
                           { return runPrivileged(args, out, err); });
        return true;
    }

    bool AptBackend::purge(const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        if (packages.isEmpty())
            return true;
        QStringList args;
        args << QStringLiteral("purge") << QStringLiteral("-y") << packages;
        runPrivilegedAsync([this, args](QString& out, QString& err)
                           { return runPrivileged(args, out, err); });
        return true;
    }

    bool AptBackend::autoremove(QString& error)
    {
        Q_UNUSED(error);
        runPrivilegedAsync(
            [this](QString& out, QString& err)
            {
                return runPrivileged(
                    {QStringLiteral("autoremove"), QStringLiteral("-y"), QStringLiteral("--purge")},
                    out, err);
            });
        return true;
    }

    bool AptBackend::cleanCache(QString& error)
    {
        Q_UNUSED(error);
        runPrivilegedAsync([this](QString& out, QString& err)
                           { return runPrivileged({QStringLiteral("clean")}, out, err); });
        return true;
    }

    QStringList AptBackend::cacheDirectories() const
    {
        return {QStringLiteral("/var/cache/apt/archives")};
    }

    bool AptBackend::checkRebootRequired(bool& required, QString& error)
    {
        Q_UNUSED(error)
        // 容器环境无独立内核，重启检查无意义（多为宿主内核），直接判否避免误报。
        if (SystemInfo::isContainer())
        {
            required = false; // 与 DnfBackend 一致：容器不报需重启
            return true;      // 支持探测，但结果为不需要重启
        }
        // Debian/Ubuntu/Deepin 约定：/run/reboot-required（或 /var/run/reboot-required）
        // 存在即表示本次更新需要重启（内核或底层库）。仅读取，不修改。
        required = QFile::exists(QStringLiteral("/run/reboot-required")) ||
                   QFile::exists(QStringLiteral("/var/run/reboot-required"));
        return true; // apt/dpkg 支持该探测
    }

    bool AptBackend::checkServicesNeedingRestart(QStringList& services, QString& error)
    {
        services.clear();
        // 容器内 systemd 通常不管理宿主服务，跳过以免把宿主服务误报为"需重启"。
        if (SystemInfo::isContainer())
            return false;
        // needs-restarting（来自 needrestart / debian-goodies）能列出因更新需重启的服务。
        // -s：仅列出服务（不列进程）。命令不存在则视为后端不支持该探测（support=false）。
        // 注意：needrestart -s 在有服务需重启时退出码非 0，故用 runProbe 读取输出，
        // 不依赖 exit 0 判定（否则会漏报）。
        if (!commandExists(QStringLiteral("needs-restarting")))
            return false;
        QString raw;
        int exitCode = -1;
        if (!runProbe({QStringLiteral("needs-restarting"), QStringLiteral("-s")}, raw, exitCode))
            return false;
        if (exitCode == 0 && raw.trimmed().isEmpty())
            return true; // 无服务需重启
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString s = line.trimmed();
            if (s.isEmpty())
                continue;
            // 输出形如 "systemd-manager" 或 "systemd-manager.service"；统一去 .service 后缀
            services.append(s.endsWith(QStringLiteral(".service")) ? s.chopped(8) : s);
        }
        return true;
    }

    bool AptBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        paths.clear();
        Q_UNUSED(error)
        // 升级后 dpkg/ucf 会留下待合并的配置文件：
        //   *.dpkg-new  /  *.dpkg-dist  /  *.ucf-dist  /  *.dpkg-old  /  *.dpkg-bak
        // 仅扫描 /etc 下 3 层以内，避免遍历过深；只列路径，绝不自动合并。
        const QStringList suffixes = {QStringLiteral(".dpkg-new"), QStringLiteral(".dpkg-dist"),
                                      QStringLiteral(".ucf-dist"), QStringLiteral(".dpkg-old"),
                                      QStringLiteral(".dpkg-bak")};
        QDir etc(QStringLiteral("/etc"));
        if (!etc.exists())
            return true;
        collectConfigFiles(etc, suffixes, 0, 3, paths);
        return true;
    }

    bool AptBackend::checkFailedUnits(QStringList& units, QString& error)
    {
        units.clear();
        // 容器内多为宿主服务，failed unit 不应归咎于本次升级，跳过。
        if (!SystemInfo::hasSystemd() || SystemInfo::isContainer())
            return false;
        QString raw;
        if (!runQuery({QStringLiteral("systemctl"), QStringLiteral("--failed"),
                       QStringLiteral("--no-legend"), QStringLiteral("--no-pager")},
                      raw, error))
            return false;
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            // 形如 "foo.service  loaded  failed  failed  Foo description"
            const QString s = line.trimmed();
            if (s.isEmpty())
                continue;
            const QString unit = s.split(QStringLiteral(" "), Qt::SkipEmptyParts).first();
            if (!unit.isEmpty())
                units.append(unit);
        }
        return true;
    }

    void AptBackend::collectConfigFiles(const QDir& dir, const QStringList& suffixes, int depth,
                                        int maxDepth, QStringList& out)
    {
        if (depth > maxDepth)
            return;
        const QFileInfoList entries =
            dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QFileInfo& fi : entries)
        {
            if (fi.isDir())
            {
                collectConfigFiles(QDir(fi.filePath()), suffixes, depth + 1, maxDepth, out);
            }
            else
            {
                for (const QString& suf : suffixes)
                    if (fi.fileName().endsWith(suf))
                    {
                        out.append(fi.filePath());
                        break;
                    }
            }
        }
    }

} // namespace DtkUpdate
