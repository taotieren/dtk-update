#include "backendfactory.h"

#include <QPointer>
#include <functional>

#include "aptbackend.h"
#include "common/distroprobe.h"
#include "common/presetconfig.h"
#include "dnfbackend.h"
#include "linyapsbackend.h"
#include "logger.h"

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
            };
            return entries;
        }

        // 按发行系对后端探测排序并裁剪：
        //  - 若该系预设后端已实现（在 registry 中），则只探测该后端（找不到即 nullptr，
        //    不静默回退到其它不相关后端）；
        //  - 若该系预设后端未实现（如 Arch→pacman、Suse→zypper），ordered 为空，
        //    createBackend 直接返回 nullptr，如实反映"无可用后端"；
        //  - 仅当发行系未知（Unknown，预设为空）时，回退为按 registry 全部顺序自动探测。
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

    } // namespace

    PackageBackend* BackendFactory::createBackend(QObject* parent, const QString& preferredId)
    {
        return createBackend(DistroProbe::detectFamily(), parent, preferredId);
    }

    PackageBackend* BackendFactory::createBackend(DistroProbe::Family family, QObject* parent,
                                                  const QString& preferredId)
    {
        // 1) 若指定了首选后端且可用，优先使用
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
        // 2) 否则按「发行系预设优先」的顺序自动探测。
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
        qCWarning(dtkUpdateCore) << "no available package backend found for family"
                                 << static_cast<int>(family);
        return nullptr;
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
