#include "backendfactory.h"

#include <QPointer>
#include <functional>

#include "aptbackend.h"
#include "common/appconfig.h"
#include "common/distroprobe.h"
#include "common/presetconfig.h"
#include "core/monitor/updatemonitor.h"
#include "dnfbackend.h"
#include "flatpakbackend.h"
#include "linyapsbackend.h"
#include "logger.h"
#include "snapbackend.h"

namespace DtkUpdate
{

    namespace
    {

        // 所有已注册后端的工厂函数（基础顺序，实际探测优先级会按发行系调整）
        using BackendCtor = std::function<PackageBackend*(QObject*)>;
        struct BackendEntry
        {
            QString id;
            BackendCtor ctor;
        };
        const QVector<BackendEntry>& registry()
        {
            static const QVector<BackendEntry> entries = {
                BackendEntry{QStringLiteral("apt"),
                             [](QObject* p) -> PackageBackend* { return new AptBackend(p); }},
                BackendEntry{QStringLiteral("dnf"),
                             [](QObject* p) -> PackageBackend* { return new DnfBackend(p); }},
                BackendEntry{QStringLiteral("linyaps"),
                             [](QObject* p) -> PackageBackend* { return new LinyapsBackend(p); }},
                BackendEntry{QStringLiteral("snap"),
                             [](QObject* p) -> PackageBackend* { return new SnapBackend(p); }},
                BackendEntry{QStringLiteral("flatpak"),
                             [](QObject* p) -> PackageBackend* { return new FlatpakBackend(p); }},
            };
            return entries;
        }

        // 按发行系对"系统级"后端探测排序并裁剪：
        //  - 若该系预设后端已实现（在 registry 中），则只探测该后端（找不到即 nullptr，
        //    不静默回退到其它不相关后端）；
        //  - 若该系预设后端未实现（如 Arch→pacman、Suse→zypper），ordered 为空，
        //    createBackend 直接返回 nullptr，如实反映"无可用后端"；
        //  - 仅当发行系未知（Unknown，预设为空）时，回退为按 registry 全部顺序自动探测。
        //
        // 注意：玲珑(linyaps) 是跨发行版的沙箱应用层包管理器，与系统包管理器(apt/dnf)正交，
        // 不在此排序内——它由 createBackends()/availableBackendIds() 单独、无条件探测，
        // 无论当前是什么发行系，只要 ll-cli 运行环境健康即可启用，绝不受发行系限制。
        QVector<const BackendEntry*> orderedEntries(DistroProbe::Family family)
        {
            const QString pref = PresetConfig::defaultBackendFor(family);
            const auto& all = registry();
            QVector<const BackendEntry*> ordered;
            if (!pref.isEmpty())
            {
                for (const auto& e : all)
                    if (e.id == pref)
                        ordered.append(&e);
                return ordered; // 可能为空（预设后端未实现）→ 不 fallthrough 到其它后端
            }
            for (const auto& e : all)
                ordered.append(&e);
            return ordered;
        }

        // 返回沙箱式应用商店注册项（跨发行系，独立于 orderedEntries 探测）。
        // 它们与系统包管理器(apt/dnf)正交，可跨任意发行系运行，每个都无条件独立探测。
        const BackendEntry* sandboxEntry(const QString& id)
        {
            for (const auto& e : registry())
                if (e.id == id)
                    return &e;
            return nullptr;
        }

        const BackendEntry* linyapsEntry()
        {
            return sandboxEntry(QStringLiteral("linyaps"));
        }
        const BackendEntry* snapEntry()
        {
            return sandboxEntry(QStringLiteral("snap"));
        }
        const BackendEntry* flatpakEntry()
        {
            return sandboxEntry(QStringLiteral("flatpak"));
        }

        // 所有沙箱式应用商店 id（探测顺序即接入顺序）
        const QStringList& sandboxIds()
        {
            static const QStringList ids = {QStringLiteral("linyaps"), QStringLiteral("snap"),
                                            QStringLiteral("flatpak")};
            return ids;
        }

    } // namespace

    PackageBackend* BackendFactory::createBackend(QObject* parent, const QString& preferredId)
    {
        return createBackend(DistroProbe::detectFamily(), parent, preferredId);
    }

    PackageBackend* BackendFactory::createBackend(DistroProbe::Family family, QObject* parent,
                                                  const QString& preferredId)
    {
        // 1) 若指定了首选后端且可用，优先使用（首选也可以是 linyaps）
        if (!preferredId.isEmpty())
        {
            PackageBackend* forced = createById(preferredId, parent);
            if (forced && forced->isAvailable())
                return forced;
            if (forced)
                delete forced;
            qCWarning(dtkUpdateCore) << "preferred backend unavailable:" << preferredId
                                     << ", fall back to auto detection";
        }
        // 2) 否则按「发行系预设优先」的顺序自动探测系统级后端。
        //    关键：若发行系对应的预设后端不存在（如 Arch/Suse 尚未实现 pacman/zypper），
        //    探测会如实失败并返回 nullptr，而不会静默回退到 apt/dnf 造成虚假可用。
        for (const auto* e : orderedEntries(family))
        {
            QPointer<PackageBackend> probe(e->ctor(nullptr));
            if (probe && probe->isAvailable())
            {
                qCInfo(dtkUpdateCore) << "selected package backend:" << probe->backendName();
                delete probe;
                return e->ctor(parent);
            }
            if (probe)
                delete probe;
        }
        qCWarning(dtkUpdateCore) << "no available system package backend found for family"
                                 << static_cast<int>(family);
        return nullptr;
    }

    QList<PackageBackend*> BackendFactory::createBackends(QObject* parent,
                                                          const QString& preferredId)
    {
        return createBackends(DistroProbe::detectFamily(), parent, preferredId);
    }

    QList<PackageBackend*> BackendFactory::createBackends(DistroProbe::Family family,
                                                          QObject* parent,
                                                          const QString& preferredId)
    {
        QList<PackageBackend*> backends;

        // 1) 首选后端（若指定且可用）—— 优先于一切，且允许是任意后端（含 linyaps）
        if (!preferredId.isEmpty())
        {
            PackageBackend* forced = createById(preferredId, parent);
            if (forced && forced->isAvailable())
            {
                backends.append(forced);
                return backends; // 显式指定后端时不再叠加其他后端
            }
            if (forced)
                delete forced;
            qCWarning(dtkUpdateCore) << "preferred backend unavailable:" << preferredId
                                     << ", fall back to auto detection";
        }

        // 2) 探测系统级后端（按发行系排序，单一主后端）
        PackageBackend* system = createBackend(family, parent);
        if (system)
            backends.append(system);

        // 3) 无论发行系如何，始终独立探测沙箱式应用商店（linglong/snap/flatpak）：
        //    它们跨发行版、与系统包管理器(apt/dnf)正交，各自运行环境健康即加入。
        for (const QString& id : sandboxIds())
        {
            const BackendEntry* se = sandboxEntry(id);
            if (!se)
                continue;
            QPointer<PackageBackend> probe(se->ctor(nullptr));
            if (probe && probe->isAvailable())
            {
                qCInfo(dtkUpdateCore)
                    << "sandbox backend available (cross-distro):" << probe->backendName();
                delete probe;
                backends.append(se->ctor(parent));
            }
            else if (probe)
            {
                qCInfo(dtkUpdateCore)
                    << "sandbox backend not available on this host:" << id
                    << (probe->availabilityError().isEmpty() ? QString()
                                                             : probe->availabilityError());
                delete probe;
            }
        }

        return backends;
    }

    PackageBackend* BackendFactory::createById(const QString& id, QObject* parent)
    {
        for (const auto& e : registry())
        {
            if (e.id == id)
                return e.ctor(parent);
        }
        qCWarning(dtkUpdateCore) << "unknown backend id:" << id;
        return nullptr;
    }

    void BackendFactory::attachSandboxBackends(UpdateMonitor* monitor, AppConfig* config,
                                               QObject* parent)
    {
        // 沙箱式应用商店（linglong/snap/flatpak）与系统级后端正交：逐个独立无条件探测，
        // 可用则接入 monitor 参与更新聚合，不可用直接丢弃。集中此逻辑，消除 GUI / 各托盘
        // 重复的接入样板。新代码应优先调用本方法，一次性接入所有沙箱后端。
        if (!monitor)
            return;
        for (const QString& id : sandboxIds())
        {
            PackageBackend* sb = createById(id, parent);
            if (!sb)
                continue;
            if (!sb->isAvailable())
            {
                qCInfo(dtkUpdateCore)
                    << id << "backend present but unavailable:" << sb->availabilityError();
                sb->deleteLater();
                continue;
            }
            if (config)
                sb->setConfig(config);
            monitor->setSandboxBackend(sb);
            qCInfo(dtkUpdateCore) << id << "sandbox backend attached";
        }
    }

    PackageBackend* BackendFactory::attachLinyaps(UpdateMonitor* monitor, AppConfig* config,
                                                  QObject* parent)
    {
        // 兼容封装：仅接入玲珑(linyaps)。新代码请用 attachSandboxBackends 一次性接入全部。
        if (!monitor)
            return nullptr;
        PackageBackend* linyaps = createById(QStringLiteral("linyaps"), parent);
        if (!linyaps)
            return nullptr;
        if (!linyaps->isAvailable())
        {
            qCInfo(dtkUpdateCore) << "linyaps backend present but unavailable:"
                                  << linyaps->availabilityError();
            linyaps->deleteLater();
            return nullptr;
        }
        if (config)
            linyaps->setConfig(config);
        monitor->setLinyapsBackend(linyaps);
        qCInfo(dtkUpdateCore) << "linyaps backend attached";
        return linyaps;
    }

    QStringList BackendFactory::availableBackendIds()
    {
        QStringList ids;
        for (const auto& e : registry())
        {
            QPointer<PackageBackend> probe(e.ctor(nullptr));
            const bool ok = probe && probe->isAvailable();
            if (probe)
                delete probe;
            if (ok)
                ids.append(e.id);
        }
        return ids;
    }

} // namespace DtkUpdate
