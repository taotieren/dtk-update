#include "dnfbackend.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QtConcurrent>
#include <algorithm>

#include "common/appconfig.h"
#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    DnfBackend::DnfBackend(QObject* parent) : PackageBackend(parent) {}

    bool DnfBackend::isAvailable() const
    {
        // 探测 dnf 系关键命令是否齐全。部分环境（如 Arch 上的 tinyget 仿真）
        // 仅提供 `dnf` 占位脚本，但 rpm 缺失，此时必须判为不可用，否则运行时会
        // 虚假可用、操作后崩溃。
        static const QStringList required = {
            QStringLiteral("dnf"),
            QStringLiteral("rpm"),
        };
        for (const QString& cmd : required)
        {
            if (!commandExists(cmd))
                return false;
        }
        // 轻量冒烟：能真正执行 `dnf list --upgrades` 才视为可用。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("dnf"), QStringLiteral("list"),
                                  QStringLiteral("--upgrades"), QStringLiteral("--quiet")},
                      out, err))
        {
            return false;
        }
        return true;
    }

    bool DnfBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // dnf list --upgrades 输出形如：
        //   firefox.x86_64    120.0.1-1.fc39    updates
        QString raw;
        if (!runQuery({QStringLiteral("dnf"), QStringLiteral("list"), QStringLiteral("--upgrades"),
                       QStringLiteral("--quiet")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            // 跳过表头与空行
            if (line.startsWith(QStringLiteral("Available")) || line.trimmed().isEmpty())
                continue;
            PackageInfo info;
            // 解析 "name.arch  version"（repo 列当前数据模型未承载，故不提取；
            // 若未来 PackageInfo 增 repo 成员需在此回填 cols.at(2)）。
            const QStringList cols = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (cols.size() < 2)
                continue;
            const QString na = cols.at(0);
            const int dot = na.lastIndexOf(QLatin1Char('.'));
            info.name = (dot > 0) ? na.left(dot) : na;
            if (dot > 0)
                info.architecture = na.mid(dot + 1);
            info.candidateVersion = cols.at(1);
            out.append(info);
        }
        // 标记来源后端，供多后端聚合时区分（如与 linyaps 并存）
        for (PackageInfo& info : out)
            info.backendId = backendId();
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable packages (dnf)";
        return true;
    }

    bool DnfBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
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

    bool DnfBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // dnf 干跑：dnf install -y --setopt=tsflags=test <pkg>
        return runQuery({QStringLiteral("dnf"), QStringLiteral("install"), QStringLiteral("-y"),
                         QStringLiteral("--setopt=tsflags=test"), pkg},
                        resolution, error);
    }

    bool DnfBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // dnf/rpm 无 "rc" 概念；此处返回空（dnf 无残留配置文件机制）。
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

    QStringList DnfBackend::operationArgs(Op op, const QStringList& packages, QString& error)
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
        case Op::Purge: // rpm/dnf 无独立 purge；remove 已同时删除配置（%postun 处理）
        {
            QStringList args{QStringLiteral("remove"), QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Autoremove:
            return {QStringLiteral("autoremove"), QStringLiteral("-y")};
        case Op::CleanCache:
            return {QStringLiteral("clean"), QStringLiteral("all")};
        }
        return {};
    }

    QVariantMap DnfBackend::backendOptions() const
    {
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends")); // dnf/rpm 无此概念
        return opts;
    }

    QStringList DnfBackend::cacheDirectories() const
    {
        return {QStringLiteral("/var/cache/dnf")};
    }

    bool DnfBackend::checkRebootRequired(bool& required, QString& error)
    {
        required = false;
        // 容器环境无独立内核，跳过以避免误报宿主内核状态。
        if (SystemInfo::isContainer())
            return true;
        // dnf5 `needs-restarting`（不带 -r；-r/--reboothint 在 dnf5 中为兼容保留的无效果选项）
        // 退出码语义：1 = 建议重启（安装了新内核/重要包），0 = 无需重启。
        // 若命令存在，按其退出码判定（正确语义，不依赖 runQuery 的 exit 0 成功约定）。
        if (commandExists(QStringLiteral("needs-restarting")))
        {
            QString out;
            int exitCode = -1;
            if (!runProbe({QStringLiteral("needs-restarting")}, out, exitCode))
                return false; // 命令无法运行
            required = (exitCode == 1);
            return true;
        }
        // 回退：比较 /usr/lib/modules 最新内核与当前运行内核版本（无 needs-restarting 时）。
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

    bool DnfBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        paths.clear();
        Q_UNUSED(error)
        // rpm 升级后留下的待审阅配置：*.rpmnew / *.rpmsave / *.rpmorig。
        // 仅扫描 /etc 下 3 层以内；只列路径，绝不自动合并（与 apt 后端保持一致）。
        const QStringList suffixes = {QStringLiteral(".rpmnew"), QStringLiteral(".rpmsave"),
                                      QStringLiteral(".rpmorig")};
        QDir etc(QStringLiteral("/etc"));
        if (!etc.exists())
            return true;
        paths = collectConfigFiles({QStringLiteral("/etc")}, suffixes, 3);
        return true;
    }

} // namespace DtkUpdate
