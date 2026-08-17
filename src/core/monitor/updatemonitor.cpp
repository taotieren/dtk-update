#include "updatemonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "core/package/packagebackend.h"
#include "logger.h"

namespace DtkUpdate
{

    namespace
    {
        // NetworkManager 状态：>= 70 视为已连接（NM_STATE_CONNECTED_GLOBAL=70）
        constexpr int kNmConnectedGlobal = 70;

        // 监听登录会话就绪（logind PrepareForSleep=false 或 session 创建）触发检查
        void watchLogindResume(UpdateMonitor* monitor)
        {
            auto bus = QDBusConnection::systemBus();
            if (!bus.isConnected())
                return;
            // 系统从睡眠恢复后触发检查：logind PrepareForSleep(bool)
            // 在进入睡眠=真、唤醒=假各触发一次， 仅唤醒（false）时检查。用带参槽接收
            // bool，避免旧式无参 SLOT 吞参数导致进入睡眠也误触发。
            bus.connect(QStringLiteral("org.freedesktop.login1"),
                        QStringLiteral("/org/freedesktop/login1"),
                        QStringLiteral("org.freedesktop.login1.Manager"),
                        QStringLiteral("PrepareForSleep"), monitor, SLOT(onPrepareForSleep(bool)));
        }
    } // namespace

    namespace
    {
        // 选择用户可写的锁文件路径：/run/lock 通常仅 root 可写，普通用户的 GUI/托盘
        // 会直接 tryLock 失败导致每次升级被拒。优先用户私有运行时目录，再回退 /tmp。
        QString userLockPath()
        {
            const QString name = QStringLiteral("dtk-update.lock");
            const QString runtime =
                QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
            if (!runtime.isEmpty())
            {
                QDir().mkpath(runtime);
                const QString p = runtime + QLatin1Char('/') + name;
                if (QFile(p).open(QIODevice::WriteOnly))
                {
                    QFile(p).remove();
                    return p;
                }
            }
            return QDir::tempPath() + QLatin1Char('/') + name;
        }
    } // namespace

    UpdateMonitor::UpdateMonitor(PackageBackend* backend, AppConfig* config, QObject* parent)
        : QObject(parent), m_backend(backend), m_config(config), m_lock(userLockPath())
    {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(false);
        applyConfigInterval();
        connect(m_timer, &QTimer::timeout, this, &UpdateMonitor::onTimeout);

        if (m_config)
            connect(m_config, &AppConfig::configChanged, this, &UpdateMonitor::onConfigChanged);

        if (m_backend)
        {
            connect(m_backend, &PackageBackend::operationProgress, this,
                    &UpdateMonitor::onBackendProgress);
            connect(m_backend, &PackageBackend::operationFinished, this,
                    &UpdateMonitor::onBackendFinished);
        }

        // 监听 NetworkManager 网络恢复
        auto bus = QDBusConnection::systemBus();
        if (bus.isConnected())
        {
            // NetworkManager StateChanged(uint)：state>=70=全局已连接，仅此时触发检查；
            // 旧式 SLOT() 会丢弃 uint 参数导致掉线/切换也误触发，故用带参 lambda 适配。
            bus.connect(QStringLiteral("org.freedesktop.NetworkManager"),
                        QStringLiteral("/org/freedesktop/NetworkManager"),
                        QStringLiteral("org.freedesktop.NetworkManager"),
                        QStringLiteral("StateChanged"), this, SLOT(onNmStateChanged(uint)));
        }
        watchLogindResume(this);
    }

    UpdateMonitor::~UpdateMonitor() = default;

    void UpdateMonitor::setSandboxBackend(PackageBackend* backend)
    {
        if (!backend)
            return;
        // 去重：相同实例不重复接入
        for (const auto& existing : m_sandboxBackends)
            if (existing == backend)
                return;
        m_sandboxBackends.append(backend);
        // 每接入一个沙箱后端都重连进度/完成信号，使各后端操作统一回传 monitor。
        connect(backend, &PackageBackend::operationProgress, this,
                &UpdateMonitor::onBackendProgress);
        connect(backend, &PackageBackend::operationFinished, this,
                &UpdateMonitor::onBackendFinished);
    }

    void UpdateMonitor::setLinyapsBackend(PackageBackend* backend)
    {
        // 兼容封装：仅接入 linyaps（或移除 linyaps）。新代码请用 setSandboxBackend。
        if (!backend)
        {
            m_sandboxBackends.erase(
                std::remove_if(m_sandboxBackends.begin(), m_sandboxBackends.end(),
                               [](const QPointer<PackageBackend>& b)
                               { return b && b->backendId() == QStringLiteral("linyaps"); }),
                m_sandboxBackends.end());
            return;
        }
        if (backend->backendId() != QStringLiteral("linyaps"))
            return; // 仅接受 linyaps 实例
        setSandboxBackend(backend);
    }

    void UpdateMonitor::applyConfigInterval()
    {
        const int minutes = m_config ? m_config->checkIntervalMinutes() : 360;
        const int msec = qMax(1, minutes) * 60 * 1000;
        m_timer->setInterval(msec);
        qCInfo(dtkUpdateCore) << "check interval set to" << minutes << "min";
    }

    void UpdateMonitor::start()
    {
        m_timer->start();
        checkNow(); // 启动即检查一次
    }

    void UpdateMonitor::stop()
    {
        m_timer->stop();
    }

    void UpdateMonitor::checkNow()
    {
        if (m_state == State::Checking || m_state == State::Updating)
            return; // 避免重入
        setState(State::Checking);
        QString error;
        PackageList list;
        if (!m_backend || !m_backend->fetchUpgradable(list, error))
        {
            setState(State::Error);
            emit checkFailed(error);
            return;
        }
        // 聚合可选的沙箱应用商店更新（linglong/snap/flatpak 等）：跨发行版，与系统后端正交。
        // 无论当前发行系如何都逐个尝试；若某沙箱后端运行环境异常，发出诊断提示而非静默忽略。
        for (QPointer<PackageBackend>& sb : m_sandboxBackends)
        {
            if (!sb)
                continue;
            PackageList apps;
            QString err;
            if (sb->isAvailable())
            {
                if (sb->fetchUpgradable(apps, err))
                    list.append(apps);
                else
                    emit backendUnavailable(sb->backendId(), err);
            }
            else
            {
                // 沙箱命令存在但环境异常 / 未安装：把具体原因交给 UI 提示用户处理
                emit backendUnavailable(sb->backendId(), sb->availabilityError());
            }
        }
        m_upgradable = list;
        m_lastCheck = QDateTime::currentDateTime();
        setState(list.isEmpty() ? State::Idle : State::HasUpdates);
        emit updatesAvailable(list);

        // 拿到可升级列表后，异步预取上游官方安全公告与发行版最近通知（不阻塞 UI）。
        // 结果在用户点击「更新」时由 SecurityAdvisor::fetchAdvisories 合并缓存使用。
        if (!list.isEmpty() && m_advisor)
        {
            QStringList names;
            for (const auto& p : list)
                names.append(p.name);
            m_advisor->prefetchUpstream(DistroProbe::detectId(), names);
            m_advisor->fetchDistroNotices(DistroProbe::detectId());
        }
    }

    void UpdateMonitor::applyUpdates()
    {
        if (m_state == State::Updating)
            return;
        if (m_upgradable.isEmpty())
            return;

        // 升级前预检（内核待重启/服务/配置审阅）——仅探测，不修改系统
        PreCheckReport pre = PreUpdateCheck::run(m_backend);

        // 升级前安全提示（若有 advisor）：聚合安全公告
        QList<SecurityAdvisor::Advisory> advs;
        QString sev = QStringLiteral("none");
        if (m_advisor)
        {
            QStringList names;
            for (const auto& p : m_upgradable)
                names.append(p.name);
            m_advisor->fetchAdvisories(names, advs);
            sev = m_advisor->overallSeverity(advs);
        }

        // 是否需弹确认对话框：有安全公告，或用户开启了安全提示，或预检有建议项
        const bool showAdvisory = m_config ? m_config->showSecurityAdvisory() : true;
        const bool needConfirm =
            showAdvisory && (sev != QStringLiteral("none") || !advs.isEmpty() || pre.hasAnything());
        if (needConfirm)
        {
            emit securityPrompt(sev, advs, pre);
            return; // 等待 proceedUpdate / 取消（绝不自动继续）
        }
        proceedUpdate();
    }

    void UpdateMonitor::proceedUpdate()
    {
        if (m_state == State::Updating)
            return;
        if (m_upgradable.isEmpty())
            return;

        // 进程级并发锁：防止 gui 与 tray 同时触发写系统（双实例竞态）。
        // /run/lock 不可写时回退到 /tmp；锁获取失败即视为已有实例在更新，放弃本次。
        if (m_lock.isLocked() || !m_lock.tryLock())
        {
            qCWarning(dtkUpdateCore) << "another dtk-update instance is updating, abort";
            emit upgradeFinished(false, tr("Another update is already in progress"));
            return;
        }

        setState(State::Updating);
        m_cancelled = false; // 新一次升级开始，清除上一次取消标志
        // 按来源后端分组：系统包走 m_backend，沙箱应用按各自 backendId 路由到对应沙箱后端。
        QStringList sysPkgs;
        QHash<QString, QStringList> sbPkgs; // backendId -> 包名列表
        for (const auto& p : m_upgradable)
        {
            bool isSb = false;
            for (const auto& sb : m_sandboxBackends)
                if (sb && sb->backendId() == p.backendId)
                {
                    sbPkgs[p.backendId].append(p.name);
                    isSb = true;
                    break;
                }
            if (!isSb)
                sysPkgs.append(p.name);
        }
        // install 为异步后台执行，成功后经 operationFinished 信号回传 onBackendFinished。
        // 此处不再阻塞主线程，故无需同步兜底。
        QString error;
        m_pendingOps = 0;
        if (m_backend && !sysPkgs.isEmpty())
        {
            ++m_pendingOps;
            m_backend->install(sysPkgs, error);
        }
        for (const auto& sb : m_sandboxBackends)
        {
            if (!sb)
                continue;
            const QString id = sb->backendId();
            if (sbPkgs.contains(id) && !sbPkgs.value(id).isEmpty())
            {
                ++m_pendingOps;
                sb->install(sbPkgs.value(id), error);
            }
        }
        // 若本次没有可安装包（理论上不会发生，因 m_upgradable 非空才进入），
        // 主动结束更新态避免卡在 Updating。
        bool anyPkg = !sysPkgs.isEmpty();
        for (const auto& pkgs : sbPkgs)
            if (!pkgs.isEmpty())
                anyPkg = true;
        if (!anyPkg)
        {
            setState(State::Idle);
            if (m_lock.isLocked())
                m_lock.unlock();
            emit upgradeFinished(true, QString());
        }
    }

    void UpdateMonitor::cancelUpdate()
    {
        // 用户拒绝升级：保留可升级列表，回到 HasUpdates 状态，等待用户下次决定。
        // 绝不自动重试或替用户继续。
        // 注意：install 可能仍在后台线程执行，置 m_cancelled 让 onBackendFinished 忽略
        // 其后续回调，避免"已取消"后又弹后检/重查。
        m_cancelled = true;
        if (m_state != State::Updating)
            setState(State::HasUpdates);
        m_upgradable.clear(); // 用户已明确放弃本次升级
        if (m_lock.isLocked())
            m_lock.unlock();
        setState(State::Idle);
        emit upgradeCancelled();
    }

    void UpdateMonitor::onTimeout()
    {
        checkNow();
    }

    void UpdateMonitor::onConfigChanged()
    {
        applyConfigInterval();
    }

    void UpdateMonitor::onPrepareForSleep(bool sleeping)
    {
        // logind PrepareForSleep：进入睡眠(sleeping=true) 与唤醒(sleeping=false) 各触发一次，
        // 仅唤醒后检查（睡眠期间网络不可用，检查无意义）。
        if (!sleeping)
            checkNow();
    }

    void UpdateMonitor::onNmStateChanged(uint state)
    {
        // NetworkManager StateChanged：state>=70 表示全局已连接（NM_STATE_CONNECTED_GLOBAL），
        // 仅在已连通时检查，避免断线/切换过程中误触发。
        if (state >= 70)
            checkNow();
    }

    void UpdateMonitor::onBackendProgress(const QString& stage, int percent)
    {
        emit upgradeProgress(stage, percent);
    }

    void UpdateMonitor::onBackendFinished(bool success, const QString& detail)
    {
        // 用户已在升级进行中取消：忽略后台 install 的后续回调，避免矛盾的重查/后检。
        if (m_cancelled)
        {
            m_cancelled = false;
            qCInfo(dtkUpdateCore) << "ignoring backend result after user cancelled";
            return;
        }
        // 多后端并存时每个后端独立 emit 一次；仅当本轮所有异步写都完成才统一收尾，
        // 否则提前 emit 会导致重复 upgradeFinished / 重复解锁 / 重复后检。
        if (m_pendingOps > 0)
            --m_pendingOps;
        if (m_pendingOps > 0)
            return;

        if (success)
        {
            m_upgradable.clear();
            setState(State::Idle);
        }
        else
        {
            setState(State::Error);
        }
        // 无论成功失败都释放并发锁，避免后续更新被永久阻塞。
        if (m_lock.isLocked())
            m_lock.unlock();
        emit upgradeFinished(success, detail);
        // 升级后重新检查，刷新状态
        if (success)
        {
            // 后检：检测需重启的内核/服务、待审阅配置文件，提示用户（不自动执行）
            PostCheckReport post = PostUpdateCheck::run(m_backend);
            emit postCheck(post);
            QTimer::singleShot(500, this, &UpdateMonitor::checkNow);
        }
    }

    void UpdateMonitor::setState(State s)
    {
        if (m_state == s)
            return;
        m_state = s;
        emit stateChanged(s);
    }

} // namespace DtkUpdate
