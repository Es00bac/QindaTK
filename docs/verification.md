<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Verification

## Gates

| Gate | Command | Must |
| --- | --- | --- |
| Configure + build | `cmake --preset dev && cmake --build --preset dev` | no warnings from `-Wall -Wextra -Wpedantic` in toolkit code |
| Unit + scene tests | `ctest --preset dev` | 100% pass |
| Examples instantiate | part of `ctest` (`example_<name>` tests run `qtk-preview --check`) | pass |
| Screenshots | `tools/scripts/screenshots.sh` | exit 0; PNGs reviewed by a person or an agent with the Read tool |
| Catalog current | `python3 tools/scripts/gen_catalog.py` then `git diff docs/catalog.json` | no drift left uncommitted |

The `dev` preset builds `build/dev` (Debug, Ninja) with tests, tools and
examples on; `release` builds `build/release` without tests/examples.
Never pass `-j`/`--parallel` limits (see AGENTS.md, build policy).

## Headless environment

Every test and every headless `qtk-preview` run uses:

| Variable | Value | Why |
| --- | --- | --- |
| `QT_QPA_PLATFORM` | `offscreen` | no display needed; windows still expose |
| `QT_QUICK_BACKEND` | `software` | `grabWindow()` works without a GPU |
| `QML_IMPORT_PATH` | `build/dev/qml` | the module from the build tree (tests set it in ctest; `qtk-preview` and the test binaries also compile the path in) |

`ctest --preset dev` sets the platform; `tests/CMakeLists.txt` sets all
three per test. `qtk-preview` sets the first two itself when a headless
option (`--check`, `--dump`, `--dump-json`, `--grab`, `--dump-theme`,
`--list-icons`, `--offscreen`) is present and the variable is unset.

## What each test covers

| Test | File | Covers |
| --- | --- | --- |
| `tst_grid_tracks` | `tests/cpp/tst_grid_tracks.cpp` | track-list grammar (plain, `repeat`, `minmax`, errors) and track sizing: fixed + fr, fr shares with gaps, `auto` content, fr zero-minimum (natural sizes), `minmax` floor, indefinite container, spanning items, auto stretch without fr, percent tracks |
| `tst_svg_path` | `tests/cpp/tst_svg_path.cpp` | SVG path parser: absolute/relative/implicit commands, arcs (half circle bounds, squeezed flags, Lucide circle), cubic/smooth/quadratic, error reporting |
| `tst_theme` | `tests/cpp/tst_theme.cpp` | role table = meta-object properties, sloom-dark values, light preset flag, density rescaling (space/size scale, radius/fonts do not, `scaleFonts`), `applyRoles` derivation and `preset = "custom"`, QST adoption incl. point→pixel fonts, JSON theme loading, colour math, reduced motion |
| `tst_dock_model` | `tests/cpp/tst_dock_model.cpp` | registration and queries (`lanes`, `zoneExtent`, `acceptsZone`), modes (float/hide/show/collapse/toggle/dock with zone rules), groups (join/activate/reorder/ungroup), `dockBeside` (before/after/lane-after/tab, zone rules), geometry (lane extents with minimums, shares, floating clamp, z-order), presets and saved layouts, serialize/deserialize incl. pending arrangements restored before registration |
| `tst_layout` | `tests/cpp/tst_layout.cpp` | rendered `QQuickView` scenes: Flex grow/gaps/padding/stretch/implicit size, shrink with `minWidth`, wrap + `SpaceBetween` + `lineCount`, column align/order/`flex`, Grid tracks/spans/`rowCount`, areas + alignment, dense auto-flow, Stack insets/centre/fill, nested Box→Flex implicit sizes |
| `tst_icon` | `tests/cpp/tst_icon.cpp` | every generated Lucide path parses into a non-empty path inside the 24×24 box (>250 icons), every alias resolves to an existing icon, `layers` has three paths, runtime `registerIcon` works and signals |
| `tst_qml` (`tests/qml/tst_*.qml`) | `tests/cpp/tst_qml.cpp` runner | `tst_smoke.qml`: Label font, Overline uppercase, Box padding+border implicit size, Panel header height, Icon availability and alias resolution, theme presets and ladder values; further `tst_*.qml` files (e.g. `tst_controls_structure.qml`) cover the controls they name |
| `example_<name>` (one per `examples/*/Main.qml`) | `examples/CMakeLists.txt` | each example instantiates headlessly at 1280×800 |

Desktop bridge (only where the QindaQt desktop libraries are installed):
`qtk-preview examples/smoke/Main.qml --qst qinda-dusk --dump` must print
`qst: published qinda-dusk, …` as its first line and render with that
theme's colours (`--grab`). `qtk-preview --list-qst` lists the ids.

The Gentoo ebuild (`packaging/gentoo/dev-libs/qindatk/`) runs the same
suite headlessly in `src_test` with USE `test`.

## Running one test

```
ctest --preset dev -R tst_layout --output-on-failure
# or the binary directly, with the environment ctest would set:
cd build/dev && QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  QML_IMPORT_PATH=$PWD/qml ./tests/tst_layout -v2
```

QML tests: all `tests/qml/tst_*.qml` files run in one binary. One file or
one function:

```
cd build/dev && QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  ./tests/tst_qml -input ../../tests/qml/tst_smoke.qml
./tests/tst_qml -input ../../tests/qml Smoke::test_box_adds_padding_and_border
```

Add a C++ test: `tests/cpp/<name>.cpp` + `qindatk_add_test(<name>)` in
`tests/CMakeLists.txt`. Add a QML test: drop `tests/qml/tst_<name>.qml`
(no CMake change).

## Screenshots

```
tools/scripts/screenshots.sh            # every examples/*/Main.qml → docs/screenshots/<name>.png
tools/scripts/screenshots.sh gallery    # one example (+ comfortable and light variants)
QTK_PREVIEW=build/release/tools/preview/qtk-preview tools/scripts/screenshots.sh
```

The script exits non-zero on any failed render. Review the PNGs (the
Read tool shows them to an agent) before committing them; a screenshot is
documentation and must match the current code.

## Manual on-desktop checklist

Run `qtk-preview examples/gallery/Main.qml` (or the studio shell) on the
QindaQt desktop and check:

- Hover: chips, buttons, rows and seams change to their hover roles and
  back; cursor changes on seams (`SizeHorCursor`/`SizeVerCursor`).
- Focus: Tab moves through controls in reading order; the 2px focus ring
  appears on keyboard focus only (`visualFocus`), not on mouse click.
- Keyboard: Space/Enter activate buttons; arrows step NumberField and
  Slider (Shift ×10); Escape closes menus, popovers, dialogs, the palette.
- Tooltips: every icon-only control shows one after ~600 ms; it does not
  cover the control.
- Density: switch to comfortable at run time (`Tk.Density.mode`); rows,
  controls and gaps grow together, text stays unless `scaleFonts`.
- Theme: switch presets at run time; no element keeps a stale colour
  (that would be a literal); light preset remains legible.
- Docking: drag a panel header → drop overlays appear; drop on an edge
  docks, on a tab strip groups; floating panels raise on press, resize from
  edges; hidden panels come back from the View menu; restart restores the
  arrangement (`storageKey`).
- HiDPI: icons and 1px borders are crisp at 200%.
- Reduced motion: with `Tk.Theme.reducedMotion` on, hover transitions are
  instant.
