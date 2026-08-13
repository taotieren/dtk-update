#include "systeminfo.h"
#include "logger.h"

#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>

namespace DtkUpdate {
namespace SystemInfo {

bool hasSystemd()
{
    static const bool ok = !QStandardPaths::findExecutable(QStringLiteral("systemctl")).isEmpty();
    return ok;
}

bool isContainer()
{
    // 优先用 systemd-detect-virt（最可靠）
    const QString sd = QStandardPaths::findExecutable(QStringLiteral("systemd-detect-virt"));
    if (!sd.isEmpty()) {
        QProcess proc;
        proc.start(sd, QStringList{QStringLiteral("--container")});
        if (proc.waitForFinished(3000) && proc.exitCode() == 0)
            return true;
        if (proc.exitCode() == 1)
            return false;  // 明确返回"非容器"
        // 其它退出码（命令异常）继续回退判断
    }

    // 回退：读取 /proc/1/cgroup，含 container/docker/lxc 等关键字即视为容器
    QFile f(QStringLiteral("/proc/1/cgroup"));
    if (f.open(QIODevice::ReadOnly)) {
        QTextStream ts(&f);
        const QString content = ts.readAll();
        return content.contains(QStringLiteral("docker"))
            || content.contains(QStringLiteral("lxc"))
            || content.contains(QStringLiteral("container"))
            || content.contains(QStringLiteral("kubepods"));
    }
    return false;
}

}  // namespace SystemInfo
}  // namespace DtkUpdate
