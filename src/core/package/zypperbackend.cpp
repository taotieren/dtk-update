#include "zypperbackend.h"

#include <QDir>
#include <QFileInfo>
#include <QSysInfo>
#include <QTextStream>
#include <algorithm>

#include "common/appconfig.h"
#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    ZypperBackend::ZypperBackend(QObject* parent) : PackageBackend(parent) {}

    bool ZypperBackend::isAvailable() const
    {
        // 探测 zypper 系关键命令是否齐全。zypper 基于 rpm，缺失 rpm 必须判不可用
        // （否则运行时会虚假可用、操作后崩溃）。
        static const QStringList required = {
            QStringLiteral("zypper"),
            QStringLiteral("rpm"),
        };
        for (const QString& cmd : required)
        {
            if (!commandExists(cmd))
                return false;
        }
        // 轻量冒烟：zypper --version 零网络、最稳定，能真正执行才视为可用。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("zypper"), QStringLiteral("--version")}, out, err))
        {
            return false;
        }
        return true;
    }

    bool ZypperBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // zypper list-updates 默认表格输出（不同版本列名/顺序有差异），形如：
        //   S | Repository       | Name   | Current Version | Available Version | Arch
        //   --+------------------+--------+-----------------+-------------------+------
        //   v | repo-update      | bash   | 5.2.15-1.1      | 5.2.21-1.1        | x86_64
        // 采用表头驱动列名映射（读表头行建立 Name/Available Version/Arch 列索引），
        // 以稳妥应对版本差异；不依赖 --no-table（兼容性存疑）。
        QString raw;
        if (!runQuery({QStringLiteral("zypper"), QStringLiteral("list-updates")}, raw, error))
        {
            return false;
        }
        int nameCol = -1, availCol = -1, archCol = -1;
        bool headerParsed = false;
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            if (trimmed.startsWith(QStringLiteral("--")) ||
                trimmed.startsWith(QStringLiteral("++")))
                continue; // 分隔行
            const QStringList cols = trimmed.split(QLatin1Char('|'), Qt::SkipEmptyParts);
            if (!headerParsed)
            {
                // 表头行：按列名定位索引
                for (int i = 0; i < cols.size(); ++i)
                {
                    const QString h = cols.at(i).toLower();
                    if (h.contains(QStringLiteral("name")) && nameCol < 0)
                        nameCol = i;
                    else if ((h.contains(QStringLiteral("available")) ||
                              h.contains(QStringLiteral("version"))) &&
                             availCol < 0 && i > nameCol)
                        availCol = i; // 取 Name 之后首个 version 列作候选版本
                    else if (h.contains(QStringLiteral("arch")) && archCol < 0)
                        archCol = i;
                }
                if (nameCol >= 0 && availCol >= 0)
                    headerParsed = true;
                continue;
            }
            if (cols.size() <= nameCol || cols.size() <= availCol)
                continue;
            PackageInfo info;
            info.name = cols.at(nameCol).trimmed();
            info.candidateVersion = cols.at(availCol).trimmed();
            if (archCol >= 0 && cols.size() > archCol)
                info.architecture = cols.at(archCol).trimmed();
            if (info.name.isEmpty())
                continue;
            info.backendId = backendId();
            out.append(info);
        }
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable packages (zypper)";
        return true;
    }

    bool ZypperBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        // 完全复用 dnf 的 rpm 查询格式（tab 分隔）。
        QString raw;
        if (!runQuery({QStringLiteral("rpm"), QStringLiteral("-qa"), QStringLiteral("--qf"),
                       QStringLiteral("%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{GROUP}\n")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QStringList parts = line.split(QLatin1Char('\t'));
            if (parts.size() < 3)
                continue;
            PackageInfo info;
            info.name = parts.at(0);
            info.currentVersion = parts.at(1);
            info.architecture = parts.at(2);
            if (parts.size() > 3)
                info.section = parts.at(3);
            if (!filter.isEmpty() && !info.name.contains(filter, Qt::CaseInsensitive))
                continue;
            out.append(info);
        }
        return true;
    }

    bool ZypperBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // zypper 多数命令支持 --dry-run，返回原生输出作可行性兜底。
        return runQuery(
            {QStringLiteral("zypper"), QStringLiteral("install"), QStringLiteral("--dry-run"), pkg},
            resolution, error);
    }

    bool ZypperBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // zypper/rpm 无 dpkg rc 概念；返回空。
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

    QStringList ZypperBackend::cacheDirectories() const
    {
        return {QStringLiteral("/var/cache/zypp")};
    }

    QStringList ZypperBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        switch (op)
        {
        case Op::Install:
        {
            QStringList args{QStringLiteral("install"), QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Remove:
        case Op::Purge: // zypper/rpm 无独立 purge；remove 已删配置（-u 清依赖）
            return QStringList{QStringLiteral("remove"), QStringLiteral("-y")} + packages;
        case Op::Autoremove: // -u = --clean-deps 移除不再被需要的依赖
            return QStringList{QStringLiteral("remove"), QStringLiteral("-y"),
                               QStringLiteral("-u")} +
                   packages;
        case Op::CleanCache:
            return {QStringLiteral("clean"), QStringLiteral("-a")};
        }
        return {};
    }

    QVariantMap ZypperBackend::backendOptions() const
    {
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends")); // zypper 无 recommends 开关概念
        return opts;
    }

    bool ZypperBackend::checkRebootRequired(bool& required, QString& error)
    {
        required = false;
        Q_UNUSED(error);
        // 容器环境无独立内核，跳过以免误报宿主内核状态。
        if (SystemInfo::isContainer())
            return true;
        // 比较 /usr/lib/modules 最新内核与当前运行内核版本（SUSE 通常无 needs-restarting）。
        const QDir modules(QStringLiteral("/usr/lib/modules"));
        if (modules.exists())
        {
            QStringList kernels;
            for (const QFileInfo& fi : modules.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                kernels.append(fi.fileName());
            if (!kernels.isEmpty())
            {
                std::sort(kernels.begin(), kernels.end());
                const QString latest = kernels.last();
                const QString running = QSysInfo::kernelVersion();
                if (!running.isEmpty() && running != latest)
                    required = true;
                return true;
            }
        }
        return false; // 无可靠探测手段，视为不支持
    }

    bool ZypperBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        paths.clear();
        Q_UNUSED(error)
        // rpm 升级后留下的待审阅配置：*.rpmnew / *.rpmsave / *.rpmorig。
        // 仅扫描 /etc 下 3 层以内；只列路径，绝不自动合并（与 apt/dnf 保持一致）。
        const QStringList suffixes = {QStringLiteral(".rpmnew"), QStringLiteral(".rpmsave"),
                                      QStringLiteral(".rpmorig")};
        QDir etc(QStringLiteral("/etc"));
        if (!etc.exists())
            return true;
        paths = collectConfigFiles({QStringLiteral("/etc")}, suffixes, 3);
        return true;
    }

} // namespace DtkUpdate
