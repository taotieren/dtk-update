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
        bool checkServicesNeedingRestart(QStringList& services, QString& error) override;
        bool checkConfigFilesToReview(QStringList& paths, QString& error) override;
        bool checkFailedUnits(QStringList& units, QString& error) override;

        bool install(const QStringList& packages, QString& error) override;
        bool remove(const QStringList& packages, QString& error) override;
        bool purge(const QStringList& packages, QString& error) override;
        bool autoremove(QString& error) override;
        bool cleanCache(QString& error) override;

      protected:
        // 运行只读探针命令（无论退出码均返回输出，供健康检查判断语义）。
        // 与基类 PackageBackend::runProbe 一致的语义；暴露为 protected 便于子类化测试。
        bool runProbe(const QStringList& args, QString& output, int& exitCode) const;

      private:
        bool runQuery(const QStringList& args, QString& output, QString& error) const;
        bool runPrivileged(const QStringList& dnfArgs, QString& output, QString& error) const;
        static bool commandExists(const QString& cmd);
        static void collectConfigFiles(const QDir& dir, const QStringList& suffixes, int depth,
                                       int maxDepth, QStringList& out);
    };

} // namespace DtkUpdate
