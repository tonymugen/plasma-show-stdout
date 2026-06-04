# plasma-show-stdout

A KDE Plasma 6 widget (plasmoid) that displays the standard output of shell scripts directly in your panel. The user can point it at one or more scripts, choose how each is triggered, and the widget shows the combined text output inline — with a configurable delimiter between each script's output — and offers a click-to-open popup for the full, scrollable text.

Each script runs on its own thread in a C++17 back end exposed to QML as a plugin. Two script execution modes are supported:

- **Timed** — the script is re-run on a fixed interval (in seconds) and its output refreshed.
- **Signal** — the script runs only when the host process receives a real-time signal `RTMIN+N` (user chooses `N`). This lets other programs push an update on demand (for example, refresh a status line the moment something changes) rather than polling on a timer.

The output of every configured script is joined with a single, user-configurable delimiter (default `" | "`) into one line. Per-script output limits (counted in characters, UTF-8-aware so multi-byte glyphs are never split) make sure the output fits the panel. The display font is configurable, so glyph-rich fonts (e.g. a Nerd Font) render Unicode and symbol output correctly.

## Requirements

- KDE Plasma 6
- Qt6 (Core, Qml)
- CMake ≥ 3.21 and a C++17 compiler
- Plasma 6 development package (standalone `Plasma` CMake package)
- KF6 I18n (`KF6I18n` CMake package)

[Extra CMake Modules (ECM)](https://api.kde.org/ecm/) is used by the build. If it is not installed system-wide it is fetched automatically via CMake `FetchContent`. [Catch2](https://github.com/catchorg/Catch2) (for the test suite) is likewise fetched on demand.

## Installation

The recommended way to use the widget in a running desktop is a system-wide install, which lands the C++ plugin on a default Qt6 QML import path so no extra environment variables are needed.

```bash
git clone https://github.com/tonymugen/plasma-show-stdout
cd plasma-show-stdout
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build          # installs both the QML plugin and the Plasma package
rm -rf ~/.cache/plasmashell/qmlcache && kbuildsycoca6 --noincremental
kquitapp6 plasmashell && kstart plasmashell
```

Then open **Add Widgets...**, search for **show-stdout**, and drag it onto a panel or the desktop.

> **Tip:** configure the system build from a *clean* `build/` directory. ECM
> caches the QML install dir; reusing a build directory that was first configured
> with a different `CMAKE_INSTALL_PREFIX` can install the plugin to the wrong
> path and make the widget fail to load with a misleading *"written for an unknown
> older version of Plasma"* error. If in doubt, force it with
> `-DKDE_INSTALL_QMLDIR=lib/qt6/qml`.

### User-local install (for development)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=~/.local
cmake --build build
cmake --install build --component plasmoid
kpackagetool6 --install package/ --type Plasma/Applet     # --upgrade on subsequent runs
```

A user-prefix plugin installs under your private QML dir, so launchers must be told where to find it:

```bash
QML_IMPORT_PATH=~/.local/lib/qml plasmawindowed com.github.tonymugen.plasma-show-stdout
```

To make it loadable from `plasmashell`'s *Add Widgets* panel without setting the variable each time, persist `QML_IMPORT_PATH` via `~/.config/plasma-workspace/env/qml-import-path.sh` and restart `plasmashell`.

### Uninstall

```bash
sudo xargs rm -v < build/install_manifest.txt
sudo rm -rf /usr/share/plasma/plasmoids/com.github.tonymugen.plasma-show-stdout
kbuildsycoca6 --noincremental
```

## Usage

1. Add the widget to a panel or the desktop. Before any script is configured it
   shows an **"Add scripts"** prompt.
2. Right-click the widget → **Configure show-stdout...** (or use the *Configure*
   button in `plasmoidviewer` when testing).
3. In the config dialog:
   - Click **Add script** to append a script entry. For each entry:
     - **Browse...** to pick the script file (or type its path).
     - Choose the trigger: **Timed** (set the refresh **interval** in seconds) or
       **Signal** (set the **RTMIN offset** `N`).
     - Optionally set a per-script **output limit** (in characters).
     - **Remove** deletes the entry.
   - Set the **delimiter** that joins script outputs (surrounding spaces matter).
   - Set the **display font** — a glyph-rich monospace font is recommended if your
     scripts emit Unicode or symbols.
4. Click **Apply**. Output appears inline in the panel; click the widget to open
   the scrollable popup with the full text.

Scripts must be executable and runnable as-is (the back end runs them via `popen`). If a script path does not exist, the widget shows a *"<script_name> does not exist"* message in place of its output rather than failing silently.

### Triggering a signal script

A **Signal** script runs only when its host process receives `RTMIN+N`. The host is the process the widget is loaded into: normally `plasmashell` (or `plasmoidviewer` / `plasmawindowed` when testing). The config dialog shows the exact command to use, including the detected host name, for example:

```bash
pkill --signal RTMIN+4 plasmashell
```

Use `pkill` (a real binary) rather than the shell's `kill` builtin: some shells (e.g. zsh) do not accept an `RTMIN+N` signal specification. Sending the signal to the wrong process fails silently, but may have unintended side effects if that process happens to change its behavior on receiving the signal. Match the host name shown in the dialog.

## Development

Build types are `Release` (default), `Debug`, `Profile`, and `Test`.

### Tests

Building tests is optional and unnecessary for normal use. A small performance hit may be incurred when running the plugin compiled for testing. The test suite uses Catch2 and is built and run with CTest. Address and leak sanitizers are enabled automatically in `Test` builds where supported.

```bash
cmake -B buildTest -DCMAKE_BUILD_TYPE=Test -DBUILD_TESTS=ON
cmake --build buildTest
ctest --test-dir buildTest
```

There are two test executables: one for the Qt-free module layer (`runScript`, `truncateUtf8`, `TimedModule`, `SignalModule`) and one for the QML-bridge layer (`ScriptOutput`).

### API documentation

The C++ plugin's public headers are documented with Doxygen. Building the docs is optional (requires `doxygen`); the [doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css) theme is fetched automatically:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_DOCS=ON
cmake --build build --target Doxygen
```

The HTML lands in `plugin/docs/doxygen/html/` (open `index.html`); the project README is used as the front page.

### Translations

The interface is translation-ready: all user-facing strings (the QML UI and the backend's error messages) are wrapped for `gettext`/KI18n, under the domain `plasma_applet_com.github.tonymugen.plasma-show-stdout`. Run `./Messages.sh` to regenerate the `po/<domain>.pot` template. To add a language, create `po/<lang>/<domain>.po` from the template (`msgmerge` to update it later); the build compiles and installs it automatically. No translations ship yet.

### Testing the config dialog

`plasmawindowed` does not expose the widget's context menu, so it cannot reach the config UI. To test configuration, use `plasmoidviewer` (from the `plasma-sdk` package), which has an explicit *Configure* button:

```bash
QML_IMPORT_PATH=~/.local/lib/qml plasmoidviewer -a com.github.tonymugen.plasma-show-stdout
```

When a structural change (e.g. new config files) is not picked up, rebuild the Plasma cache with `kbuildsycoca6 --noincremental` and clear the host's compiled QML cache under `~/.cache/<host>/qmlcache`.

See [`CLAUDE.md`](CLAUDE.md) for a detailed architecture overview and build notes. I used Claude Code (the Opus model) for development, in particular for building and calling the Qt API, debugging widget loading problems, and writing unit tests. I defined step-by-step goals, then inspected and edited the code before each commit.

## License

BSD 3-Clause. See [`LICENSE`](LICENSE).
