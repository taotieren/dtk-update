#include "common/backendconfig.h"
#include "common/presetconfig.h"

#include <gtest/gtest.h>

#include <QTemporaryDir>
#include <QFile>
#include <QTextStream>

using namespace DtkUpdate;

namespace {
void writeConf(const QString &path, const QString &content)
{
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << content;
}
}

TEST(BackendConfigTest, PreferredBackendFromConfig)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/backend.conf";
    writeConf(p, "PreferredBackend = dnf\n");
    BackendConfig cfg;
    ASSERT_TRUE(cfg.loadFrom({p}));
    EXPECT_EQ(cfg.preferredBackend().toStdString(), "dnf");
}

TEST(BackendConfigTest, EffectiveBackendFallsToPreset)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/backend.conf";
    // 无效后端 -> 应回退到预设/已注册第一后端
    writeConf(p, "PreferredBackend = notarealbackend\n");
    BackendConfig cfg;
    cfg.loadFrom({p});
    QStringList reg = { "apt", "dnf" };
    QString eff = cfg.effectiveBackend(reg);
    EXPECT_TRUE(reg.contains(eff));
    EXPECT_NE(eff.toStdString(), "notarealbackend");
}

TEST(BackendConfigTest, PerBackendOverride)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/backend.conf";
    writeConf(p, "PreferredBackend = apt\n"
                  "NoInstallRecommends = false\n"   // 全局
                  "\n"
                  "[apt]\n"
                  "NoInstallRecommends = true\n"     // 段级覆盖全局
                  "\n"
                  "[dnf]\n"
                  "AutoCleanCache = true\n");
    BackendConfig cfg;
    cfg.loadFrom({p});
    QVariantMap aptOpts = cfg.optionsFor("apt");
    EXPECT_TRUE(aptOpts["NoInstallRecommends"].toBool());  // 段级胜出
    QVariantMap dnfOpts = cfg.optionsFor("dnf");
    EXPECT_TRUE(dnfOpts["AutoCleanCache"].toBool());
}

TEST(BackendConfigTest, GlobalAppliedToAllBackends)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/backend.conf";
    writeConf(p, "AutoCleanCache = false\n"
                  "[apt]\n"
                  "NoInstallRecommends = true\n");
    BackendConfig cfg;
    cfg.loadFrom({p});
    // 全局项对未显式覆盖的后端仍生效
    EXPECT_FALSE(cfg.optionsFor("dnf")["AutoCleanCache"].toBool());
    EXPECT_TRUE(cfg.optionsFor("apt")["NoInstallRecommends"].toBool());
    EXPECT_FALSE(cfg.optionsFor("apt")["AutoCleanCache"].toBool());  // 全局 false 生效
}

TEST(BackendConfigTest, MissingFileReturnsFalse)
{
    BackendConfig cfg;
    EXPECT_FALSE(cfg.loadFrom({"/no/such/file.conf"}));
}
