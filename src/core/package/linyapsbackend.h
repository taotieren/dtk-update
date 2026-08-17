#pragma once

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 linyaps/玲珑 (ll-cli) 的沙箱应用包管理后端
     *
     * 玲珑是应用层沙箱包管理器，与系统包管理器（apt/dnf）正交：
     * 负责桌面应用的安装/更新/卸载，不触及系统库与内核。
     *
     * 因此本后端刻意关闭以下系统级探针：
     *  - 内核重启 (checkRebootRequired)：沙箱应用更新不影响内核，返回 support=false
     *  - 需重启的服务 (checkServicesNeedingRestart)：玲珑应用自管理运行态，无系统服务概念
     *  - 待审阅配置文件 (checkConfigFilesToReview)：无 dpkg/ucf 式残留配置
     *  - 失败的系统单元 (checkFailedUnits)：与系统服务无关
     *  - 残余配置清理 (supportsResidualConfig)：返回 false
     *
     * 写操作经 `pkexec ll-cli` 提权（由基类 runPrivileged 拼接 privilegedPrefix）。
     */
    class LinyapsBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit LinyapsBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Linyaps; }
        QString backendId() const override { return QStringLiteral("linyaps"); }
        QString backendName() const override { return QStringLiteral("Linyaps (玲珑)"); }
        bool isAvailable() const override;
        QString availabilityError() const override { return m_availabilityError; }
        bool supportsResidualConfig() const override { return false; }
        QVariantMap backendOptions() const override;

        // ---- 查询 ----
        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;

        // ---- 残余清理 ----
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

        // ---- 预检/后检探针（沙箱应用层，系统级探针禁用）----
        bool checkRebootRequired(bool& required, QString& error) override;
        bool checkServicesNeedingRestart(QStringList& services, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;
        bool checkFailedUnits(QStringList& units, QString& error) override;

        // ---- 写操作：复用基类 runWriteOperation 异步模板，仅映射 ll-cli 命令 ----
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;

      protected:
        // linyaps 写操作经 pkexec + ll-cli 提权（由基类 runPrivileged 调用）
        QStringList privilegedPrefix() const override
        {
            return {QStringLiteral("pkexec"), QStringLiteral("ll-cli")};
        }

      private:
        // 解析 `ll-cli list [--upgradable]` 的机器可读输出为 PackageList
        bool parseList(const QString& raw, PackageList& out, bool onlyUpgradable) const;

        // 最近一次 isAvailable() 探测到的诊断信息（环境异常的具体原因），
        // 供 availabilityError() 返回，便于 UI 提示用户如何修复。
        mutable QString m_availabilityError;
    };

} // namespace DtkUpdate
