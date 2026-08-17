#pragma once

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 snap (snapd) 的沙箱应用商店后端
     *
     * 查询：snap refresh --list
     * 写操作：snap refresh [pkg]（经 snapd 的 polkit 策略自行提权，故 privilegedPrefix 为空）
     *
     * 与 apt/dnf 等系统包管理器正交：跨发行系、只管理沙箱应用、更新不涉及
     * 内核/系统服务，因此四探针（重启/服务/残留配置/失败 unit）全部 support=false。
     * 探测时必须同时验证 snap 命令与 snapd 守护可用（snap 命令存在但 daemon 未起
     * 会虚假可用、运行即挂），故 isAvailable 额外冒烟 snap list。
     */
    class SnapBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit SnapBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Snap; }
        QString backendId() const override { return QStringLiteral("snap"); }
        QString backendName() const override { return QStringLiteral("Snap"); }
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

        // 沙箱后端：命令名由 prefix 提供，snapd 自身 polkit 提权，不套 pkexec。
        QStringList privilegedPrefix() const override;
        bool checkRebootRequired(bool& required, QString& error) override;
        bool checkServicesNeedingRestart(QStringList& services, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;
        bool checkFailedUnits(QStringList& units, QString& error) override;
    };

} // namespace DtkUpdate
