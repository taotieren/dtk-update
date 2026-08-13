#pragma once

#include <QString>

namespace DtkUpdate
{

    /**
     * @brief 轻量系统环境探测（跨后端/跨发行版通用）
     *
     * 这些方法不依赖具体包管理器，用于健康检查的"环境前置判断"，
     * 避免在非预期环境下误报（如容器内误判需重启）。
     */
    namespace SystemInfo
    {

        /**
         * @brief 当前是否运行在容器内（如 docker/lxc/systemd-nspawn）
         *
         * 通过 systemd-detect-virt --container 探测；若该命令不可用则回退到
         * 读取 /proc/1/cgroup 中是否含 container 关键字。容器环境下：
         *  - 内核待重启检查无意义（无独立内核）→ 跳过
         *  - 失败的 systemd units 多为宿主服务，不应归咎于本次升级 → 跳过
         */
        bool isContainer();

        /** 是否存在 systemd（systemctl 可用），用于决定是否采用 systemd 探针 */
        bool hasSystemd();

    } // namespace SystemInfo

} // namespace DtkUpdate
