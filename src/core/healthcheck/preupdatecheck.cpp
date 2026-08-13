#include "preupdatecheck.h"
#include "logger.h"

namespace DtkUpdate {

PreCheckReport PreUpdateCheck::run(PackageBackend *backend)
{
    PreCheckReport r;
    if (!backend)
        return r;

    bool required = false;
    QString err;
    if (backend->checkRebootRequired(required, err)) {
        r.rebootRequired = required;
        if (required)
            qCInfo(dtkUpdateCore) << "pre-check: reboot required (kernel/base library updated)";
    } else if (!err.isEmpty()) {
        r.error = err;
    }

    QStringList svcs;
    if (backend->checkServicesNeedingRestart(svcs, err)) {
        r.servicesNeedRestart = svcs;
        if (!svcs.isEmpty())
            qCInfo(dtkUpdateCore) << "pre-check: services needing restart:" << svcs;
    }

    QStringList cfgs;
    if (backend->checkConfigFilesToReview(cfgs, err)) {
        r.configFilesToReview = cfgs;
        if (!cfgs.isEmpty())
            qCInfo(dtkUpdateCore) << "pre-check: config files to review:" << cfgs.size();
    }

    QStringList units;
    if (backend->checkFailedUnits(units, err)) {
        r.failedUnits = units;
        if (!units.isEmpty())
            qCInfo(dtkUpdateCore) << "pre-check: failed units:" << units;
    }

    return r;
}

}  // namespace DtkUpdate
