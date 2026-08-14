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
        // 标记来源后端，供多后端聚合时区分（如与 linyaps 并存）
        for (PackageInfo& info : out)
            info.backendId = backendId();
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

    QStringList AptBackend::operationArgs(Op op, const QStringList& packages, QString& error)
    {
        Q_UNUSED(error);
        switch (op)
        {
        case Op::Install:
        {
            QStringList args{QStringLiteral("install"), QStringLiteral("-y")};
            const bool noRec = m_config ? m_config->noInstallRecommends() : true;
            if (noRec)
                args << QStringLiteral("--no-install-recommends");
            args.append(packages);
            return args;
        }
        case Op::Remove:
        {
            QStringList args{QStringLiteral("remove"), QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Purge:
        {
            QStringList args{QStringLiteral("purge"), QStringLiteral("-y")};
            args.append(packages);
            return args;
        }
        case Op::Autoremove:
            return {QStringLiteral("autoremove"), QStringLiteral("-y"), QStringLiteral("--purge")};
        case Op::CleanCache:
            return {QStringLiteral("clean")};
        }
        return {};
    }

    QVariantMap AptBackend::backendOptions() const
    {
        return defaultBackendOptions();
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
        paths = collectConfigFiles({QStringLiteral("/etc")}, suffixes, 3);
        return true;
    }

} // namespace DtkUpdate
