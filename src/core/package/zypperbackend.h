#pragma once

#include <QDir>
#include <QStringList>

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 zypper/rpm 的后端实现（openSUSE/SLES 等 SUSE 系发行版）
     *
     * 查询：zypper list-updates / rpm -qa
     * 提权写操作：pkexec zypper ...（SUSE 使用 polkit）
     *
     * 所有发行版相关的命令、输出格式解析、可用性探测均已下沉到本类，
     * 上层（UI/monitor/cleanup）无需关心具体发行版。
     *
     * 注意：zypper 基于 rpm，因此 rpm 命令缺失时必须判为不可用（防伪可用）。
     */
    class ZypperBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit ZypperBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Zypper; }
        QString backendId() const override { return QStringLiteral("zypper"); }
        QString backendName() const override { return QStringLiteral("Zypper (openSUSE/SLES)"); }
        bool isAvailable() const override;
        bool supportsResidualConfig() const override { return false; }
        QVariantMap backendOptions() const override;

        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

        // 预检/后检探针（zypper/rpm 系）
        bool checkRebootRequired(bool& required, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;

      protected:
        // 写操作经基类 runWriteOperation 模板执行，本类仅描述"操作→参数"
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;

        // zypper 写操作经 pkexec + zypper 提权（由基类 runPrivileged 调用）
        QStringList privilegedPrefix() const override
        {
            return {QStringLiteral("pkexec"), QStringLiteral("zypper")};
        }
    };

} // namespace DtkUpdate
