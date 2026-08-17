#include "flatpakbackend.h"

#include <QDir>
#include <QStandardPaths>
#include <QTextStream>

#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    FlatpakBackend::FlatpakBackend(QObject* parent) : PackageBackend(parent) {}

    QStringList FlatpakBackend::privilegedPrefix() const
    {
        // flatpak 写操作命令名由 prefix 提供（operationArgs 只给子命令）；
        // flatpak 自身处理 polkit，不套 pkexec，故 prefix 仅含 flatpak 命令名。
        return {QStringLiteral("flatpak")};
    }

    bool FlatpakBackend::checkRebootRequired(bool& required, QString& error)
    {
        Q_UNUSED(error)
        required = false;
        return false; // support=false：沙箱应用不触内核，无需重启
    }

    bool FlatpakBackend::checkServicesNeedingRestart(QStringList& services, QString& error)
    {
        Q_UNUSED(error)
        services.clear();
        return false; // support=false：与系统服务无关
    }

    bool FlatpakBackend::checkConfigFilesToReview(QStringList& paths, QString& error)
    {
        Q_UNUSED(error)
        paths.clear();
        return false; // support=false：沙箱无系统级配置文件残留
    }

    bool FlatpakBackend::checkFailedUnits(QStringList& units, QString& error)
    {
        Q_UNUSED(error)
        units.clear();
        return false; // support=false：与系统服务无关
    }

    bool FlatpakBackend::isAvailable() const
    {
        // flatpak 命令存在还不够：纯净最小化系统可能装了 flatpak 但没有任何远端，
        // 此时 remote-ls 无意义且易误报。需至少一个已配置远端才视为可用。
        static const QStringList required = {QStringLiteral("flatpak")};
        for (const QString& cmd : required)
            if (!commandExists(cmd))
                return false;
        QString out, err;
        if (!runQuery({QStringLiteral("flatpak"), QStringLiteral("remotes"),
                       QStringLiteral("--columns=name")},
                      out, err))
        {
            return false;
        }
        return !out.trimmed().isEmpty();
    }

    bool FlatpakBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // flatpak remote-ls --updates --columns=application,version,branch 输出形如：
        //   org.gnome.Builder  46.0  stable
        // 同时覆盖 system 与 user 安装（默认两者都查）。
        QString raw;
        if (!runQuery({QStringLiteral("flatpak"), QStringLiteral("remote-ls"),
                       QStringLiteral("--updates"),
                       QStringLiteral("--columns=application,version,branch")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            const QStringList cols = trimmed.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
            if (cols.isEmpty())
                continue;
            PackageInfo info;
            info.name = cols.at(0);
            if (cols.size() > 1)
                info.candidateVersion = cols.at(1);
            // 无可用新版本的条目（仅列 appid）跳过，避免把已是最新的刷出来
            if (info.candidateVersion.isEmpty())
                continue;
            out.append(info);
        }
        for (PackageInfo& info : out)
            info.backendId = backendId();
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable flatpaks";
        return true;
    }

    bool FlatpakBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        // flatpak list --app 输出：application,version,branch（仅应用，排除运行时）
        QString raw;
        if (!runQuery({QStringLiteral("flatpak"), QStringLiteral("list"), QStringLiteral("--app"),
                       QStringLiteral("--columns=application,version,branch")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            const QStringList cols = trimmed.split(QLatin1Char('\t'), Qt::SkipEmptyParts);
            if (cols.isEmpty())
                continue;
            PackageInfo info;
            info.name = cols.at(0);
            if (cols.size() > 1)
                info.currentVersion = cols.at(1);
            if (!filter.isEmpty() && !info.name.contains(filter, Qt::CaseInsensitive))
                continue;
            out.append(info);
        }
        return true;
    }

    bool FlatpakBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // flatpak 无原生 dry-run；remote-info 可验证 ref 存在即可用，作为可行性兜底。
        return runQuery({QStringLiteral("flatpak"), QStringLiteral("remote-info"),
                         QStringLiteral("flathub"), pkg},
                        resolution, error);
    }

    bool FlatpakBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // 沙箱应用无系统级残留配置概念
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

    QStringList FlatpakBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        switch (op)
        {
        case Op::Install:
        {
            // flatpak install 需指定远端；常用 flathub，缺失远端时由 snap/flatpak 报错给出诊断。
            QStringList args{QStringLiteral("install"), QStringLiteral("-y"),
                             QStringLiteral("flathub")};
            args.append(packages);
            return args;
        }
        case Op::Remove:
        case Op::Purge: // flatpak 无独立 purge，remove 即卸载
        {
            QStringList args{QStringLiteral("uninstall"), QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Autoremove: // flatpak 无孤儿依赖概念（运行时由引用计数管理）
            return {};
        case Op::CleanCache: // flatpak 缓存由自身管理
            return {};
        }
        return {};
    }

    QVariantMap FlatpakBackend::backendOptions() const
    {
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends"));
        opts.remove(QStringLiteral("autoRemoveOrphans"));
        opts.remove(QStringLiteral("autoCleanCache"));
        return opts;
    }

    QStringList FlatpakBackend::cacheDirectories() const
    {
        return {QStringLiteral("/var/lib/flatpak/repo"),
                QStandardPaths::writableLocation(QStandardPaths::HomeLocation) +
                    QStringLiteral("/.local/share/flatpak/repo")};
    }

} // namespace DtkUpdate
