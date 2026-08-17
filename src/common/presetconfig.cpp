#include "presetconfig.h"

namespace DtkUpdate
{

    QStringList PresetConfig::knownBackendIds()
    {
        // 与 BackendFactory::registry() 保持一致；此处仅用于配置文件合法性校验。
        // 沙箱式应用商店（linyaps/snap/flatpak）跨发行系、与系统包管理正交，均列入已知后端。
        return {"apt", "dnf", "linyaps", "snap", "flatpak"};
    }

    QString PresetConfig::defaultBackendFor(DistroProbe::Family family)
    {
        switch (family)
        {
        case DistroProbe::Family::Debian:
            return "apt";
        case DistroProbe::Family::Fedora:
            return "dnf";
        case DistroProbe::Family::Arch:
            return "pacman"; // 预留，尚未实现后端
        case DistroProbe::Family::Suse:
            return "zypper"; // 预留，尚未实现后端
        case DistroProbe::Family::Unknown:
        default:
            return QString();
        }
    }

    QVariantMap PresetConfig::defaultOptionsFor(const QString& backendId,
                                                DistroProbe::Family family)
    {
        Q_UNUSED(family); // 当前各后端默认选项与发行系无关，预留 family 以便后续差异化
        QVariantMap opts;
        if (backendId == "apt")
        {
            // Debian 系普遍建议不安装推荐包，减少冗余依赖
            opts["NoInstallRecommends"] = true;
            opts["AutoRemoveOrphans"] = true;
            opts["AutoCleanCache"] = false;
        }
        else if (backendId == "dnf")
        {
            opts["NoInstallRecommends"] = false; // dnf 默认即不装弱依赖之外
            opts["AutoRemoveOrphans"] = true;
            opts["AutoCleanCache"] = false;
        }
        else
        {
            // 未知后端给出保守默认
            opts["NoInstallRecommends"] = false;
            opts["AutoRemoveOrphans"] = false;
            opts["AutoCleanCache"] = false;
        }
        return opts;
    }

} // namespace DtkUpdate
