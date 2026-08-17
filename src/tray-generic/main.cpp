#include <QApplication>
#include <QLockFile>
#include <QStandardPaths>

#include "common/translator.h"
#include "genericindicator.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    // 资源初始化：本可执行文件直接编译了 resources.qrc，须在全局命名空间显式初始化，
    // 否则 QSystemTrayIcon 的回退图标（:/icons/*.svg）在运行时取不到。
    Q_INIT_RESOURCE(resources);

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
