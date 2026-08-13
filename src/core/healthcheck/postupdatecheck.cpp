#include "postupdatecheck.h"

#include <QDir>
#include <QDirIterator>

#include "logger.h"

namespace DtkUpdate
{

    namespace
    {

        // 递归统计目录占用字节数（用于估算可清理缓存）
        qint64 dirSizeBytes(const QString& path)
        {
            qint64 total = 0;
            QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext())
            {
                it.next();
                total += it.fileInfo().size();
            }
            return total;
        }

    } // namespace

    PostCheckReport PostUpdateCheck::run(PackageBackend* backend)
    {
        PostCheckReport r;
        if (!backend)
            return r;

        bool required = false;
        QString err;
        if (backend->checkRebootRequired(required, err))
        {
            r.rebootRequired = required;
            if (required)
                qCInfo(dtkUpdateCore) << "post-check: reboot required after update";
        }
        else if (!err.isEmpty())
        {
            r.error = err;
        }

        QStringList svcs;
        if (backend->checkServicesNeedingRestart(svcs, err))
        {
            r.servicesNeedRestart = svcs;
            if (!svcs.isEmpty())
                qCInfo(dtkUpdateCore) << "post-check: services needing restart:" << svcs;
        }

        QStringList cfgs;
        if (backend->checkConfigFilesToReview(cfgs, err))
        {
            r.configFilesToReview = cfgs;
            if (!cfgs.isEmpty())
                qCInfo(dtkUpdateCore) << "post-check: config files to review:" << cfgs.size();
        }

        QStringList units;
        if (backend->checkFailedUnits(units, err))
        {
            r.failedUnits = units;
            if (!units.isEmpty())
                qCInfo(dtkUpdateCore) << "post-check: failed units:" << units;
        }

        // 残留配置(rc)/孤儿包 + 可清理缓存：并入完成页，提示用户清理（不自动执行）
        PackageList residual;
        if (backend->listResidualPackages(residual, err))
        {
            r.residualPackages.reserve(residual.size());
            for (const auto& p : residual)
                r.residualPackages.append(p.name);
            if (!r.residualPackages.isEmpty())
                qCInfo(dtkUpdateCore)
                    << "post-check: residual/orphan packages:" << r.residualPackages.size();
        }
        qint64 cache = 0;
        for (const QString& dir : backend->cacheDirectories())
        {
            if (QDir(dir).exists())
                cache += dirSizeBytes(dir);
        }
        r.cleanableCacheBytes = cache;
        if (cache > 0)
            qCInfo(dtkUpdateCore) << "post-check: cleanable cache bytes:" << cache;

        return r;
    }

} // namespace DtkUpdate
