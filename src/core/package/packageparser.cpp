#include "packageparser.h"

#include <QRegularExpression>

namespace DtkUpdate {

bool PackageParser::parseUpgradableLine(const QString &line, PackageInfo &out)
{
    // 形如: firefox/stable 120.0.1 amd64 [upgradable from: 119.0]
    static const QRegularExpression re(
        QStringLiteral(R"(^(\S+?)/(\S+)\s+([^\s]+)\s+(\S+)\s+\[upgradable from:\s*([^\]]+)\]$)"));
    const auto m = re.match(line);
    if (!m.hasMatch())
        return false;

    out.name = m.captured(1);
    out.repository = m.captured(2);
    out.candidateVersion = m.captured(3);
    out.architecture = m.captured(4);
    out.currentVersion = m.captured(5);
    out.isInstalled = true;
    out.isUpgradable = true;
    return true;
}

bool PackageParser::parseDpkgLine(const QString &line, PackageInfo &out)
{
    const QStringList f = line.split(QStringLiteral("\t"));
    if (f.size() < 5)
        return false;
    if (!f[3].startsWith(QStringLiteral("ii")))
        return false;  // 仅返回已安装包

    out.name = f[0];
    out.currentVersion = f[1];
    out.architecture = f[2];
    out.section = f[4];
    out.isInstalled = true;
    return true;
}

}  // namespace DtkUpdate
