# QindaTK — Agent Instructions

QindaTK is a Qt 6 / QML toolkit for **dense, visually controlled desktop
UIs** — the layout freedom of a CSS/Electron app (flexbox, grid, absolute
stacks, overflow scrolling, per-side borders, docking) without a browser.
It is native QtQuick: a shared C++ library (`qindatk`) plus a QML module
(`QindaTK`), themed after Sloom Studio's dark, cyan-accented, 10–12px
chrome and built to be adopted by QindaStudio and other apps on the
QindaQt desktop.

This file is the source of truth for working in this repository. Read it
before touching anything; read `docs/` for the module you touch.

## Non-negotiable rules

1. **One import, one prefix.** Every QML file — inside the module too —
   uses `import QindaTK as Tk` and refers to toolkit types as `Tk.Flex`,
   `Tk.Theme`, `Tk.Label`. Unqualified names collide with QtQuick (`Grid`,
   `Label`, `Button`) and the collision is silent.
2. **No literal colours, sizes, fonts or durations in controls.** Everything
   comes from `Tk.Theme.color.*`, `Tk.Theme.font.*`, `Tk.Theme.space.*`,
   `Tk.Theme.radius.*`, `Tk.Theme.size.*`, `Tk.Theme.motion.*`,
   `Tk.Theme.opacity.*`. If a role you need is missing, add a *derived* role
   in `theme_presets.cpp` (with its mix ratio) and a Q_PROPERTY in
   `theme.h` + the role table in `theme.cpp`; `tst_theme` enforces the
   table. Examples may use literals for demo data only.
3. **Controls are QtQuick.Templates + our visuals.** Behaviour (focus,
   keyboard, accessibility, checked state) comes from
   `import QtQuick.Templates as T`; never from a QtQuick.Controls style.
   Never `QQuickStyle`-specific code, never QSS.
4. **Layout is Flex/Grid/Stack/Box/Scroll.** Do not use QtQuick.Layouts or
   positioners in toolkit or example code; the point of the toolkit is one
   layout model with CSS semantics (see `docs/layout.md`). `anchors` are
   fine for overlays and for filling a parent.
5. **Flex/Grid/Stack own their implicit size.** Binding `implicitWidth` on
   one of them is overwritten by the layout. Wrap in an `Item` (see
   `SectionHeader.qml`) when a component must report a fixed size.
6. **Dock arrangement lives in `DockModel` (C++).** QML draws it and reports
   gestures. Never keep panel placement state in QML.
7. **Every control has a `tooltip`/accessible name path** and sets
   `Accessible.role`. Icon-only controls must set `tooltip`.
8. **objectName discipline**: internal parts get stable, documented
   objectNames (`panelHeader`, `panelBody`, `sectionTitle`); tests and
   `qtk-preview --dump` rely on them. Never rename one without updating
   `docs/controls.md` and the tests.
9. **Files ≤ 500 non-blank lines** (generated `icon_data.cpp` exempt). Split
   rather than grow. SPDX header `LGPL-3.0-or-later` in every file.
10. **Markers for future agents**: `AGENT-NOTE:` (why a surprising design
    exists, with its source of truth), `AGENT-GUARD:` (an invariant a nearby
    edit must keep, and how it fails), `AGENT-CONTRACT:` (a requirement
    shared across a boundary, both sides named). A stale marker is a
    defect; update or delete it with the code.
11. **Documentation is part of the change.** A new control updates
    `docs/controls.md`; a layout semantic change updates `docs/layout.md`;
    a durable cross-cutting choice gets an entry in `docs/decisions.md`.
    Regenerate `docs/catalog.json` with `tools/scripts/gen_catalog.py`.
12. **Write complete files.** Other agents may be building from this tree
    concurrently; a half-written `.qml` breaks their qmlcachegen step.

## Build policy (explicit user requirement — non-negotiable)

- Never override the user's configured system `MAKEOPTS`. Plain
  `cmake --build` / `ninja` use all cores and are compliant; do not add
  `-j`, `--parallel`, `CMAKE_BUILD_PARALLEL_LEVEL`, job pools or presets
  that reduce parallelism. Read the real value with
  `portageq envvar MAKEOPTS` if you need it (currently `-j24 -l24`).
- Only an explicit user instruction authorizes an exception.

## Build, test, verify

```
cmake --preset dev            # build/dev, Debug, Ninja, tests + tools + examples
cmake --build --preset dev
ctest --preset dev            # headless: offscreen platform + software renderer
```

Verification tools (`build/dev/tools/preview/qtk-preview`):

```
qtk-preview file.qml                      # show on screen (needs a display)
qtk-preview file.qml --check              # exit 0 iff the file instantiates
qtk-preview file.qml --dump               # item tree: Type#objectName x,y WxH "text"
qtk-preview file.qml --dump-json          # same, machine readable
qtk-preview file.qml --grab out.png --size 1280x800
qtk-preview --dump-theme                  # resolved colours, fonts, ladders
qtk-preview --list-icons                  # built-in icon names
qtk-preview file.qml --theme sloom-light --density comfortable --grab out.png
qtk-preview --list-qst                    # installed QindaQt desktop theme ids
qtk-preview file.qml --qst qinda-dusk --grab out.png   # render under a real desktop theme (bridge)
cmake --build --preset dev --target all_qmllint        # qmllint over every module QML file
```

`cmake --build --preset dev` also writes `src/.qmlls.ini` (git-ignored) so
`qmlls` and editor integrations resolve the module from the build tree.

A layout claim is verified when `--dump` shows the geometry and `--grab`
shows the pixels. Look at the PNG (Read tool) before calling visual work
done. Add the `--dump` line(s) that prove a fix to the commit message or
the report.

When working in parallel with other agents use your own build directory
(`cmake -S . -B build/<name> -G Ninja -DCMAKE_BUILD_TYPE=Debug`) and run
`ctest --test-dir build/<name>`; the QML file list is globbed at configure
time, so re-run cmake after adding files.

## Repository map

```
src/core/       C++: Theme/Density (theme*.cpp, density.cpp), Flex/Grid/Stack
                (flex.cpp, grid.cpp, grid_tracks.cpp, stack.cpp,
                layout_attached.*, layout_info.*), Icon (+icon_data.cpp, generated;
                svg_path.cpp), DockModel (dock_model*.cpp)
src/qml/        QML controls; type name == file name; globbed by CMake
src/bridge/     QindaTK.QindaQt: QST-1 token bridge for the QindaQt desktop
tools/preview/  qtk-preview (+ qst_support: optional QindaQt desktop theme publishing)
tools/scripts/  gen_icons.py (Lucide → icon_data.cpp), gen_catalog.py (docs/catalog.json)
packaging/      Gentoo ebuild for the qindaqt overlay (dev-libs/qindatk)
tests/cpp/      QtTest (tst_*.cpp); tests/qml/ QtQuickTest (tst_*.qml, one runner)
examples/       QML-only examples run by qtk-preview; each is a ctest smoke test
docs/           human + agent documentation (index.md is the map)
```

## How to add a control

1. Read `docs/controls.md` for the naming/size/variant conventions and the
   nearest existing control; copy its structure.
2. Create `src/qml/<Name>.qml` (template base, theme roles, `tooltip`,
   `Accessible.role`, objectNames for internal parts).
3. Re-run cmake (glob), build, `qtk-preview` a scratch QML with the control
   in a `Tk.Flex`, check `--dump` geometry at compact and comfortable
   density, `--grab` and look.
4. Add a case to `tests/qml/tst_controls*.qml` (instantiates, implicit size
   equals the documented control height, key signal fires).
5. Document it in `docs/controls.md`; add it to `examples/gallery`.

## QML test gotchas

- A QtQuickTest `TestCase` is invisible by default; Flex/Grid skip invisible
  children and synthetic mouse events miss them. Set `visible: true` on the
  TestCase (see `tests/qml/tst_controls_structure.qml`).
- Popups (Menu, Dialog, Popover, CommandPalette) are not Items: attach
  `Keys`/`Accessible` to their content items, and read them in
  `qtk-preview --dump` under the `overlay:` section.
- Tooltips: declare `Tk.ToolTip { text: ...; visible: hovered }` as a
  child. The attached form (`ToolTip.text` / `Tk.ToolTip.text`) instantiates
  the QtQuick.Controls *style's* tooltip (Basic in qtk-preview), not ours.
- `T.TextField`/`T.TextArea` put `background` at z −1 and a control with a
  pointer handler is visited before its background: clickable parts
  (clear buttons, step arrows) must be direct children of the control.
- Never use `layer` as an id: `Item.layer` shadows it inside delegates.

## How to verify a layout claim

`qtk-preview x.qml --dump | grep '#name'` gives `x,y WxH`. For flex/grid
math, a C++ test in `tests/cpp/tst_layout.cpp` with an inline QML scene is
the reference pattern (waits for the first frame, then `QTRY_COMPARE`).

## Conventions you will be tempted to break

- `Tk.Flex.grow: 1` (basis auto) grows from content; `Tk.Flex.flex: 1`
  (basis 0) shares space equally regardless of content — pick deliberately.
- `1fr` in `Tk.Grid` is `minmax(0, 1fr)`: it shrinks below content, which
  is what dense panels want (decision D-004). Use `minmax(auto, 1fr)` to
  floor at content.
- `Tk.Box` stretches its single child unless that child anchors itself or
  sets an explicit size (detected via `Tk.LayoutInfo`). Several children
  position themselves with anchors inside `contentItem`.
- `Tk.Scroll` hides horizontal overflow by default so columns wrap to the
  width; set `overflowX: Tk.Scroll.Auto` for wide content (timelines).
- Panel headers are 24px; rows 22px; controls 24px; chips 32px; islands
  36px. These are measured from Sloom Studio, not preferences.
