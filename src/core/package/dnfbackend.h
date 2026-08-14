#pragma once

#include <QDir>
#include <QStringList>

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 dnf/rpm 的后端实现（Fedora/RHEL 等）
     *
     * 这是多后端架构的示例实现，用于验证抽象层可适配非 Debian 系发行版。
     * 与 AptBackend 一样，所有发行版相关命令、解析、探测均下沉到本类。
     *
     * 注意：下列命令与解析为初始实现，可能需要按具体发行版微调。
     */
    class DnfBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit DnfBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Dnf; }
        QString backendId() const override { return QStringLiteral("dnf"); }
        QString backendName() const override { return QStringLiteral("DNF (Fedora/RHEL)"); }
        bool isAvailable() const override;
        bool supportsResidualConfig() const override { return false; }
        QVariantMap backendOptions() const override;

        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

        // 预检/后检探针（dnf/rpm 系）
        bool checkRebootRequired(bool& required, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;

      protected:
        // 写操作经基类 runWriteOperation 模板执行，本类仅描述"操作→参数"
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;

        // dnf 写操作经 pkexec + dnf 提权（由基类 runPrivileged 调用）
        QStringList privilegedPrefix() const override
        {
            return {QStringLiteral("pkexec"), QStringLiteral("dnf")};
        }
    };

} // namespace DtkUpdate
