#pragma once

#include "common/distroprobe.h"
#include "packagebackend.h"

namespace DtkUpdate
{

    class UpdateMonitor;
    class AppConfig;

    /**
     * @brief 包管理后端工厂
     *
     * 负责根据当前系统探测并创建合适的 PackageBackend 实例。
     * 上层（daemon/tray/ui）只需调用 createBackend() 即可获得与发行版匹配的后端，
     * 无需关心具体是 apt 还是 dnf。
     *
     * 扩展新发行版：在 createBackend() 中追加对应后端的可用性探测与实例化即可。
     */
    class BackendFactory
    {
      public:
        /**
         * @brief 创建当前系统首选的后端
         * @param parent 父对象（可选）
         * @param preferredId 首选后端 id（如 "apt"/"dnf"）；为空时按发行版预设自动探测
         * @return 若 preferredId 指定且可用则用它；否则优先选发行版预设后端，
         *         再回退其它可用后端；都不可用返回 nullptr（绝不静默选错后端）
         */
        static PackageBackend* createBackend(QObject* parent = nullptr,
                                             const QString& preferredId = QString());

        /**
         * @brief 同上，但显式传入发行系（避免重复探测 / 测试可控）
         */
        static PackageBackend* createBackend(DistroProbe::Family family, QObject* parent = nullptr,
                                             const QString& preferredId = QString());

        /**
         * @brief 创建当前系统所有可用后端（多后端）。
         *
         * 与 createBackend() 不同，此方法返回"系统后端 + 可选玲珑(linyaps)"的组合，
         * 而非只选一个：玲珑是跨发行版的沙箱应用层包管理器，与系统包管理器(apt/dnf)正交，
         * 无论发行系为何都会被独立探测——只要 ll-cli 运行环境健康就会加入结果。
         *
         * 上层（如更新管理器 UI）可据此同时展示系统包与玲珑沙箱应用的更新。
         *
         * @return 可用后端列表（至少含一个系统后端，可能额外含 linyaps）；都不可用则空列表
         */
        static QList<PackageBackend*> createBackends(QObject* parent = nullptr,
                                                     const QString& preferredId = QString());

        /**
         * @brief 同上，但显式传入发行系（避免重复探测 / 测试可控）
         */
        static QList<PackageBackend*> createBackends(DistroProbe::Family family,
                                                     QObject* parent = nullptr,
                                                     const QString& preferredId = QString());

        /**
         * @brief 按 backendId 强制创建指定后端（用于测试或手动选择）
         * @param id 如 "apt" / "dnf" / "linyaps"
         */
        static PackageBackend* createById(const QString& id, QObject* parent = nullptr);

        /**
         * @brief 列出当前系统所有可用的后端 id（调试/UI 展示用）
         * @note 包含跨发行系的 linyaps（若 ll-cli 环境健康）
         */
        static QStringList availableBackendIds();

        /**
         * @brief 将可选的玲珑(linyaps)沙箱后端接入 UpdateMonitor
         *
         * 玲珑与系统级后端正交：无条件探测 ll-cli 运行环境，健康则接入 monitor 参与更新聚合，
         * 不可用则直接丢弃（不接入）。集中此逻辑以消除 GUI / 各托盘重复的接入样板。
         * @param monitor 目标 UpdateMonitor（不可为空）
         * @param config  配置（可为空；非空则转发给后端 setConfig）
         * @param parent  创建的 linyaps 后端父对象（通常为调用方 this）
         * @return 接入的 linyaps 实例，或 nullptr（未接入）；调用方可保存以便生命周期管理
         */
        static PackageBackend* attachLinyaps(UpdateMonitor* monitor, AppConfig* config = nullptr,
                                             QObject* parent = nullptr);

        /**
         * @brief 将可选的沙箱应用商店后端（linyaps/snap/flatpak）接入 UpdateMonitor
         *
         * 它们与系统级后端正交：各自无条件探测运行环境，健康则接入 monitor 参与更新聚合，
         * 不可用则直接丢弃（不接入）。集中此逻辑以消除 GUI / 各托盘重复的接入样板。
         * 新代码应优先调用本方法而非逐个 attachLinyaps/attachSnap/...。
         * @param monitor 目标 UpdateMonitor（不可为空）
         * @param config  配置（可为空；非空则转发给后端 setConfig）
         * @param parent  创建的沙箱后端父对象（通常为调用方 this）
         */
        static void attachSandboxBackends(UpdateMonitor* monitor, AppConfig* config = nullptr,
                                          QObject* parent = nullptr);
    };

} // namespace DtkUpdate
