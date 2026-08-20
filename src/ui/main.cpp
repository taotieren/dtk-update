#include <DApplication>
#include <DLog>

#include "common/appconfig.h"
#include "common/logger.h"
#include "common/translator.h"
#include "core/package/backendfactory.h"
#include "core/security/securityadvisor.h"
#include "mainwindow.h"
#if QT_VERSION_MAJOR >= 6
#include <LogManager.h>
#else
#include <DLogManager>
#endif

#include <QDir>
#include <QLockFile>
#include <QPointer>
#include <QStandardPaths>

DWIDGET_USE_NAMESPACE
DCORE_USE_NAMESPACE

int main(int argc, char* argv[])
{
    DApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("dtk-update"));
    app.setApplicationDisplayName(QObject::tr("Dtk Update"));
    app.setApplicationVersion(QStringLiteral("0.0.1"));
    app.loadTranslator();

    DLogManager::registerConsoleAppender();
    DLogManager::registerFileAppender();

    DtkUpdate::loadTranslator(QStringLiteral("dtk-update"));

    // 单实例：防止多个 GUI 同时启动互相干扰（与 daemon/tray 同范式，优先 RuntimeLocation 回退
    // /tmp）。
    QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtime.isEmpty())
        runtime = QDir::tempPath();
    QLockFile guiLock(runtime + QLatin1Char('/') + QStringLiteral("dtk-update-gui.lock"));
    guiLock.setStaleLockTime(0);
    if (!guiLock.tryLock())
    {
        qCWarning(dtkUpdateUi) << "another dtk-update-gui is running, exiting";
        return 1;
    }

    // 配置透明化：--show-config 打印合并后生效配置并退出（不启动 GUI）
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--show-config"))
    {
        DtkUpdate::AppConfig config;
        fprintf(stdout, "%s", config.showConfig().toUtf8().constData());
        return 0;
    }

    DtkUpdate::AppConfig config;
    DtkUpdate::PackageBackend* backend =
        DtkUpdate::BackendFactory::createBackend(&app, config.preferredBackend());
    if (backend)
        backend->setConfig(&config);
    DtkUpdate::SecurityAdvisor advisor;
    // 是否到上游官方源获取公告，完全由用户配置决定（默认关闭，用户可开）
    advisor.setFetchUpstream(config.fetchUpstreamAdvisories());
    QPointer<DtkUpdate::MainWindow> w = new DtkUpdate::MainWindow(backend, &config, &advisor);
    w->show();

    return app.exec();
}
