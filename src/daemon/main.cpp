#include <DLog>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDir>
#include <QLockFile>
#include <QStandardPaths>

#include "dtkupdated.h"
#include "logger.h"
DCORE_USE_NAMESPACE

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dtk-update-daemon"));
    app.setOrganizationName(QStringLiteral("dtk"));

    // 单实例：防止多个 daemon 同时抢占 DBus service 名 com.dtk.update.Daemon。
    // 优先用户运行时目录，回退 /tmp。
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QLockFile lock(runtime + QLatin1Char('/') + QStringLiteral("dtk-update-daemon.lock"));
    if (!lock.tryLock())
    {
        qCWarning(dtkUpdateDaemon) << "another dtk-update-daemon is running, exiting";
        return 1;
    }

    DtkUpdate::Daemon daemon;
    if (!daemon.registerOnBus())
    {
        qCWarning(dtkUpdateDaemon) << "Failed to register on DBus, exiting";
        return 1;
    }
    qCInfo(dtkUpdateDaemon) << "dtk-update daemon started";

    return app.exec();
}
