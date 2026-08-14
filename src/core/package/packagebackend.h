#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVariantMap>
#include <functional>

#include "common/systeminfo.h"
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
        Apt,     ///< Debian/Ubuntu/Deepin/UOS 等 (apt + dpkg)
        Dnf,     ///< Fedora/RHEL 等 (dnf + rpm)
        Linyaps, ///< 玲珑 (ll-cli) 沙箱应用包管理
        // 后续可扩展：Pacman, Zypper, Portage ...
    };
    Q_ENUM_NS(BackendType)

    /**
     * @brief 写操作语义枚举，用于基类模板方法消除各后端重复实现。
     * @see PackageBackend::operationArgs / runWriteOperation
     */
    enum class Op
    {
        Install,    ///< 安装并保留配置
        Remove,     ///< 移除但保留配置
        Purge,      ///< 移除并删除配置
        Autoremove, ///< 清理孤儿依赖
        CleanCache, ///< 清理下载缓存
    };
    Q_ENUM_NS(Op)

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
         * @brief 该后端在当前系统是否可用（命令存在、且环境健康）
         * @note 由 BackendFactory 用于自动选择，也可用于 UI 禁用某些功能
         */
        virtual bool isAvailable() const = 0;

        /**
         * @brief 不可用时的诊断信息（供 UI 提示用户"为什么不能用 / 出了什么错"）。
         *
         * 仅当 isAvailable() 返回 false 时才有意义；可用时返回空字符串。
         * 子类应在探测过程中把"命令存在却跑不起来 / 环境损坏 / 权限不足"等具体
         * 原因写入此字段，便于上层向用户给出可执行的修复建议，而不是笼统地说
         * "后端不可用"。默认实现返回空。
         */
        virtual QString availabilityError() const { return QString(); }

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
            services.clear();
            Q_UNUSED(error);
            // 通用实现：needs-restarting -s 列出因更新需重启的服务（Debian/Ubuntu/Fedora
            // 系命令与解析一致）。容器内 systemd 不管理宿主服务，跳过以免误报。
            // 退出码非 0（有服务需重启）时用 runProbe 读取输出，避免漏报。
            if (!SystemInfo::hasSystemd() || SystemInfo::isContainer())
                return false;
            if (!commandExists(QStringLiteral("needs-restarting")))
                return false;
            QString raw;
            int exitCode = -1;
            if (!runProbe({QStringLiteral("needs-restarting"), QStringLiteral("-s")}, raw,
                          exitCode))
                return false;
            if (exitCode == 0 && raw.trimmed().isEmpty())
                return true; // 无服务需重启
            services = parseServiceList(raw);
            return true;
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
            units.clear();
            Q_UNUSED(error);
            // 通用实现：systemctl --failed 解析（apt/dnf 完全一致）。
            // 容器内多为宿主服务，failed unit 不应归咎于本次升级，跳过。
            if (!SystemInfo::hasSystemd() || SystemInfo::isContainer())
                return false;
            QString raw;
            if (!runQuery({QStringLiteral("systemctl"), QStringLiteral("--failed"),
                           QStringLiteral("--no-legend"), QStringLiteral("--no-pager")},
                          raw, error))
                return false;
            units = parseFailedUnits(raw);
            return true;
        }

        /**
         * @brief 后端支持的配置项（Key=展示名，Value=可选值说明），供 UI/控制中心展示
         * @note 抽象层只负责描述，具体生效由各后端在对应操作中读取配置。
         */
        /**
         * @brief 后端支持的配置项（Key=展示名，Value=可选值说明），供 UI/控制中心展示。
         *        默认实现读取 m_config 的通用布尔开关（noInstallRecommends /
         *        autoRemoveOrphans / autoCleanCache），子类可按需增删（如 dnf 无
         *        noInstallRecommends 概念则移除该 key）。
         */
        virtual QVariantMap backendOptions() const;

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
        // 基类提供默认实现：经 operationArgs() 取得该操作的命令参数后统一交由
        // runWriteOperation() 后台执行。子类仅需覆盖 operationArgs() 描述"操作→参数"，
        // 不再各自重复 install/remove/... 的异步骨架（模板方法模式）。
        // linyaps 因沙箱同步语义不同，仍按需覆盖。
        virtual bool install(const QStringList& packages, QString& error);
        virtual bool remove(const QStringList& packages, QString& error); // 保留配置
        virtual bool purge(const QStringList& packages, QString& error);  // 删除配置
        virtual bool autoremove(QString& error);                          // 移除孤儿依赖
        virtual bool cleanCache(QString& error);                          // 清理下载缓存

        /**
         * @brief 返回某写操作对应的本机包管理器参数（不含提权前缀）。
         *        子类唯一需要实现的"写操作差异点"，供 runWriteOperation 复用。
         *        默认实现返回空（表示不支持该操作）。
         */
        virtual QStringList operationArgs(Op op, const QStringList& packages, QString& error);

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
         * @brief 运行只读查询命令，仅在 exit 0 时视为成功（输出存入 output）。
         * @return true 表示命令存在且成功执行（exit 0）；false 表示失败或命令缺失。
         */
        bool runQuery(const QString& command, const QStringList& args, QString& output,
                      QString& error, int timeoutMs = 30000) const;

        /** 便利重载：命令与参数打包为单个 QStringList（如 {"apt","list","--upgradable"}）。 */
        bool runQuery(const QStringList& args, QString& output, QString& error) const
        {
            if (args.isEmpty())
                return false;
            return runQuery(args.first(), args.mid(1), output, error, 120000);
        }

        /** 判断命令是否存在于 PATH 中（静态工具方法）。 */
        static bool commandExists(const QString& command);

        /**
         * @brief 经提权前缀执行写操作（如 pkexec apt-get / pkexec dnf）。
         *
         * 具体的提权命令前缀由各后端通过 privilegedPrefix() 提供；本方法负责拼接
         * 前缀与参数、注入稳定 locale、阻塞等待并据退出码判定成败。子类无需再重复实现。
         *
         * @param args     命令参数（不含提权前缀与本机管理器命令）
         * @param output   标准输出/错误回传
         * @param timeoutMs 超时（默认 10 分钟）
         * @param cancelled 若非空，外部置 true 时立即中止（配合 runPrivilegedAsync 取消）
         * @return true 表示 exit 0 成功；false 表示失败或命令缺失。
         */
        bool runPrivileged(const QStringList& args, QString& output, int timeoutMs = 600000,
                           bool* cancelled = nullptr) const;

        /** 便利重载：args 为本机管理器参数（不含提权前缀），error 由 output 回传。 */
        bool runPrivileged(const QStringList& args, QString& output, QString& error) const
        {
            Q_UNUSED(error);
            return runPrivileged(args, output, 600000, nullptr);
        }

        /**
         * @brief 各后端应返回的提权命令前缀（含 pkexec 与本机包管理器）。
         *        例如 APT 返回 {"pkexec","apt-get"}，DNF 返回 {"pkexec","dnf"}。
         *        默认回退为 {"pkexec","sudo"}，具体后端必须覆盖。
         */
        virtual QStringList privilegedPrefix() const
        {
            return {QStringLiteral("pkexec"), QStringLiteral("sudo")};
        }

        /**
         * @brief 扫描待审阅配置文件的公共实现（被 apt/dnf 的后处理探针共用）。
         * @param dirs     要扫描的根目录（如 /etc）
         * @param suffixes 视为"待审阅"的后缀（如 .dpkg-new, .rpmnew）
         * @param maxDepth 最大递归深度
         * @return 命中路径列表
         */
        static QStringList collectConfigFiles(const QStringList& dirs, const QStringList& suffixes,
                                              int maxDepth);

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

        /**
         * @brief 写操作模板方法：消除各后端 install/remove/purge/autoremove/cleanCache
         *        的重复异步骨架。处理空参数检查、进度信号、后台提权执行。
         * @param op        语义操作（决定参数与进度文案）
         * @param packages  目标包列表（CleanCache/Autoremove 可为空）
         * @param error     输出错误信息
         * @return 总是 true（已启动异步任务；真实成败经 operationFinished 信号回传）
         */
        bool runWriteOperation(Op op, const QStringList& packages, QString& error);

        /**
         * @brief 通用"需重启服务列表"解析：剥去 .service 后缀（needs-restarting -s 输出）。
         *        供各后端 checkServicesNeedingRestart 复用，避免逐行重复。
         */
        static QStringList parseServiceList(const QString& raw);

        /**
         * @brief 通用 systemctl --failed 输出解析：取每行首个空格分词作为 unit 名。
         *        供各后端 checkFailedUnits 复用。
         */
        static QStringList parseFailedUnits(const QString& raw);

        /**
         * @brief 通用后端配置项读取：从 m_config 取三个布尔开关。
         *        子类 backendOptions() 可调用本方法后按需增删 key。
         */
        QVariantMap defaultBackendOptions() const;

        AppConfig* m_config = nullptr;
    };

} // namespace DtkUpdate
