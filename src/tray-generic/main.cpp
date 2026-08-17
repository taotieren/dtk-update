#include <QApplication>
#include <QLockFile>
#include <QStandardPaths>

#include "common/translator.h"
#include "genericindicator.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    DtkUpdate::loadTranslator(QStringLiteral("dtk-update"));

    // 单实例保护：避免多个托盘进程同时操作同一系统后端
    const QString runDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QString lockPath = (runDir.isEmpty() ? QStringLiteral("/tmp") : runDir) +
                             QStringLiteral("/dtk-update-tray-generic.lock");
    QLockFile lock(lockPath);
    if (!lock.tryLock())
    {
        qCWarning(dtkUpdateTray) << "another instance is running; exiting";
        return 1;
    }

    DtkUpdate::GenericIndicator indicator;
    indicator.show();

    qCInfo(dtkUpdateTray) << "generic (freedesktop) tray started";
    return app.exec();
}
