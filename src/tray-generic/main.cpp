#include <QApplication>

#include "common/translator.h"
#include "genericindicator.h"
#include "logger.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    DtkUpdate::loadTranslator(QStringLiteral("dtk-update"));

    DtkUpdate::GenericIndicator indicator;
    indicator.show();

    qCInfo(dtkUpdateTray) << "generic (freedesktop) tray started";
    return app.exec();
}
