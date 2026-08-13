#pragma once

#include <QString>

namespace DtkUpdate {

/**
 * @brief 发行版探测
 * @note 只读 /etc/os-release（标准 freedesktop 规范），不缓存、无副作用。
 */
class DistroProbe {
public:
    // 发行系（按包管理器家族归类）
    enum class Family {
        Debian,    // apt/dpkg: Debian, Ubuntu, deepin, LinuxMint, UOS...
        Fedora,    // dnf/rpm: Fedora, RHEL, CentOS, Rocky, Alma...
        Arch,      // pacman: Arch, Manjaro, EndeavourOS...
        Suse,      // zypper/rpm: openSUSE, SLES
        Unknown
    };

    // 从 /etc/os-release 读取并返回发行系
    static Family detectFamily();

    // 返回原始 ID（如 "debian"、"ubuntu"、"fedora"）
    static QString detectId();

    // 人类可读的家族名
    static QString familyName(Family f);

    // 指定 os-release 路径（用于单测；默认 /etc/os-release）
    static Family detectFamilyFrom(const QString &osReleasePath);
    static QString detectIdFrom(const QString &osReleasePath);
};

}  // namespace DtkUpdate
