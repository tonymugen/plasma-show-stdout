# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A KDE Plasma plasmoid that displays stdout from shell scripts. Each script runs on its own thread in a C++17 backend (`TimedModule` for interval polling, `SignalModule` for `RTMIN`-triggered runs) exposed to QML as a plugin.

Current scope: `ScriptOutput` (`plugin/plasma-show-stdout.cpp`) drives an arbitrary, ordered list of modules described by a `std::vector<PSSspace::ModuleSpec>` (each spec: trigger kind, script path, interval, signal number, output-length limit). The QML-facing constructor starts with an **empty** list (via the `defaultSpecs()` helper); the modules are then populated **at runtime** by the config panel, which calls the `setModules(QVariantList)` Q_INVOKABLE. Every module's output is joined with `" | "` into the single `text` property. A `SignalModule` is triggered with `pkill --signal RTMIN+N <host>`, where `<host>` is the process the plugin is loaded into (`plasmashell`, or `plasmawindowed`/`plasmoidviewer` when testing).

The **first version of the config panel** is implemented (a single user-defined script: file picker + Timed/interval or Signal/RTMIN-offset). See the *Package config* subsection under Architecture. Multi-module config (the full module list) and the user-configurable delimiter are still future work.

**Future goals (in order):**
- Extend the config panel from a single script to the full, ordered module list (multiple scripts + per-module trigger kind + interval/signal + per-module output limit). The runtime seam already exists: `setModules` takes a `QVariantList`, so this is mostly UI (a repeater/list editor) feeding more than one entry.
- *After* multi-module config: make the output delimiter (currently the hardwired `" | "` in `rebuildCombinedText_()`) user-configurable too.

## Build

Requires: CMake ≥3.21, Qt6 (Core, Qml), Plasma 6 (standalone `Plasma` cmake package). ECM is fetched automatically via `FetchContent` if not present on the system.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --component plasmoid  # installs plasma package + QML plugin
```

`--component plasmoid` is required to avoid installing ECM's own cmake files alongside the plasmoid. The `plasma_install_package()` call is not component-aware and installs unconditionally regardless.

Build types: `Release` (default), `Debug`, `Profile`, `Test`.

### ECM via FetchContent

When ECM is not installed system-wide, `CMakeLists.txt` fetches it with `FetchContent_Populate` (populate-only, not `FetchContent_MakeAvailable`). This avoids running ECM's own `CMakeLists.txt`, which would add unwanted install rules and interfere with Qt6 detection. After populating, `ECM_MODULE_DIR`, `ECM_FIND_MODULE_DIR`, `ECM_KDE_MODULE_DIR`, and `ECM_MODULE_PATH` are set manually.

### Qt6 and qtpaths

`find_package(Qt6)` must appear **before** `include(KDEInstallDirs6)` because `KDEInstallDirs6` calls `ecm_query_qt`, which requires Qt6 to already be initialized. On this system `qtpaths6` lives at `/usr/lib/qt6/bin/` (not in `PATH`); `CMakeLists.txt` locates it via `qmake6 -query QT_INSTALL_BINS` and caches it as `QUERY_EXECUTABLE` (unset from cache first to avoid stale Qt5 values).

### Plasma 6

In KDE 6, the Plasma framework is a standalone cmake package (`find_package(Plasma REQUIRED)`), not a component of KF6. Its config file is at `/usr/lib/cmake/Plasma/PlasmaConfig.cmake`.

`package/metadata.json` must use the Plasma 6 format:
- `X-Plasma-API`: `"plasmoid"` — **not** `"declarativeappletscript"`, which is the Plasma 5 form and causes plasmashell to report `"This Widget was written for an unknown older version of Plasma"` (a misleading error: the metadata is being rejected, not the QML).
- `X-Plasma-API-Version`: `"2"`
- `KPackageStructure`: `"Plasma/Applet"` — top-level, not nested inside `KPlugin`.

### Tests

Tests use Catch2 (fetched automatically via FetchContent):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Test -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

There are **two** test executables, both registered with `ctest` via `catch_discover_tests`. Each compiles the code under test directly rather than linking `plasma_show_stdout`, because `KDECompilerSettings` applies `-fvisibility=hidden` to all targets — without an explicit export annotation, neither `runScript` nor `ScriptOutput` is exported from the shared library.

**`tests`** (`plugin/tests/tests.cpp`, links `Catch2::Catch2WithMain`, compiles `scriptModules.cpp`) — the pure-C++ module layer, kept Qt-free and fast. Covers three areas:

- **`runScript`** (`[runScript]`): non-existent path, stdout capture, empty output, multi-buffer output, stderr isolation, non-executable file.
- **`TimedModule`** (`[TimedModule]`): output written after first execution, truncation to `outputLengthLimit_`, loop termination via stop signal, repeated execution across multiple loop iterations.
- **`SignalModule`** (`[SignalModule]`): output written after trigger, truncation, loop termination via stop signal, repeated execution on successive triggers.

These thread tests use the shared `mutex_` for all synchronisation: the test waits on the `signalToMain` raw pointer with a 5-second `wait_for` timeout (so a broken stop mechanism fails fast rather than hanging). Output is read and cleared under the lock to avoid races between test reads and module writes.

**`scriptOutputTests`** (`plugin/tests/scriptOutputTests.cpp`, compiles `plasma-show-stdout.cpp` + `scriptModules.cpp`, links `Qt::Core`/`Qt::Qml`) — the QML-bridge layer (`[ScriptOutput]`). Constructor-path cases: empty-spec construction (no threads, empty `text`), a timed module's output reaching the `text` property, per-spec output truncation, multiple fragments joined in spec order (`"AAA | BBB"`), the `textChanged` `NOTIFY` signal firing, and a signal module staying idle until `raise(SIGRTMIN+4)` then running (exercising the real `sigaction` → semaphore → `signalWaitLoop_` path). Runtime `setModules` cases (the QVariant path the config UI uses): starting a timed module at runtime, **reconfiguring** from script A to script B (B's output appears and A's is gone — exercises `stopModules_` → `startModules_` reuse), an empty list clearing `text`, an empty-`script` entry being skipped, and a signal entry carrying `signalOffset` triggered by `raise(SIGRTMIN + offset)`. Because `ScriptOutput`'s bridge posts updates via `QMetaObject::invokeMethod(Qt::QueuedConnection)`, these tests need a `QCoreApplication` and a serviced event loop: the file links `Catch2::Catch2` (not `…WithMain`) and supplies its own `main()` that owns the `QCoreApplication` for the whole run (created before any test, destroyed before the exit-time leak check). A `pumpUntil(pred, timeout)` helper services the event loop with a 5-second cap, mirroring the module tests' fail-fast convention. The signal cases install the process-wide handler and `sem_init`/`sem_destroy` per construction, so they must run sequentially (Catch2's default — do not enable parallel `ctest -j` for this target).

### Running locally

After `cmake --install build --component plasmoid` (with `-DCMAKE_INSTALL_PREFIX=~/.local` for a user install), the Plasma package side typically also needs `kpackagetool6` because `plasma_install_package()` under a user prefix does not always land the package where `plasmashell`/`plasmawindowed` searches:

```bash
kpackagetool6 --upgrade package/ --type Plasma/Applet  # use --install on first run
QML_IMPORT_PATH=~/.local/lib/qml plasmawindowed com.github.tonymugen.plasma-show-stdout
```

`QML_IMPORT_PATH` is required so the C++ plugin (installed under the user QML dir) is discoverable. To avoid setting it per-invocation — and to make the widget loadable from `plasmashell`'s "add widgets" panel — persist it via `~/.config/plasma-workspace/env/qml-import-path.sh` and restart `plasmashell`.

Note that the argument to `plasmawindowed` is the **KPackage applet ID** (with hyphens), not the QML module URI.

**Testing the config dialog:** `plasmawindowed` does **not** expose the applet context menu / "Configure" action (right-clicking the windowed widget produces no menu at all), so it cannot reach the config UI — use it only for the widget's own display. To test configuration use `plasmoidviewer` (from the `plasma-sdk` package), which has an explicit *Configure* toolbar button:

```bash
QML_IMPORT_PATH=~/.local/lib/qml plasmoidviewer -a com.github.tonymugen.plasma-show-stdout
```

(The alternative is the real `plasmashell`: add the widget to a panel/desktop and right-click → *Configure*.) Plasma caches package metadata, so when a structural change isn't picked up — e.g. config files added to an already-installed applet — rebuild the cache (`kbuildsycoca6 --noincremental`), clear the host's compiled-QML cache (`~/.cache/plasmoidviewer/qmlcache`, `~/.cache/plasmawindowed/qmlcache`), and/or bump `metadata.json`'s `Version`.

## Architecture

Two distinct layers communicate via condition variables:

**C++ plugin** (`plugin/`, namespace `PSSspace`) — compiled as shared library `plasma_show_stdout`:
- `runScript(path)`: free function wrapping `popen()`; returns stdout as a string, or an error message string if the script doesn't exist or the pipe fails — errors surface in the widget rather than throwing
- `TimedModule`: calls `runScript` in a loop; acquires `mutex_`, writes to `outputString_`, notifies `signalToMain_`, then does `stopSignal_->wait_for(mutex_, refreshInterval_, stop_pred)` — if the predicate fires (spawning thread set `*stop_` and notified `stopSignal_`), the loop exits
- `SignalModule`: waits on `executionSignal_` with predicate `*execute_ || *stop_`; if stop, exits; otherwise resets `*execute_`, releases lock, runs script, reacquires lock, writes output, notifies `signalToMain_`; to trigger a run the spawning thread sets `*execute_ = true` and notifies `executionSignal_`; to stop it sets `*stop_ = true` and notifies the same CV
- Both classes are move-only; `mutex_`, `stop_`, and (for `SignalModule`) `execute_` are `shared_ptr` so the spawning thread retains access after constructing the module; `signalToMain_` and `executionSignal_` are `unique_ptr` — the spawning thread keeps a raw pointer before moving them in

**QML plugin** (`plugin/plasma-show-stdout.hpp`/`.cpp`):
- `ShowStdoutPlugin` (a `QQmlExtensionPlugin`) registers `PSSspace::ScriptOutput` as the QML type `ScriptOutput` at URI `com.github.tonymugen.plasmashowstdout` version 1.0.
- `ScriptOutput` (`QObject`) exposes a `text` `Q_PROPERTY` (`QString`, `NOTIFY textChanged`) and a `maxSignalOffset` `Q_PROPERTY` (`int`, `CONSTANT`, = `SIGRTMAX - SIGRTMIN`, so the config UI can size its signal spinbox without hard-coding glibc numbers). **Two constructors:** the QML entry point `ScriptOutput(QObject *parent)` — all `qmlRegisterType` can invoke — delegates to `ScriptOutput(std::vector<ModuleSpec>, QObject *parent)`, passing `defaultSpecs()` (a file-scope helper returning an **empty** vector). Both are `explicit`. `ModuleSpec` is a namespace-scope struct in `plasma-show-stdout.hpp` (so callers/tests can name it): a `Kind` enum (`Timed`/`Signal`), `script` path, `interval` (timed), `signalNumber` (signal), and per-module `outputLimit`. Each spec is built into a `std::variant<TimedModule, SignalModule>` and a `ModuleSlot`. Modules are run polymorphically via `std::visit([](auto &m){ m(); }, mod)` inside their worker thread (both types expose `void operator()() const`, so **`scriptModules.hpp` needs no base class**); the variant lives only in the spawn path, not in the slot.
- **Runtime (re)configuration.** Spawn/teardown is factored out of the constructor/destructor into `startModules_(std::vector<ModuleSpec>)` and `stopModules_()`, so the live module set can change after construction. The `Q_INVOKABLE void setModules(const QVariantList &specs)` is the path the QML config uses: it `stopModules_()`s, parses each `QVariantMap` (`kind` 0=Timed/1=Signal, `script` string, `interval` seconds, `signalOffset` → `signalNumber = SIGRTMIN + offset`, optional `outputLimit`; entries with an empty `script` are skipped), `startModules_()`s, then clears `text_` and emits so stale output disappears immediately. `stopModules_()` resets state for reuse (`slots_.clear()`, etc.) and is safe to call when nothing is running; `startModules_()` clears `*stop_` first since the instance may be restarting.
- A `ModuleSlot` (one per module, kept in `slots_` in display order) holds the worker + bridge threads, observer raw pointers into the worker (`changeSignal`, `outputBuffer`), the per-module `fragment` `QString`, and — for signal modules only — a per-module `execute` flag, an `executionSignal` observer, and the `signalNumber`. `slots_` is a `std::deque` (not `vector`) precisely because the bridges capture `&slot` fields: `deque` keeps references to existing elements valid when new ones are appended, so pointer stability does not depend on pre-reserving capacity.
- One `bridgeLoop_(ModuleSlot *slot)` runs per slot: it waits on the slot's change CV, snapshots+clears its `outputBuffer` under the shared `mutex_`, then posts a lambda to the GUI thread (`QMetaObject::invokeMethod(Qt::QueuedConnection)`) that updates the slot's `fragment`, calls `rebuildCombinedText_()` (joins all fragments in order with `" | "`), and `emit textChanged()`. Fragments are read/written only on the GUI thread. The bridge short-circuits redundant updates (`if (slot->fragment == newText) return;`).
- **Signal triggering:** there is no portable C++17 way to handle `SIGRTMIN+N` — `std::signal` covers only the six standard signals, and a handler may call only async-signal-safe functions (a `mutex`/`condition_variable` is not). So a file-scope POSIX `sigaction` handler does only a lock-free atomic store into `gSignalFired` plus `sem_post()`; a single `signalWaitLoop_` thread (serving all signal modules) drains the semaphore with `sem_wait` (retrying on `EINTR`), then `exchange(false)`s each fired flag and triggers the matching slot's `execute`/`executionSignal`. `gSignalFired` is a `std::array<std::atomic<bool>, _NSIG>` indexed by **raw signal number**: `_NSIG` (65) is a macro constant so it can size a `std::array`, whereas `SIGRTMIN`/`SIGRTMAX` are glibc runtime calls (`__libc_current_sigrt*()`) and cannot. The array's size is `constexpr`; the array object is not (the handler mutates it). (POSIX symbols are visible because ECM's `KDECompilerSettings` defines `_GNU_SOURCE`, despite `-std=c++17` without extensions.) The trigger semaphore (`gTriggerSemaphore`) and handlers are file-scope globals shared across **all** `ScriptOutput` instances, so `startModules_` only `sem_init`s / installs handlers / spawns `signalWaitLoop_` when a signal module is actually present, tracking that in `signalsActive_`; `stopModules_` undoes exactly what was set up. This lets a module-less instance — e.g. the throwaway `ScriptOutput` the config UI builds just to read `maxSignalOffset` — coexist with the running widget without touching the shared semaphore. (Two instances each with signal modules remain a pre-existing limitation of the global table.)
- `stopModules_()` (called by both the destructor and `setModules`) sets `*stop_ = true`, notifies all CVs, and `sem_post`s to wake the wait loop, then **joins every bridge and the signal-wait loop before the workers** — those observers reference worker-owned `condition_variable`/`std::string` objects (and, for the wait loop, `SignalModule`-owned `executionSignal`s) through raw pointers, so they must exit first. Then (only if `signalsActive_`) it points each used signal at `SIG_IGN` and `sem_destroy`s, so a late signal cannot post to a destroyed semaphore, and finally clears `slots_`.

**QML module URI vs KPackage applet ID** — these are deliberately distinct because QML import URIs must be dot-separated identifiers and may not contain hyphens (a hyphen in the URI triggers a QML parse error at the `import` line, which plasmashell reports as the same misleading "unknown older version of Plasma" error described above):
- QML module URI: `com.github.tonymugen.plasmashowstdout` — used in `qmldir`, in the QML `import` statement, and as the install subpath under `${KDE_INSTALL_QMLDIR}/`.
- KPackage applet ID: `com.github.tonymugen.plasma-show-stdout` — used by `plasma_install_package()`, in `metadata.json`'s `KPlugin.Id`, and as the argument to `plasmawindowed`/`plasmoidviewer`.

**QML plugin module** (`plugin/qmldir`) maps the URI to the `plasma_show_stdout` shared library and declares `classname ShowStdoutPlugin`. Both the library and `qmldir` install to `${KDE_INSTALL_QMLDIR}/com/github/tonymugen/plasmashowstdout`.

**Plasma package** (`package/`) installs under the applet ID `com.github.tonymugen.plasma-show-stdout` via `plasma_install_package()`. `package/contents/ui/main.qml` is a `PlasmoidItem` with a `compactRepresentation` (terminal icon, click-to-expand) and a `fullRepresentation` (scrollable monospace `Label` bound to `root.scriptOutput.text`); colors track the active scheme via `Kirigami.Theme.View` (with `inherit: false`). `Plasmoid.status` is currently pinned to `ActiveStatus` and `X-Plasma-NotificationAreaCategory` is set to `SystemServices` so the widget can live in the system tray. The `ScriptOutput { id: scriptOutput }` lives at the **root** `PlasmoidItem` (not inside `fullRepresentation`) so the configured modules run whether or not the widget is expanded; `main.qml`'s `applyConfig()` builds the `setModules` argument from `Plasmoid.configuration` and is invoked on `Component.onCompleted` and from a `Connections { target: Plasmoid.configuration }` block on each key's change signal.

**Package config** (the first config version — a single user-defined script):
- `package/contents/config/main.xml` — KConfigXT schema, group `General`: `scriptPath` (String), `triggerKind` (Int: `0`=Timed, `1`=Signal, matching `ModuleSpec::Kind` order), `interval` (Int seconds), `signalOffset` (Int, the RTMIN+N offset). `outputLimit` is deliberately **not** exposed yet — specs fall back to `ModuleSpec`'s default of 300.
- `package/contents/config/config.qml` — a `ConfigModel` with one `ConfigCategory` ("General") sourcing `configGeneral.qml`.
- `package/contents/ui/configGeneral.qml` — a `KCM.SimpleKCM` + `Kirigami.FormLayout` using the standard `cfg_<key>` alias convention (Plasma auto-loads/saves). A `TextField` + `Browse…` `FileDialog` for the script, a `ComboBox` for Timed/Signal, and conditionally a seconds `SpinBox` (Timed) or an offset `SpinBox` ranged `0..signalInfo.maxSignalOffset` (Signal). The signal offset is converted to a raw signal **C++-side** in `setModules` (QML can't know `SIGRTMIN`); a module-less `ScriptOutput { id: signalInfo }` probe supplies `maxSignalOffset` for the spinbox range.

## Conventions

- C++ standard: 17 (no extensions)
- Compiler warnings are maximally strict (see `CMakeLists.txt` for the full list); new code must compile cleanly
- Address and leak sanitizers are auto-enabled in `Test` builds when the compiler supports them
- Namespace: `PSSspace` for all plugin C++ code
