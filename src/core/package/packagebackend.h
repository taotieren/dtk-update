#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>
#include <functional>

#include "packageinfo.h"

namespace DtkUpdate
{

    class AppConfig;

    Q_NAMESPACE

    /**
     * @brief 包管理器后端类型枚举（用于探测与标识）
     */
    enum class BackendType
    {
        Unknown = 0,
        Apt, ///< Debian/Ubuntu/Deepin/UOS 等 (apt + dpkg)
        Dnf, ///< Fedora/RHEL 等 (dnf + rpm)
        // 后续可扩展：Pacman, Zypper, Portage ...
    };
    Q_ENUM_NS(BackendType)

    /**
     * @brief 包管理后端抽象接口
     *
     * UI 与 tray 仅依赖此接口，不感知具体实现（apt/dpkg、dnf/rpm 等）。
     * 所有涉及修改系统的操作通过 pkexec/polkit 提权执行。
     *
     * 抽象原则：
     * - 接口方法描述"语义操作"（升级/安装/移除/残余清理），而非具体命令；
     * - 发行版相关命令、解析、探测逻辑全部下沉到具体后端实现（如 AptBackend）；
     * - 新增发行版支持仅需实现该接口并在 BackendFactory 注册，无需改动 UI/monitor。
     */
    class PackageBackend : public QObject
    {
        Q_OBJECT
      public:
        explicit PackageBackend(QObject* parent = nullptr) : QObject(parent) {}
        virtual ~PackageBackend() = default;

        /** 绑定全局配置（可选），供后端在读操作选项时读取（如是否安装 recommends） */
        void setConfig(AppConfig* config) { m_config = config; }
        AppConfig* config() const { return m_config; }

        // ---- 后端标识 ----
        virtual BackendType backendType() const = 0;
        virtual QString backendId() const = 0;   // 稳定标识，如 "apt"
        virtual QString backendName() const = 0; // 展示名，如 "APT (Debian/Ubuntu)"

        /**
         * @brief 该后端在当前系统是否可用（命令存在、且非容器等）
         * @note 由 BackendFactory 用于自动选择，也可用于 UI 禁用某些功能
         */
        virtual bool isAvailable() const = 0;

        /** 该后端是否支持"残留配置文件"(如 dpkg 的 rc 状态)。dnf/rpm 无此概念。 */
        virtual bool supportsResidualConfig() const = 0;

        // ---- 预检 / 后检（借鉴 arch-update 的 pre/post update 分离）----
        // 以下方法均为"只读探针"，不改变系统状态，供 UI 在升级前/后展示建议。
        // 默认实现返回 false/support=false，具体后端按需实现；抽象层不强制。

        /** 升级前/后：系统是否需要重启（内核/底层库更新）。返回 support=false 表示后端不支持。 */
        virtual bool checkRebootRequired(bool& required, QString& error)
        {
            Q_UNUSED(required);
            Q_UNUSED(error);
            return false;
        }

        /**
         * 升级前/后：列出需要重启的服务（如升级了 openssh/systemd 但仍在跑旧进程）。
         * 返回 service 名称列表；support=false 表示后端不支持该探测。
         */
        virtual bool checkServicesNeedingRestart(QStringList& services, QString& error)
        {
            Q_UNUSED(services);
            Q_UNUSED(error);
            return false;
        }

        /**
         * 升级后：列出待审阅的配置文件（如 dpkg 的 *.dpkg-new、rpm 的 *.rpmnew）。
         * 仅列出路径，不自动合并；用户应手动审阅（或经专用工具）。
         */
        virtual bool checkConfigFilesToReview(QStringList& paths, QString& error)
        {
            Q_UNUSED(paths);
            Q_UNUSED(error);
            return false;
        }

        /**
         * 升级后：列出处于 failed 状态的 systemd units（升级后某服务起不来）。
         * 容器环境下通常无意义（多为宿主服务），由具体后端按需跳过。
         * 返回 support=false 表示后端/环境不支持该探测。
         */
        virtual bool checkFailedUnits(QStringList& units, QString& error)
        {
            Q_UNUSED(units);
            Q_UNUSED(error);
            return false;
        }

        /**
         * @brief 后端支持的配置项（Key=展示名，Value=可选值说明），供 UI/控制中心展示
         * @note 抽象层只负责描述，具体生效由各后端在对应操作中读取配置。
         */
        virtual QVariantMap backendOptions() const = 0;

        // ---- 查询（无需提权）----
        virtual bool fetchUpgradable(PackageList& out, QString& error) = 0;
        virtual bool listInstalled(PackageList& out, const QString& filter, QString& error) = 0;

        /**
         * @brief 查询包安装将引入的依赖（干跑），返回后端原生输出文本。
         *        解析由 DependencyResolver 负责（其解析逻辑也按后端分流）。
         */
        virtual bool simulateInstall(const QString& pkg, QString& resolution, QString& error) = 0;

        // ---- 残余清理（包管理相关）----
        /** 列出处于"残留配置"(rc) 状态的包 */
        virtual bool listResidualPackages(PackageList& out, QString& error) = 0;

        /**
         * @brief 可安全清理的下载缓存目录
         * @note 默认返回空；具体后端返回对应目录（如 apt 的 /var/cache/apt/archives）
         */
        virtual QStringList cacheDirectories() const = 0;

        // ---- 写操作（内部经 pkexec 提权）----
        virtual bool install(const QStringList& packages, QString& error) = 0;
        virtual bool remove(const QStringList& packages, QString& error) = 0; // 保留配置
        virtual bool purge(const QStringList& packages, QString& error) = 0;  // 删除配置
        virtual bool autoremove(QString& error) = 0;                          // 移除孤儿依赖
        virtual bool cleanCache(QString& error) = 0;                          // 清理下载缓存

      signals:
        void operationProgress(const QString& stage, int percent);
        void operationFinished(bool success, const QString& detail);

      protected:
        /**
         * @brief 运行只读探针命令，无论退出码如何都返回标准输出（供健康检查探针使用）。
         *
         * 与 runQuery 的区别：runQuery 仅在 exit 0 时视为成功；而部分探针命令以退出码
         * 表达语义（如 `needs-restarting` 退出码 1 表示"需要重启"），必须读取其输出与
         * 退出码本身。本方法 start 失败（命令不存在/无法启动）才返回 false；调用方通过
         * *exitCode 判断语义。
         *
         * @param exitCode 输出参数，接收进程退出码（start 失败时为 -1）
         * @return true 表示进程正常结束（无论退出码），false 表示无法启动
         */
        bool runProbe(const QStringList& args, QString& output, int& exitCode) const;

        /**
         * @brief 为 QProcess 注入稳定的 C locale 环境。
         *
         * 输出解析（如 apt 的 "[upgradable from: ...]"、dnf 的 "Available" 表头、
         * apt-get -s 的 "Inst"/"Remv"）依赖程序固定的英文字段锚点，非英文 locale 下
         * 这些锚点会被翻译，导致解析失败、返回空列表（用户看不到任何更新）。强制
         * LC_ALL/LANG=C 让这些字段稳定，且对 apt/dnf/rpm/dpkg-query/systemctl 的
         * 机器可读输出格式无副作用。
         */
        static void applyStableLocale(QProcess& proc)
        {
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
            env.insert(QStringLiteral("LANG"), QStringLiteral("C"));
            env.insert(QStringLiteral("LC_MESSAGES"), QStringLiteral("C"));
            proc.setProcessEnvironment(env);
        }

        /**
         * @brief 在后台线程执行耗时的提权写任务，完成后经 operationFinished 信号回传主线程。
         *
         * 写操作（install/remove/clean 等）原本同步调用 runPrivileged 的
         * QProcess::waitForFinished(-1) 会阻塞 UI/tray 主线程数分钟、冻结桌面。改用本方法
         * 在 QtConcurrent 后台线程执行；install/remove/... 立即返回表示"已启动"，真实结果
         * 通过 operationFinished 信号异步送达。task 签名为 bool(QString &out, QString &err)。
         */
        void runPrivilegedAsync(std::function<bool(QString&, QString&)> task);

        AppConfig* m_config = nullptr;
    };

} // namespace DtkUpdate
