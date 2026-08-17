#pragma once

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 flatpak 的沙箱应用商店后端
     *
     * 查询：flatpak remote-ls --updates --columns=application,version,branch
     * 写操作：flatpak update -y [ref]（默认同时更新 system 与 user 安装，可加
     *           --system/--user 限定；flatpak 自身处理 polkit，故 privilegedPrefix 为空）
     *
     * 与 snap 同属沙箱式应用商店：跨发行系、只管沙箱应用、更新不触内核/服务，
     * 四探针全部 support=false。isAvailable 需同时验证 flatpak 命令可用且至少有一个
     * 已配置远端（否则 remote-ls 无意义，且纯净最小化系统可能装了 flatpak 却无任何远端）。
     */
    class FlatpakBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit FlatpakBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Flatpak; }
        QString backendId() const override { return QStringLiteral("flatpak"); }
        QString backendName() const override { return QStringLiteral("Flatpak"); }
        bool isAvailable() const override;
        bool supportsResidualConfig() const override { return false; }
        QVariantMap backendOptions() const override;

        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

      protected:
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;
    };

} // namespace DtkUpdate
