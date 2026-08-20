#include "linyapsbackend.h"

#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include "common/appconfig.h"
#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    LinyapsBackend::LinyapsBackend(QObject* parent) : PackageBackend(parent) {}

    bool LinyapsBackend::isAvailable() const
    {
        // 玲珑是跨发行版的沙箱应用包管理器，不依赖特定发行版，
        // 只要本机能找到 ll-cli 且环境健康即可，不应按发行系限制。
        m_availabilityError.clear();
        if (!commandExists(QStringLiteral("ll-cli")))
        {
            // ll-cli 未安装：给出明确的安装/获取指引，而不是笼统"不可用"。
            m_availabilityError =
                tr("ll-cli command not found. The linglong runtime is not installed. "
                   "Please install the linglong runtime for your distribution (e.g. the "
                   "linglong package on deepin/fedora, or follow the cross-distribution "
                   "installation guide at https://linglong.dev).");
            return false;
        }
        // 轻量冒烟：能真正执行 `ll-cli list` 才视为可用。
        // 这一步能暴露"命令存在但环境损坏/权限异常"的情况，并把具体错误留给用户。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("ll-cli"), QStringLiteral("list")}, out, err))
        {
            m_availabilityError =
                tr("ll-cli exists but `ll-cli list` failed; the linglong runtime may be "
                   "broken: ") +
                (err.isEmpty() ? tr("(no error output; possibly insufficient permissions or "
                                    "uninitialized runtime)")
                               : err);
            return false;
        }
        return true;
    }

    QVariantMap LinyapsBackend::backendOptions() const
    {
        // 沙箱应用商店无系统级包选项（recommends/orphans/cache 均不适用），
        // 与 snap/flatpak 一致剔除这三个键，避免"看起来可配置但行为不变"的虚开关。
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends"));
        opts.remove(QStringLiteral("autoRemoveOrphans"));
        opts.remove(QStringLiteral("autoCleanCache"));
        return opts;
    }

    bool LinyapsBackend::parseList(const QString& raw, PackageList& out, bool onlyUpgradable) const
    {
        Q_UNUSED(onlyUpgradable)
        out.clear();
        // `raw` 是 const 引用；此处仅读取内容，const_cast 安全（QTextStream 不修改它）
        QTextStream stream(const_cast<QString*>(&raw));
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString id = line.trimmed();
            if (id.isEmpty() || id.startsWith(QStringLiteral("/")) ||
                id.startsWith(QStringLiteral("ID"))) // 跳过表头之类
                continue;
            PackageInfo info;
            // ll-cli list 输出每行形如 "org.deepin.demo/1.0.0" 或纯应用 id
            const QStringList parts = id.split(QStringLiteral("/"));
            info.name = parts.first();
            if (parts.size() > 1)
                info.candidateVersion = parts.at(1);
            info.isInstalled = true;
            out.append(info);
        }
        return true;
    }

    bool LinyapsBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // 玲珑可升级列表
        QString raw;
        if (!runQuery(QStringList{QStringLiteral("ll-cli"), QStringLiteral("list"),
                                  QStringLiteral("--upgradable")},
                      raw, error))
        {
            return false;
        }
        PackageList all;
        if (!parseList(raw, all, true))
            return false;
        // --upgradable 列出的都是可升级项
        for (PackageInfo& info : all)
        {
            info.isUpgradable = true;
            info.backendId = backendId(); // 标记来源后端，供多后端聚合区分
        }
        out = all;
        return true;
    }

    bool LinyapsBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        QString raw;
        if (!runQuery(QStringList{QStringLiteral("ll-cli"), QStringLiteral("list")}, raw, error))
            return false;
        PackageList all;
        if (!parseList(raw, all, false))
            return false;
        if (filter.isEmpty())
        {
            out = all;
            return true;
        }
        for (const PackageInfo& info : all)
            if (info.name.contains(filter, Qt::CaseInsensitive))
                out.append(info);
        return true;
    }

    bool LinyapsBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // 玲珑无严格 dry-run；用 search 探测包是否存在以给出可安装性判断。
        QString raw;
        if (!runQuery(QStringList{QStringLiteral("ll-cli"), QStringLiteral("search"), pkg}, raw,
                      error))
        {
            resolution = tr("Cannot query %1 (ll-cli search failed)").arg(pkg);
            return false;
        }
        if (raw.trimmed().isEmpty())
        {
            resolution = tr("No installable application found: %1").arg(pkg);
            return false;
        }
        resolution = tr("%1 can be installed via ll-cli").arg(pkg);
        return true;
    }

    bool LinyapsBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // 玲珑无 dpkg 式 rc 残余包概念，始终为空。
        Q_UNUSED(error)
        out.clear();
        return true;
    }

    QStringList LinyapsBackend::cacheDirectories() const
    {
        // 玲珑的应用层/缓存默认位于用户或系统 linglong 目录
        QStringList dirs;
        const QString sys = QStringLiteral("/var/lib/linglong");
        if (QDir(sys).exists())
            dirs.append(sys);
        const QString home = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
        const QString userDir =
            (home.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : home) +
            QStringLiteral("/linglong");
        if (QDir(userDir).exists())
            dirs.append(userDir);
        return dirs;
    }

    bool LinyapsBackend::checkRebootRequired(bool& required, QString& error)
    {
        // 沙箱应用更新不涉及内核，无重启需求。
        Q_UNUSED(error)
        required = false;
        return false; // support=false：上层跳过此探针
    }

    bool LinyapsBackend::checkServicesNeedingRestart(QStringList& services, QString& error)
    {
        Q_UNUSED(error)
        services.clear();
        return false; // support=false
    }

    bool LinyapsBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        Q_UNUSED(error)
        paths.clear();
        return false; // support=false：玲珑无系统级配置文件残留
    }

    bool LinyapsBackend::checkFailedUnits(QStringList& units, QString& error)
    {
        Q_UNUSED(error)
        units.clear();
        return false; // support=false：与系统服务无关
    }

    QStringList LinyapsBackend::operationArgs(Op op, const QStringList& packages, QString& error)
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
            // 玲珑升级用 upgrade（install 对已装应用报已安装，不会升级到新版本）
            QStringList args{QStringLiteral("upgrade")};
            args.append(packages);
            return args;
        }
        case Op::Remove:
        case Op::Purge:
            // 玲珑无 purge 概念，uninstall 即彻底移除沙箱应用。
            return QStringList{QStringLiteral("uninstall")} + packages;
        case Op::Autoremove:
            // 玲珑无 apt 式孤儿依赖概念，明确返回空表示不支持（诚实而非伪装成 prune）。
            return {};
        case Op::CleanCache:
            // prune 清理无用旧版本层（等价"回收空间/清缓存"）。
            return {QStringLiteral("prune")};
        }
        return {};
    }

} // namespace DtkUpdate
