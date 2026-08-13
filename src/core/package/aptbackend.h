#pragma once

#include "packagebackend.h"

#include <QDir>
#include <QStringList>

namespace DtkUpdate {

/**
 * @brief 基于 apt/dpkg 的后端实现（Debian/Ubuntu/Deepin/UOS 等）
 *
 * 查询：apt list / dpkg-query
 * 提权写操作：pkexec apt-get ...（deepin 使用 polkit）
 *
 * 所有发行版相关的命令、输出格式解析、可用性探测均已下沉到本类，
 * 上层（UI/monitor/cleanup）无需关心具体发行版。
 */
class AptBackend : public PackageBackend {
    Q_OBJECT
public:
    explicit AptBackend(QObject *parent = nullptr);

    BackendType backendType() const override { return BackendType::Apt; }
    QString backendId() const override { return QStringLiteral("apt"); }
    QString backendName() const override { return QStringLiteral("APT (Debian/Ubuntu/Deepin)"); }
    bool isAvailable() const override;
    bool supportsResidualConfig() const override { return true; }
    QVariantMap backendOptions() const override;

    bool fetchUpgradable(PackageList &out, QString &error) override;
    bool listInstalled(PackageList &out, const QString &filter, QString &error) override;
    bool simulateInstall(const QString &pkg, QString &resolution, QString &error) override;
    bool listResidualPackages(PackageList &out, QString &error) override;
    QStringList cacheDirectories() const override;

    bool install(const QStringList &packages, QString &error) override;
    bool remove(const QStringList &packages, QString &error) override;
    bool purge(const QStringList &packages, QString &error) override;
    bool autoremove(QString &error) override;
    bool cleanCache(QString &error) override;

    // 预检/后检（apt/dpkg 具体实现）
    bool checkRebootRequired(bool &required, QString &error) override;
    bool checkServicesNeedingRestart(QStringList &services, QString &error) override;
    bool checkConfigFilesToReview(QStringList &paths, QString &error) override;
    bool checkFailedUnits(QStringList &units, QString &error) override;

protected:
    // 运行探针命令（无论退出码均返回输出，供健康检查判断语义）。
    // 与基类 PackageBackend::runProbe 一致的语义；暴露为 protected 便于子类化测试。
    bool runProbe(const QStringList &args, QString &output, int &exitCode) const;

private:
    // 普通用户权限执行查询命令（捕获 stdout，失败填 stderr）
    bool runQuery(const QStringList &args, QString &output, QString &error) const;
    // 以 pkexec 提权执行 apt-get 写操作
    bool runPrivileged(const QStringList &aptArgs, QString &output, QString &error) const;
    // 探测关键命令是否存在
    static bool commandExists(const QString &cmd);
    // 递归扫描 /etc 下待审阅配置文件（*.dpkg-new 等）
    static void collectConfigFiles(const QDir &dir, const QStringList &suffixes,
                                   int depth, int maxDepth, QStringList &out);
};

}  // namespace DtkUpdate
