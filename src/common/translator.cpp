#include "translator.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

namespace DtkUpdate
{

    /**
     * @brief 从安装目录加载应用翻译文件
     *
     * dde-tray-loader 不会自动加载第三方插件翻译，插件/应用必须自行持有
     * QTranslator 并 installTranslator。翻译 basename 与 applicationName 一致。
     */
    void loadTranslator(const QString& appName)
    {
        auto* translator = new QTranslator(QCoreApplication::instance());
        const QStringList dirs = {
            QDir::currentPath(),
            QCoreApplication::applicationDirPath(),
            QStringLiteral("/usr/share/dtk-update/translations"),
            QStringLiteral("/usr/local/share/dtk-update/translations"),
            QLibraryInfo::location(QLibraryInfo::TranslationsPath),
        };
        for (const auto& dir : dirs)
        {
            if (translator->load(QLocale::system(), appName, QStringLiteral("_"), dir))
            {
                QCoreApplication::installTranslator(translator);
                return;
            }
        }
        delete translator; // 无翻译文件时释放，避免泄漏
    }

} // namespace DtkUpdate
