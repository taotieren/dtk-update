#include "dependencyresolver.h"

#include <QRegularExpression>

#include "logger.h"
#include "package/backendfactory.h"
#include "package/packagebackend.h"

namespace DtkUpdate
{

    // APT 格式：apt-get install -s 输出 `Inst <pkg>` / `Remv <pkg>`
    static const QRegularExpression instRe(QStringLiteral(R"(^Inst\s+(\S+))"));
    static const QRegularExpression remRe(QStringLiteral(R"(^Remv\s+(\S+))"));
    // DNF 格式：dnf install --assumeno 输出 `Installing: <pkg>` / `Removing: <pkg>`
    static const QRegularExpression dnfInstRe(QStringLiteral(R"(^Installing:\s+(\S+))"));
    static const QRegularExpression dnfRemRe(QStringLiteral(R"(^Removing:\s+(\S+))"));

    DependencyResolver::DependencyResolver(QObject* parent) : QObject(parent) {}

    void DependencyResolver::setBackend(PackageBackend* backend)
    {
        m_backend = backend;
    }

    bool DependencyResolver::resolve(const QString& package, QString& error)
    {
        m_toInstall.clear();
        m_toRemove.clear();
        if (!m_backend)
        {
            error = QStringLiteral("no backend bound");
            return false;
        }
        QString resolution;
        if (!m_backend->simulateInstall(package, resolution, error))
            return false;

        // 按后端格式分流解析。APT/DNF 有结构化干跑输出；玲珑(ll-cli)无事务摘要，
        // 无法结构化解析。若某格式解析无结构化行（格式未识别或已满足依赖），
        // 降级为"仅安装目标包"而非误报失败，避免对 dnf/linyaps 的假阴性。
        const QString id = m_backend->backendId();
        const bool isDnf = id.compare(QStringLiteral("dnf"), Qt::CaseInsensitive) == 0;
        if (parseSimulateOutput(resolution, m_toInstall, m_toRemove, isDnf))
            return true;

        qCInfo(dtkUpdateCore) << "dependency dry-run produced no structured transaction lines"
                              << "for backend" << id << "- falling back to target package only";
        m_toInstall.append(package);
        return true;
    }

    bool DependencyResolver::parseSimulateOutput(const QString& text, QStringList& outToInstall,
                                                 QStringList& outToRemove, bool dnfFormat)
    {
        outToInstall.clear();
        outToRemove.clear();
        const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        bool found = false;
        for (const QString& line : lines)
        {
            QRegularExpressionMatch m;
            if ((m = instRe.match(line)).hasMatch())
            {
                outToInstall.append(m.captured(1));
                found = true;
            }
            else if ((m = remRe.match(line)).hasMatch())
            {
                outToRemove.append(m.captured(1));
                found = true;
            }
            else if (dnfFormat && (m = dnfInstRe.match(line)).hasMatch())
            {
                outToInstall.append(m.captured(1));
                found = true;
            }
            else if (dnfFormat && (m = dnfRemRe.match(line)).hasMatch())
            {
                outToRemove.append(m.captured(1));
                found = true;
            }
        }
        return found;
    }

} // namespace DtkUpdate
