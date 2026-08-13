#pragma once

#include <QString>

#include "packageinfo.h"

namespace DtkUpdate
{

    /**
     * @brief APT/dpkg 输出解析（纯函数，无进程依赖，便于单测）
     * @note 仅负责 APT 系后端格式解析；其它后端的解析在其自身实现内完成。
     */
    class PackageParser
    {
      public:
        // 解析 `apt list --upgradable` 的逐行文本
        static bool parseUpgradableLine(const QString& line, PackageInfo& out);

        // 解析 `dpkg-query -W -f '${Package}\t${Version}\t${Arch}\t${Status}\t${Section}'` 行
        static bool parseDpkgLine(const QString& line, PackageInfo& out);
    };

} // namespace DtkUpdate
