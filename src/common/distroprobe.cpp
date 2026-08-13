#include "distroprobe.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace DtkUpdate
{

    DistroProbe::Family DistroProbe::detectFamily()
    {
        return detectFamilyFrom("/etc/os-release");
    }
    QString DistroProbe::detectId()
    {
        return detectIdFrom("/etc/os-release");
    }

    QString DistroProbe::detectIdFrom(const QString& osReleasePath)
    {
        QFile f(osReleasePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        QString id;
        while (!ts.atEnd())
        {
            QString line = ts.readLine().trimmed();
            if (line.startsWith("ID="))
            {
                QString v = line.mid(3).trimmed();
                // 去掉首尾可能的引号
                if (v.startsWith('"') && v.endsWith('"'))
                    v = v.mid(1, v.size() - 2);
                id = v;
            }
        }
        return id;
    }

    DistroProbe::Family DistroProbe::detectFamilyFrom(const QString& osReleasePath)
    {
        QFile f(osReleasePath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return Family::Unknown;
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);
        QString id;
        QStringList idLike;
        while (!ts.atEnd())
        {
            QString line = ts.readLine().trimmed();
            if (line.startsWith("ID="))
            {
                QString v = line.mid(3).trimmed();
                if (v.startsWith('"') && v.endsWith('"'))
                    v = v.mid(1, v.size() - 2);
                id = v;
            }
            else if (line.startsWith("ID_LIKE="))
            {
                QString v = line.mid(9).trimmed();
                if (v.startsWith('"') && v.endsWith('"'))
                    v = v.mid(1, v.size() - 2);
                idLike = v.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            }
        }

        // 优先按 ID_LIKE 归类（家族血缘）
        for (const QString& like : idLike)
        {
            if (like == "debian")
                return Family::Debian;
            if (like == "fedora")
                return Family::Fedora;
            if (like == "archlinux")
                return Family::Arch;
            if (like == "suse")
                return Family::Suse;
        }
        if (id == "debian" || id == "ubuntu" || id == "linuxmint" || id == "deepin" ||
            id == "uos" || id.startsWith("kylin") || id == "raspbian")
            return Family::Debian;
        if (id == "fedora" || id == "rhel" || id == "centos" || id == "rocky" ||
            id == "almalinux" || id == "oracle")
            return Family::Fedora;
        if (id == "arch" || id == "archlinux" || id == "manjaro" || id == "endeavouros")
            return Family::Arch;
        if (id == "opensuse" || id == "opensuse-leap" || id == "opensuse-tumbleweed" ||
            id == "sles")
            return Family::Suse;
        return Family::Unknown;
    }

    QString DistroProbe::familyName(Family f)
    {
        switch (f)
        {
        case Family::Debian:
            return "Debian";
        case Family::Fedora:
            return "Fedora";
        case Family::Arch:
            return "Arch";
        case Family::Suse:
            return "SUSE";
        case Family::Unknown:
            return "Unknown";
        }
        return "Unknown";
    }

} // namespace DtkUpdate
