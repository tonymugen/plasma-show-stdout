# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A KDE Plasma plasmoid that displays stdout from shell scripts. Scripts are configured via a config panel and run either on a timer or triggered via `RTMIN` signal. Each script runs on its own thread in a C++17 backend exposed to QML as a plugin.

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

### Tests

Tests use Catch2 (fetched automatically via FetchContent):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Test -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

The `tests` executable compiles `plugin/scriptModules.cpp` directly rather than linking `plasma_show_stdout`. This is necessary because `KDECompilerSettings` applies `-fvisibility=hidden` to all targets; without an explicit export annotation, `runScript` is not exported from the shared library.

Tests are in `plugin/tests/tests.cpp` and cover three areas:

- **`runScript`** (`[runScript]`): non-existent path, stdout capture, empty output, multi-buffer output, stderr isolation, non-executable file.
- **`TimedModule`** (`[TimedModule]`): output written after first execution, truncation to `outputLengthLimit_`, loop termination via stop signal, repeated execution across multiple loop iterations.
- **`SignalModule`** (`[SignalModule]`): output written after trigger, truncation, loop termination via stop signal, repeated execution on successive triggers.

The thread tests use the shared `mutex_` for all synchronisation: the test waits on the `signalToMain` raw pointer with a 5-second `wait_for` timeout (so a broken stop mechanism fails fast rather than hanging). Output is read and cleared under the lock to avoid races between test reads and module writes.

## Architecture

Two distinct layers communicate via condition variables:

**C++ plugin** (`plugin/`, namespace `PSSspace`) — compiled as shared library `plasma_show_stdout`:
- `runScript(path)`: free function wrapping `popen()`; returns stdout as a string, or an error message string if the script doesn't exist or the pipe fails — errors surface in the widget rather than throwing
- `TimedModule`: calls `runScript` in a loop; acquires `mutex_`, writes to `outputString_`, notifies `signalToMain_`, then does `stopSignal_->wait_for(mutex_, refreshInterval_, stop_pred)` — if the predicate fires (spawning thread set `*stop_` and notified `stopSignal_`), the loop exits
- `SignalModule`: waits on `executionSignal_` with predicate `*execute_ || *stop_`; if stop, exits; otherwise resets `*execute_`, releases lock, runs script, reacquires lock, writes output, notifies `signalToMain_`; to trigger a run the spawning thread sets `*execute_ = true` and notifies `executionSignal_`; to stop it sets `*stop_ = true` and notifies the same CV
- Both classes are move-only; `mutex_`, `stop_`, and (for `SignalModule`) `execute_` are `shared_ptr` so the spawning thread retains access after constructing the module; `signalToMain_` and `executionSignal_` are `unique_ptr` — the spawning thread keeps a raw pointer before moving them in

**QML frontend** (`package/contents/ui/main.qml`) imports the plugin as `com.github.tonymugen.plasma-show-stdout 1.0`. The QML plugin registration lives in `plasma-show-stdout.hpp`/`.cpp` (currently skeletal — `QQmlExtensionPlugin` subclass not yet fleshed out).

**QML plugin module** (`plugin/qmldir`) maps the module name to the `plasma_show_stdout` shared library. Both the library and `qmldir` install to `${KDE_INSTALL_QMLDIR}/com/github/tonymugen/plasma-show-stdout`.

**Plasma package** (`package/`) installs under the applet ID `com.github.tonymugen.plasma-show-stdout` via `plasma_install_package()`.

## Conventions

- C++ standard: 17 (no extensions)
- Compiler warnings are maximally strict (see `CMakeLists.txt` for the full list); new code must compile cleanly
- Address and leak sanitizers are auto-enabled in `Test` builds when the compiler supports them
- Namespace: `PSSspace` for all plugin C++ code
