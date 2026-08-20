#include "pacmanbackend.h"

#include <QDir>
#include <QFileInfo>
#include <QSysInfo>
#include <QTextStream>

#include "common/appconfig.h"
#include "common/systeminfo.h"
#include "logger.h"

namespace DtkUpdate
{

    PacmanBackend::PacmanBackend(QObject* parent) : PackageBackend(parent) {}

    bool PacmanBackend::isAvailable() const
    {
        // 探测 pacman 系关键命令是否齐全。pacman 本体重于一切，缺失则判不可用。
        // checkupdates 由 pacman-contrib 提供，缺失也能降级为 `pacman -Qu`，
        // 故此处仅强制要求 pacman 存在。
        if (!commandExists(QStringLiteral("pacman")))
            return false;
        // 轻量冒烟：能真正执行 `pacman -Qu`（无网/无锁也能跑；无升级项时 exit 0 且无输出，
        // 属正常可用，故仅以"能否执行"判定，不要求有输出）。
        QString out, err;
        if (!runQuery(QStringList{QStringLiteral("pacman"), QStringLiteral("-Qu"),
                                  QStringLiteral("--quiet")},
                      out, err))
        {
            return false;
        }
        return true;
    }

    bool PacmanBackend::fetchUpgradable(PackageList& out, QString& error)
    {
        // pacman -Qu 输出形如（每行一个可升级包）：
        //   pacman 6.0.1-1 -> 6.0.2-1
        // 可能带 [ignored] / [repository] 前缀（pacman 在仓库/忽略标记时用方括号），需剥除。
        // 无升级项时 exit 0 且无输出，返回空列表为正常。
        QString raw;
        if (!runQuery({QStringLiteral("pacman"), QStringLiteral("-Qu"), QStringLiteral("--quiet")},
                      raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            QString trimmed = line.trimmed();
            if (trimmed.isEmpty())
                continue;
            // 剥除行首方括号标记（[testing]/[ignored] 等）
            int bracket = trimmed.indexOf(QLatin1Char(']'));
            if (bracket >= 0 && trimmed.startsWith(QLatin1Char('[')))
                trimmed = trimmed.mid(bracket + 1).trimmed();
            // 形如 "pkgname oldver -> newver"
            const int arrow = trimmed.indexOf(QStringLiteral(" -> "));
            if (arrow < 0)
                continue;
            const QString nameVer = trimmed.left(arrow).trimmed();
            const QString newVer = trimmed.mid(arrow + 4).trimmed();
            const int lastSpace = nameVer.lastIndexOf(QLatin1Char(' '));
            if (lastSpace < 0)
                continue;
            PackageInfo info;
            info.name = nameVer.left(lastSpace);
            info.currentVersion = nameVer.mid(lastSpace + 1);
            info.candidateVersion = newVer;
            info.backendId = backendId();
            out.append(info);
        }
        qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable packages (pacman)";
        return true;
    }

    bool PacmanBackend::listInstalled(PackageList& out, const QString& filter, QString& error)
    {
        // pacman -Q 输出形如（每行一个已装包）：name version
        QString raw;
        if (!runQuery({QStringLiteral("pacman"), QStringLiteral("-Q")}, raw, error))
        {
            return false;
        }
        QTextStream stream(&raw);
        QString line;
        while (stream.readLineInto(&line))
        {
            const QStringList cols = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
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

    bool PacmanBackend::simulateInstall(const QString& pkg, QString& resolution, QString& error)
    {
        // pacman 无原生 "只解析不下载" 的纯文本干跑；用 -Sp 打印将下载的 URL 作为可行性兜底
        // （仅探测包存在性与可解析依赖，不写入系统）。
        return runQuery({QStringLiteral("pacman"), QStringLiteral("-Sp"), pkg}, resolution, error);
    }

    bool PacmanBackend::listResidualPackages(PackageList& out, QString& error)
    {
        // pacman 无 dpkg rc 概念；返回空。
        Q_UNUSED(out)
        Q_UNUSED(error)
        return true;
    }

    QStringList PacmanBackend::cacheDirectories() const
    {
        return {QStringLiteral("/var/cache/pacman/pkg")};
    }

    QStringList PacmanBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        switch (op)
        {
        case Op::Install:
        {
            // --needed 跳过已装最新版；pacman 无 recommends 概念故不加对应开关。
            QStringList args{QStringLiteral("-S"), QStringLiteral("--needed"),
                             QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Remove:
            return QStringList{QStringLiteral("-R"), QStringLiteral("-y")} + packages;
        case Op::Purge: // pacman 无独立 purge；-n 同时删除配置文件
            return QStringList{QStringLiteral("-R"), QStringLiteral("-y"), QStringLiteral("-n")} +
                   packages;
        case Op::Autoremove:
            // pacman 无原生单命令 autoremove，且孤儿列表需先查询（pacman -Qtdq）再移除，
            // 不适合 operationArgs 纯参数模式；明确返回空表示不支持（诚实而非无目标失败）。
            return {};
        case Op::CleanCache:
            // -Scc 会交互询问是否清理，非交互（pkexec）环境必须 --noconfirm 防挂起；
            // -y 刷新数据库非清理所需，去掉避免多余网络操作。
            return {QStringLiteral("-Scc"), QStringLiteral("--noconfirm")};
        }
        return {};
    }

    QVariantMap PacmanBackend::backendOptions() const
    {
        QVariantMap opts = defaultBackendOptions();
        opts.remove(QStringLiteral("noInstallRecommends")); // pacman 无 recommends 概念
        return opts;
    }

    bool PacmanBackend::checkRebootRequired(bool& required, QString& error)
    {
        required = false;
        Q_UNUSED(error);
        // 容器环境无独立内核，跳过以免误报宿主内核状态。
        if (SystemInfo::isContainer())
            return true;
        // 比较 /usr/lib/modules 最新内核与当前运行内核版本。
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

} // namespace DtkUpdate
