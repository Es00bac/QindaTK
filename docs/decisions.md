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

## D-016 — Keep toolkit theme ownership process-wide

**Context.** `Tk.Theme` and `Tk.Density` are QML singletons backed by C++
`instance()` objects shared across engines. Every current control reads
`Tk.Theme` directly. The QindaQt token bridge also updates that object.
Consequently, applying a preset in one window changes toolkit chrome in
every window. A per-window theme object would have no effect on existing
controls merely by being placed in a window or QML context.

**Decision.** The supported toolkit-chrome theme and density contract remains
process-wide for WP-007. Applications select one preset, role map or QST
token source for all QindaTK windows in a process. They may style document
content independently as application data, but must not claim that toolkit
controls in different windows use separate palettes. No `ThemeScope` API is
published until controls and detached popups can resolve a nearest explicit
scope, with one defined owner for QindaQt token updates and a migration path
for existing `Tk.Theme` bindings.

**Consequences.** This is the conditional FND-08 disposition, not an
implementation of per-window theming. Existing QML and C++ theme calls keep
their behavior. Future scoped work must cover inheritance across item trees,
popup reparenting, window movement, runtime scope changes, density and text
scale, and the bridge's global-token policy before advertising mixed-theme
windows. The other WP-007 foundations still require implementation and tests.

## D-017 — Raise text-bearing chrome heights for user text zoom

**Context.** `Tk.Density.textScale` can increase the font ramp independently
of compact/comfortable/touch density. An integrated gallery run at 2× text
zoom showed a 30px label at y=-4 inside a 22px segment, visibly spilling
across the row. Scaling fonts without a corresponding minimum for fixed
chrome heights does not provide a usable large-text path.

**Decision.** Keep the authored density-scaled `Tk.Theme.size` ladder as the
floor. After fonts resolve, raise text-bearing vertical roles to the larger
of that floor and a documented font-based minimum. The affected roles are
controlSm/control/controlLg, chip, row/rowLg, tab, toolbar, menuItem, header,
statusBar and timeRuler. Recompute from authored bases on every theme/density
change so reducing text zoom or changing a JSON/QST font base cannot leave
stale larger sizes. Width and icon roles retain their existing contracts.

**Consequences.** At compact density with 2× default text, row is 34px and
control is 36px; a segment's 30px label fits its 34px row in the preview
dump. Control heights can grow even when density mode stays compact. Fixed
application widths can still crowd at large text, so this is a necessary
foundation, not WP-007's complete large-text acceptance. The source of
truth is `Theme::finishUpdate()` and the WP-007 visual evidence.

## D-018 — Use Qt item baselines for horizontal Flex lines

**Context.** `Tk.Flex.Baseline` previously behaved as `Start`. The WP-006
diagnostic mixed 12px and 26px labels and placed both at y=0, leaving their
text baselines visibly apart. [Qt 6.8's public `QQuickItem::baselineOffset`](https://doc.qt.io/qt-6.8/qquickitem.html#baselineOffset-prop)
reports a local baseline for text and controls; plain Items default to zero.

**Decision.** For row and row-reverse Flex lines, align participating items
by that public offset. A plain Item with no declared baseline uses its
bottom edge. Compute each line's natural cross extent from the largest
ascent plus largest descent, as well as the tallest child. Watch baseline
offset changes for relayout. `alignSelf: Baseline` uses the same rule.
Columns retain Start behavior for Baseline until a writing-mode and logical
direction contract is defined.

**Consequences.** This intentionally changes positions in rows requesting
Baseline; rows using other alignment modes keep their geometry. The mixed
font/Item test passes on Qt 6.8.3 and 6.11.1, and compact/comfortable
preview dumps and captures show the corrected placement. WP-006 still owns
intrinsic tracks, RTL, borders and nested scroll behavior.

## D-019 — Explicit logical direction for custom layout engines

**Context.** The WP-006 RTL diagnostic enabled Qt Quick
`LayoutMirroring.enabled`, but Flex children remained left-to-right because
Flex writes their x coordinates itself. [Qt documents that custom x
positioning is not affected by LayoutMirroring](https://doc.qt.io/qt-6/qml-qtquick-layoutmirroring.html).
Apps need predictable logical Start and End for both row and column layouts.

**Decision.** Custom layout engines expose a `layoutDirection` property
with `Qt.LeftToRight` as the compatibility default. `Qt.RightToLeft`
mirrors final horizontal child positions within the padded content box.
RowReverse still reverses the logical main axis, so RowReverse and RTL
together place Start on the physical left. Column cross-axis Start moves to
the right. Left and right padding remain physical; applications may bind
`layoutDirection: Qt.application.layoutDirection` on each custom container.
An ancestor `LayoutMirroring.enabled` is not an implicit input to these
engines.

**Consequences.** Existing LTR geometry is preserved. RTL behavior is
explicit and can change at runtime. Each layout engine needs its own
tests and documentation; Flex, Grid and Stack implement the shared contract.
Box and Scroll do not place arbitrary children by a logical Start/End axis.

## D-020 — Bound responsive Grid repeats and preserve intrinsic approximations

**Context.** Integer repeats cannot adapt an asset grid to a changing width.
QQuickItem exposes one implicit size per axis, so distinct CSS min-content and
max-content measurement cannot be inferred reliably for arbitrary child items.

**Decision.** Accept a sole `repeat(auto-fill|auto-fit, minmax(min, max))` in
`columns` or `rows`. The minimum must be a positive finite pixel or percentage
length; the maximum must be a nonnegative finite pixel, percentage, or `fr`
length. Expand against the current inner axis before placement. Use the larger
of definite minimum/maximum as the fitting breadth, or the minimum for an `fr`
maximum. Include gaps; use at least one repetition, cap at 4096, and use one
repetition on an indefinite axis. The fitting breadth is floored at one pixel
for numerical stability. This is deliberately a bounded responsive grammar:
multiple pattern tracks, surrounding tracks and nested automatic repeats are
rejected, as are automatic repeats in implicit-track properties.

After placement, `auto-fill` retains unused tracks; `auto-fit` collapses every
unoccupied repeated track and its gaps. A span occupies all tracks it crosses,
even when the child's implicit size is zero. Explicit track indices and counts
remain stable after collapse; placement beyond the repeated region creates
ordinary implicit tracks. Visibility, ignore flags, resize and gap changes
trigger the usual relayout path.

Keep `Nfr = minmax(0, Nfr)`, the existing content floor of `minmax(auto, Nfr)`,
and the existing min/max-content aliases to implicit content. Do not advertise
distinct intrinsic text measurement. Finite `minmax` maxima bound growth;
maxima below minima resolve to the minimum. Invalid syntax preserves the parsed
prefix and error reporting. Negative/non-finite lengths, excessive integer
repeat expansion and nesting are errors rather than accidental zero tracks or
unbounded allocation. Integer `repeat(0, ...)` remains an empty repetition for
compatibility. This grammar does not provide named lines, subgrid or fit-content.

**Consequences.** Asset grids can choose retained slots or stretched occupied
slots without imperative breakpoint logic. Existing plain tracks and `fr`
behavior remain covered by regression tests. A future true intrinsic sizing
API must define child measurement contracts before expanding the grammar.

## D-021 — Hard bounds and event handoff for nested Scroll viewports

**Context.** The original Scroll used `Flickable.StopAtBounds`, but its
keyboard promise had no key handler and focusing an offscreen child did not
reveal it. A nested wheel fixture already moved the inner viewport and then
the outer one at the inner edge on Qt 6.11. The equivalent keyboard and
focus fixtures failed.

**Decision.** Preserve the default hidden horizontal overflow and hard
Flickable bounds. Let Qt's nested Flickables route wheel events; a focused
Scroll root handles arrow, Page Up/Down and Home/End keys with themed row
steps and clamps to content bounds. Accept a key only when it moves the
current viewport, allowing the event to reach an ancestor at an edge.
`interactive: false` suppresses keyboard movement in that viewport.
When focus changes, the inner Scroll reveals the actual item while each
ancestor Scroll reveals the immediately nested viewport instead of the
deep item. The exposed `ensureVisible(item)` remains for explicit use.

**Consequences.** A deeply focused control becomes visible through nested
viewports without overscrolling its ancestors. Wheel and keyboard routing
have event-level QtQuickTest coverage. `Tk.Scroll` gains a focusable Pane
and an `accessibleName` path. Touch drag at an edge remains bounded rather
than transferring an in-progress gesture to the ancestor; that policy
avoids a disruptive mid-gesture ownership change.
When resize removes overflow or interaction is disabled while Scroll owns
focus, its tab stop remains until focus leaves. Qt rejects clearing
`activeFocusOnTab` on the active item; the deferred release avoids a stale
focusable viewport.

`Tk.Viewport` owns Ctrl+wheel zoom and ordinary wheel panning while it can
move. Its WheelHandler sets
[`blocking: false`](https://doc.qt.io/qt-6/qml-qtquick-wheelhandler.html#blocking-prop)
and accepts an ordinary wheel event only when content moved. This lets an
ancestor Scroll take the wheel at the document edge. The nested canvas test
checks both edge handoff and exclusive Ctrl+wheel zoom ownership.

## D-022 — Floor touch-density interactive row targets

**Context.** Density scaling alone resolves a compact 24px control to 36px
and a 22px menu or list row to 33px in Touch mode. Those are smaller than
the project's 44px logical target for direct pen/finger interaction. The
measured Compact and Comfortable chrome remains the desktop baseline.

**Decision.** After density and text-scale resolution, raise interactive
vertical size roles `controlSm`, `control`, `controlLg`, `row`, `rowLg`, `tab`,
`menuItem`, `header`, `timeRuler`, `sliderTarget`, `sliderTargetSm` and
`toggleTargetSm` to at least 48 logical pixels in Touch mode. The 48px outer
row leaves at least 44px for a joined control's inner button after two 2px
borders. Larger authored or font-derived values win. Keep the authored metric
bases intact; switching back to Compact or Comfortable recomputes their
original resolved heights. Icon artwork and fixed noninteractive metrics do
not grow solely because of this floor.

**Consequences.** Controls bound to these roles acquire a larger touch
interaction area while their internal art can remain visually compact.
This role floor does not enlarge a control that ignores the relevant theme
size or guarantee every custom pointer handler has a 44px target. WP-007
continues auditing those controls and whole-workflow reachability.

## D-023 — Separate owner state from user-intent notifications

**Context.** Application state can arrive through bindings, document loads,
undo/redo, collaboration or user input. An app cannot treat every property
change as a new local edit. The existing controls mix template state signals
with custom `activated`, `valueModified` and `colorEdited` signals, so the
distinction needs a documented baseline before adding more controls.

**Decision.** A control's public value, selection or checked property is the
state the owner can assign. A named interaction signal such as `activated`,
`selected`, `valueModified` or `rangeModified` reports a validated user
operation only. Direct owner assignment changes the property and its normal
property-change notification without emitting that interaction signal.
Pointer, keyboard and accessible actions call the same guarded operation and
produce one interaction signal for one accepted operation. Disabled and
read-only gates apply to those operations, never to owner assignment. An
explicit `reset()` is an operation and documents whether it emits. Editable
controls preserve a draft until commit or cancel; invalid drafts remain
visible with an error state where the control supports parsing. Mixed state is
used only for aggregate selection and must clear on the first accepted edit.

**Consequences.** Hosts can bind values without causing edit feedback loops
and can use interaction signals for undo entries or document commands. New
controls document the applicability of default/reset, mixed, invalid draft,
read-only, disabled, focus and accessible action behavior. Existing controls
are audited against this contract in WP-007; unsupported capabilities and
legacy exceptions must be stated per control instead of implied.

## D-024 — Small docking windows scroll an application-sized workspace

**Context.** Dock lanes can consume more than a small host's entire width or
height, leaving a zero-sized canvas. Automatically changing modes or zones
would overwrite the user's arrangement and create unstable resize behavior.

**Decision.** Keep DockModel arrangement independent of window size. The
bundled docking example demonstrates a two-axis Scroll around an Item with
an explicit useful workspace minimum. This is an application presentation
recipe, not a new automatic DockHost arrangement policy. Its 1280×800 minimum
covers its default panels; each application chooses its own requirements.

**Consequences.** Narrow windows can reach the preserved workspace through
scrolling, including keyboard scrolling and focus reveal. Resize alone does
not persist new panel positions. A future adaptive panel selector or recovery
of arbitrary off-workspace floating panels requires a separate design.

## D-025 — Popup height uses the anchor screen's work area

**Context.** Full-screen height includes reserved desktop chrome, while QML
Screen's desktop-available height is a virtual-desktop union. Neither provides
the particular screen's usable popup height.

**Decision.** `ScreenWorkArea` maps the popup anchor to its owning window's
virtual sibling screen, reads `QScreen::availableGeometry`, and observes
screen/window/anchor changes. A point outside every sibling falls back to the
window screen. A detached anchor has no work area. Menu uses the resulting
logical height without DPR multiplication before native popup creation. Its
per-frame transform check runs only while the menu is visible; reopening
refreshes the geometry synchronously before display.

**Consequences.** Qt continues to own popup placement and flipping. Platform
work-area limitations (especially multi-monitor X11) remain visible and are
not replaced with guessed geometry. Native mixed-monitor placement and scale
changes require the physical platform qualification in WP-092.
