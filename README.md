# dtk-update

DTK tray applet for monitoring and managing system package updates across
distributions, with correct dependency resolution, install / remove / purge /
autoremove / cleanup operations. Package operations delegate to the system's
package manager (apt/dpkg, dnf/rpm, …) behind a **pluggable backend abstraction**,
so the project is no longer tied to a single distribution or package manager.

## Features

- System tray plugin for `dde-tray-loader` (V2 interface)
- Update / install / remove / purge / autoremove / clean operations
- Distribution-agnostic **multi-backend** design (APT, DNF; easy to extend)
- Dependency resolution via backend dry-run parsing
- Residual config & cache cleanup (`rc` packages, orphan configs)
- Optional security advisories (deepin security center D-Bus, with offline heuristic fallback)
- **Upstream security advisory fetch** before applying updates (configurable, times-out
  gracefully and never blocks the update flow)
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
src/core      business logic (UI-agnostic, fully unit-tested)
  package/      PackageBackend(abstract interface) · AptBackend(apt/dpkg) · DnfBackend(dnf/rpm)
                BackendFactory(auto-detect by distro) · PackageParser(pure parsing)
  dependency/   DependencyResolver (backend dry-run parsing)
  security/     SecurityAdvisor (deepin security center D-Bus + upstream advisory fetch, optional)
  healthcheck/  PreUpdateCheck / PostUpdateCheck (pre/post update, read-only probes)
  monitor/      UpdateMonitor (state machine + periodic scheduling)
src/tray      dde-tray-loader plugin (PluginsItemInterfaceV2)
src/ui        standalone DTK main window (DMainWindow)
src/daemon    background DBus service (com.dtk.update.Daemon)
src/common    logging, config (DConfig + INI backend.conf), translator
translations  .ts sources (zh_CN / en_US / es / fr / de) + CMake compile rules
tests         GoogleTest for core layer
```

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

### Adding a new package-manager backend

Adapting to a new distribution takes three steps, with no changes to UI / monitor:

1. Subclass `PackageBackend` and implement all pure-virtual functions
   (`fetchUpgradable`, `simulateInstall`, `listResidualPackages`, `cacheDirectories`,
   `install`/`remove`/`purge`/`autoremove`/`cleanCache`, `isAvailable`,
   `backendId`/`backendName`/`backendType`). Distro commands, output parsing, and
   availability probing are all done inside this class. **Also override the health-check
   probes** (`checkRebootRequired`, `checkServicesNeedingRestart`,
   `checkConfigFilesToReview`, `checkFailedUnits`) — return `false` for `support` if the
   probe is not applicable to your distro, but do not leave them as the default no-op
   silent "unsupported" without justification. Remember container-awareness: skip
   reboot / service / failed-unit probes when `SystemInfo::isContainer()` is true.
2. Append a `{id, ctor}` entry to `BackendFactory::registry()` to set the detection priority.
3. Add the new implementation files to `src/core/package/CMakeLists.txt`.
   If the dependency-resolution output format differs from APT, branch in
   `DependencyResolver` by `backendType()`.

See `src/core/package/dnfbackend.cpp` (Fedora/RHEL family) for an example.

## Build

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure   # unit tests
sudo make install
```

CI builds on the `deepin/deepin-build:25` image and produces `.deb` artifacts.

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
