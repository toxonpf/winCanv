# Workspace Templates

`workspace-templates` is a KDE Plasma 6 / Wayland desktop utility for saving and restoring workspace layouts.

The current app is implemented in Qt 6 / C++ and uses short-lived KWin JavaScript scripts loaded over D-Bus, so window discovery and window placement are performed by the compositor instead of X11-era tools such as `xdotool` or `wmctrl`.

## Features

- Save the current workspace as JSON
- Restore saved templates on demand
- Optional "close everything first" restore mode
- Detect already-running apps and avoid unnecessary duplicates
- Delay-aware window placement after app launch
- System tray drawer for fast access
- Left-side drawer panel with slide-in animation
- CLI for automation and hotkeys
- Wayland-native KDE integration through `org.kde.KWin`

## Architecture

The C++ app is split into a few focused modules:

1. `src/main.cpp`
   - Splits CLI mode from tray/UI mode.
   - Keeps lightweight commands such as `list` and `--help` independent from the GUI stack.

2. `src/workspace_manager.cpp`
   - Orchestrates save, load, duplicate avoidance, and launch flow.
   - Lazily initializes the KWin bridge only when a compositor operation is actually needed.

3. `src/kwin_controller.cpp`
   - Loads short-lived KWin JavaScript files through `org.kde.KWin` `/Scripting`.
   - Uses KWin's scripting API to inspect windows, close windows, and apply `frameGeometry`.
   - Sends results back through a small per-process D-Bus bridge.

4. `src/workspace_panel.cpp`
   - Implements the tray-hosted drawer UI.
   - Keeps long save/load work off the UI thread and accepts toggle commands over a local Qt socket.

Why this design works well on Plasma 6 / Wayland:

- Capture is compositor-native: the KWin script reads managed windows from `workspace.stackingOrder`.
- Restore is compositor-native: the KWin script reassigns desktops and writes `frameGeometry`.
- No X11 compatibility shims are used.
- The non-KWin parts stay local, testable, and cheap to start.

## Project Structure

```text
src/
├── app_paths.cpp
├── control_service.cpp
├── desktop_entries.cpp
├── kwin_controller.cpp
├── main.cpp
├── models.cpp
├── template_store.cpp
├── workspace_manager.cpp
└── workspace_panel.cpp
workspace_templates/assets/
├── io.github.toxonpf.workspacetemplates.desktop
└── kwin-shortcut/
examples/
└── engineering-day.json
```

The old Python sources are still in the repository as a migration reference, but the build and package entrypoints now target the Qt/C++ application.

## Install On Arch / CachyOS

Build and install the package from this checkout with `makepkg`:

```bash
makepkg -si
```

Or build it and install the resulting package with `pacman`:

```bash
makepkg
sudo pacman -U workspace-templates-0.1.0-1-x86_64.pkg.tar.zst
```

For AUR publishing, the repository includes `PKGBUILD`, `.SRCINFO`, and `workspace-templates.install`.
For `yay`, you still need to publish this package recipe to AUR. For direct local installation, `pacman -U` works right away after `makepkg`.

## Build From Source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Optional autostart:

```bash
mkdir -p ~/.config/autostart
cp /usr/share/applications/io.github.toxonpf.workspacetemplates.desktop ~/.config/autostart/
```

## Run

Start the tray app:

```bash
workspace-templates
```

Toggle the left drawer panel from an existing app instance:

```bash
workspace-templates toggle-panel
```

## Meta+W Through KWin

The project includes a small KWin script package that registers `Meta+W` and calls the app over D-Bus.

If the package is installed system-wide, the script files are already placed in `/usr/share/kwin/scripts/workspacetemplates-shortcut`.

To enable it from an unpacked checkout:

```bash
kpackagetool6 --type=KWin/Script -i "$(pwd)/workspace_templates/assets/kwin-shortcut"
qdbus6 org.kde.KWin /KWin reconfigure
```

Then enable `Workspace Templates Shortcut` in:

```text
System Settings -> Window Management -> KWin Scripts
```

For reliable hotkey behavior, keep the app running in the background:

```bash
workspace-templates
```

The panel opens on the left edge of the screen where the cursor currently is.

CLI examples:

```bash
workspace-templates save --name "Morning"
workspace-templates list
workspace-templates load "Morning" --close-existing
workspace-templates delete "Morning"
```

Templates are stored in:

```text
~/.local/share/workspace-templates/templates/
```

## Example Template JSON

See [`examples/engineering-day.json`](examples/engineering-day.json).

## Hotkey Setup

The cleanest KDE-native route is to bind the CLI commands in System Settings:

1. Open `System Settings -> Keyboard -> Shortcuts -> Custom Shortcuts`.
2. Add a new `Command/URL` action.
3. Use one of these commands:

```bash
workspace-templates toggle-panel
workspace-templates load "Morning" --close-existing
workspace-templates save --name "Scratchpad"
```

4. If you do not want to use the bundled KWin shortcut script, bind `workspace-templates toggle-panel` to the shortcut you want, for example `Meta+W`.
5. Bind direct load commands to `Meta+1`, `Meta+2`, etc. if you want one-shot restore shortcuts too.

The drawer action talks to the running app over a local Qt socket, so repeated presses toggle the same panel instead of spawning a new window each time.

## Notes

- KWin scripting on Plasma 6 documents `workspace.stackingOrder`; this project uses that instead of leaning on the older `clientList()` alias.
- The `org.kde.KWin` `/Scripting` D-Bus entry point is lightly documented in user docs, but it is used directly by KWin tooling and by KWin's own scripting plumbing.
- Some apps do not faithfully create a new top-level window when relaunched. Browsers and single-instance Electron apps can still attach to an existing process. The restore flow handles this as well as KWin allows, but that behavior is still application-specific.
