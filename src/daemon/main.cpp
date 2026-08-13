#include "dtkupdated.h"
#include "logger.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>

#include <DLog>
DCORE_USE_NAMESPACE

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dtk-update-daemon"));
    app.setOrganizationName(QStringLiteral("dtk"));

    DtkUpdate::Daemon daemon;
    if (!daemon.registerOnBus()) {
        qCWarning(dtkUpdateDaemon) << "Failed to register on DBus, exiting";
        return 1;
    }
    qCInfo(dtkUpdateDaemon) << "dtk-update daemon started";

    return app.exec();
}
