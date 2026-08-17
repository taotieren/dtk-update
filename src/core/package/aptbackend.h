#pragma once

#include <QDir>
#include <QStringList>

#include "common/systeminfo.h"
#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 apt/dpkg 的后端实现（Debian/Ubuntu/Deepin/UOS 等）
     *
     * 查询：apt list / dpkg-query
     * 提权写操作：pkexec apt-get ...（deepin 使用 polkit）
     *
     * 所有发行版相关的命令、输出格式解析、可用性探测均已下沉到本类，
     * 上层（UI/monitor/cleanup）无需关心具体发行版。
     */
    class AptBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit AptBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Apt; }
        QString backendId() const override { return QStringLiteral("apt"); }
        QString backendName() const override
        {
            return QStringLiteral("APT (Debian/Ubuntu/Deepin)");
        }
        bool isAvailable() const override;
        bool supportsResidualConfig() const override { return true; }
        QVariantMap backendOptions() const override;

      protected:
        QStringList privilegedPrefix() const override
        {
            return {QStringLiteral("pkexec"), QStringLiteral("apt-get")};
        }

        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

        // 预检/后检（apt/dpkg 具体实现）
        bool checkRebootRequired(bool& required, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;

      protected:
        // 写操作经基类 runWriteOperation 模板执行，本类仅描述"操作→参数"
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;
    };

} // namespace DtkUpdate
