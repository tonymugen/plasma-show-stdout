# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A KDE Plasma plasmoid that displays stdout from shell scripts. Scripts are configured via a config panel and run either on a timer or triggered via `RTMIN` signal. Each script runs on its own thread in a C++17 backend exposed to QML as a plugin.

## Build

Requires: CMake ≥3.21, Qt5 (Core, Qml), KF5 (Plasma). ECM is fetched automatically via FetchContent if not present on the system.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --component plasmoid  # installs plasma package + QML plugin
```

`--component plasmoid` is required to avoid installing ECM's own cmake files alongside the plasmoid. The `plasma_install_package()` call is not component-aware and installs unconditionally regardless.

Build types: `Release` (default), `Debug`, `Profile`, `Test`.

### Tests

Tests use Catch2 (fetched automatically via FetchContent):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Test -DBUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Architecture

Two distinct layers communicate via condition variables:

**C++ plugin** (`plugin/`, namespace `PSSspace`) — compiled as shared library `plasma_show_stdout`:
- `runScript(path)`: free function wrapping `popen()`; returns stdout as a string, or an error message string if the script doesn't exist or the pipe fails — errors surface in the widget rather than throwing
- `TimedModule`: calls `runScript` in a loop, truncates to `outputLengthLimit_`, writes to shared `outputString_`, notifies main thread, then sleeps for `refreshInterval_`
- `SignalModule`: waits on `executionSignal_` condition variable, then calls `runScript`, truncates, writes to shared `outputString_`, and notifies main thread
- Both classes own their output string and condition variables via `std::unique_ptr`; they are move-only; each `operator()()` creates a local `std::mutex` for its `unique_lock`

**QML frontend** (`package/contents/ui/main.qml`) imports the plugin as `com.github.tonymugen.plasma-show-stdout 1.0`. The QML plugin registration lives in `plasma-show-stdout.hpp`/`.cpp` (currently skeletal — `QQmlExtensionPlugin` subclass not yet fleshed out).

**QML plugin module** (`plugin/qmldir`) maps the module name to the `plasma_show_stdout` shared library. Both the library and `qmldir` install to `${KDE_INSTALL_QMLDIR}/com/github/tonymugen/plasma-show-stdout`.

**Plasma package** (`package/`) installs under the applet ID `com.github.tonymugen.plasma-show-stdout` via `plasma_install_package()`.

## Conventions

- C++ standard: 17 (no extensions)
- Compiler warnings are maximally strict (see `CMakeLists.txt` for the full list); new code must compile cleanly
- Address and leak sanitizers are auto-enabled in `Test` builds when the compiler supports them
- Namespace: `PSSspace` for all plugin C++ code
