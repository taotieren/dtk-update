#pragma once

#include <QMetaType>
#include <QString>

namespace DtkUpdate
{

    /**
     * @brief 单个软件包的描述信息（来自 apt / dpkg 解析）
     */
    struct PackageInfo
    {
        QString name;             // 包名
        QString currentVersion;   // 已安装版本（未安装为空）
        QString candidateVersion; // 可升级候选版本
        QString architecture;     // amd64 / all ...
        QString section;          // admin/utils/...
        QString backendId;        // 来源后端 id（"apt"/"dnf"/"linyaps"），用于多后端聚合区分
        bool isInstalled = false;
        bool isUpgradable = false;

        bool operator==(const PackageInfo& o) const { return name == o.name; }
    };
    using PackageList = QList<PackageInfo>;

} // namespace DtkUpdate

Q_DECLARE_METATYPE(DtkUpdate::PackageInfo)
