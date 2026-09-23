<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Decisions

ADR-lite log of durable, cross-cutting choices. Add an entry when a
choice would surprise a future reader; keep entries short.

## D-001 — LGPL-3.0-or-later for the library

**Context.** QindaStudio and the user's applications are GPL-3.0-or-later;
the QindaQt desktop's own libraries (Tokens, Controls, AppShell) are
LGPL-3.0-or-later. **Decision.** QindaTK is LGPL-3.0-or-later, following
the desktop's library precedent, so GPL applications and other software can
link it. **Consequences.** Every file carries the SPDX header;
`COPYING.LESSER` and `COPYING` ship; Lucide's ISC notice is in
`THIRD_PARTY_NOTICES.md`.

## D-002 — One QML module backed by one shared library

**Context.** Layout engines and the theme need C++; controls need to be
editable QML. **Decision.** `qt_add_qml_module(qindatk URI QindaTK)` with a
SHARED backing library and a generated plugin; QML files globbed from
`src/qml`. **Consequences.** QML-only apps need nothing at link time; C++
apps link `QindaTK::qindatk` and get in-process type registration; adding
a control is adding a file and re-running cmake.

## D-003 — Zone/lane dock model, not a free split tree

**Context.** Sloom Studio's `dockablePanel` contract and QindaStudio's
`PanelLayoutController` are zone based (left/right/top/bottom/center/
overlay, columns, tab groups, saved layouts). **Decision.** `DockModel`
keeps that vocabulary — zones, lanes (columns/rows inside a zone), order,
groups, floating rects — and adds `dockBeside`, `lanes()` and per-slot
`share` for drag-to-dock and lane splitting. **Consequences.** QindaStudio
can adopt it by renaming calls; layouts persist as small JSON; a general
split tree (VS Code style) is out of scope.

## D-004 — `Nfr` means `minmax(0, Nfr)` in Grid

**Context.** CSS's `1fr` is `minmax(auto, 1fr)`; dense panels almost
always write `minmax(0, 1fr)` so content cannot blow a column out.
**Decision.** QindaTK's `fr` has a zero minimum. `minmax(auto, 1fr)`
restores the CSS default. **Consequences.** Documented in layout.md and
controls; `tst_grid_tracks::autoTakesContent` guards it.

## D-005 — Flex, Grid and Stack own their implicit size

**Context.** Containers must report their content size so nested layouts
size bottom-up; a QML binding on `implicitHeight` would fight the layout.
**Decision.** The containers call `setImplicitSize` on every layout and
document that user bindings on those properties do not hold. **Consequences.**
Components that need a fixed size wrap the container in an `Item`
(`SectionHeader.qml`).

## D-006 — Box seating detected through QuickPrivate

**Context.** `Box` and `Scroll` stretch a single child unless it places
itself. QML cannot tell whether an anchor line is set (unset lines are not
`undefined`), nor whether `width` was set explicitly. **Decision.**
`LayoutInfo` (C++) reads `QQuickItemPrivate` (`_anchors->usedAnchors()`,
`widthValid()`, `heightValid()`). At the time of this decision it was the
only private-API use; D-013 later added `StylusHandler` and made
`QuickPrivate` a public link dependency. **Consequences.** A Qt release
that changes these layout privates affects `LayoutInfo`; seating can fall
back to explicit `fill: false` if needed. Stylus compatibility is a
separate concern under D-013 and WP-005.

## D-007 — Metric ladders are `QQmlPropertyMap`s

**Context.** Space/radius/size/motion/opacity keys must be bindable in QML
(`Tk.Theme.space.md`), scalable by density, and settable from JSON themes
without one Q_PROPERTY per key. **Decision.** `ThemeMetrics` subclasses
`QQmlPropertyMap`; base values are kept separately and republished on
rescale. Colour roles and the font ramp stay typed properties for tooling.
**Consequences.** Ladder keys are documented in theming.md rather than
in `.qmltypes`; `qtk-preview --dump-theme` prints them.

## D-008 — Lucide icons as a generated path table with an alias map

**Context.** Dense chrome needs crisp, tintable icons at 12–16px on any
DPI; Sloom Studio uses Lucide. **Decision.** `tools/scripts/gen_icons.py`
converts a curated Lucide subset to SVG path strings (`icon_data.cpp`);
`Icon` paints them with `QPainter` from a parsed-path cache; deprecated
Lucide names resolve through an alias table. Custom SVG files and runtime
registration cover the rest. **Consequences.** No image provider, no
raster cache, no network; ISC notice required; regenerate rather than
hand-edit.

## D-009 — Headless verification via offscreen platform + software renderer

**Context.** Tests and agents run without a display. **Decision.** Every
ctest sets `QT_QPA_PLATFORM=offscreen` and `QT_QUICK_BACKEND=software`;
`qtk-preview` switches to them automatically for `--check/--dump/--grab`.
Layout tests render a `QQuickView` and `QTRY_COMPARE` geometry after the
first frame. **Consequences.** Pixels come from the software renderer
(shader effects are unavailable there); on-screen checks stay in the
manual checklist.

## D-010 — Controls on QtQuick.Templates, never a Controls style

**Context.** The toolkit must look the same regardless of the platform's
QtQuick.Controls style and must not depend on style plugins. **Decision.**
Interactive controls subclass `QtQuick.Templates` types and draw their own
`background`/`contentItem` from theme roles. **Consequences.**
`QQuickStyle` settings of the host do not affect QindaTK; accessibility
and keyboard behaviour come from the templates.

## D-011 — Compact density is the measured Sloom scale

**Context.** "Dense" needs a definition. **Decision.** Base metrics are
measurements of Sloom Studio at 100% (header 24, row 22, control 24, chip
32/r6, island action 36, toolbar 36, status 22, caption 10px, tracking
0.14–0.18em); `Density` scales `space` and `size` (not radius, motion,
opacity, and fonts only on request). **Consequences.** Changing a metric
is a documented deviation from the reference app, not a taste call.

## D-012 — The QST-1 bridge is a separate QML-only module

**Context.** The toolkit must build and run without the QindaQt desktop,
but on the desktop it must follow the published tokens. **Decision.**
`QindaTK.QindaQt` (`QindaQtTheme.qml`) imports `QindaQt.Tokens` and calls
`Theme.applyQst()`; `Theme` itself has no desktop dependency.
**Consequences.** Applications opt in with one object. The module is a
regular plugin module (`qindatk_qindaqtplugin` with the QML in resources),
so it loads from the build tree and from the install without linking
anything; an earlier `NO_PLUGIN`/`NO_CACHEGEN` variant could not be
imported from the filesystem because its qmldir preferred a `qrc:` path.
`qtk-preview --qst <theme>` (compiled in when the desktop libraries are
found) publishes a real desktop theme through the Tokens facade, which is
how the bridge is verified headlessly.

## D-013 — Pen input is a pointer handler; the application window is a toolkit type

**Context.** Document applications on the QindaQt desktop (the office suite's
QindaNote first) need pen pressure and tilt, a zoomable page viewport, a
window with menu/tool/status bands, three-button consent dialogs, colour
swatch pickers and wrapping toolbars. Verified on Qt 6.11.1: `QQuickItem::event()`
never sees a `QTabletEvent`; a QML `PointHandler` sees only the mouse event
Qt synthesises from it (pressure 0); a `QQuickPointerDeviceHandler` subclass
receives the raw tablet event when it enters through the platform.
**Decision.** `Tk.StylusHandler` (C++, `src/core/stylus_handler.*`) is that
subclass and the toolkit's second private-API use after `LayoutInfo`; its
header is public, so `QuickPrivate` became a PUBLIC link dependency of
`qindatk`. It drops the synthesised mouse twins of tablet samples and never
accepts touch. `Tk.AppWindow` (`T.ApplicationWindow`) hosts the bands in
plain Items that report their column's implicit height, because a `Flex`
placed directly as the window's header re-polished itself from inside the
window's relayout. `Tk.Viewport`, `Tk.MessageDialog`, `Tk.PromptDialog`,
`Tk.ColorSwatches`, `Tk.ColorButton`, `Tk.ToolBar.wrap`, `Tk.TreeRow.editable`,
`Tk.MenuItem.radio` and `Tk.Dialog.tertiaryText` complete the set; the
office command icons were added to the curated Lucide list, which is now
regenerated from the `lucide-react` npm tarball's `.mjs` modules.
**Consequences at the time.** A C++ user of `<qindatk/stylus_handler.h>`
needed the private include path. D-015 subsequently moved that dependency
behind the public facade. `examples/office-shell` is the smoke test for the
whole set.


## D-014 — Telemetry draws twice, and a ramp is a theme role

**Context.** QindaTK had no way to plot a value over time, which is the one
thing a system monitor is made of. Adding one raised two problems that are
not obvious from the outside.

First, drawing. A monitor keeps dozens of live plots, so `Graph` is a
scene-graph item (`QSGGeometryNode` + `QSGVertexColorMaterial`): appending a
sample costs a vertex rewrite, not a raster repaint. But Qt's **software
adaptation draws only the node types it knows** and silently skips a custom
`QSGGeometryNode`. Every graph rendered blank under `qtk-preview --grab`,
which forces that adaptation so it works headless — and would have rendered
blank on any llvmpipe machine, in a VM, or under a remote session.

Second, colour. btop's legibility comes from colouring a reading by its
*magnitude*, not by which series it belongs to. Rule 2 forbids a control
naming a literal colour, and a gradient is not expressible as a single role.

**Decision.** `Graph` carries two drawing paths: the geometry one, and a
`QPainter` twin (`graph_raster.cpp`) presented through a `QSGImageNode` when
`QSGRendererInterface::graphicsApi()` reports `Software`. Both consume the
same `GraphMesh::Plot` and the same sample mapping (`normalised`, `sampleX`,
`sampleY`, `baseline`, `sampleColor`), which is published in `graph_mesh.h`
for exactly that reason. `Meter` and `Sparkline` avoid the problem instead of
solving it twice: `Meter` is a `QQuickPaintedItem` (small, cheap to raster,
correct everywhere for free) and `Sparkline` is a QML wrapper over `Graph`.

Colour scales became a theme concept: `Theme.ramp` publishes six named
`ThemeRamp`s derived from the colour roles and rebuilt in `finishUpdate()`,
so `ramp: Tk.Theme.ramp.load` is as themed as `color: Tk.Theme.color.accent`.

`qtk-preview` gained `--gpu`. Without it none of the geometry path was
verifiable, and merely leaving `QT_QUICK_BACKEND` unset is not enough: the
offscreen platform advertises no OpenGL, so QtQuick picks software on its own.

**Consequences.** A change to how a graph *looks* must land in both
`graph_mesh.cpp` and `graph_raster.cpp`; an `AGENT-GUARD` in
`graph_raster.h` says so, and the two are compared by grabbing the same scene
with and without `--gpu`. `tst_telemetry` pins the ring-buffer ordering, ramp
interpolation, and the rule that no preset may collapse a ramp's ends.

## D-015 — Keep private Qt behind a public stylus facade

**Context.** D-013's installed `stylus_handler.h` derives from
`QQuickPointerDeviceHandler`, a Qt Quick private class. This exposes
versioned private headers and `Qt6::QuickPrivate` to every C++ consumer of
`QindaTK::qindatk`. `LayoutInfo` already keeps its private Qt use inside a
`.cpp`; stylus input is the remaining installed-header leak. A public
`QQuickItem::event()` override and the standard QML PointHandler did not
provide raw pressure/tilt in the Qt 6.11.1 platform-event probe recorded by
D-013. [Qt 6.8's input-handler documentation](https://doc.qt.io/qt-6.8/qtquickhandlers-index.html)
describes item-owned handlers and grab permissions. The subsequent Qt 6.8.3
build and run are recorded in [WP-005 evidence](universal-toolkit/implementation/evidence/WP-005.md).

**Decision.** Retain the raw event receiver as an internal
`QQuickPointerDeviceHandler` subclass. Export a `QObject` and
`QQmlParserStatus` `Tk.StylusHandler` facade whose installed header uses
public Qt types only. The internal handler is directly parented to its
`QQuickItem`, because Qt Quick's delivery registration and parent-item
lookup require an item parent. The facade forwards the sample stream,
active/cancel state and supported input configuration; QML construction
attaches the backend after parent resolution. Reparenting and destruction
must detach safely, and a failed exclusive-grab request must not claim a
stroke. `Qt6::QuickPrivate` becomes a private build link, and installed
CMake package configuration no longer asks consumers to find it.

**Compatibility.** Existing `Tk.StylusHandler` QML usage and typed
`StylusSample` signals are intended to continue. C++ code treating
`StylusHandler` as a `QQuickPointerDeviceHandler`, invoking protected
handler methods or depending on its private-base enum type must migrate and
rebuild. This is a source and ABI change in the pre-1.0 toolkit; the facade
does not claim binary compatibility with the old class. The intended
version targets are the declared Qt 6.8 minimum and the current Qt 6.11.1
development version. Only actual build/run and installed-consumer results
qualify either target; source inspection alone does not.

**Consequences.** D-013 remains the historical reason for raw tablet
delivery. WP-005 implemented the facade and passed the QML/C++ consumer
test on Qt 6.8.3 and 6.11.1. The rest of the toolkit continues to use
`LayoutInfo`'s private implementation internally. The former inherited
`dragThreshold` and `cursorShape` properties are not proxied; callers using
them must migrate. The verified boundary does not imply binary compatibility
with the prior class or physical-tablet/GPU qualification.
