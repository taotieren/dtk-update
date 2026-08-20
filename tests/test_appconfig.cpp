#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVector>

#include "common/appconfig.h"

#include <gtest/gtest.h>

namespace
{
    // 读取 DConfig schema 文件（编译期注入路径）中的键集合（contents 对象的键），
    // 用于防 schema 漂移：映射表里的键若不在 schema 中即漂移，需告警。
    QSet<QString> schemaKeys()
    {
        QSet<QString> keys;
#ifdef DTK_UPDATE_DCONFIG_SCHEMA
        QFile f(QStringLiteral(DTK_UPDATE_DCONFIG_SCHEMA));
        if (!f.open(QIODevice::ReadOnly))
            return keys;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonObject contents = doc.object().value(QStringLiteral("contents")).toObject();
        for (auto it = contents.constBegin(); it != contents.constEnd(); ++it)
            keys.insert(it.key());
#endif
        return keys;
    }
} // namespace

// backend.conf(PascalCase) → DConfig schema(camelCase) 键映射必须与 schema 文件一致。
// 回归：此前 boolOption 用 key.toLower() 查 keyList()（全小写），而 schema 键是 camelCase，
// keyList().contains 大小写敏感永不命中 → DConfig 层 5 个布尔开关全部静默失效回落到默认值。
TEST(AppConfigTest, DConfigKeyMappingMatchesSchema)
{
    const QSet<QString> schema = schemaKeys();
    ASSERT_FALSE(schema.isEmpty())
        << "无法读取 DConfig schema 文件（编译定义 DTK_UPDATE_DCONFIG_SCHEMA 缺失或路径错误）";

    const QVector<QString> backendConfKeys{
        QStringLiteral("NoInstallRecommends"), QStringLiteral("AutoRemoveOrphans"),
        QStringLiteral("AutoCleanCache"), QStringLiteral("ShowSecurityAdvisory"),
        QStringLiteral("FetchUpstreamAdvisories")};
    const QVector<QString> expected{
        QStringLiteral("noInstallRecommends"), QStringLiteral("autoRemoveOrphans"),
        QStringLiteral("autoCleanCache"), QStringLiteral("showSecurityAdvisory"),
        QStringLiteral("fetchUpstreamAdvisories")};
    for (int i = 0; i < backendConfKeys.size(); ++i)
    {
        const QString dkey = DtkUpdate::AppConfig::dConfigKeyFor(backendConfKeys[i]);
        EXPECT_EQ(dkey, expected[i])
            << backendConfKeys[i].toStdString() << " 的 DConfig 映射键不正确";
        EXPECT_TRUE(schema.contains(dkey))
            << "DConfig 映射键 " << dkey.toStdString() << " 不在 schema contents 中（schema 漂移）";
    }
}

// 未映射的键按原样返回（兜底，不破坏既有查询行为）
TEST(AppConfigTest, DConfigKeyMappingPassthroughForUnknown)
{
    EXPECT_EQ(DtkUpdate::AppConfig::dConfigKeyFor(QStringLiteral("UnknownOption")),
              QStringLiteral("UnknownOption"));
    EXPECT_EQ(DtkUpdate::AppConfig::dConfigKeyFor(QString()), QString());
}
