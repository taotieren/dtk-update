#include <QDebug>
#include <QRegularExpression>

#include "core/dependency/dependencyresolver.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

TEST(DependencyResolverTest, ParseSimulateOutputInstalls)
{
    QString text = "Inst libfoo (1.2-3 stable [amd64])\n"
                   "Conf libfoo (1.2-3 stable [amd64])\n"
                   "Inst libbar (2.0-1 stable [amd64])\n";
    QStringList toInstall, toRemove;
    ASSERT_TRUE(DependencyResolver::parseSimulateOutput(text, toInstall, toRemove));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libfoo")));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libbar")));
    EXPECT_TRUE(toRemove.isEmpty());
}

TEST(DependencyResolverTest, ParseSimulateOutputRemoves)
{
    QString text = "Inst libfoo (1.2-3 stable [amd64])\n"
                   "Remv libbaz (1.0-1 stable [amd64])\n";
    QStringList toInstall, toRemove;
    ASSERT_TRUE(DependencyResolver::parseSimulateOutput(text, toInstall, toRemove));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libfoo")));
    EXPECT_TRUE(toRemove.contains(QStringLiteral("libbaz")));
}

TEST(DependencyResolverTest, ParseSimulateOutputDnfFormat)
{
    // DNF 干跑输出用 Installing:/Removing: 行，且不应被 APT 正则误吞。
    QString text = "Installing: libfoo\n"
                   "Installing: libbar\n"
                   "Removing:  libbaz\n";
    QStringList toInstall, toRemove;
    ASSERT_TRUE(DependencyResolver::parseSimulateOutput(text, toInstall, toRemove, true));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libfoo")));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libbar")));
    EXPECT_TRUE(toRemove.contains(QStringLiteral("libbaz")));
}

TEST(DependencyResolverTest, ParseSimulateOutputDnfIgnoresAptLines)
{
    // dnfFormat=true 时，含 "Inst"/"Remv" 的噪声行不应被 APT 正则误判为事务。
    QString text = "Inst something unrelated\n"
                   "Installing: libfoo\n";
    QStringList toInstall, toRemove;
    ASSERT_TRUE(DependencyResolver::parseSimulateOutput(text, toInstall, toRemove, true));
    EXPECT_TRUE(toInstall.contains(QStringLiteral("libfoo")));
    EXPECT_FALSE(toInstall.contains(QStringLiteral("something")));
}

TEST(DependencyResolverTest, ParseSimulateOutputEmpty)
{
    QStringList toInstall, toRemove;
    EXPECT_FALSE(DependencyResolver::parseSimulateOutput(
        QStringLiteral("Reading package lists... Done\n"), toInstall, toRemove));
    EXPECT_TRUE(toInstall.isEmpty());
    EXPECT_TRUE(toRemove.isEmpty());
}
