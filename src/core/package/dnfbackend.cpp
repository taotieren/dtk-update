#include "dnfbackend.h"
#include "logger.h"
#include "common/appconfig.h"
#include "common/systeminfo.h"

#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>
#include <QSysInfo>
#include <QtConcurrent>
#include <algorithm>

namespace DtkUpdate {

DnfBackend::DnfBackend(QObject *parent) : PackageBackend(parent) {}

bool DnfBackend::commandExists(const QString &cmd)
{
    return !QStandardPaths::findExecutable(cmd).isEmpty();
}

bool DnfBackend::isAvailable() const
{
    // 探测 dnf 系关键命令是否齐全。部分环境（如 Arch 上的 tinyget 仿真）
    // 仅提供 `dnf` 占位脚本，但 rpm 缺失，此时必须判为不可用，否则运行时会
    // 虚假可用、操作后崩溃。
    static const QStringList required = {
        QStringLiteral("dnf"),
        QStringLiteral("rpm"),
    };
    for (const QString &cmd : required) {
        if (!commandExists(cmd))
            return false;
    }
    // 轻量冒烟：能真正执行 `dnf list --upgrades` 才视为可用。
    QString out, err;
    if (!runQuery(QStringList{QStringLiteral("dnf"), QStringLiteral("list"),
                              QStringLiteral("--upgrades"), QStringLiteral("--quiet")},
                  out, err)) {
        return false;
    }
    return true;
}

bool DnfBackend::runQuery(const QStringList &args, QString &output, QString &error) const
{
    QProcess proc;
    applyStableLocale(proc);
    proc.start(args.first(), args.mid(1));
    if (!proc.waitForStarted(8000)) {
        error = proc.errorString();
        return false;
    }
    proc.waitForFinished(-1);
    output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        error = QString::fromLocal8Bit(proc.readAllStandardError());
        return false;
    }
    return true;
}

bool DnfBackend::runPrivileged(const QStringList &dnfArgs, QString &output, QString &error) const
{
    QStringList args{QStringLiteral("pkexec"), QStringLiteral("dnf")};
    args.append(dnfArgs);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(QStringLiteral("pkexec"), args);
    if (!proc.waitForStarted(8000)) {
        error = proc.errorString();
        return false;
    }
    proc.waitForFinished(-1);
    output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    const int code = proc.exitCode();
    if (proc.exitStatus() != QProcess::NormalExit || code != 0) {
        error = output;
        return false;
    }
    return true;
}

bool DnfBackend::runProbe(const QStringList &args, QString &output, int &exitCode) const
{
    exitCode = -1;
    QProcess proc;
    applyStableLocale(proc);
    proc.start(args.first(), args.mid(1));
    if (!proc.waitForStarted(8000)) {
        exitCode = -1;
        return false;
    }
    proc.waitForFinished(-1);
    output = QString::fromLocal8Bit(proc.readAllStandardOutput());
    if (proc.exitStatus() == QProcess::NormalExit)
        exitCode = proc.exitCode();
    else
        exitCode = -1;
    return true;  // 进程已正常结束（无论退出码），交由调用方解释语义
}

bool DnfBackend::fetchUpgradable(PackageList &out, QString &error)
{
    // dnf list --upgrades 输出形如：
    //   firefox.x86_64    120.0.1-1.fc39    updates
    QString raw;
    if (!runQuery({QStringLiteral("dnf"), QStringLiteral("list"),
                   QStringLiteral("--upgrades"), QStringLiteral("--quiet")}, raw, error)) {
        return false;
    }
    QTextStream stream(&raw);
    QString line;
    while (stream.readLineInto(&line)) {
        // 跳过表头与空行
        if (line.startsWith(QStringLiteral("Available")) || line.trimmed().isEmpty())
            continue;
        PackageInfo info;
        // 解析 "name.arch  version  repo"
        const QStringList cols = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (cols.size() < 2)
            continue;
        const QString na = cols.at(0);
        const int dot = na.lastIndexOf(QLatin1Char('.'));
        info.name = (dot > 0) ? na.left(dot) : na;
        if (dot > 0) info.architecture = na.mid(dot + 1);
        info.candidateVersion = cols.at(1);
        out.append(info);
    }
    qCInfo(dtkUpdateCore) << "fetched" << out.size() << "upgradable packages (dnf)";
    return true;
}

bool DnfBackend::listInstalled(PackageList &out, const QString &filter, QString &error)
{
    QString raw;
    if (!runQuery({QStringLiteral("rpm"), QStringLiteral("-qa"),
                   QStringLiteral("--qf"),
                   QStringLiteral("%{NAME}\t%{VERSION}-%{RELEASE}\t%{ARCH}\t%{GROUP}\n")},
                  raw, error)) {
        return false;
    }
    QTextStream stream(&raw);
    QString line;
    while (stream.readLineInto(&line)) {
        const QStringList parts = line.split(QLatin1Char('\t'));
        if (parts.size() < 3)
            continue;
        PackageInfo info;
        info.name = parts.at(0);
        info.currentVersion = parts.at(1);
        info.architecture = parts.at(2);
        if (parts.size() > 3) info.section = parts.at(3);
        if (!filter.isEmpty() && !info.name.contains(filter, Qt::CaseInsensitive))
            continue;
        out.append(info);
    }
    return true;
}

bool DnfBackend::simulateInstall(const QString &pkg, QString &resolution, QString &error)
{
    // dnf 干跑：dnf install -y --setopt=tsflags=test <pkg>
    return runQuery({QStringLiteral("dnf"), QStringLiteral("install"), QStringLiteral("-y"),
                     QStringLiteral("--setopt=tsflags=test"), pkg}, resolution, error);
}

bool DnfBackend::listResidualPackages(PackageList &out, QString &error)
{
    // dnf/rpm 无 "rc" 概念；此处返回空（dnf 无残留配置文件机制）。
    Q_UNUSED(out)
    Q_UNUSED(error)
    return true;
}

bool DnfBackend::install(const QStringList &packages, QString &error)
{
    Q_UNUSED(error);
    if (packages.isEmpty())
        return true;
    emit operationProgress(tr("Installing"), 0);
    QStringList args{QStringLiteral("install"), QStringLiteral("-y")};
    args.append(packages);
    // 后台线程执行，避免阻塞 GUI/tray 主线程；结果经 operationFinished 回传。
    runPrivilegedAsync([this, args](QString &out, QString &err) {
        return runPrivileged(args, out, err);
    });
    return true;
}

QVariantMap DnfBackend::backendOptions() const
{
    QVariantMap opts;
    if (m_config) {
        opts.insert(QStringLiteral("autoRemoveOrphans"),
                    m_config->autoRemoveOrphans());
        opts.insert(QStringLiteral("autoCleanCache"),
                    m_config->autoCleanCache());
    }
    return opts;
}

bool DnfBackend::remove(const QStringList &packages, QString &error)
{
    Q_UNUSED(error);
    if (packages.isEmpty())
        return true;
    QStringList args{QStringLiteral("remove"), QStringLiteral("-y")};
    args.append(packages);
    runPrivilegedAsync([this, args](QString &out, QString &err) {
        return runPrivileged(args, out, err);
    });
    return true;
}

bool DnfBackend::purge(const QStringList &packages, QString &error)
{
    // rpm/dnf 没有独立的 "purge"；remove 已同时删除配置（由包 %postun 处理）
    return remove(packages, error);
}

bool DnfBackend::autoremove(QString &error)
{
    Q_UNUSED(error);
    runPrivilegedAsync([this](QString &out, QString &err) {
        return runPrivileged({QStringLiteral("autoremove"), QStringLiteral("-y")}, out, err);
    });
    return true;
}

bool DnfBackend::cleanCache(QString &error)
{
    Q_UNUSED(error);
    runPrivilegedAsync([this](QString &out, QString &err) {
        return runPrivileged({QStringLiteral("clean"), QStringLiteral("all")}, out, err);
    });
    return true;
}

QStringList DnfBackend::cacheDirectories() const
{
    return {QStringLiteral("/var/cache/dnf")};
}

bool DnfBackend::checkRebootRequired(bool &required, QString &error)
{
    required = false;
    // 容器环境无独立内核，跳过以避免误报宿主内核状态。
    if (SystemInfo::isContainer())
        return true;
    // dnf5 `needs-restarting`（不带 -r；-r/--reboothint 在 dnf5 中为兼容保留的无效果选项）
    // 退出码语义：1 = 建议重启（安装了新内核/重要包），0 = 无需重启。
    // 若命令存在，按其退出码判定（正确语义，不依赖 runQuery 的 exit 0 成功约定）。
    if (commandExists(QStringLiteral("needs-restarting"))) {
        QString out;
        int exitCode = -1;
        if (!runProbe({QStringLiteral("needs-restarting")}, out, exitCode))
            return false;  // 命令无法运行
        required = (exitCode == 1);
        return true;
    }
    // 回退：比较 /usr/lib/modules 最新内核与当前运行内核版本（无 needs-restarting 时）。
    const QDir modules(QStringLiteral("/usr/lib/modules"));
    if (modules.exists()) {
        QStringList kernels;
        for (const QFileInfo &fi : modules.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            kernels.append(fi.fileName());
        if (!kernels.isEmpty()) {
            std::sort(kernels.begin(), kernels.end());
            const QString latest = kernels.last();
            const QString running = QSysInfo::kernelVersion();
            if (!running.isEmpty() && running != latest)
                required = true;
            return true;
        }
    }
    return false;  // 无可靠探测手段，视为不支持
}

bool DnfBackend::checkServicesNeedingRestart(QStringList &services, QString &error)
{
    services.clear();
    if (!SystemInfo::hasSystemd() || SystemInfo::isContainer())
        return false;
    // needs-restarting -s 列出因更新需重启的服务（Fedora 系）。
    // 注意：有服务需重启时退出码为 1，故用 runProbe 读取输出，避免漏报。
    if (!commandExists(QStringLiteral("needs-restarting")))
        return false;
    QString raw;
    int exitCode = -1;
    if (!runProbe({QStringLiteral("needs-restarting"), QStringLiteral("-s")}, raw, exitCode))
        return false;
    if (exitCode == 0 && raw.trimmed().isEmpty())
        return true;  // 无服务需重启
    QTextStream stream(&raw);
    QString line;
    while (stream.readLineInto(&line)) {
        const QString s = line.trimmed();
        if (s.isEmpty())
            continue;
        services.append(s.endsWith(QStringLiteral(".service")) ? s.chopped(8) : s);
    }
    return true;
}

bool DnfBackend::checkConfigFilesToReview(QStringList &paths, QString &error)
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
    collectConfigFiles(etc, suffixes, 0, 3, paths);
    return true;
}

bool DnfBackend::checkFailedUnits(QStringList &units, QString &error)
{
    units.clear();
    if (!SystemInfo::hasSystemd() || SystemInfo::isContainer())
        return false;
    QString raw;
    if (!runQuery({QStringLiteral("systemctl"), QStringLiteral("--failed"),
                   QStringLiteral("--no-legend"), QStringLiteral("--no-pager")}, raw, error))
        return false;
    QTextStream stream(&raw);
    QString line;
    while (stream.readLineInto(&line)) {
        const QString s = line.trimmed();
        if (s.isEmpty())
            continue;
        const QString unit = s.split(QStringLiteral(" "), Qt::SkipEmptyParts).first();
        if (!unit.isEmpty())
            units.append(unit);
    }
    return true;
}

void DnfBackend::collectConfigFiles(const QDir &dir, const QStringList &suffixes,
                                    int depth, int maxDepth, QStringList &out)
{
    if (depth > maxDepth)
        return;
    const QFileInfoList entries =
        dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    for (const QFileInfo &fi : entries) {
        if (fi.isDir()) {
            collectConfigFiles(QDir(fi.filePath()), suffixes, depth + 1, maxDepth, out);
        } else {
            for (const QString &suf : suffixes)
                if (fi.fileName().endsWith(suf)) {
                    out.append(fi.filePath());
                    break;
                }
        }
    }
}

}  // namespace DtkUpdate
