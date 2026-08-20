# dtk-update

DTK tray applet for monitoring and managing system package updates across
distributions, with correct dependency resolution, install / remove / purge /
autoremove / cleanup operations. Package operations delegate to the system's
package manager (apt/dpkg, dnf/rpm, pacman, zypper, linyaps/玲珑, …) behind a **pluggable backend abstraction**,
so the project is no longer tied to a single distribution or package manager.

## Features

- System tray plugin for `dde-tray-loader` (V2 interface)
- Update / install / remove / purge / autoremove / clean operations
- Distribution-agnostic **multi-backend** design (APT, DNF, Pacman, Zypper, Linyaps; easy to extend),
  with **cross-distro Linyaps** probed independently of the system package manager
- Dependency resolution via backend dry-run parsing
- Residual config & cache cleanup (`rc` packages, orphan configs)
- Optional security advisories (deepin security center D-Bus, with offline heuristic fallback)
- **Upstream security advisory fetch** before applying updates (auto-selects the per-distro
  source: Debian DSA / Ubuntu USN / openSUSE / Arch, configurable, time-out graceful degrade,
  async prefetch never blocks the update flow)
- **Distro official "recent news / notices" fetch** (package-independent; pulled from the
  distro website / announcement service and shown as an informational notification in the tray / GUI)
- **Pre-update / post-update health checks** (reboot required, services to restart,
  config files to review, **failed systemd units**) — inspection only, never auto-applies
- **Container-aware**: reboot / service / failed-unit probes are skipped inside
  containers to avoid false positives about the host
- **Residual packages & downloadable cache** reported after an update, so the user
  can clean them explicitly (never auto-removed)
- Process-level concurrency lock prevents the GUI and tray from triggering a system
  write at the same time
- Background monitor via systemd user service
- Transparent configuration through DConfig and a user-editable `backend.conf`
  (INI/conf style; see `--show-config`)
- **Periodic update check**: Off / hourly / daily / monthly (off by default and must be
  enabled explicitly; when off, checks only run on demand or on events such as wake-up /
  network reconnect; config changes hot-reload the schedule)
- **Automatic update**: disabled by default and must be enabled explicitly. When enabled,
  only updates found by *periodic* checks are installed automatically; if a security
  advisory or pre-update check recommends attention, your explicit confirmation is still
  required first (focus defaults to Cancel) — never an implicit, unconfirmed change
- **Localization**: Simplified Chinese, English, Spanish, French, German

## Development Skills

This project is developed following the CodeBuddy skill conventions below, to stay
aligned with the deepin/UOS v25 ecosystem:

- **dde-tray-development**: the tray plugin follows `PluginsItemInterfaceV2`
  (`com.deepin.dock.PluginsItemInterface_V2`); `flags` uses
  `Type_Tray | Attribute_CanSetting`; `icon()` returns a themed icon; translations
  are loaded inside `init()`.
- **dtk-development**: the app/plugin uses DTK6 (with automatic DTK5 detection);
  follows `DApplication`, `DConfig`, DCI icons, `DLogManager`, etc.; debian
  packaging dependencies map to DTK modules.
- **dtk-development** (widget): the main window is based on `DMainWindow`; progress
  and dialog controls use DTK widgets.

> The module layout is inspired by [arch-update](https://github.com/Antiz96/arch-update)
> (pre/post update separation, tray integration, transparent config), but the
> package management is rewritten behind a pluggable backend abstraction for multiple
> distributions — its Rust implementation is not copied verbatim.

## Architecture

```
src/core        business logic (UI-agnostic, fully unit-tested)
  package/      PackageBackend(abstract interface) · AptBackend(apt/dpkg) · DnfBackend(dnf/rpm)
                · PacmanBackend(pacman, Arch) · ZypperBackend(zypper/rpm, openSUSE)
                · LinyapsBackend(ll-cli/玲珑, cross-distro) · SnapBackend(snap, cross-distro)
                · FlatpakBackend(flatpak, cross-distro) · BackendFactory(auto-detect by distro
                  + always probe sandbox backends independently) · PackageParser(pure parsing)
  dependency/   DependencyResolver (backend dry-run parsing)
  security/     SecurityAdvisor (deepin security center D-Bus + per-distro upstream advisories + recent notices fetch, optional)
  healthcheck/  PreUpdateCheck / PostUpdateCheck (pre/post update, read-only probes)
  monitor/      UpdateMonitor (state machine + periodic scheduling, aggregates sandbox backends)
src/indicator  UpdateIndicator (desktop-agnostic core shared by both trays: builds backend /
                monitor / advisor, exposes hooks for front-ends)
                UpdateDialogs (shared DDialog builders: sandbox-unavailable prompt,
                security/advisory confirm, post-update report — used by both trays)
src/tray       dde-tray-loader plugin (PluginsItemInterfaceV2, deepin/UOS only; needs dde-dock SDK)
src/tray-generic  cross-distro freedesktop tray (QSystemTrayIcon, any DTK6 distro; no dde-dock)
src/ui        standalone DTK main window (DMainWindow)
src/daemon    background DBus service (com.dtk.update.Daemon)
src/common    logging, config (DConfig + INI backend.conf), translator
translations  .ts sources (zh_CN / en_US / es / fr / de) + CMake compile rules
tests         GoogleTest for core layer
```

Two tray front-ends share one `UpdateIndicator` core:

- **dde-tray** (`src/tray`): deepin/UOS Dock plugin via `PluginsItemInterfaceV2`. Built only
  when the `dde-dock` SDK is present; otherwise the target is skipped.
- **generic tray** (`src/tray-generic`): a standalone `dtk-update-tray-generic` process using
  Qt6 `QSystemTrayIcon`. It has **no deepin-specific dependency** and runs on any distribution
  that ships DTK6 + Qt6 (Ubuntu, Arch, Fedora, ...). Autostarted via
  `dtk-update-tray-generic.desktop` with `NotShowIn=deepin` so deepin keeps a single tray.

Design constraints:

- `src/core` must NOT directly include Dock/Tray/UI headers (UI-agnostic, independently testable).
- All privileged write operations go through `pkexec` (polkit); no in-process `sudo`.
- Config items are exposed through `AppConfig` (DConfig) to stay transparent and configurable.
- The `PackageBackend` interface describes *semantic operations*; distro-specific
  commands, parsing, and probing are all pushed down into the concrete backend.
  Upper layers (UI / tray / monitor / dependency) only depend on the interface.
- `PackageParser` is a pure parsing layer, decoupled from process execution, for easy unit testing.
- **Pre/post update separation**: `PreUpdateCheck` runs before the user confirms;
  `PostUpdateCheck` runs after a successful update. Both are read-only probes
  (kernel reboot pending / services to restart / config files to review /
  **failed systemd units**) and **never automatically** reboot or merge configs —
  whether to act is left to the user. Probes are **container-aware**: inside a
  container, host-kernel and host-service checks are skipped to avoid false positives.
- No feature decides for the user: the update confirmation dialog focuses "Cancel"
  by default; security advisories and pre-check results are shown and the user must
  explicitly confirm to proceed.
- **Backend wiring is centralized**: sandbox-app backends (Linyaps / Snap / Flatpak, all
  cross-distro) are attached to `UpdateMonitor` via the single factory helper
  `BackendFactory::attachSandboxBackends`, so front-ends (GUI / both trays) only write one
  line and never repeat the probe / wire boilerplate. The wiring stays explicit (called from
  each front-end) rather than hidden inside `UpdateMonitor` construction, keeping unit tests
  of the monitor deterministic. `attachLinyaps` is kept only as a Linyaps-only compatibility
  shim.
- **Concurrency safety**: a single `QLockFile` in the runtime dir guards against
  concurrent GUI + tray write operations on the same system. It is held as a **value
  member** (not heap-allocated) so it is released automatically when the monitor is
  destroyed — no manual `delete` and no leak.
- **Object lifetime discipline**: non-QObject resources are owned by value or by a
  parented QObject; optional child-owned backends held across objects use `QPointer`
  so they auto-null when the owner deletes the backend, preventing dangling pointers
  and use-after-free. Front-ends own `UpdateMonitor` / `AppConfig` / `SecurityAdvisor`
  via Qt parentage; cross-object raw pointers (`m_backend`, `m_config`) are externally
  owned and never deleted by the holder.

### Adding a new package-manager backend

There are **two distinct kinds** of backend. Pick the right pattern — they are
treated differently by the monitor and UI.

**A. System package managers (apt / dnf, distro-bound).** Exactly one is active for
a given host, decided by the distro family (`DistroProbe::Family`). Adapting to a new
distribution takes three steps, with no changes to UI / monitor:

1. Subclass `PackageBackend` and implement the backend-specific virtuals
   (`fetchUpgradable`, `simulateInstall`, `listResidualPackages`, `cacheDirectories`,
   `install`/`remove`/`purge`/`autoremove`/`cleanCache`, `isAvailable`,
   `backendId`/`backendName`/`backendType`). Command construction, output parsing, and
   availability probing live entirely in this class.
   **Common infrastructure is already provided by the base class** — do **not** re-implement
   `runQuery` / `runProbe` / `runPrivileged` / `commandExists` / `collectConfigFiles`; they
   are shared and only differ by the privilege prefix. Override the single virtual
   `privilegedPrefix()` (e.g. `{"pkexec","apt-get"}`) so `runPrivileged` knows how to
   escalate for your backend.
   **Also override the health-check probes** (`checkRebootRequired`,
   `checkServicesNeedingRestart`, `checkConfigFilesToReview`, `checkFailedUnits`) — return
   `false` for `support` when a probe does not apply, but never silently leave them as a
   no-op "unsupported" without reason. Remember container-awareness:
   skip reboot / service / failed-unit probes when `SystemInfo::isContainer()` is true.
2. Append a `{id, ctor}` entry to `BackendFactory::registry()`.
3. Add the new implementation files to `src/core/package/CMakeLists.txt`, and register the
   `id` in `PresetConfig::knownBackendIds()` for config validation.
   If the dependency-resolution output format differs from APT, branch in
   `DependencyResolver` by `backendId()` (the base implementation already handles APT
   and DNF; backends without structured transaction output fall back to target-only).

**B. Sandbox-app backends (Linyaps / Snap / Flatpak, cross-distro).** These are
**orthogonal to the system package manager**: they are not tied to a distro, each runs
its own daemon + runtime, and a single host may have **none, one, or several** of them
installed at the same time. They must be probed **independently and unconditionally** —
never gated by `DistroProbe::Family`, and never assumed to be "present and unique" the
way a system backend is. Follow the same three steps as above, plus the sandbox rules in
the next section.

See `src/core/package/dnfbackend.cpp` (Fedora/RHEL family, system backend) and
`src/core/package/linyapsbackend.cpp` / `snapbackend.cpp` / `flatpakbackend.cpp`
(sandbox-app family) for examples.

### Sandbox-app backends (Linyaps / Snap / Flatpak, cross-distro)

Sandbox-app managers are **not tied to a single distribution** and must be probed
independently of the distro family. Linyaps (ll-cli) is available on deepin, Fedora,
Ubuntu, Arch, and more whenever the `linglong` runtime is installed; Snap (snapd) and
Flatpak (flatpak + at least one remote) follow the same pattern. They are **orthogonal**
to the system package manager (system packages vs. sandbox apps), and crucially a host
can have **zero, one, or multiple** of them at once — this is the key difference from a
system backend, which a given distro has exactly one of. Therefore:

- A sandbox backend must **never** be gated by `DistroProbe::Family`. Its
  `isAvailable()` only checks whether the CLI exists and the runtime is healthy
  (e.g. flatpak requires `flatpak remotes` to report at least one remote; snap requires
  a passing `snap list --unicode=never` smoke test). Missing commands / broken runtime
  must return `false`, never a "command exists ⇒ available" false positive.
- `BackendFactory::attachSandboxBackends` probes **every** sandbox id
  (`sandboxIds()` = `linyaps`, `snap`, `flatpak`) and attaches only those whose
  `isAvailable()` is true. A false result drops the backend **silently without error and
  without falling back** to any "default" sandbox backend. The UI / monitor must make no
  assumption about how many sandbox backends exist (0 / 1 / N are all valid): the
  upgradable list, update confirmation, and post-check report are generated dynamically
  from the set that is actually available, routed by `backendId`, and **never hard-code
  `linyaps`** or assume snap/flatpak are present.
- All four health-check probes (`checkRebootRequired` / `checkServicesNeedingRestart` /
  `checkConfigFilesToReview` / `checkFailedUnits`) return `support=false` for sandbox
  backends — they do not touch the kernel / system services / systemd units.
- `privilegedPrefix()` returns **empty** for sandbox backends: snapd / flatpak escalate
  through their own polkit policy, not `pkexec`.
- When `isAvailable()` returns `false` for a reason other than "not installed"
  (e.g. the CLI exists but the runtime is broken / daemon down / permission denied), the
  backend must populate `availabilityError()` with a concrete, actionable message. The UI
  and both trays surface this through `UpdateMonitor::backendUnavailable` (now a generic
  per-`backendId` prompt, not linyaps-specific) so the user knows **how to fix it** rather
  than just "backend unavailable".
- New sandbox backends follow the same rule: register them in
  `BackendFactory::registry()` **and** append the id to `BackendFactory::sandboxIds()` so
  `attachSandboxBackends` picks them up automatically — no front-end call-site changes.

## Build

Install build dependencies first. On Debian/Ubuntu/Deepin/UOS (the full list mirrors
`debian/control` `Build-Depends`):

```bash
sudo apt-get install -y \
  cmake debhelper-compat pkg-config \
  qt6-base-dev qt6-tools-dev \
  libdtk6core-dev libdtk6gui-dev libdtk6widget-dev libdtk6log-dev \
  libgtest-dev libpolkit-qt6-1-dev \
  libxkbcommon-dev          # provides libxkbcommon (CMake 'XKB' check); Qt6 GUI needs it
```

> `dde-dock-dev` is an **optional** dependency. It lives only in deepin/UOS sources
> (in beige it is provided virtually by `dde-tray-loader-dev`) and is **not** declared
> in `debian/control` `Build-Depends`, so `apt-get build-dep` succeeds on plain
> Debian/Ubuntu/Fedora/Arch. When the dde-dock SDK is absent, CMake automatically skips
> `src/tray` with the status message `dde-dock SDK not found, skip building dde-dock tray
> plugin`, while every other target (generic tray, GUI, daemon, core + tests) still
> builds. To also build the dde-dock plugin, install `dde-dock-dev`
> (`dde-tray-loader-dev` on deepin) and reconfigure. The official `deb` from `build.yml`
> installs this SDK inside the beige chroot, so it still ships the **dde-tray** plugin.
> If CMake prints `Could NOT find XKB`, install `libxkbcommon-dev` (normally pulled in by
> `qt6-base-dev` but a minimal container may miss it).

Then configure and build:

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure   # unit tests
sudo make install
```

CI has two pipelines. The **unit-test** pipeline runs on the `ubuntu:devel` image
(the only Ubuntu suite that ships the full DTK6 dev stack:
`libdtk6gui-dev`/`libdtk6widget-dev`/`libdtk6log-dev`), building core/UI/daemon and
running `ctest`; the tray plugin is skipped there because the `dde-dock` SDK it
depends on is a deepin/UOS component not present in Ubuntu. The **build** pipeline
produces a complete `.deb` including the tray plugin in a deepin-based packaging
environment (see `ci/package-deb.sh` — the only CI script — driven by
`.github/workflows/build.yml` via a debootstrap deepin beige chroot + qemu for
loong64). The legacy `ci/multiarch-build.sh` was removed.

## Translations

The UI supports five common languages: Simplified Chinese (zh_CN), English (en_US),
Spanish (es), French (fr), and German (de). The translation sources live in
`translations/` and are compiled to `.qm` via `lupdate`/`lrelease` (or Qt
LinguistTools) at build time and installed to `share/dtk-update/translations`.

To refresh the translation templates after adding or changing UI strings:

```bash
lupdate src -ts translations/dtk-update_en_US.ts \
    translations/dtk-update_zh_CN.ts translations/dtk-update_es.ts \
    translations/dtk-update_fr.ts translations/dtk-update_de.ts -source-language en_US
```

Translation loading is done by `DtkUpdate::loadTranslator("dtk-update")` (called by
both the GUI and the tray plugin), which selects the `.qm` automatically based on the
system locale.

## License

GPL-3.0-or-later
