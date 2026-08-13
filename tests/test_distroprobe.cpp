#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "common/distroprobe.h"

#include <gtest/gtest.h>

using namespace DtkUpdate;

namespace
{
    void writeOsRelease(const QString& path, const QString& content)
    {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        ts << content;
    }
} // namespace

TEST(DistroProbeTest, DetectDebianFamily)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/os-release";
    writeOsRelease(p, "ID=debian\nID_LIKE=debian\n");
    EXPECT_EQ(DistroProbe::detectFamilyFrom(p), DistroProbe::Family::Debian);
    EXPECT_EQ(DistroProbe::detectIdFrom(p).toStdString(), "debian");
}

TEST(DistroProbeTest, DetectUbuntuViaIdLike)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/os-release";
    // Ubuntu os-release 通常有 ID_LIKE=debian
    writeOsRelease(p, "ID=ubuntu\nID_LIKE=debian\n");
    EXPECT_EQ(DistroProbe::detectFamilyFrom(p), DistroProbe::Family::Debian);
}

TEST(DistroProbeTest, DetectFedora)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/os-release";
    writeOsRelease(p, "ID=fedora\nID_LIKE=\"fedora\"\n");
    EXPECT_EQ(DistroProbe::detectFamilyFrom(p), DistroProbe::Family::Fedora);
}

TEST(DistroProbeTest, DetectArch)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/os-release";
    writeOsRelease(p, "ID=arch\nID_LIKE=archlinux\n");
    EXPECT_EQ(DistroProbe::detectFamilyFrom(p), DistroProbe::Family::Arch);
}

TEST(DistroProbeTest, Unknown)
{
    QTemporaryDir dir;
    QString p = dir.path() + "/os-release";
    writeOsRelease(p, "ID=someweirdos\n");
    EXPECT_EQ(DistroProbe::detectFamilyFrom(p), DistroProbe::Family::Unknown);
}
