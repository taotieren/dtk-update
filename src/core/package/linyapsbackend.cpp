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
        // 玲珑后端只需 ll-cli 命令可用即可；沙箱应用管理不依赖特定发行版包管理。
        if (!commandExists(QStringLiteral("ll-cli")))
            return false;
        // 轻量冒烟：能真正执行 `ll-cli list` 才视为可用。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("ll-cli"), QStringLiteral("list")}, out, err))
            return false;
        return true;
    }

    QVariantMap LinyapsBackend::backendOptions() const
    {
        QVariantMap m;
        if (m_config)
        {
            m.insert(QStringLiteral("noInstallRecommends"), m_config->noInstallRecommends());
            m.insert(QStringLiteral("autoRemoveOrphans"), m_config->autoRemoveOrphans());
            m.insert(QStringLiteral("autoCleanCache"), m_config->autoCleanCache());
        }
        return m;
    }

    bool LinyapsBackend::parseList(const QString& raw, PackageList& out, bool onlyUpgradable) const
    {
        Q_UNUSED(onlyUpgradable)
        out.clear();
        QTextStream stream(const_cast<QString*>(&raw));
        QString line;
        while (stream.readLineInto(&line))
        {
            const QString id = line.trimmed();
            if (id.isEmpty() || id.startsWith(QStringLiteral("/"))
                || id.startsWith(QStringLiteral("ID"))) // 跳过表头之类
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
            info.isUpgradable = true;
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
            resolution = QStringLiteral("无法查询 %1（ll-cli search 失败）").arg(pkg);
            return false;
        }
        if (raw.trimmed().isEmpty())
        {
            resolution = QStringLiteral("未找到可安装的应用：%1").arg(pkg);
            return false;
        }
        resolution = QStringLiteral("%1 可经 ll-cli 安装").arg(pkg);
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
        const QString home =
            QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
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

    bool LinyapsBackend::install(const QStringList& packages, QString& error)
    {
        QString out;
        QStringList args{QStringLiteral("install")};
        args.append(packages);
        if (!runPrivileged(args, out, error))
            return false;
        emit operationFinished(true, out);
        return true;
    }

    bool LinyapsBackend::remove(const QStringList& packages, QString& error)
    {
        QString out;
        QStringList args{QStringLiteral("uninstall")};
        args.append(packages);
        if (!runPrivileged(args, out, error))
            return false;
        emit operationFinished(true, out);
        return true;
    }

    bool LinyapsBackend::purge(const QStringList& packages, QString& error)
    {
        // 玲珑无 purge 概念，uninstall 即彻底移除沙箱应用。
        return remove(packages, error);
    }

    bool LinyapsBackend::autoremove(QString& error)
    {
        // 玲珑无 apt 式 autoremove；用 prune 清理无用的旧版本层（等价"回收空间"）。
        QString out;
        if (!runPrivileged(QStringList{QStringLiteral("prune")}, out, error))
            return false;
        emit operationFinished(true, out);
        return true;
    }

    bool LinyapsBackend::cleanCache(QString& error)
    {
        // 清理 linglong 缓存层。
        QString out;
        if (!runPrivileged(QStringList{QStringLiteral("prune")}, out, error))
            return false;
        emit operationFinished(true, out);
        return true;
    }

} // namespace DtkUpdate
