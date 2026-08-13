#include "common/iniparser.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(IniParserTest, GlobalScalars)
{
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("PreferredBackend = apt\n"
                        "NoInstallRecommends = true\n"
                        "AutoCleanCache = false\n",
                        &err))
        << err.toStdString();
    EXPECT_EQ(p.value("PreferredBackend").toStdString(), "apt");
    EXPECT_EQ(p.value("NoInstallRecommends").toStdString(), "true");
    EXPECT_EQ(p.value("AutoCleanCache").toStdString(), "false");
    EXPECT_EQ(p.value("Missing", "def").toStdString(), "def");
}

TEST(IniParserTest, SectionsOverrideGlobals)
{
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("NoInstallRecommends = true\n"
                        "\n"
                        "[apt]\n"
                        "NoInstallRecommends = false\n"
                        "AutoCleanCache = true\n",
                        &err))
        << err.toStdString();
    // 全局键
    EXPECT_EQ(p.value("NoInstallRecommends").toStdString(), "true");
    // 段内键（点号查询）
    EXPECT_EQ(p.value("apt.NoInstallRecommends").toStdString(), "false");
    EXPECT_EQ(p.value("apt.AutoCleanCache").toStdString(), "true");
    // 段内查询优先于全局同名键
    EXPECT_EQ(p.sectionValue("apt", "NoInstallRecommends").toStdString(), "false");
}

TEST(IniParserTest, CommentsAndQuotes)
{
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("# this is a comment\n"
                        "Title = \"quoted value\"   ; inline comment\n"
                        "[dnf]\n"
                        "AutoCleanCache = false # trailing\n",
                        &err))
        << err.toStdString();
    EXPECT_EQ(p.value("Title").toStdString(), "quoted value");
    EXPECT_EQ(p.value("dnf.AutoCleanCache").toStdString(), "false");
}

TEST(IniParserTest, CaseInsensitiveKeys)
{
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("[APT]\n"
                        "NoInstallRecommends = false\n",
                        &err))
        << err.toStdString();
    EXPECT_EQ(p.value("apt.NoInstallRecommends").toStdString(), "false");
    EXPECT_EQ(p.value("APT.NoInstallRecommends").toStdString(), "false");
}

TEST(IniParserTest, InvalidLinesFail)
{
    IniParser p;
    QString err;
    EXPECT_FALSE(p.parse("no equals sign here\n", &err));
    EXPECT_FALSE(p.parse("[]\n", &err));
}

TEST(IniParserTest, CrlfTolerated)
{
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("PreferredBackend = dnf\r\n[apt]\r\nX = 1\r\n", &err))
        << err.toStdString();
    EXPECT_EQ(p.value("PreferredBackend").toStdString(), "dnf");
    EXPECT_EQ(p.value("apt.X").toStdString(), "1");
}

TEST(IniParserTest, EmptySectionValueDoesNotShadowGlobal)
{
    // 段内显式为空字符串（Key =）应视为"命中"并覆盖全局，而非错误回退到默认。
    IniParser p;
    QString err;
    ASSERT_TRUE(p.parse("NoInstallRecommends = true\n"
                        "\n"
                        "[apt]\n"
                        "NoInstallRecommends =\n",
                        &err))
        << err.toStdString();
    EXPECT_EQ(p.value("apt.NoInstallRecommends").toStdString(), "");
    // 段内为空时不应回退到全局 true（曾用 isNull() 误判为空值）
    EXPECT_NE(p.value("apt.NoInstallRecommends").toStdString(), "true");
}
