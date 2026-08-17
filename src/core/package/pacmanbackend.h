#pragma once

#include <QDir>
#include <QStringList>

#include "packagebackend.h"

namespace DtkUpdate
{

    /**
     * @brief 基于 pacman 的后端实现（Arch/Manjaro/Artix 等）
     *
     * 系统级后端，与 apt/dnf 同类，强绑定 Arch 系发行版。
     * 所有写操作经 pkexec + pacman 提权（由基类 runPrivileged 调用）。
     *
     * 注意：下列命令与解析为初始实现，可能需要按具体发行版微调。
     */
    class PacmanBackend : public PackageBackend
    {
        Q_OBJECT
      public:
        explicit PacmanBackend(QObject* parent = nullptr);

        BackendType backendType() const override { return BackendType::Pacman; }
        QString backendId() const override { return QStringLiteral("pacman"); }
        QString backendName() const override { return QStringLiteral("Pacman (Arch/Manjaro)"); }
        bool isAvailable() const override;
        bool supportsResidualConfig() const override { return false; }
        QVariantMap backendOptions() const override;

        bool fetchUpgradable(PackageList& out, QString& error) override;
        bool listInstalled(PackageList& out, const QString& filter, QString& error) override;
        bool simulateInstall(const QString& pkg, QString& resolution, QString& error) override;
        bool listResidualPackages(PackageList& out, QString& error) override;
        QStringList cacheDirectories() const override;

        // 预检/后检探针（pacman 系）
        bool checkRebootRequired(bool& required, QString& error) override;

      protected:
        // 写操作经基类 runWriteOperation 模板执行，本类仅描述"操作→参数"
        QStringList operationArgs(Op op, const QStringList& packages, QString& error) override;

        // pacman 写操作经 pkexec + pacman 提权（由基类 runPrivileged 调用）
        QStringList privilegedPrefix() const override
        {
            return {QStringLiteral("pkexec"), QStringLiteral("pacman")};
        }
    };

} // namespace DtkUpdate
