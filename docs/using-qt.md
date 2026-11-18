# Using Omarchy UI from Qt Quick

> Paths such as `examples/...` and `qt/...` are relative to a checkout and work as written.

The Qt binding is two QML modules, plus a small native module they use:

| Import | What's in it |
|---|---|
| `Omarchy.Theme` | Singletons: `Color` (palette and surfaces), `Style` (rounding, spacing, fonts, motion, state fills), `Border` (border specs), `Util`, `I18n` (the kit's strings in the display language), `Tokens` (everything as canonical JSON, and the changed hook) |
| `Omarchy.Ui` | The components: `Button`, `Toggle`, `TextField`, `Dropdown`, `Card`, `Pane`, `Heading`, `ListRow`, `Menu`, `Dialog` and the rest ([`spec/components.md`](../spec/components.md)) |
| `Omarchy.Theme.Native` | `ThemeSource` (reads the theme, Hyprland and fontconfig, and watches them) and `IconLookup`. You don't import this yourself: `Omarchy.Theme` does. |

## Two ways in

| | Installed modules | Vendored `.pri` |
|---|---|---|
| How | The `omarchy-ui` Arch package (or `make install`) puts the modules in Qt's QML directory | `include(omarchy-ui/qt/omarchy-ui.pri)` in your `.pro` compiles the kit into your binary |
| Works from | Anything that runs QML on the system's Qt: the `qml` tool, a C++ app, PySide6 (Arch's), Quickshell | qmake apps |
| Your build | Nothing to add, no C++ call | One `include()`, one `OmarchyUi::registerTypes()` call |
| Kit version | Whatever is installed; it updates with the package | Exactly the commit you vendored |
| At runtime | The package must be installed | Nothing; the binary is self-contained |

Pick **installed** for scripts, single-file QML tools, Quickshell configs, Python apps, and Omarchy apps that are themselves Arch packages (add `omarchy-ui` to `depends`). Pick **vendored** when the app ships as one binary, must pin the kit's exact version, or can't count on the package being there.

If an app vendors the kit and the package is also installed, the vendored copy wins entirely. Its QML is embedded at `qrc:/qt/qml`, which Qt searches before its own QML directory, and its `Omarchy/Theme/Native/qmldir` names no plugin, so the installed plugin is never loaded. The one way to change that order is an app calling `engine.addImportPath()` on the installed directory itself; don't.

## Installed modules

### Install

From a checkout of [omacom/omarchy-ui](https://github.com/omacom/omarchy-ui):

```sh
cd packaging/arch/omarchy-ui && makepkg -fsi       # the Arch package: modules + omarchy-theme CLI
```

Or without a package:

```sh
mkdir -p build/qt-module && cd build/qt-module
qmake6 ../../qt/module/module.pro && make
sudo make install                                   # into $(qmake6 -query QT_INSTALL_QML)/Omarchy
make install INSTALL_ROOT=/tmp/stage                # or staged, for packaging or a look
```

To install for one user without root, choose the directory at qmake time and put it on the import path:

```sh
qmake6 ../../qt/module/module.pro QML_DIR=$HOME/.local/lib/qt6/qml && make && make install
export QML_IMPORT_PATH=$HOME/.local/lib/qt6/qml
```

Either way the result is:

```text
<qml dir>/Omarchy/Theme/          qmldir, Color.qml, Style.qml, ... (the theme singletons)
<qml dir>/Omarchy/Theme/Native/   qmldir, libomarchyuiplugin.so  (ThemeSource, IconLookup)
<qml dir>/Omarchy/Ui/             qmldir, Button.qml, Toggle.qml, ... (the components)
```

### A minimal app

One QML file, nothing to build ([`examples/qt-installed/main.qml`](../examples/qt-installed/main.qml)):

```qml
import QtQuick
import QtQuick.Layouts
import Omarchy.Theme as Theme
import Omarchy.Ui as O

Window {
  id: root
  width: 400
  height: panel.implicitHeight + 2 * Theme.Style.spacing.panelPadding
  visible: true
  color: Theme.Color.background

  property bool wifi: true

  O.Pane {
    id: panel
    anchors.fill: parent
    anchors.margins: Theme.Style.spacing.panelPadding
    bordered: true

    O.Heading { text: "Network"; subtitle: "Wi-Fi and saved networks"; level: "heading"; Layout.fillWidth: true }
    O.Toggle {
      label: "Wi-Fi"; description: root.wifi ? "On" : "Off"
      checked: root.wifi; onClicked: root.wifi = !root.wifi
      Layout.fillWidth: true
    }
    O.ListRow { glyph: String.fromCodePoint(0xF0928); title: "Basecamp"; status: "Connected"; statusTone: "success"; current: true; Layout.fillWidth: true }
    O.Button { text: "Forget network"; bordered: true }
  }
}
```

```sh
QT_QUICK_CONTROLS_STYLE=Basic qml6 main.qml
```

Use `qml6` (or `/usr/lib/qt6/bin/qml`): on Arch, `/usr/bin/qml` can be Qt 5's, from `qt5-declarative`.

The same file works from any other QML host on the system's Qt:

- **C++**: a plain `QQmlApplicationEngine` loading it. Link nothing extra and call nothing; the engine finds the modules and loads the plugin itself.
- **PySide6** (Arch's `pyside6`, which uses the system Qt; the wheel from pip bundles its own Qt and can't load this plugin):

  ```python
  import os, sys
  os.environ.setdefault("QT_QUICK_CONTROLS_STYLE", "Basic")
  from PySide6.QtGui import QGuiApplication
  from PySide6.QtQml import QQmlApplicationEngine

  app = QGuiApplication(sys.argv)
  engine = QQmlApplicationEngine("main.qml")
  if not engine.rootObjects():
      sys.exit(1)
  sys.exit(app.exec())
  ```

- **Quickshell**: import the modules in any config file (`FloatingWindow { O.Button { ... } }`) and launch it with `QT_QUICK_CONTROLS_STYLE=Basic`.

## Vendored `.pri`

### Add it

Put the repository inside yours, pinned to a commit:

```sh
git submodule add https://github.com/omacom/omarchy-ui third_party/omarchy-ui
```

(or any checkout; a path is all qmake needs). Then ([`examples/qt-vendored`](../examples/qt-vendored)):

```qmake
# app.pro
TEMPLATE = app
QT += quick
include(third_party/omarchy-ui/qt/omarchy-ui.pri)
SOURCES += main.cpp
app_qml.files = Main.qml
app_qml.prefix = /app
RESOURCES += app_qml
```

```cpp
// main.cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include "omarchyui.h"

int main(int argc, char **argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QGuiApplication app(argc, argv);
  OmarchyUi::registerTypes();            // before the engine loads anything
  QQmlApplicationEngine engine(QUrl("qrc:/app/Main.qml"));
  return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
```

`Main.qml` is the same QML as above. The `.pri` adds `quick`, `quickcontrols2` and `network` to `QT`, compiles `qt/src/*` (the native types) into the app, and embeds the QML under `qrc:/qt/qml`, where the engine finds `import Omarchy.Theme` and `import Omarchy.Ui` with no import path set.

## Writing views with the kit

### Qualify the imports

```qml
import Omarchy.Theme as Theme
import Omarchy.Ui as O
```

and write `O.Button`, `O.TextField`, `Theme.Color.accent`. The kit has types named like Qt Quick's and Qt Quick Controls' (`Button`, `TextField`, `Dialog`, `Menu`, `Tooltip`, `SplitView`, ...), and when two unqualified imports both have a name, QML takes the last import's silently: you get a stock Controls button with no error. Qualified, there's nothing to guess.

### Use tokens, never literals

Colors are `Theme.Color.*` (`foreground`, `background`, `accent`, `urgent`, `muted`, and surfaces like `Theme.Color.popups.background`). Structure is `Theme.Style.*` (`cornerRadius`, `spacing.*`, `font.*`, `duration(ms)`, `hoverFill` and the other state fills). Borders are `Theme.Border.*` specs, drawn with `O.BorderSurface`. A color, radius, font, size or duration a token covers is never a literal. [`spec/tokens.md`](../spec/tokens.md) lists them all; [`AGENTS.md`](../AGENTS.md) has the rules that make a view read as Omarchy.

### `QT_QUICK_CONTROLS_STYLE=Basic`

A few components are built on Qt Quick Controls (`Dropdown`, `NumberField`, `Tooltip`, `MultiSelect`, ...) and draw every part themselves. Under the Basic style nothing else draws underneath them. Under a platform style (Breeze, Fusion, the desktop's) you get its padding, focus frames and scroll indicators mixed in. Set it before the application object exists: `qputenv("QT_QUICK_CONTROLS_STYLE", "Basic")` or `QQuickStyle::setStyle("Basic")` in C++, `os.environ` in Python, or the environment for `qml6` and Quickshell.

### Bind a dropdown to confirmed state

`O.Dropdown` and `O.SearchableDropdown` update their `value` and emit `changed(value)` when a user selects an option. Set `updateValueOnSelection: false` if an app owns the value. In that mode `changed(value)` requests a change and the dropdown keeps the existing binding, so it shows the selection only after the app's model confirms it.

```qml
O.Dropdown {
  label: "Audio profile"
  options: audio.profiles
  value: audio.activeProfile
  updateValueOnSelection: false
  onChanged: function(profile) { audio.requestProfile(profile) }
}
```

### The theme follows itself

Bind to tokens and the view follows the desktop live, with no restart and no code. `ThemeSource` watches:

- theme switches (`~/.local/state/omarchy/current/theme`) and edits inside the theme
- `~/.config/omarchy/shell.toml` (`omarchy display text size`)
- Hyprland's rounding, `gaps_out`, animations and border size (re-read on `configreloaded`)
- fontconfig's `monospace` (`omarchy font set`)
- the display language (`omarchy language`)
- the icon theme the Omarchy theme names

The one way to break this is copying a token into a plain value at startup (`property color c` set once from JS, a color cached in C++). Keep bindings. The contract every binding shares is [`spec/following.md`](../spec/following.md).

**The transition.** On a theme switch the colors ease from the old theme to the new over 280 ms (`Theme.Style.themeFade`) on CSS's `ease` curve (`Theme.Style.ease`), which reads the same as the Rune kit's color spring. Every color fades: the palette, secondary text, surfaces, state fills and borders. Rounding, gaps, sizes and the font switch at once. There's no fade on the first frame, under reduced motion (Hyprland animations off, `OMARCHY_UI_REDUCE_MOTION=1`), or in any token dump, so snapshots are exact. `Theme.Color.fading` is true while it runs.

**The changed hook.** For what bindings can't reach (a `Canvas`, a cache, a value handed to something outside QML), the `Tokens` singleton announces each change:

```qml
import Omarchy.Theme as Theme

Connections {
  target: Theme.Tokens
  function onChanged(tokens) {        // the canonical JSON of spec/tokens.md, "#rrggbbaa" colors
    chart.requestPaint()
  }
}
```

- `changed(tokens)` fires once per change that alters the tokens, after `Color` and `Style` have re-resolved (they already read the new values in the handler), on the GUI thread, before the fade starts. Never for the theme the app started with.
- `Tokens.revision` is 0 at start, plus one per change: a cache key (`if (cachedRevision !== Theme.Tokens.revision) ...`).
- `Tokens.current` is the tokens in effect, as an object.

**From C++.** An app with its own renderer (a `QQuickPaintedItem`, scene-graph nodes) gets the same hook from `omarchyui.h`, in the vendored route:

```cpp
#include "omarchyui.h"

QObject::connect(OmarchyUi::Theme::instance(), &OmarchyUi::Theme::changed, renderer,
                 [renderer](const QJsonObject &tokens) {
                   renderer->setAccent(OmarchyUi::Theme::color(tokens["palette"]["accent"]));
                   renderer->update();
                 });
```

`OmarchyUi::Theme::tokens()` and `revision()` hold the current set once QML has loaded `Omarchy.Theme`, and `OmarchyUi::Theme::color()` reads a `"#rrggbbaa"` token (which `QColor` alone would take as `#aarrggbb`). With the installed modules, the plugin's C++ isn't linkable; connect to the QML singleton instead:

```cpp
QObject *tokens = engine.singletonInstance<QObject *>("Omarchy.Theme", "Tokens");
QObject::connect(tokens, SIGNAL(changed(QVariant)), renderer, SLOT(onThemeChanged(QVariant)));
```

### Preview other themes, roundings and icon themes

Every run reads these overrides, in both routes ([`spec/tokens.md`](../spec/tokens.md)):

| Variable | Effect |
|---|---|
| `OMARCHY_UI_THEME_DIR` | Read `colors.toml` / `shell.toml` from this rendered theme directory instead of the current theme. The user's `shell.toml` is ignored and nothing is watched, so a render is repeatable. |
| `OMARCHY_UI_ROUNDING` | Hyprland's `decoration:rounding`; `0` is sharp, `8` is rounded |
| `OMARCHY_UI_GAPS_OUT` | Hyprland's `general:gaps_out` |
| `OMARCHY_UI_REDUCE_MOTION` | `1` reduced motion, `0` motion on |
| `OMARCHY_UI_FONT_FAMILY` | The font family, instead of fontconfig's `monospace` |
| `OMARCHY_UI_ICON_THEME` | The icon theme, instead of the one the theme names |
| `OMARCHY_UI_ICON_DIRS` | Icon search roots (colon-separated), instead of the XDG directories |

```sh
OMARCHY_UI_THEME_DIR=/usr/share/omarchy-ui/themes/catppuccin-latte OMARCHY_UI_ROUNDING=8 qml6 main.qml
```

The theme directory has to be rendered, the way `omarchy-theme-set` leaves `~/.local/state/omarchy/current/theme`: Omarchy's source theme directories have no `shell.toml`. Every Omarchy theme comes rendered in `fixtures/themes/` in a checkout, and the package installs the same set to `/usr/share/omarchy-ui/themes/`. Light ones are `catppuccin-latte`, `flexoki-light` and `white`.

### Render snapshots for review

Look at a view in more than one theme and rounding before calling it done.

- **Installed:** `snap.qml` renders a QML file whose root is a `Window` to a PNG with the `qml` tool. The package installs it at `/usr/share/omarchy-ui/snap.qml`; in a checkout it's [`examples/qt-installed/snap.qml`](../examples/qt-installed/snap.qml). Paths must be absolute:

  ```sh
  QT_QPA_PLATFORM=offscreen QT_QUICK_CONTROLS_STYLE=Basic QT_SCALE_FACTOR=2 \
  OMARCHY_UI_THEME_DIR=/usr/share/omarchy-ui/themes/catppuccin-latte OMARCHY_UI_ROUNDING=8 \
    qml6 /usr/share/omarchy-ui/snap.qml -- "$PWD/main.qml" "$PWD/latte-r8.png"
  ```

- **From a checkout:** the gallery renders any QML file (an `Item` or a `Window`) with the kit, offscreen at 2x: `bin/build qt && build/qt/omarchy-qt-gallery --qml view.qml --snap out.png`.
- **In your app:** grab the window yourself, as [`examples/qt-vendored/main.cpp`](../examples/qt-vendored/main.cpp) does for `--snap out.png` (`QT_QPA_PLATFORM=offscreen`, `QT_SCALE_FACTOR=2`, then `window->grabWindow().save(path)` after a short timer).

## Versioning and pinning

The kit is pre-1.0 and has no release tags yet. The QML modules are version 1.0 (imports need no version), and a component's properties can still change between commits.

- **Vendored:** you pin by the commit you vendor (the submodule's commit). Update on purpose, render your views, commit.
- **Installed:** the package's version names the commit it was built from (`0.1.0.r<commits>.g<hash>`; `pacman -Qi omarchy-ui`). Everything on the machine shares that one version.
- **Qt:** the plugin is built against the installed Qt and loads only into that version or a newer 6.x. Rebuild the package when Qt updates, like any package with a QML plugin.

## Where things are documented

- [`spec/components.md`](../spec/components.md) and [`spec/components/`](../spec/components): every component, its Qt and Rune names, states and metrics.
- The comment at the top of each `qt/qml/Omarchy/Ui/<Name>.qml`: its properties and a usage example.
- [`spec/tokens.md`](../spec/tokens.md): every token and how it's resolved.
- The gallery (`build/qt/omarchy-qt-gallery`, sections from [`spec/gallery.md`](../spec/gallery.md)): every component live; `qt/gallery/sections/*.qml` is example code.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| `module "Omarchy.Ui" is not installed` | Not installed, or installed where this Qt doesn't look. Check `ls $(qmake6 -query QT_INSTALL_QML)/Omarchy`, or set `QML_IMPORT_PATH` to the directory holding `Omarchy/`. `QML_IMPORT_TRACE=1` shows where the engine looks. |
| `module "Omarchy.Theme.Native" is not installed` (vendored) | `OmarchyUi::registerTypes()` wasn't called before the engine loaded QML. |
| `Library import requires a version` | That's Qt 5's `qml` tool. Use `qml6`. |
| `Cannot load library ... libomarchyuiplugin.so` or "uses incompatible Qt library" | The plugin was built for a different Qt: a pip PySide6 with its own Qt, or Qt was updated. Use the system Qt, or rebuild the package. |
| Dropdowns, tooltips or steppers have extra frames or padding | Not running under the Basic style. Set `QT_QUICK_CONTROLS_STYLE=Basic` before the application starts. |
| A stock-looking `Button` or `TextField` | An unqualified name resolved to Qt Quick Controls. Use `O.Button`. |
| The view doesn't follow theme switches | `OMARCHY_UI_THEME_DIR` is set (a pinned theme follows edits to that directory, not the desktop), or a token was copied into a plain value. |
| A custom-drawn part (`Canvas`, a C++ item) keeps the old colors | It doesn't bind to tokens. Repaint it from `Tokens.onChanged` (QML) or `OmarchyUi::Theme::changed` (C++). |
| Colors jump instead of fading on a switch | Reduced motion is on (`omarchy toggle animations`, `OMARCHY_UI_REDUCE_MOTION=1`). |
| Glyphs show as boxes | The kit's glyphs are Nerd Font icons from the theme's `monospace` font (Omarchy's default fonts have them). |
| `console.log` output is missing | Under a systemd session Qt logs to the journal. Set `QT_FORCE_STDERR_LOGGING=1`. |
