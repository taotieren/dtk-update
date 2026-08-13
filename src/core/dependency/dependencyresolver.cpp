#include "dependencyresolver.h"
#include "logger.h"
#include "package/backendfactory.h"

#include <QRegularExpression>

namespace DtkUpdate {

static const QRegularExpression instRe(QStringLiteral(R"(^Inst\s+(\S+))"));
static const QRegularExpression remRe(QStringLiteral(R"(^Remv\s+(\S+))"));

DependencyResolver::DependencyResolver(QObject *parent) : QObject(parent) {}

void DependencyResolver::setBackend(PackageBackend *backend)
{
    m_backend = backend;
}

bool DependencyResolver::resolve(const QString &package, QString &error)
{
    m_toInstall.clear();
    m_toRemove.clear();
    if (!m_backend) {
        error = QStringLiteral("no backend bound");
        return false;
    }
    QString resolution;
    if (!m_backend->simulateInstall(package, resolution, error))
        return false;

    // 按后端类型分流解析；其它后端复用 APT 格式（多数干跑输出兼容）
    return parseSimulateOutput(resolution, m_toInstall, m_toRemove);
}

bool DependencyResolver::parseSimulateOutput(const QString &text,
                                             QStringList &outToInstall,
                                             QStringList &outToRemove)
{
    outToInstall.clear();
    outToRemove.clear();
    const QStringList lines =
        text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    bool found = false;
    for (const QString &line : lines) {
        QRegularExpressionMatch m;
        if ((m = instRe.match(line)).hasMatch()) {
            outToInstall.append(m.captured(1));
            found = true;
        } else if ((m = remRe.match(line)).hasMatch()) {
            outToRemove.append(m.captured(1));
            found = true;
        }
    }
    return found;
}

}  // namespace DtkUpdate
