#include "snapbackend.h"

#include <QDir>
#include <QTextStream>

#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    SnapBackend::SnapBackend(QObject* parent) : PackageBackend(parent) {}

    QStringList SnapBackend::privilegedPrefix() const
    {
        // snap 写操作命令名由 prefix 提供（operationArgs 只给子命令）；
        // snapd 自身经 polkit 策略提权，不套 pkexec，故 prefix 仅含 snap 命令名。
        return {QStringLiteral("snap")};
    }

    bool SnapBackend::checkRebootRequired(bool& required, QString& error)
    {
        Q_UNUSED(error)
        required = false;
        return false; // support=false：沙箱应用不触内核，无需重启
    }

    bool SnapBackend::checkServicesNeedingRestart(QStringList& services, QString& error)
    {
        Q_UNUSED(error)
        services.clear();
        return false; // support=false：与系统服务无关
    }

    bool SnapBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        Q_UNUSED(error)
        paths.clear();
        return false; // support=false：沙箱无系统级配置文件残留
    }

    bool SnapBackend::checkFailedUnits(QStringList& units, QString& error)
    {
        Q_UNUSED(error)
        units.clear();
        return false; // support=false：与系统服务无关
    }

    bool SnapBackend::isAvailable() const
    {
        // 沙箱应用商店：不仅要求 snap 命令存在，还要 snapd 守护真正可用。
        // 仅 commandExists("snap") 会陷入"命令在但 daemon 没起"的伪可用陷阱，
        // 后续 fetchUpgradable 发起 snap refresh --list 时直接挂起或报错。
        static const QStringList required = {QStringLiteral("snap")};
        for (const QString& cmd : required)
            if (!commandExists(cmd))
                return false;
        // 冒烟：snap list 能在 daemon 就绪时正常返回（daemon 未运行会失败）。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("snap"), QStringLiteral("list"),
                                  QStringLiteral("--unicode=never")},
                      out, err))
        {
            return false;
        }
        return true;
    }

    bool SnapBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // snap refresh --list 输出形如（表头 + 每行可升级 snap）：
        //   Name  Version  Rev  Channel  Publisher  Notes
        //   core22  20241011  1883  latest/stable  Canonical  base
        QString raw;
        if (!runQuery({QStringLiteral("snap"), QStringLiteral("refresh"), QStringLiteral("--list"),
                       QStringLiteral("--unicode=never")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        bool header = true;
        while (stream.readLineInto(&line))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (header)
            {
                // 首行为表头 "Name Version ..."，跳过
                header = false;
                if (trimmed.startsWith(QStringLiteral("Name")))
                    continue;
            }
            PackageInfo info;
            const QStringList cols = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (cols.size() < 2)
                continue;
            info.name = cols.at(0);
            info.candidateVersion = cols.at(1);
            out.append(info);
        }
        for (PackageInfo& info : out)
            info.backendId = backendId();
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable snaps";
        return true;
    }

    bool SnapBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        // snap list 输出：Name Version Rev Tracking Publisher Notes
        QString raw;
        if (!runQuery(
                {QStringLiteral("snap"), QStringLiteral("list"), QStringLiteral("--unicode=never")},
                raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        bool header = true;
        while (stream.readLineInto(&line))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (header)
            {
                header = false;
                if (trimmed.startsWith(QStringLiteral("Name")))
                    continue;
            }
            const QStringList cols = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (cols.size() < 2)
                continue;
            PackageInfo info;
            info.name = cols.at(0);
            info.currentVersion = cols.at(1);
            if (!filter.isEmpty() && !info.name.contains(filter, Qt::CaseInsensitive))
                continue;
            out.append(info);
        }
        return true;
    }

    bool SnapBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // snap 无原生 dry-run；用 info 返回元信息作为最轻量可行性探测兜底，
        // 真正安装由 snap install 执行。此处仅验证包名可被解析。
        return runQuery({QStringLiteral("snap"), QStringLiteral("info"), pkg}, resolution, error);
    }

    bool SnapBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // 沙箱应用无系统级残留配置概念
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

    QStringList SnapBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        switch (op)
        {
        case Op::Install:
        {
            QStringList args{QStringLiteral("install")};
            args.append(packages);
            return args;
        }
        case Op::Upgrade:
        {
            // snap 升级用 refresh（install 对已装包报 already installed 会失败）
            QStringList args{QStringLiteral("refresh")};
            args.append(packages);
            return args;
        }
        case Op::Remove:
        case Op::Purge: // snap 无独立 purge，remove 即卸载应用
        {
            QStringList args{QStringLiteral("remove")};
            args.append(packages);
            return args;
        }
        case Op::Autoremove: // snap 无孤儿依赖概念（每个 snap 自包含）
            return {};
        case Op::CleanCache: // snap 缓存由 snapd 自行管理
            return {};
        }
        return {};
    }

    QVariantMap SnapBackend::backendOptions() const
    {
        // 沙箱应用商店无系统级包选项（recommends/orphans/cache 均不适用）
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends"));
        opts.remove(QStringLiteral("autoRemoveOrphans"));
        opts.remove(QStringLiteral("autoCleanCache"));
        return opts;
    }

    QStringList SnapBackend::cacheDirectories() const
    {
        // snapd 缓存路径（只读参考，clean 由 snapd 自管）
        return {QStringLiteral("/var/lib/snapd/cache")};
    }

} // namespace DtkUpdate
