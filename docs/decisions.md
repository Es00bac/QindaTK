<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Decisions

ADR-lite log of durable, cross-cutting choices. Continue with [application decisions](decisions-application.md), [service decisions](decisions-services.md) and [domain decisions](decisions-domains.md). Add an entry when a choice would surprise a future reader; keep entries short.

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
windows. The other WP-007 foundations are tracked in the package's integrated
acceptance record; this decision does not imply per-window theme support.

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

## D-286 — Gantt dates project through a working calendar

**Context.** A schedule edit over a weekend or DST boundary cannot be
represented faithfully by adding a fixed count of wall-clock pixels or local
hours. A view also cannot own document-level dependency and undo policy.

**Decision.** Task instants cross QML as exact UTC millisecond strings.
`GanttCalendar` maps them through local working intervals, counting actual
elapsed minutes and compressing nonworking time. `GanttTaskModel` keeps stable
UUID row identity and a separate validated preview. A host transaction
adapter accepts or rejects the entire schedule change and supplies undo.

**Consequences.** Timeline/grid selection can share `SelectionModel`, and
previews never mutate committed data. Hosts own resource constraints,
persisted undo and calendar boundary-time policy. The bundled adapter is a
reference finish-to-start planner only.

## D-287 — Polar plots declare bearing and radial semantics outside Cartesian axes

**Context.** A Cartesian `ChartAxis` fraction does not determine a bearing,
wraparound, category sector, direction or radial distance. Reusing it for
polar marks would let paint and hit testing disagree about the same point.

**Decision.** The polar family uses one explicit transform for paint,
projection, inverse coordinates and hit testing. Zero is north; positive
angles are clockwise by default. Numeric input declares degrees or radians;
categorical input declares a unique order. Radial values declare an
increasing nonnegative domain and unit. Source rows keep stable IDs and
remain available for table and export.

**Consequences.** A missing category, incomplete radar polygon or value
outside the radial domain surfaces as an error rather than plausible but
false geometry. Cartesian chart APIs remain unchanged. CSV records the
coordinate convention so the display can be interpreted outside the view.

## D-289 — Canvas guides are document content

**Context.** CAN-07 needs guides that snap, move through transactions and
return after save/reopen, and it left the document/workspace ownership
policy open. `SnapModel` and `GuideLayer` already read plain host records.

**Decision.** The reference `CanvasDocument` owns an ordered guide list as
content. Add, move and delete are ordinary `CanvasEdit` steps carrying the
whole before/after list, so undo, stale-edit rejection and the shared session
history need no new mechanism. The file keeps its binary layout and version:
the record travels in the extensions JSON under `qindatk.canvas.guides`,
which the decoder validates and moves out of the host extensions. Guide ids
come from a saved counter that is never reused. Host extensions and guides
share one byte budget; an overflowing save fails explicitly.

**Consequences.** A document's guides survive save, reopen, recovery and
undo, and older readers keep them as opaque extension data. Workspace-only
or per-user guides stay possible because the overlays accept any host list;
no workspace guide store is provided.

## D-045 — Chart axes own linked viewports; selections keep source domains

**Context.** Sharing a chart scale while each renderer stores independent
pan/zoom fractions produces mismatched ticks and hit coordinates. Histogram
bin selection also loses its meaning if it reports only painted pixels.

**Decision.** `ChartAxis` owns a normalized viewport with a separate change
signal from domain changes. A `ChartView` may use an external axis instance;
sharing that instance links its domain and viewport across views. Chart
annotations keep stable IDs and original coordinates in an atomic model.
Histogram selection is a renderer-owned typed object with source bin indices,
exact domain edges, units and final-edge inclusivity.

**Consequences.** A domain edit resets the shared viewport for every linked
view. Hosts that need only X synchronization share only X. The annotation
model and histogram selection do not infer analysis or color context; the
host supplies those source semantics.

## D-047 — Own ICC encodings and keep numeric color independent of Theme

**Context.** QColor and canvas metadata cannot represent a validated document
profile pipeline or prove that display pixels were transformed.

**Decision.** The core numerical ColorEngine privately links Little CMS.
Immutable profiles own exact ICC bytes and content hashes. ColorValue carries
straight-alpha RGB doubles; ICC owns its complete transfer encoding. The first
backend performs Float32 relative-colorimetric CPU conversions with BPC off,
preserves alpha separately, tags results with context generations, and returns
structured errors for unsupported proof/model/intent operations.

**Consequences.** LCMS and pkg-config become required build dependencies.
Profile import and the transform cache are bounded; no native handles enter
public headers. Gamut status remains unknown. Canvas/GPU display management,
proofing and gradient interpolation require their own fixtures and integration;
no Theme change modifies stored colors. See [color.md](color.md).

## D-048 — Keep editable curve geometry normalized and its domains typed

**Context.** Tangent handles, evaluator samples and editor gestures need one
coordinate convention, while automation and color curves have different units.

**Decision.** Curve knots carry stable string IDs; knots and handles use
absolute normalized coordinates. Typed axis domains provide explicit affine unit
projection. A stateless validator rejects invalid proposals; a double-precision
evaluator defines step, linear, cubic and edge semantics without repairing or
mutating host data. Ordered cubic controls permit a bounded monotone x solve.

**Consequences.** The editor proposes handle coordinates and the host owns
constraints and commits. Unit conversions, exact tick projection, mirrored
tangents, adaptive sampling and undo remain explicit integration boundaries.
See [curve.md](curve.md) for endpoints, repeat seams and numerical fixtures.

## D-049 — Interpolate tagged gradient stops in linear light with coverage alpha

**Context.** Encoded RGB interpolation darkens light-space midpoints, and
straight-channel mixing lets hidden transparent colors tint visible samples.

**Decision.** The numerical gradient evaluator converts stops through
ColorEngine into linear sRGB, interpolates premultiplied coverage, and converts
the straight result to an explicit RGB ICC target. Array order resolves equal
positions; the last duplicate wins exactly. Zero-alpha output has canonical
zero channels while stored colors and ColorEngine hidden RGB remain intact.

**Consequences.** Hosts preserve stable stop IDs and tie order, own transactions,
and explicitly integrate renderer previews. Initial policies are relative
colorimetric/BPC off with padded edges; proofing and alternative interpolation
spaces remain unsupported. See [gradient.md](gradient.md).

## D-082 — Local WGS84 vectors precede remote geographic providers

**Context.** WP-084 requires geographic coordinates, source identity and an
offline story before tile/network integration. **Decision.** The first MapView
slice consumes local GeoJSON only, declares WGS84 longitude/latitude at the
provider boundary, and paints normalized Web Mercator coordinates. Tile state
is explicitly unsupported. **Consequences.** Local features can be picked and
round-tripped without external services. Later slices added bounded local and
network XYZ caches, vector editing, a WGS84 spatial grid and selectable
Mercator/equirectangular display. XYZ tiles remain Mercator-specific; a
projection switch hides them with an explicit status.

## D-083 — Own typed canvas fills and composite before output conversion

**Context.** QColor alone drops typed ICC identities and gradient stops.
Converting individual vertices before blending does not match a converted
composite, and a reopened document must resolve its own colors.

**Decision.** Closed vector objects may carry a bounded managed fill with
owned RGB ICC bytes and tagged solid channels or a typed gradient definition.
The reference canvas composites managed fills in sRGB tiles on either backend,
then converts the full tile to the persisted RGB output profile. Native binary
version 2 adds the output ICC and object payloads; its reader accepts version 1
and the session schema ID stays stable. Rendering explicitly supports relative
colorimetric intent with BPC off.

**Consequences.** Managed objects take the raster tile path even on RHI;
ordinary sRGB objects keep the fast geometry path. A plain QColor fill clears
the managed fill. Other rendering intents, soft proof, tagged raster sources,
and calibrated monitor presentation remain separate work.

## D-084 — Store editable cubic paths in the reference canvas

**Context.** The canvas could draw open polylines but could not retain Bézier
handles, winding or stroke descriptors through undo and save. A vector export
must state exactly which appearances it can preserve.

**Decision.** A path starts with an ordered contour of local-space anchors
with absolute incoming/outgoing controls, closure and fill rule. Version 4 adds
bounded extra contours with individual closure; the fill rule applies across
all contours. Stroke cap, join and dash values live on `CanvasObject`. Native
binary version 3 saves the first contour and stroke fields; version 4 saves
extra contours. Version 5 stores polygon, star and rounded-rectangle
parameters beside their generated nodes, which every commit and decode must
match; a manual node edit converts the shape to a freeform path. Versions 1
through 4 remain readable. Every path edit is one `CanvasEdit`. The
software painter draws the cubic directly; RHI uploads its composited tile
when the content needs path or nondefault stroke semantics. SVG 1.1 export
writes the flat-color vector subset and rejects image, managed ICC fill and
output-profile cases explicitly. Clipboard path copies use a bounded native
snapshot and paste as one edit with fresh destination IDs; SVG is offered
when the style can be exported.

**Consequences.** The reference authoring backend now preserves editable
path geometry, editable parametric primitives and supported stroke appearance.
Arbitrary boolean geometry, pattern fills and effect stacks need separate
contracts.

## D-085 — Place raster composites on the canvas as shared image assets

**Context.** WP-045's tiled raster document (layers, masks, stroke undo) and
the WP-040 canvas image assets were separate contracts, so raster content
could not be transformed, composed with vectors, saved in a canvas or output
through the WP-043 ICC path. **Decision.** `TiledRasterDocument` stays the
editing owner of layered pixels and masks; `placeOnCanvas` and
`publishToCanvas` hand its flattened, untagged sRGB composite to a
`CanvasDocument` as a 256 px tiled image object, each in one recorded
`CanvasEdit`. A publish reuses every unchanged tile's `QImage`, and a
`canvasToRaster` mapping lets pen input paint through the object's
transform. **Consequences.** Transforms, paint order, undo, canvas files,
sessions and output conversion apply to raster content without a second
image model. The histories stay separate, the canvas shows a stroke when it
is published, and layered raster state is saved in the raster file, not in
the canvas document.

## D-092 — Canvas layers form a tree of groups and content layers

**Context.** CAN-09 needs nested layers that reorder with parent checks and
undo, while painting, picking, snapping, alignment, tools and the native file
already rely on a flat `(layer order, z)` paint order and on
`CanvasEdit` before/after deltas. **Decision.** `CanvasLayer` gains a
`parent` group and a `group` flag; `order` becomes sibling-relative. Groups
hold layers and never objects, carry no transform, opacity or blend, and
cascade visibility and lock to their descendants. The paint key uses the
layer's rank in the depth-first flattening of the tree, recomputed after each
layer change. Structural commands build ordinary layer deltas, so one undo
step restores a whole move, group, ungroup or tree deletion, and every commit
validates parents, cycles, depth (`maximumLayerDepth`, 64) and group content.
The tree is saved in the extensions JSON under `qindatk.canvas.layerTree`
(only for non-flat layers), like the guides of D-289. **Consequences.**
Existing flat documents keep their bytes and behaviour; older readers keep the
tree as opaque data and see the layers flat. Group transforms, opacity,
blend modes and masks remain outside this model (WP-044/WP-045 own
appearance and masks).

## D-093 — Artboards are document content painted below objects

**Context.** CAN-08 left board records host-owned, so boards could not be
saved, undone, rendered or exported with the document. **Decision.** The
reference `CanvasDocument` owns an ordered artboard list (`id`, `name`,
`rect`, 8-bit sRGB `background`, `bleed`) with ids from a saved counter that
is never reused. Board edits are `CanvasEdit` steps carrying the whole list.
Boards own no objects: arranging or resizing a board never changes object
coordinates. Backgrounds reach the renderers through
`CanvasDocumentAdapter::backdrops()` and paint after the page colour and
below every object; the export region is the board plus its bleed. The list
is saved under `qindatk.canvas.artboards` in the extensions JSON.
**Consequences.** Boards survive save, reopen, recovery and undo, render in
both providers and export as SVG per board; clipping content to a board and
board-scoped object membership are not modelled.

## D-094 — Tree and overview views derive from the model and never feed back

**Context.** A layer panel, several canvases and an overview on one document
can drive each other: a tree click selects, the selection scrolls the tree,
the overview moves the view, and a fitted view refits over navigation.
**Decision.** `CanvasLayerTreeModel` derives rows from the document after
every change and stores no hierarchy; it writes only the shared
`CanvasSelectionModel` and document commands, and asks the view to reveal a
row only for selections it did not make. `NavigatorView` keeps a private,
content-fitted preview pose and writes the main pose with one atomic
`setCenter`. A `CanvasView` treats any pose change it did not make itself as
navigation, which ends `fitMode` refitting until `fitDocument()`.
**Consequences.** Each state has one writer per gesture and each change
travels one way, so no selection or viewport loop exists; a host that wants
a fitted view to follow a shared pose again calls `fitDocument()`.

## D-095 — Alignment reference choices come from the domain model

**Context.** `AlignDistributePanel` hard-coded selection, key object and page,
so a domain with artboards could not offer a board reference. **Decision.**
`CanvasAlignmentModel.referenceOptions` lists the references the document
supports (`selection`, `keyObject`, `page`, and `artboard` when boards exist,
available once `artboardId` names one) and the panel shows whatever list it
is given. Alignment to a board uses the board rectangle, like the page.
**Consequences.** Other domains can add references without panel changes;
the panel's default list keeps existing hosts working.

## D-096 — Decode and encode media with the FFmpeg libraries as an optional private backend

**Context.** WP-070 needs exact frame identity, cancellable seeks, audio
samples and a real encoded export. The existing asset path ran the FFmpeg
command-line tools per request (`select=eq(n,N)` decodes from the start every
time, cannot be cancelled inside a packet and knows nothing of B-frame
presentation order), and Qt Multimedia exposes neither presentation indices
nor frame-accurate seeking. **Decision.** Link `libavformat`, `libavcodec`,
`libavutil`, `libswscale` and `libswresample` (found by `pkg-config`,
`QINDATK_ENABLE_FFMPEG` default ON) privately into `qindatk`; their headers
stay in `media_ffmpeg_p.h` and `.cpp` files. `MediaDecoder` indexes packets
once and seeks by key frame with an interrupt-callback cancellation;
`MediaEncoder` writes lossless FFV1 (8-bit RGB) and PCM s16le in Matroska, the
codecs every FFmpeg build ships. A build without the libraries keeps every
type and reports `ErrorCode::Unsupported`; the asset backend falls back to the
command-line tools. No Qt Multimedia dependency is added.
**Consequences.** Consumers never need FFmpeg headers and lightweight builds
still link. Distributors of FFmpeg-enabled builds inherit FFmpeg's licence
terms for the configuration they ship (qualified in WP-088/WP-093).

## D-097 — Identify a source frame by presentation index and derive its time from one rate policy

**Context.** Monitors, trims, exports and caches must agree on which frame a
time means, including 1000/1001 rates stored with rounded container
timestamps, variable-rate files and B-frame streams. **Decision.** A frame is
(resource, content revision, zero-based presentation index) over an index
sorted by presentation timestamp. When every timestamp is within one time-base
unit of `i / rate` for a declared rate, the stream is constant-rate and frame
*i* starts at exactly `i / rate`; otherwise it starts at its own timestamp
relative to the first frame. Frame starts round up to the next tick; a tick
holds the last frame that has started; media time zero is the first video
frame and audio positions carry the stream's offset from it.
**Consequences.** `frameAtTicks(frameStartTicks(i)) == i` in every timebase;
23.976 fps sources conform to a 25 fps program by holding, and exports write
each frame's exact sample span. Duplicate timestamps and truncation are
reported as damage instead of being guessed around.

## D-098 — Make the playback engine the transport authority and present only the current target

**Context.** `TransportModel` already projected acknowledged engine state, but
no media engine existed; rapid seeks with asynchronous decoding can otherwise
display a frame that belongs to an earlier request. **Decision.**
`MediaPlaybackEngine` implements `TransportAdapter`: it owns the position (in
program samples), acknowledges every command with its snapshot on the next
event-loop turn and advances playback from a `MediaPlaybackClock`. One decode
thread per engine renders program frames; only the result for the current
target frame and program revision is presented, and superseded work is
cancelled through its token. Failed targets are presented as failures and an
empty program clears the display. `MediaFrameView` uploads each new presented
frame once into a texture owned by its scene-graph node. Audible device output
and a device-driven clock belong to the audio engine (WP-060).
**Consequences.** Monitors may show a pending seek but never a superseded or
stale frame; frame identity, playback position and audio positions share one
clock and one sample domain.

## D-099 — Keep media bins virtual over resolver resources and proxies preview-only

**Context.** VID-02 requires organizing and relinking media without changing
sequence references, with proxy/original status inspectable per clip.
**Decision.** `MediaCatalog` assets wrap `ResourceResolver` resource IDs.
Bins, labels and tags are catalog records; relinking changes only the
resolver's location (only exact content-hash matches relink from a folder).
Proxies are separate resolver resources with the original's frame count and
rate; a player decodes pictures from an active proxy while frame identity,
audio and exports use the original. **Consequences.** Timelines, compositions
and graphs keep their resource IDs through any reorganization or relink, and
a proxy can never shift a cut or change an export.

## D-118 — Plan NLE edits in the timeline from clip records, on frame starts

**Context.** VID-03 needs roll, ripple, slip and slide that stay inside legal
media and land on frames, including when media is offline, and must share
the timeline's transactions and history. **Decision.** Roll, slip and slide
are `TimelineModel` edit kinds beside WP-048's ripple trims, planned by the
same pure planner. Every bound comes from the clip records (`sourceOffset`,
`sourceDuration`, neighbours, minimum length), never from decoded media;
with `frameExact` the moved edit point lands on the nearest program frame
start inside the legal range (or does not move). `SequenceEditor` runs the
views' previews as ordinary revision-guarded sessions. **Consequences.** An
offline clip trims exactly like an online one and keeps its exact source
offsets; previews, commits and undo behave like every other timeline edit;
a clip whose recorded length is unknown has an unbounded tail.

## D-119 — Link picture and sound by a shared link ID

**Context.** Audio-linked selections must move, trim, split and switch
together without a second placement store. **Decision.** Clips carry an
optional `linkId`; a request with `linked` expands its selection to every
clip in the selected clips' groups and applies the same edit to each (a
roll's partners follow the side they belong to; vertical moves keep partners
on their tracks). Split, paste and source switches give the new parts a
group derived from the operation (UUIDv5), so new left and right parts stay
paired. **Consequences.** Linking is document data that persists and undoes
with the clips; a locked partner refuses a linked edit instead of letting
picture and sound drift apart.

## D-120 — Store a transition on its incoming clip and evaluate it once for playback and export

**Context.** VID-05 needs transitions with explicit handle rules that
preview and export the same frames. **Decision.** A transition is a field of
the incoming clip: type, exact ticks before and after the cut, alignment and
string parameters; its window moves with the cut. Setting one requires an
outgoing clip ending at the incoming clip's start and enough head and tail
handle. After any other edit the planner drops a transition whose cut no
longer exists (in the same change set) and refuses an edit that leaves too
little handle. A clip's two windows (its own lead-in after the cut and the
lead-out into the next clip before the next cut) must fit in its length
together: setting or editing into an overlap is refused (field `duration`),
and a timeline loaded with one keeps the later cut when it becomes a
program, because the renderer draws a span inside a fade-out only as the
partner of the incoming span. `MediaProgram` extends both spans over the window and
`MediaTransition` evaluates it: the weight at each frame's midpoint rounded
once to 1/65536, an integer channel blend, and per-sample linear or
equal-power gains for sound. **Consequences.** Transitions need no new
record kind, history or persistence path; a 4-frame dissolve is 1/8, 3/8,
5/8, 7/8 in the monitor, the queued export and the file; paste and split
parts start without transitions.

## D-121 — Make a multicam cut a source switch positioned by the sync map

**Context.** VID-04 needs angle switching during playback with stable sync
after reopen, and an explicit audio policy. **Decision.** `MulticamModel`
holds the angle table (resource, sync offset, available length) over one
program track of ordinary clips. A switch is one frame-exact `switchSource`
transaction: the program clip is cut at the switch time and the part after
it shows the new angle at `(T − syncStart) + syncOffset`, computed from the
sync map rather than from the clip being cut; `audioFollowsVideo` decides
whether the linked sound is cut and switched too. An angle whose sound is a
separate recording carries that file's own sync offset and length, and the
switch gives the linked sound part that position and bound
(`linkedSourceOffset`/`linkedSourceDuration`); `unsyncedClipIds()` checks
the sound track against it too. **Consequences.** Switches
never accumulate drift, undo removes one cut at a time, the saved timeline
plus the angle table reopens the same cuts (the angle is recovered from each
clip's resource), and program playback and export need no multicam-specific
rendering.


## D-100 — A held control is heard in every mode; static values are the parameter default

**Context.** WP-061 controls must change real engine parameters, and one
gesture must be one transaction. The reference automation engine (WP-049)
sent the live value to the engine only while a write pass ran or in mode
`off`. So a fader moved in read mode, or with the transport stopped, was not
heard, and an `off` value lived in a transient field that undo and save never
saw. **Decision.** An `EngineScheduleSnapshot` parameter overrides its curve
while a control holds it (`touched`) or a pass writes it. Mode `off` plays
the parameter's `defaultValue`, which is the static value of any parameter
without points. `AudioParameterBinding` commits a gesture as the pass when
one records it. Otherwise, in `off` or on a lane without points, it commits
one `EditParameter` history step on `defaultValue`, applied while the control
still holds the value. In read mode over a curve nothing is stored.
**Consequences.** Every gesture is heard while held; static edits undo, redo
and save like curve edits. A host that drives `AutomationModel` gestures
directly in `off` mode must commit the value as the parameter default to keep
it after release.

## D-101 — A dB gain's range minimum is silence

**Context.** Faders need negative infinity (AUD-11), but automation curves
store finite canonical values, and the session's track gain spans −60…+12 dB.
Storing −60 dB for "fader at the bottom" substitutes an arbitrary
attenuation. **Decision.** For a parameter of dimension `gain` in `dB`, the
range minimum means −∞ dB: `AudioRenderGraph` multiplies by exactly 0 at or
below it, `AudioParameterBinding` reports `silent`, and the Fader's lowest
mark reads −∞. The finite range is everything above the minimum.
`AudioRenderGraph::hasSilenceFloor` is the one predicate both sides use.
**Consequences.** Silence is bit-exact in renders, device output and meters.
A host that needs −60 dB as a finite level gives the parameter a lower
minimum.

## D-102 — Meters measure 10 ms blocks on the audio thread; ballistics run on audio time

**Context.** Meter values must leave the device thread without locks or
allocation, and AUD-12 requires hold, decay and clip behaviour that does not
change with the UI refresh rate. **Decision.** `AudioMeterTap` measures each
rendered period in place: peak, sum of squares, and K-weighted sum of squares
per channel, in blocks of `round(rate / 100)` frames. Closed blocks go
through a preallocated 1024-block SPSC ring; a full ring drops and counts,
and never waits. The GUI drains every block. `AudioMeterModel` advances hold
(1500 ms) and fall (24 dB/s) by each block's audio duration and publishes
once per drain. **Consequences.** Snapshots are identical at any publish
cadence. The GUI can be at most 10 s late before blocks drop, and a drop is
reported. One tap serves both peak/RMS and loudness, and further measurement
points (inputs, tracks, buses) are further taps.

## D-103 — Loudness follows ITU-R BS.1770-4 with a bounded histogram gate

**Context.** AUD-29 asks for loudness history with provenance, and forbids
labelling generic values as calibrated loudness. Integrated loudness gates
over the whole programme, so storing every 400 ms block grows without bound.
**Decision.** `AudioLoudnessModel` implements BS.1770-4. The K-weighting is
bilinear from the analog prototypes, so every rate gets an exact filter.
Channel weights come from Table 3. Momentary and short-term are 400 ms and 3 s
windows updated every 100 ms, and the integrated value is gated at −70 LUFS
and −10 LU. Gating blocks are kept in a 0.01 LU histogram with exact energy
sums; the relative threshold uses the exact mean. The model names its metric
`ITU-R BS.1770-4` and its report states that it is not a certified meter.
**Consequences.** Memory is constant for any duration. Only blocks within
0.005 LU of the relative threshold are classified at bin resolution. True
peak, LRA and conformance certification are separate work.

## D-106 — Object appearance lives in each canvas object record

**Context.** WP-044 needs stroke alignment, a miter limit, opacity, blend
modes, pattern fills and an ordered effect stack that survive undo, save,
clipboard and export, while a richer host may carry effects the reference
renderer cannot draw. **Decision.** `CanvasObject::appearance` holds them
through an immutable, implicitly shared handle (`CanvasAppearanceRef`) that
is null for the default, so a plain object and each undo step's before and
after copies pay one pointer instead of a ~100-byte record (inline, it failed
the canvas history-memory bound). Effects have stable token ids, an enabled flag and parameters stored as
canonical compact JSON, so an unknown kind round-trips byte for byte and is
reported unsupported instead of dropped. One rule set
(`CanvasAppearanceRules`) validates commits, the decoder and both models.
Native version 6 appends a length-prefixed appearance block to every object
record (empty for the default appearance) rather than using the extensions
JSON, because commit-time size accounting covers records but not that block,
and a committed canvas must always save. **Consequences.** Plain objects grow
by four bytes and versions 1–5 still load; appearance edits are ordinary
object deltas, so undo, sessions and the clipboard carry them unchanged.

## D-107 — Appearance renders through an offscreen layer; paint bounds index it

**Context.** Group opacity, blend modes, drop shadows, blurs and one-sided
strokes cannot be drawn primitive by primitive, and tiles would show seams
if an effect only saw its own tile. **Decision.** The software painter paints
such an object into an offscreen layer covering the tile plus the effect
stack's input reach, applies the effects in stack order (the blur is the SVG
three-box approximation, in document units scaled to device pixels), then
composites the layer with the object's opacity and blend mode. Aligned
strokes are twice as wide and masked by the fill area. The RHI provider
rasterizes tiles holding non-default appearance through the same painter.
`CanvasGeometry::paintBounds` (document bounds plus effect reach) feeds the
spatial index and damage; selection, alignment and snapping keep
`documentBounds`. **Consequences.** Both backends agree and shadows reach
neighbouring tiles; layers are bounded at 4096 px, beyond which very large
blurs render at lower resolution. Host adapters must index by paint bounds.

## D-108 — Path booleans flatten operands and replace the backmost operand

**Context.** Union, intersection, subtraction and exclusion need robust
handling of fill rules and self-intersections; exact curve-curve booleans
are a large, risky engine. **Decision.** Operands are flattened to straight
segments within 1e-4 of their combined extent and combined with Qt's path
clipper, which honours each operand's fill rule; `combine` merges contours
unchanged instead. The result replaces the backmost operand (its id, layer,
paint order, style, appearance and transform) and removes the others in one
undoable edit. **Consequences.** Results are exact for straight input and
within a stated tolerance for curves, and become straight-segment paths;
selection and paint order survive through the backmost id.
Curve-preserving booleans remain future work.

## D-109 — SVG export writes appearance with SVG 1.1 constructs in document units

**Context.** Canvas strokes, shadow offsets and blurs are document units,
while SVG strokes and filters scale with element transforms, and SVG 1.1 has
no stroke alignment or blend modes. **Decision.** Patterns become `<pattern>`
cells in local units; aligned strokes become the fill plus a doubled stroke
under a document-space `<mask>`; effects become filter primitives chained in
stack order on a document-space `<g>` with `color-interpolation-filters`
sRGB; opacity becomes `opacity` and blend the CSS `mix-blend-mode` on that
group. A similarity transform stays an attribute with stroke widths and
dashes divided by its scale; any other transform is written into
document-space path geometry. Enabled effects of an unknown kind refuse
export explicitly. **Consequences.** Scaled strokes export at their canvas
width (earlier output scaled them), browsers reproduce the appearance, and
renderers without CSS compositing ignore the blend mode.


## D-110 — Mixer state lives in AudioMixerModel; channel values are automation parameters

**Context.** WP-062 mixer and routing controls held host snapshots, so no
engine graph changed and nothing was saved or undone. Track mute and solo
already live on `TimelineTrack` (edited by `TrackHeader`), and WP-061 made
gain an `AutomationModel` parameter with one transaction per gesture.
**Decision.** Each `AudioSession` owns an `AudioMixerModel`: buses, per-channel
routing (output, layout, sends, solo safe), mute/solo groups and the pan law,
validated as a whole and recorded as one step of its own history. Channels
are `track:<uuid>`, `bus:<uuid>` and `master`. Track mute, solo, arm and name
stay on the `TimelineTrack`; gain, pan, width and send levels are
`AutomationModel` parameters (`<channel>/gain|pan|width`, `<send>/level`)
created on request and never removed. The mixer hands out one shared
`AudioParameterBinding` per parameter. Hidden channels are saved view state,
not undo steps. The session saves the mix as its `mixer` part and loads it
all or nothing. **Consequences.** Every mixer value has one owner and one
history, which the controls and automation already share. A track's settings
stay dormant after the track is deleted, so undo in the timeline brings back
its routing.
**Amended after the WP-062 audit.** `session.history` (`AudioSessionHistory`)
joins the three histories: each model keeps and applies its own steps, and
the journal records which model took each step, in order, so one Undo always
reverses the session's newest step; undo on a model directly moves its
entry, a new step ends every redo, and a model that reloads drops its
entries. Lanes and dormant settings stay in memory for undo, but a save
writes only what a reopened session can reach (no settings of deleted
tracks, no lanes of channels or sends that are gone), and bindings and
meters of channels that are gone are released. `AudioSession::open` checks
the mixer part before it replaces any part, so a bad mixer part keeps every
history and held gesture. A hardware control surface (`AudioControlSurface`,
Mackie Control over an ALSA sequencer port or host bytes) edits and follows
the same shared bindings and operations, so automation, strips and hardware
feedback update one parameter model.

## D-111 — Pan laws, layouts and conversions

**Context.** AUD-15 needs defined laws and coordinates, and the engine must
not change what existing sessions play. **Decision.** Tracks are Mono or
Stereo; buses may also be Quad (L R Ls Rs) or 5.1 (L R C LFE Ls Rs); the
master is Stereo. A mono source into a stereo destination follows
the session pan law: `-3dB` constant power (default,
L = cos((1+p)π/4), R = cos((1−p)π/4)), `-4.5dB`, `-6dB` linear or `0dB`
balance. Pan p runs from −1 (left) to +1 (right) and width from 0 to 2, both
in steps of 0.01. A stereo source applies mid/side width, then balance (unity
at the centre). A mono file on a stereo channel plays on both lanes, a mono
channel averages the first two file channels, and a stereo route into a mono
destination averages. Every route, sends included, passes through the
channel's panner; a pre-fader send skips only the fader. **Consequences.** The
default mix (stereo tracks, centre, width 1, 0 dB) is bit-exact with the
pre-mixer engine, and the laws are exact at the ends and symmetric at the
centre.
**Amended after the WP-062 audit (spatial panning).** A mono or stereo
source into a Quad or 5.1 destination is a point at (pan, depth), depth
−1 rear … +1 front (default front, parameter `<channel>/depth`): the session
law applies left–right and rear–front, the 5.1 front pans pairwise L–C–R and
the LFE takes no panned source; each side of a stereo source is a point at
pan ∓ 1. A surround source converts through a fixed matrix: lane to lane,
Quad and 5.1 onto each other (a centre into L and R at −3 dB), the ITU-R
BS.775 stereo downmix (centre and surrounds at −3 dB, no LFE), and its mono
average. Device ports carry 1, 2, 4 or 6 lanes. Channel meters carry up to
six lanes.

## D-112 — The bus graph stays acyclic; solo propagates through it; ports never fall back

**Context.** AUD-16 requires refused feedback routes to be explained and
forbids silently connecting another port when a device goes away. Solo must
not silence the bus a soloed track feeds. **Decision.** Tracks only feed;
buses feed buses, the master or a port; the master feeds only a port. An
operation that would close a loop is refused with the loop spelled out
(for example "B → A → B"), and a file containing one is not loaded. Solo is
solo in place: a soloed channel keeps everything upstream and downstream of
it, through outputs and sends. Every other channel is implicitly muted
unless it is solo safe, and a muted channel feeds nothing. Group mute and
solo apply to the members. Ports are `main` (the running device's first
outputs, or the file) and `device:<n>[-<m>]@<device id>`. A device port plays
only while that device runs; after the device goes, a route to it stays
unchanged, silent and explained, and a new route can only name an available
port. An offline render is the main output with the master connected.
**Consequences.** No feedback can reach the engine, and one mixing pass in
topological order is enough. A device change never redirects audio: the user
re-routes explicitly, and the default `main` port keeps new sessions audible.

## D-113 — Channel meters travel as generation-tagged frames; held strips pin the mixer view

**Context.** Per-track and bus meters were left to WP-062, and every gesture
preview recompiles the graph, so slot numbers change between graphs.
Virtualized strips must not lose or redirect a gesture when banks,
visibility or track order change. **Decision.** `render()` adds each
channel's post-fader lanes to an `AudioChannelMeterSink` (one per stream,
preallocated). The sink closes 10 ms frames of up to 128 channels and closes
one early when the graph generation changes. The GUI maps a frame's slots
through the channel list of that generation and feeds one `AudioMeterModel`
per channel (`AudioEngine::channelMeter`). `AudioMixerStripModel` moves rows
instead of resetting them. While any channel's parameter is held, `MixerView`
defers bank changes, keeps the held strip in view, and keeps a hidden held
channel's strip until release. `Fader` and `Knob` send a whole gesture to the
binding it began on and cancel it if a recycled delegate changes the binding.
**Consequences.** Meters stay on audio time with no allocation on the device
thread, and a frame never mixes two slot layouts. A gesture survives a track
reorder and a bank or visibility change, and a delegate can never commit into
another channel.
**Amended after the WP-062 audit.** Slots are not handed out in processing
order: while every channel fits, all are metered; with more, the master
comes first, then the channels a view watches (`watchMeter`, which each
`ChannelStrip` does for its channel), then the rest, and `meterProblem`
explains a channel left out. `PanningControl` keeps each gesture on the
binding it began on as `Fader` and `Knob` do, and `RoutingMatrix` rows come
from a model that moves rows in place, with selection by input ID.

## D-126 — Grade and captions are a finishing stage of the media program

**Context.** VID-06/08 require a grade and burned-in captions that the
monitor previews and the export writes identically, with the grade's undo
history and the wheel gestures of WP-072's controls, and without a second
render path. **Decision.** `MediaFinishing` (grade parameters, burned cues,
embedded cues and language) is plain data on `MediaProgram`, applied last in
`MediaProgram::render` for every consumer: the grade through
`ColorGradeProcessor` in a fixed context (frame tag into linear sRGB, output
encoded sRGB) on 32-bit float pixels, then `CaptionBurnIn` at the frame's
floor program millisecond. It is part of the program's cache key and JSON.
`VideoFinishing` builds it from a `ColorGradeEditSession` and a
`CaptionTrackModel`; `MediaPlayer.finishing` shows the live value, exports use
the committed grade. **Consequences.** Preview and export cannot drift; a
neutral grade is an identity; a drag re-renders the presented frame without
re-probing media; HDR sources are graded into SDR sRGB output, and an HDR or
wide-gamut output grade is future work. *Amended (WP-072 audit):* the session
`VideoFinishing` reads is switched to `ProgramGrade` scope, so feeding it new
frames never resets the program grade, and it previews in
`ColorGradeProcessor::programContext()`, the one context `MediaFinishing`
grades with.

## D-127 — Scopes measure the encoded signal under a stated matrix, range and space

**Context.** VID-07 asks for scope values with explicit range, transfer and
channel conventions and reference-bar verification, and for frame
association while scrubbing without analyzing every repaint. **Decision.**
`VideoScopeConvention` names the Y′CbCr matrix (709, 601, 2020 NCL), the 8-bit
code mapping (full, or limited 16–235/16–240) and the analysis space (`srgb`
converts tagged frames to encoded sRGB; `signal` measures delivered values and
reports their transfer). Waveform, parade and histogram index 8-bit codes, and
each snapshot carries exact per-column and frame-wide code extents and the
convention's vectorscope targets. `VideoScopesModel` analyzes only on demand
and, bound to a `MediaPlayer`, once per presented frame revision, naming the
snapshot with the player's `presentedFrameKey`. **Consequences.** Known-answer
bars give exact codes (Rec.709 limited 75% yellow Y′ 168, Cb 44, Cr 136);
limited range describes the legal-range encoding of the decoded frame, not the
file's original codes; linear-light and HDR nit scopes are not provided.
*Amended (WP-072 audit):* the default matrix and range are `source`, the
Y′CbCr decode the decoder reports for each frame, so a Y′CbCr file's scope
shows its own codes; stated values remain available and each snapshot names
the origin of its matrix and range.

## D-128 — Captions interchange as plain text with BCP 47 languages

**Context.** VID-06's completion check round-trips a supported format with
timing and language preserved; WebVTT and SRT carry language differently and
the export needs an embedded path. **Decision.** Cue text is plain text. WebVTT
writes the track language as a `Language:` header and wraps a cue in another
language in one `<lang>` span; import takes an explicit language, else the
header, and decodes/escapes only the entities that plain text needs. SRT
carries no language (the host names it). Exports embed cues as a Matroska
SubRip stream whose language is the track's BCP 47 tag, muxed as frames pass
each cue's start; `MediaCaptions::readEmbedded` reads it back.
**Consequences.** JSON, WebVTT and embedded round trips keep timing, text and
language; WebVTT styling markup and SRT tags are literal text; bitmap and ASS
subtitle streams are refused, not approximated.

## D-122 — Diagram and CAD documents share a duck-typed canvas contract

**Context.** WP-083 asks for separate diagram and CAD domain models behind
common canvas contracts. A C++ base class would couple two unrelated data
models and complicate `QML_ELEMENT` registration. **Decision.** Each domain
document is its own `QObject` and exposes the same members: `revision`,
`canUndo`, `canRedo`, `lastError`, `undo()`, `redo()`, `saveToFile(path)`,
`loadFromFile(path)` (atomic, fresh history) and `exportSvg(path)`. Its view
takes a `document` and owns only selection, snapping and viewport pose
(`zoom`, `panX`, `panY`). `tst_diagram_document::sharedCanvasContract`
calls both documents through the meta-object only. **Consequences.** Hosts
can wire toolbars, undo and export generically; a new domain surface must
pass the same test to join the contract.

## D-123 — Diagram layout orders columns by predecessor barycenter

**Context.** Rank layout placed nodes in saved order within a column, which
crossed edges whenever saved order disagreed with the previous column.
**Decision.** After cycle condensation and longest-path ranking, one
downward sweep orders each column's components by the mean row of their
predecessors; saved order breaks ties. `autoLayout` and `sankeyLayout`
share this ranking. **Consequences.** Output stays deterministic and
idempotent. It is a heuristic, not a crossing minimum; no up-sweeps or
transposition passes are run.

## D-124 — Diagram routes are computed by the document

**Context.** The canvas drew wires in JavaScript while an SVG export also
needs the same geometry; two routers would drift. **Decision.**
`DiagramDocument.routeEdges(metrics)` computes obstacle-aware orthogonal
routes in document space. The view passes its card metrics and draws the
polylines through `NodeGraph.edgeRoutes`; `exportSvg` uses the same
function. A route enters no card interior when one of its candidates
(midpoint channel, channels beside cards, lanes above/below cards) allows it;
otherwise it takes the outermost lane and reports `clear: false`.
**Consequences.** Export matches the screen. The router checks every card
per candidate, which suits interactive diagrams; large-graph performance is
qualified with the deferred packages.

## D-125 — CadDocument declares 2D sketch operations; 3D goes to the scene viewport

**Context.** EXT-04 mentions 2D/3D engineering geometry, while the toolkit
does not certify solid-modeling precision. **Decision.** The declared CAD
operations are the 2D sketch set in `docs/cad.md` (points, segments,
circles, arcs, six constraint kinds, dimensions, snapping, measurement,
native reopen, SVG/DXF output). 3D presentation of engineering geometry
uses the accepted WP-076 scene viewport; a solid-modeling kernel remains a
host-provided extension. **Consequences.** WP-083's CAD qualification
covers only the declared 2D operations.

## D-283 — Domain documents group a view gesture into one transaction

**Context.** EXT-03 asks that connect, move and align commands share canvas
transactions. A CAD line gesture creates up to two points and a segment, and
a diagram stencil drop creates a card and its ports; recording each call
split one gesture into several undo steps, and a rejected finish left
orphan points behind. **Decision.** `DiagramDocument` and `CadDocument`
both expose `beginTransaction()`, `commitTransaction()` and
`cancelTransaction()`. Inside a transaction only the first edit records the
pre-transaction snapshot; nested pairs join the outer one; cancel restores
the state and the undo/redo stacks from begin and keeps `lastError`. Undo
and redo are refused while a transaction is open, and a load discards it.
Single commands that already touch many cards (`moveNodes`, `alignNodes`,
`distributeNodes`, `removeNodes`) stay one step on their own.
**Consequences.** `revision` still advances per edit, so bindings refresh
during a gesture; history grows by one entry per gesture.
`tst_diagram_commands::transactionContractIsShared` drives both documents
through the meta-object.

## D-134 — Source edits are an operation list per resource with their own history

**Context.** WP-063 needs real trim, fade, gain and spectral edits that play,
render, analyse and undo consistently, while the engine's device thread must
never allocate or run editing code. **Decision.** Each `AudioSession` owns an
`AudioSourceEditModel` (`session.edits`). A source is its decoded original
plus an ordered list of edits in source-sample coordinates (`gain`, `fadeIn`,
`fadeOut`, `silence`, `delete`, `crop`, `spectralGain`); in-place edits keep
their computed replacement samples, so undo refolds from the original with
copies only. The model has its own history (per-owner undo, as D-110). Every
change bumps the source revision and emits `sourceChanged`; the session
drops resampled copies and recompiles, so the engine adopts new immutable
buffers at a period boundary. The session saves the edit list as its
`edits` part and replays it on open. Clips keep their timeline placement: a
shortened source leaves the tail of a clip silent. **Consequences.**
Playback, offline renders and both analysis sources always read the same
samples, the audio thread stays allocation-free, and edits are reversible.
Moving clips with a length-changing edit, and joining histories across
owners, are host or timeline work.

## D-135 — Waveform extrema come from an exact min/max pyramid

**Context.** AUD-20 requires zooming from the whole file to single samples
without dropping peaks, with bounded memory and exact sample boundaries.
**Decision.** `AudioPeakPyramid` stores min/max of 16-sample blocks and of
each ×4 coarser level (about ⅔ byte per sample per channel), built once per
source revision on the thread pool with cancellation. A range query combines
whole blocks from the coarsest fitting level and reads raw samples only at
the partial ends, so every answer is the exact min/max. Columns split a range
of `L` samples into `n = min(count, L, 8192)` parts at `⌊i·L/n⌋`; when
`L ≤ count` each column is one sample. **Consequences.** A single-sample
spike is visible at every zoom and in exactly one column, selections snap to
exact sample numbers, and zooming never rescans the samples.

## D-136 — Spectral analysis uses centred periodic-Hann frames and amplitude dBFS

**Context.** AUD-21 requires known tones to land at the expected bins and
declared window, hop and normalization metadata, with bounded tiles.
**Decision.** Frame *f* is centred on sample `f·hop` and zero-padded outside
the source; the window is the periodic Hann (`Σw = N/2`); bin *k* is
`k·rate/N`; magnitudes are amplitudes `2|X[k]|/Σw` (DC and Nyquist
`|X[k]|/Σw`), so a full-scale sine on a bin reads 0 dBFS. Display cells take
the maximum over the frames centred in a column and the bins in a band.
Requests are capped at 12 000 cells and `maxFrames` frames: wider ranges use
a power-of-two multiple of the hop. Frames are computed in 32-frame tiles per
(revision, channel, N, hop), cached in a 32 MiB least-recently-used cache.
**Consequences.** Tile boundaries cannot show seams, strided frames are
frames of the full analysis, the same conventions serve analysis and
processing, and memory and paint cost are bounded for any file length.

## D-137 — Spectral processing is an STFT gain with exact patch bounds and provisional previews

**Context.** Spectral selections must be processed with cancellable
previews and reversible commits, without touching audio outside the
selection. **Decision.** `spectralGain` multiplies the bins in
`[lowHz, highHz]` of the frames centred in `[start, end)` (hop N/4, Hann
analysis and synthesis, `Σw² = 1.5`) and adds only the difference those
frames make, so samples outside `[f0·h − N/2, (f1−1)·h + N/2)` are
bit-identical and untouched bins reconstruct to rounding. A preview runs on
the thread pool, can be cancelled between frames, and once ready is
provisional: the session plays and analyses it until it is committed (one
undo step) or cancelled (the committed samples return). **Consequences.**
A user hears a spectral change before accepting it, a cancelled preview
never lands, and undo restores the exact original samples.

## D-142 — Roto masks sample the frame their layer's pixels came from

**Context.** WP-073 left `MotionFrameRenderer` sampling a layer's mask at the
composition time parsed as a frame index, so an offset, remapped or
tick-timebase layer cut its pixels with the wrong frame's shape. Masks and
tracks are keyed by source frame indices, the indices the media decoder
presents. **Decision.** A masked layer samples its mask at the source frame
index supplied with its pixels (`setSourceImage(sourceId, image,
sourceFrame)`; `MediaFrameFeed` passes the frame it decoded for that layer),
else at `floor(localTicks / maskTicksPerFrame)` of the evaluated layer time
(default 1 tick per frame). Composition time is never used. A negative layer
time fails the frame. **Consequences.** Tracking, mask keys and rendered
masks share one coordinate, so a mask stays on the decoded pixels through
offsets, remaps, holds at media ends and proxies (masks are normalized). A
host that supplies still images at a tick timebase sets `maskTicksPerFrame`
to its frame duration. Two layers sharing one `sourceId` at different times
remain unsupported, as for pixels.

## D-143 — Tracking runs on a worker, holds lost frames and refines to subpixel

**Context.** VID-15 needs cancellable analysis, explicit failure instead of
extrapolated certainty, occlusion handling and measured accuracy; the WP-075
first slice stepped a whole-pixel search on the GUI thread and stopped at the
first lost frame. **Decision.** `TrackingEngine` runs a job on a Qt Concurrent
worker over implicitly shared proxy copies and posts samples back; a job ID
makes cancellation immediate and discards late samples, and replacing a frame
cancels the job. Each frame takes the lowest mean-absolute-error integer
match, then a parabola through the squared-error neighbours for the subpixel
offset (skipped for a zero-error match). A lost frame keeps the last trusted
position with its measured confidence; with `occlusionFrames` the search
continues around that position with a radius growing per lost frame and
resumes `tracked` on a match. Mask propagation (`applyTrackToMask`) keys only
accepted, nonlost frames and reports skipped ones. **Consequences.** Known
integer shifts track exactly and smooth subpixel shifts within 0.2 px;
lost frames are visible in data and UI and never move a mask.

**Amended after the WP-075 audit.** Motion models: a track's `motion` is
`translation` or `similarity`; similarity searches positions under the prior
rotation/scale, then refines position, rotation and scale by a bounded compass
search on bilinear squared error. Perspective is a four-track corner pin
(`QTransform::quadToQuad`) applied to mask paths, not a patch model. Frames
above 4 MP are tracked on a smooth downscale with positions mapped back to
source pixels (up to 64 MP sources). Results are versioned: a preview records
the frame versions it measured, replacing one discards it, accept refuses a
stale one, and undo/redo restore accepted data while keeping live previews
and analysis state. Mask keys record `user` or `generated`; propagation is
previewed per range, committed in one step and never overwrites user keys.
`saveToFile` validates its bytes with the loader so every save reopens, and a
track holds at most 4096 points. 3D camera solving (a camera path from many
tracks) is not part of this toolkit model.

## D-150 — MIDI data is musical ticks at MIDI 2.0 resolution, owned by one model per session

**Context.** WP-064 needs stable note and event IDs, exact positions that
follow tempo, high-resolution and per-note expression, and exact
reversible edits and roundtrips. **Decision.** `MidiSequenceModel`
(`session.midi`) owns patterns, scenes and launcher slots. Positions are
musical ticks at the session PPQ relative to the pattern start; velocity is
16-bit and controller values are 32-bit, with MIDI 2.0 min-centre-max
scaling to and from 7- and 14-bit values. Per-note events (pitch bend,
pressure) name their note and stay inside it and on its channel through
every move, resize and delete. Every change is validated whole and is one
step of the model's own history, which stores the complete before and after
content; the session saves it as the `midi` part. Standard MIDI File
export and import are the declared interchange, and every reduction or
omission is reported. **Consequences.** Undo is exact, MIDI 1.0 values
survive any number of roundtrips, tempo changes move notes without editing
them, and nothing is lost silently at a format boundary.

## D-151 — Step probability is a pure function of seed, row, step and pass

**Context.** Pattern probability must be deterministic across devices,
file renders, seeks and loops, and a user must be able to re-roll.
**Decision.** Each pattern stores a 32-bit seed (default derived from its
ID). Whether step *s* of a row plays on pass *n*, counted from the
placement origin, is SplitMix64 of (seed, row key, s, n) compared with its
probability; 0 never and 1 always plays. A new seed is one undoable edit.
Step rows loop on their own length (steps × step ticks), so rows of
different lengths are polymetric. **Consequences.** The same placement
plays the same steps in every render; a loop repeats its passes exactly;
re-rolling is explicit and reversible.

## D-152 — The engine schedules MIDI statelessly from a compiled musical clock

**Context.** Notes must start on exact samples, the device thread may not
allocate, graphs are recompiled on every edit, and no note may hang after a
seek, loop, edit, stop or device loss. **Decision.** Graph compilation
builds a `MidiClock` (TimeMap's exact tick arithmetic at the graph rate)
and one plain-array program per pattern; arrangement clips become
placements. `render()` evaluates the notes that overlap each chunk and the
built-in test instrument computes every sample from the note alone (cosine
at the pitch, 1 ms attack, 5 ms release after the note end). The same
evaluation yields the scheduled event stream (`midiEvents`) that WP-066
instruments will receive. Audition notes are the only stateful voices;
note-off, all-notes-off, a transport stop, a device stop or loss and
detaching the session release them. **Consequences.** Device output equals
the offline render at any block size, recompiles never cut a note, and a
note never outlives its end by more than the release.
**Amended (WP-064 audit).** A played note lasts at most its pattern's length
(it would otherwise overlap its own repeat); the stored duration is kept.
So a render window looks back at most one pass and device-thread work per
block never grows with playback time. The test instrument follows its
placement's CC7 volume, CC11 expression, channel pitch bend (±2 semitones)
and per-note pitch bend (±48); a bent note integrates its phase from its own
start over fixed change ticks, so it stays stateless and block-independent.

## D-153 — Clip launches are quantized on the device thread and acknowledged from it

**Context.** Launches must stay quantized when the GUI is late, queued and
playing must be distinct, and stops and cancellations must be exact.
**Decision.** Launch and stop commands carry only track and slot keys and a
quantization (none, beat, bar). The device thread computes the boundary
from its own position when the command arrives, answers once (queued,
stop-queued, stopped or rejected) and reports each transition (started,
stopped) at its exact sample, splitting the period there; a later request
on the same track cancels a waiting one. A launched clip plays its pattern
from the boundary's musical tick; a replaced or stopped clip keeps only its
release. `MidiLauncherModel` changes slot states only from these events.
Launched clips and the arrangement sum on a track; a file render is the
arrangement only. **Consequences.** No GUI delay can move a start off the
grid, the launcher shows queued and playing from engine facts, and a
transport stop, device loss or detached session leaves nothing playing.
**Amended (WP-064 audit).** Launcher state is kept in device samples, so a
transport jump (loop wrap, seek) re-bases it on the device thread: a playing
clip keeps its musical progress and is heard from the new position; a
waiting launch or stop is re-quantized from the new position and reported
again. A destroyed session sends the same reset as a detached one. MIDI
recording is engine-owned the same way: the device thread quantizes a
take's start, stamps each audition note (the session's ALSA MIDI input or
the host) with a take-relative tick at the period that applies it, and
sizes the take on stop; the launcher commits it as a new pattern in its
slot, one undo step.

## D-154 — Mesh paint is a surface-space dab with an exact CPU reference

**Context.** VID-24 needs painting across UV seams, camera-aware projection
and paint transactions shared with canvas tools, and headless tests run on
the software renderer, where no GPU draws. **Decision.** A mesh dab is a
sphere on the posed surface whose radius is measured in texels of the hit
face; every face within reach paints the texels whose centre maps back inside
it, composited once per texel at its strongest coverage. Coverage and
source-over compositing are `raster_brush_p.h`, shared with
`TiledRasterDocument`. Stroke samples are joined by re-cast rays interpolated
between camera rays. Paint and picking rays are computed on the CPU from the
same camera parameters the Quick 3D camera binds (`SceneView3D.meshRay`), and
`MeshRigGeometry` uploads exactly the CPU channel bytes (checked byte for byte
by `tst_mesh_rig_geometry`). **Consequences.** A dab continues across seams
without seam-specific code; tests compare exact texels against plain geometry;
the GPU draws the verified data under any backend. Texels are painted where
their centre falls inside the sphere, so very oblique faces sample coarsely,
and GPU-only on-screen qualification is deferred to WP-088..094.

## D-155 — Rig evaluation: keys, then copy-rotation constraints, then limits

**Context.** VID-18 asks for posing within constraints, reset and undo
without silently baking or destroying animation data. **Decision.** A bone's
rotation at a frame is its linearly interpolated keys (or static pose), plus
`influence ×` each `copyRotation` target's evaluated rotation per Euler axis,
clamped to its limits. Constraint graphs are acyclic by construction: adding
or loading a constraint through which a bone would depend on itself is
refused. Evaluation never writes back; limit changes that would exclude a
stored pose or key are refused; reset clears static poses only; posing an
animated bone keys the current frame. **Consequences.** Rig edits are
non-destructive and deterministic; the editor edits the source pose, never
the evaluated one. Euler-sum composition is exact for single-axis rigs and an
approximation for combined axes; IK and other constraint types are future
work.

## D-156 — Mesh materials are three same-size texture channels

**Context.** VID-24 names material channels; the GPU material and exports
must agree with what is painted. **Decision.** `MeshRigDocument` stores
`baseColor` (sRGB), `roughness` and `metalness` (linear scalars as gray, read
from red) at one size; defaults reproduce the earlier material (white,
roughness 0.8, metal 0). Scalar channels are never colour-converted on load.
The renderer maps them with material factors of 1. Version 2 files store all
three; version 1 files open with default scalar channels. **Consequences.** A
roughness or metal stroke changes the GPU material and the exported PNG by
the same bytes; further channels (normal, emissive) extend the channel list.

## D-157 — MeshRigDocument records its edits in UndoService

**Context.** The first slice kept a private 16-state stack, so mesh edits
could not share history with scene or canvas edits in one session.
**Decision.** `MeshRigDocument` is a `DocumentModel`: each edit or paint
stroke commits one immutable before/after snapshot command (unchanged texture
channels share storage) into an `UndoService`, a private one until a host
adopts it with `adoptModel`. Commands carry content tokens, so a step applies
only to the exact state it was recorded against. **Consequences.** Hosts get
one history across mesh, scene and canvas models, undo text and clean
tracking; a paint stroke is never split. The session codec
`qindatk.mesh-rig.v2` (`meshRigDocumentCodec`) now registers the mesh schema,
so `DocumentSession` save, autosave and recovery cover it; an edit gesture
(`beginEditGesture`/`endEditGesture`) is one merged step.

## D-174 — A comp is a selection over take ranges, placed by a timeline clip

**Context.** WP-065 comping must select actual source audio, never replace
or copy it, survive reopen, and move and trim with the arrangement.
**Decision.** `AudioTakeCompModel` (`session.comps`) owns comps. A comp has
one sample rate, takes (a resource, its usable frames and the source frame
at comp sample 0) and sorted, disjoint segments that each play one take
inside that take's coverage. A timeline clip whose `resourceId` is the comp
ID places it; its `sourceOffset` is the comp position at its start, exactly
as for media. Each accepted comp edit is one step of the model's own
history holding the whole comp before and after (per-owner undo, as D-110
and D-134); the comp is saved as the session's `comps` part.
**Consequences.** Take audio is never rewritten, timeline moves, trims and
undo work on comps unchanged, and comp undo is exact. A take must share the
comp's rate.

## D-175 — Crossfades sit at take junctions, centred unless a side lacks audio

**Context.** Adjacent segments of different takes click without a fade, a
fade must not read audio a take does not have, and a split inside one take
must not change a sample. **Decision.** A junction of two different takes
fades over `x` samples before it and `y` after it, `x + y` up to the comp's
fade length: centred when possible, `x` bounded by the right take's audio
before the junction and half the left run, `y` by the left take's audio
after it and half the right run, a shortfall on one side moving to the
other. A run is the touching segments of one take on that side (the WP-065
audit found that per-segment bounds let a split beside a long fade move
it); every piece of a run carries the run's fade regions. Weights use `t = (s − a + 0.5)/(c − a)`: linear or equal power
(sin/cos). Same-take junctions, gaps and comp edges are exact cuts. The
graph plays each resolved piece as an ordinary clip with a frame limit and
fade regions evaluated per sample. **Consequences.** Boundary samples are
exact and testable, both fades of a run never overlap, merges and
splits within a take are silent, and the realtime path gains no tables or
allocation.

## D-176 — Comp gestures preview from the committed comp and commit once

**Context.** A swipe must be heard while it moves, a cancelled gesture must
leave nothing behind, and the view holds exact boundary indices during the
gesture. **Decision.** `preview()` applies the selection to a copy of the
committed comp (never to the previous preview) and plays it until
`commitPreview()` records one step or `cancelPreview()` restores the
committed comp; any other edit, undo or redo cancels it first. The view's
boundary table is built from the committed comp only, so it holds still
while a preview runs. `TakeCompView` reports a cancelled swipe with
`compProposalPreviewCanceled`. Audition and comparison play one take over
the requested range (the view's selection) and change what plays, not the
comp; they are neither saved nor undone, and the view can end them
(`auditionStopRequested`). Previews and auditions are device-only: the
offline graph (`renderToFile`, exports) always plays the committed comp.
**Consequences.** Swipes
are audible and reversible, a rejected proposal leaves the committed
samples, and history holds only accepted comps.

## D-177 — Loop passes are cut from record jumps, and punch selects inside whole takes

**Context.** WP-060 recorded linearly through loop wraps and refused punch.
Passes must line up with what was heard, and a punch must be undoable and
widenable. **Decision.** The device thread reports every transport jump
during a take (loop wrap or seek) through its own bounded ring: the file
frame where the new position starts and the position. Pass *n* starts at
`jumpₙ + latency` in the file and is heard from the jump's position. With
two or more passes (and no dropped frames or lost jumps) the session makes
one take per pass in a comp, later passes winning. Punch is transport state
only: the whole pass is recorded, and the session selects the new takes
inside the punch range, in the comp already on the track, in a media clip
turned into a comp by one source switch, or in a new comp. A recording
that reaches before comp sample 0 or past the comp end grows the comp: the
clip switches, in one timeline step, to a grown copy with shifted
positions, so timeline undo restores the old comp and no comp undo can
misalign the clip's offset. **Consequences.**
Every pass plays exactly what was heard at its position, pre- and post-roll
stay available, the event ring's acknowledgment guarantee is untouched, and
a recording with lost jumps falls back to one linear clip.

## D-291 — Rig timeline animation lives in the WP-073 AutomationModel

**Context.** WP-077 kept bone keys in a private linear list with a manual
integer frame, so KeyframeTimeline, DopeSheet and CurveEditor could not show
or edit rig animation and no exact-tick playhead drove it. **Decision.**
`MeshRigDocument.animationModel` takes an `AutomationModel`;
`bindBoneAnimation(boneId)` adds three angle lanes
`meshrig/bone:<boneId>/rotation.x|y|z` and copies the private keys into them.
A bound bone's source pose is its lanes' values at `timeTicks`, the one
playhead (`timeTicks = currentFrame × ticksPerFrame`). Pose and key edits on a
bound bone key the lanes, one automation step per call; a gesture keys once at
its end. Limits refuse lane values outside them. **Consequences.** Rig keys
get exact ticks, every curve interpolation and the timeline editors for free;
the private keys stay in the mesh file, so nothing is lost without the model.
The host saves the `AutomationModel` beside the mesh, as with
`MotionComposition`, and orders the two histories.

## D-292 — Mesh painting happens where the pointer is shown

**Context.** `TexturePaintView` drew the flat texture but sent its pointer
position into a camera ray, so the painted texel was not the one under the
pointer, and no surface painted from the camera view the user saw.
**Decision.** Texture space and camera space each paint where they are
shown. `TexturePaintView` maps its pointer to the UV of the texel drawn under
it and requests `MeshRigDocument.paintUv`, which centres the usual surface
sphere dab on the point with that UV. `SceneView3D.meshTool: "paint"` sends
camera rays through the pixel under the pointer (`meshRay`), and `"select"`
picks faces and posed bones; the view also draws the host's selected face and
bone from `cameraOverlay`, the exact inverse of `meshRay`. The layer is
CPU-only. **Consequences.** Both surfaces paint the texel under the pointer
under any graphics backend and headless; selection synchronizes both ways
between the 3D view and the UV and rig editors. `rayProvider` and
`paintRayRequested` on `TexturePaintView` are gone.

## D-293 — Spectral masks are sample/Hz polygons applied per analysis frame

**Context.** AUD-21 asks for paint and lasso masks, and WP-063 is the only
package that owns it; a rectangle band gain cannot follow a sweep or a
harmonic. **Decision.** A mask is a list of polygons whose vertices are
exact source samples and Hz, carried by the `spectralGain` edit as `mask`
and saved with it. A lasso is one polygon; a brush stroke adds one octagon
per dab (dabs at least a brush radius apart), so the mask is a union. A
frame centred on sample *c* changes bin *k* when `k·rate/N` lies inside any
polygon on the line `x = c` (even-odd per polygon, half-open edge rule);
frames the mask misses are skipped and contribute nothing, and a rectangle
mask gives exactly the band edit. At most 4096 points per edit.
`SpectrogramView` converts gestures to sample/Hz points (decimal addition
keeps samples exact past 2^53) and draws the committed mask from them, so
it keeps its place through zoom. **Consequences.** Masks are reversible,
persistent and numerically the same machinery as band gain; free-form
repair tools remain out of scope.

## D-294 — Committed edits hold parameters; spectral patches are a bounded cache

**Context.** Every in-place edit kept a float copy of its range for the
session, so repeated whole-file gains on long sources grew without limit.
**Decision.** Gain, fades and silence keep no samples: `apply()` recomputes
them from the base. Spectral patches are a cache of a deterministic result,
bounded by `edits.patchBudgetBytes` (64 MiB by default); past it the oldest
are dropped, the newest always stays, and a fold or redo recomputes a
dropped one bit for bit. A fold edits one private working copy in place;
only a length change allocates another. **Consequences.** Memory for edits
is the current buffer plus at most the budget; an undo past dropped
spectral patches re-runs their STFT on the GUI thread (bounded by each
edit's range).

## D-190 — Plugins run as CLAP in one host process per instance

**Context.** WP-066 needs real plugin processing, state and editors, and a
plugin that crashes or hangs must not take the session down (PLT-11,
ENG-16). No plugin SDK is installed on the build machines; CLAP is a
header-only MIT ABI, LV2 needs lilv, VST3 needs its SDK. **Decision.** The
supported format is CLAP 1.2.9, its headers vendored in `third_party/clap`
and used only by `qindatk-plugin-host` and the test plugins; the library
never loads plugin code. Scanning runs `--scan` per binary and hosting runs
`--serve` per instance, each in its own process with a time limit. Control
travels as JSON lines over the process's stdin/stdout (the protocol uses a
private copy of fd 1; the plugin's stdout goes to stderr); audio travels
through one shared memory block per instance (`memfd`, inherited by that
child only) with futex request/response words. Instances start only from
plugins the registry found, never from a path in a session file. LV2 and
VST3 are declared unavailable with their reasons. **Consequences.** A crash
or hang kills one process; the session, the device and other plugins
continue. Each instance costs a process and a context switch per block. A
format adapter added later brings its own scan and serve paths behind the
same protocol and its own evidence.

## D-191 — Plugin parameters are automation parameters

**Context.** Knobs, automation lanes, undo and the plugin's own editor must
agree on one value, as they do for the mixer (D-100, D-110). **Decision.**
Every automatable, visible, writable plugin parameter is the
`AutomationModel` parameter `<instance>/<CLAP id>` (plain range, unit
`plain`, step 1 when stepped), created with the plugin's value when the
instance is added and never removed. The automation is the source of
truth: the graph sends each value at block starts when it changes and
resends all after an activation, a flush carries changes while nothing
plays, and editor changes come back as binding transactions (gesture begin,
previews, commit). Read-only, hidden and bypass parameters stay the
plugin's. **Consequences.** Parameter IDs survive moves, bypass, failure,
save/reopen and reload; a recalled session plays its automation whatever
the plugin's state held. Automation resolution inside a plugin is one block
(at most 256 frames), not per sample.

## D-192 — Late, failed and bypassed inserts pass dry audio aligned to their latency

**Context.** The device thread must not block indefinitely on another
process, and a plugin that stops answering, crashes or is bypassed must not
shift the mix. **Decision.** An insert waits for its block at most 50 ms on
the device (5 s offline), watching the host's pidfd. Without a usable answer
it plays its input delayed by its reported latency (the processor keeps a
dry delay line fed at all times), sends nothing new until a late block is
answered, and after 2 s of audio time is declared hung and its process is
killed. Latency is compensated per route at graph compile: every route into
a destination waits for the latest one. Offline renders claim the processors
(the device passes them dry), reactivate them at the render rate, reset them
before and after, and drop the leading `pluginLatency` frames so the file
lines up with the timeline. **Consequences.** A hang costs one late period
once; bypass and failure never move other tracks. The device hears the mix
`pluginLatency` frames late (no look-ahead). Delay lines restart from
silence when the graph is recompiled.

## D-193 — The toolkit owns the plugin editor's window

**Context.** A plugin editor runs in the plugin's process but must behave
like a window of the application (ownership, closing, stacking).
**Decision.** Under xcb the toolkit creates an `AudioPluginEditorWindow`,
transient for the requesting window, and the plugin embeds its x11 child in
it (`clap.gui`, not floating); the offscreen and minimal platforms take the
same path so the lifecycle is testable. The toolkit owns title, position,
transient parent, closing and lifetime; the plugin owns its child and that
child's keyboard focus. Closing the window closes the editor first and then
deletes the window; a failed plugin closes it. Where an x11 child cannot be
embedded (Wayland), the plugin shows a floating window of its own.
**Consequences.** Editors follow the application's window ownership on X11;
on Wayland they are separate windows until a Wayland embedding protocol is
supported.

## D-194 — Pitch is analysed by YIN on a 5 ms grid in MIDI cents

**Context.** AUD-28/ENG-12 need an actual, declared pitch detector with
confidence and unvoiced states, whose results PitchEditor can draw and
edits can target. **Decision.** `AudioPitch::analyse` runs YIN
(de Cheveigné & Kawahara 2002): FFT difference function over a window of
one maximum lag, cumulative-mean normalisation, the first dip below the
absolute threshold (0.15) followed to its minimum, then a centred
re-evaluation of `d` at τ−1…τ+1 and a parabola for the sub-sample period.
Frames sit on the global grid `f · round(rate × 5 ms)`; confidence is
`1 − d′`; a frame without a dip or at ≤ −50 dBFS is unvoiced. Pitch is in
MIDI cents (A4 = 6900). Notes are medians of voiced runs split at 50-cent
departures lasting 4 frames; phrases bridge up to 6 unvoiced frames.
`AudioPitchSource` runs it once per committed buffer on the thread pool.
**Consequences.** Sines read within 0.02 cents and a 1200 cent/s glide
within 5 cents; partial and whole analyses agree exactly; polyphony and
pitch below 20 Hz are out of scope; the detector is replaceable behind
`AudioPitch::analyse` without changing the snapshot contract.

## D-195 — Pitch correction is TD-PSOLA on epoch-aligned sub-sample marks

**Context.** The correction must be a documented method whose output can be
verified numerically, keep formants, support retiming and leave the rest of
the source untouched. **Decision.** The `pitch` edit re-analyses its region
and applies TD-PSOLA (Moulines & Charpentier 1990): one mark per period on
the waveform's extreme of one polarity (parabolic sub-sample position), hop
marks in unvoiced material, synthesis marks spaced by period ÷ pitch ratio,
Hann grains at least as long as the spacing on both sides, grain resampling
by the formant ratio, and the grain sum divided by the window sum. Time
edits map output to input with two linear pieces. Channels share the marks
of their mean; 5 ms crossfades join the region to the untouched samples.
Unvoiced grains read the input at one whole-sample offset held through the
run (re-synced to the time map past half a hop), and unvoiced frames next to
a voiced run whose periods repeat join the run for marking, so noise inside
a region passes through and a voice onset after noise is shifted.
**Consequences.** No change reproduces the input to rounding; noise between
voiced parts of a corrected region is the input, and retimed noise keeps
0.90–0.96 of its energy (grains averaged at different offsets had lost
~⅓ in a pitch edit and ~40% in a retime); a ±699-cent
shift of a harmonic tone lands within 0.25 cents per frame; stereo phase is
kept. A phase vocoder or a formant-envelope method would be a new operation
or a versioned parameter, not a silent swap.

## D-196 — Pitch edits declare their limits and measured limitations

**Context.** WP-068 asks for stated limitations instead of general quality
claims. **Decision.** Edits beyond ±700 cents, formant ratios outside
0.8–1.25, stretches outside 2/3–1.5× per side, regions under 20 ms, and
pitch or formant changes over unvoiced material are refused with a reason;
`AudioPitchSource.editFor` clamps gesture targets into those limits. The
docs list only what the fixtures assert (docs/audio-pitch.md) and name the
known artefacts: envelope preservation (a pure sine barely moves), voiced-grain
repetition buzz on breathy material, the joins between voice and noise or silence
(within 3 cents over the last 11 ms before a gap; 6 cents from 11 ms and
0.25 cents from 21 ms after an onset), splices in retimed unvoiced runs,
and re-processing when a note is edited twice.
**Consequences.** Hosts can show the limits from the snapshot
(`maximumShiftCents`, `formantMinimum`, `formantMaximum`); listening tests,
real vocal fixtures and throughput belong to WP-088…WP-094.

## D-197 — A pitch edit is a source edit and the analysis holds through previews

**Context.** Pitch parameters must be non-destructive, undoable, saved and
rendered, and a stale analysis must never replace newer edits.
**Decision.** Pitch corrections are the `pitch` operation of
`AudioSourceEditModel` (D-134): one undo step each, persisted as their
parameters (target cents, amount, formant ratio, anchor/target samples,
analysis settings) and recomputed deterministically on replay, previewed on
the thread pool (D-137); the computed patch is held like a spectral patch,
inside the D-294 budget and recomputed when dropped. `AudioPitchSource` holds its analysis of the
committed samples while a preview is ready, marks it stale on any change to
the committed samples, refuses requests while stale, and drops any result
computed for an older revision. `commitEdit` commits the ready preview only
when it is exactly the request. **Consequences.** Playback and offline
render use the corrected samples with no new code on the device thread;
note IDs survive a drag; the audio-editing stack stays the single owner of
sample edits and history.

## D-198 — NumberField never writes its own value

**Context.** NumberField clamped by assigning `value` and applied user edits
the same way. Either write silently ended a host binding such as
`value: model.xMm`: WP-051's millimetre fields froze at X 0.0 and Width 1.0
once rounding changed the number, and hosts grew workarounds (Binding
elements, `onShownChanged: value = shown`, re-asserting after a refusal).
**Decision.** The host owns `value` and `mixed`; the field never assigns
either. It shows `clampedValue` (`value` clamped to `from`..`to`, rounded to
`decimals`), steps and scrubs from it, and reports every user edit and
`reset()` only through `valueModified(clamped)`. The display follows `value`
when the host applies an edit and snaps back when it refuses. A field with no
model echoes the edit itself (`onValueModified: (v) => value = v`).
**Consequences.** Plain bindings on `value` are the normal way to connect a
field, and WP-051's copy-in workarounds and the Knob/Fader Binding elements
are gone. A host that ignores `valueModified` now shows an unchanged field,
and one that clears `mixed` late shows the placeholder until it does.

## D-199 — Qt modules come only from the Qt in use

**Context.** Optional modules were found with `find_package(Qt6Quick3D 6.8)`
and `find_package(Qt6ShaderTools 6.8)`, which search every default prefix
and accept any newer version. Configuring against the Qt 6.8 SDK loaded the
system Qt 6.11 module configs and failed ("Unknown CMake command
`_qt_internal_should_include_targets`"), so round builds had to pass
`CMAKE_DISABLE_FIND_PACKAGE_Qt6Quick3D/ShaderTools`. **Decision.** Every Qt
module outside the required `find_package(Qt6 ... COMPONENTS ...)` call goes
through the root `qindatk_find_qt_module(<Module> QUIET|REQUIRED)` macro. It
searches only the directory that holds `Qt6_DIR` (`NO_DEFAULT_PATH`), requires
exactly `Qt6_VERSION`, and drops a cached `Qt6<Module>_DIR` that points into
another Qt. **Consequences.** A module missing from the Qt in use is reported
unavailable instead of being borrowed from another Qt; Qt add-on modules
installed in a separate prefix are not picked up; a bare
`find_package(Qt6<Module>)` is a defect. The D-201 `QINDATK_ENABLE_*`
switches decide whether the macro is called at all.

## D-201 — Optional engines are explicit build switches with a resolved-feature summary

**Context.** Qt Quick 3D, Qt Shader Tools, Qt Widgets and Qt PrintSupport
were used whenever `find_package` happened to find them. A package could not
omit them (Gentoo forbids such automagic dependencies), and the WP-088
harness could not build the "engine omitted" variants. **Decision.** Every
optional engine has a `QINDATK_ENABLE_*` switch, default ON:
`POPPLER_QT6`, `ALSA`, `FFMPEG`, `QUICK3D`, `SHADER_TOOLS`, `WIDGETS` and
`NATIVE_PRINT`. OFF means the package is not even searched for, and CMake
code tests `QINDATK_WITH_*`, not bare `TARGET` checks. Configure writes
`qindatk-features.json` with what the build resolved. **Consequences.**
`tools/qualify` builds each omission and compares the installed consumer's
capability report with that file ([qualification](qualification.md)).
Packagers can map USE flags one to one.

## D-202 — Installed QindaTK is relocatable: relative QML directory and $ORIGIN RUNPATHs

**Context.** The WP-088 relocation check found three developer-path
assumptions:
- `qtk-preview` compiled in the build tree's absolute QML directory and
  searched it first, even when installed;
- setting its QindaQt RUNPATH replaced Qt's `$ORIGIN/../lib64`, so an
  installed copy outside `/usr` could not find `libqindatk`;
- the QML plugins in `<qml>/QindaTK` inherited Qt's bindir-relative RUNPATH,
  so a QML-only application could not load the plugin from a moved prefix.

Also, the default QML directory pointed into Qt's prefix even when
installing elsewhere. **Decision.**
- Installing into Qt's prefix keeps Qt's QML directory. Any other prefix
  uses the relative `${CMAKE_INSTALL_LIBDIR}/qml`.
- Each plugin gets `$ORIGIN/<relative path to libdir>`.
- `qtk-preview` finds its import root relative to its own executable (the
  build tree is recognized by its `CMakeFiles/` sibling) and appends to
  `CMAKE_INSTALL_RPATH`.

**Consequences.** A staged install can be moved, and consumers run from it
with no source, build or old-stage path. `tools/qualify` checks this
with `QML_IMPORT_TRACE`, `LD_DEBUG`, `ldd` and `strings`. Distro installs
(`QINDATK_QML_INSTALL_DIR` set by the ebuild) are unchanged.

## D-203 — An omitted device backend is reported, not hidden

**Context.** Without ALSA, `AudioEngine` just listed fewer devices, so an
application could not tell "no sound card" from "built without ALSA".
FFmpeg and Poppler already answered with a capability error. **Decision.**
`AudioEngine.backends` lists every backend the toolkit knows (`builtin`,
`alsa`) with `available` and a reason: "QindaTK was built without the ALSA
backend", or "The application did not enable the ALSA backend" after
`setBackends`. **Consequences.** Device settings and the qualification
consumer can show why a backend is missing. A new backend adds its entry
here.

## D-295 — The hub log compacts into a replica state, and connected edits are durable or taken back

**Context.** The operation log had a lifetime cap of 4,096 packets and no
compaction, so a normal editing session filled a document for good; after
that an edit showed locally but lived only in memory. **Decision.** The
authoritative service (the hub; a socket server marks its service) folds its
log into a base: the state a replica derives from the operations (winners,
supporters, removed sets, text sequences with element IDs and tombstones).
The log keeps newer operations and the shared checkpoints; the base is
written before the store names it. Replicas rebuild from base + log + their
own queue; socket peers adopt each new base and keep their unacknowledged
outbox. Every merge rule is idempotent, so a folded operation delivered again
changes nothing, and clocks never reset. A connected edit that the store
cannot hold is refused before it applies or taken back, and multi-operation
edits are checked whole before the first part applies. **Consequences.**
Documents have no lifetime operation limit; a headless hub needs
`setCompactionBaseline`; a rebuild resets the document once; replica stores
move to format 2 (version 1 still opens).

## D-284 — Path node ids are editor state; miter joins follow SVG

**Context.** CAN-19 needs stable node and segment ids so a node selection
survives segment insertion, but the native path record stores nodes as an
array, and persisting ids would change the v6 format and every node-level
equality in history, clipboard and parametric regeneration. Separately, the
canvas stroked miter joins with `Qt::MiterJoin`, which clips an over-long
miter at limit x width from the corner, while SVG export writes
`stroke-miterlimit`, which bevels past limit x width of miter length; and
bounds padded strokes by half the width only, so a spike under a raised
limit fell outside the spatial index. **Decision.** `CanvasPathEditModel`
numbers nodes itself (`n<k>`; a segment is `s` + its start node): its own
edits carry ids explicitly, geometry it has seen before (undo, redo) gets
the same ids back from a bounded memory, and other edits keep ids by anchor
matching (LCS, then equal-length runs by position). Ids are not saved; a
reopened document numbers afresh. `CanvasPath::pen` uses `Qt::SvgMiterJoin`
(verified against `QPainterPathStroker`), and `documentBounds` includes each
join's miter tip up to the limit (`CanvasAppearanceRules::miterTips`), which
also covers the RHI tessellator's clipped spikes. **Consequences.** The
format is unchanged and selections survive every edit in a session. Joins
sharper than the limit now bevel on the canvas as in exported SVG; objects
with sharp mitred joins have larger (correct) paint bounds.

## D-296 — A permanently refused edit is discarded from the replica

**Context.** An edit queued offline before a permission loss stayed applied
in the replica and store, and was resent and rejected on every reconnect: the
replica never converged. **Decision.** The server marks a reject permanent
(no permission, identity mismatch, invalid packet, reused ID) or transient
(full or unwritable store). On a permanent reject the bridge removes that
operation from its outbox and store and rebuilds every local replica without
it, emitting `operationDiscarded`; transient refusals stay queued.
**Consequences.** Replicas converge with the hub after a downgrade; the host
tells the user which edit was dropped. An edit made without permission is
not kept for a later upgrade.

## D-285 — Panels name the fields they set; fill kinds recall per object

**Context.** Style panels sent whole descriptors and the model applied the
fields that differed from the published (first object's) values, so setting
a mixed width to the first object's value did nothing. Choosing fill kind
`none` and back applied a fixed grey or the first pattern swatch, and stroke
units were a host concern. **Decision.** StrokeStyleEditor and
FillStyleEditor add `changed` (the fields the user set) to each request, and
`CanvasStyleModel` applies exactly those. The model keeps each selected
object's last colour, pattern (with the colour behind it) and gradient while
it is bound, and a kind change restores each object's own settings of that
kind before falling back to the descriptor's. Units (`document`, `px`, `pt`,
`mm`, `in`; one document unit is one CSS pixel, as SVG export writes it) are
presentation: the width is converted on request and the last typed value is
shown exactly while the document still holds what it produced. The
selection comes from a `CanvasSelectionModel` shared with the
ToolController. **Consequences.** Mixed selections keep exact intent and
kind switches are reversible without undo; recall is session state, not
document data, and ends when the model is destroyed.

## D-206 — Text carets and selections travel as element anchors

**Context.** Presence carried text positions, which point at the wrong place
after a concurrent edit, and element IDs were private. **Decision.**
`LocalPropertySyncClient::textAnchor` names the element before a position
and `textPosition` resolves it on any replica; a deleted element resolves to
the gap it left. **Consequences.** Shared selections follow edits without
host rebasing; anchors survive compaction because element IDs do.

## D-297 — The ribbon squeezes and wraps; it never scrolls commands away

**Context.** The first `Ribbon` drew every item as the same small button in
one row and scrolled a group row that did not fit. QindaOffice (its first
real host) moves four applications from wrapping tool strips to ribbons and
holds a rule that no command may be hidden at 520 px; a horizontally
scrolled row hides exactly the commands a narrow window needs. The Office
suites also needed a weight hierarchy (a primary command per group),
row-shaped groups (font family and size above B/I/U) and live binding to
their own action objects.

**Decision.** Items carry a weight (`large`, `small`, `icon`); a group is
either column-stacked `items` or explicit `rows`. One `arrange(level)` in
`RibbonGroup` measures and places, and the ribbon picks the lowest
`squeezeLevel` whose summed `widthAt(level)` fits: 0 as declared, 1 large
items small, 2 icon-only where an icon exists, 3 the same plus wrapping
lines. Widths per weight come from label metrics only, so planning never
reads the placement it produces. Items bind to an Action-shaped QObject
(`text`, `iconName`, `enabled`, `visible`, `checkable`, `checked`,
`trigger()`) without the toolkit knowing its class; `CommandRegistry` items
and host components remain. An unknown icon name falls back to the label.
The look — pill tabs, soft group cards with Overline captions, a host
`trailing` slot beside the tabs — is the toolkit's own.

**Consequences.** A narrow window gets a taller ribbon, never a hidden
command; hosts can test that every item of every tab is inside the ribbon
at any width. `ribbonGroupViewport` is no longer a `Scroll` (it has no
`scrollableX`). `compact` mode is a host preference, not a squeeze level.
Tests: `tst_ribbon.qml`, `tst_navigation_models.qml`.

## D-302 — ShapePalette tiles carry host thumbnails and report drags, never drops

**Context.** QindaDiagram (QindaOffice M50, D-050 there) needs a Visio-style
stencil panel: ~50 drawn shapes in seven stencils, previews that look like
the shapes they insert, drag from the panel onto the page, and search
across stencils in a panel ~240 px wide. `ShapePalette` offered Lucide icon
buttons, a `Segmented` category row (seven segments do not fit) and click
only.

**Decision.** Backward-compatible additions. A definition may carry an
`imageSource`; such definitions draw as tiles (thumbnail over a two-line
caption) while icon definitions keep the button. `categoryPicker: "menu"`
swaps the segmented row for a `ComboBox`; `searchAllCategories` lets typed
text search every category. `dragEnabled` adds a `DragHandler` (target
null) to every tile and button that reports `shapeDragStarted`,
`shapeDragMoved` and `shapeDragFinished` (or `shapeDragCanceled`) with the
pointer in scene coordinates. The palette does not move a ghost, accept
drops or know about canvases: the host maps the scene point into its view,
draws its own preview in document space and inserts — the same split as
`shapeRequested`, where the host owns creation and undo.

**Consequences.** Hosts need no `DropArea`/MIME plumbing and keep one
insertion path for click and drag. A drag never also clicks (the handler
takes the grab). Existing hosts are unchanged (defaults: segmented,
category-scoped search, no drag). Tests: `tst_vector_authoring.qml`.

## D-304 — Touch: a finger may ink, two fingers are the viewport's

**Context.** `StylusHandler` dropped touch outright (a resting palm) and
`Viewport` had no touch gestures, so a touchscreen could not draw, zoom or
scroll a canvas; QindaNote asked for all three. **Decision.**
`StylusHandler` inks with ONE finger when the consumer lists `TouchScreen`
in `acceptedDevices` (default unchanged: never touch); the sample's
`pointerType` is `"touch"` and its pressure is `mousePressure`. When a
second finger lands during a finger stroke the handler cancels
(`canceled`, grab released) and never resumes that stroke; a finger's grab
always approves takeover, so a pinch or pan may steal it. `Viewport` gains
a `PinchHandler` (touchscreen and touchpad): the document point under the
fingers' centroid at the start stays under the centroid, so spreading
zooms and moving together pans. `touchPan` (default off, so children that
handle touch keep it) adds a one-finger `DragHandler` pan. **Consequences.**
Consumers opt in per surface and drop the canceled finger stroke; pen and
mouse grabs keep their permissions. Tests: `tst_viewport_touch.qml`,
`tst_stylus_handler` (touch ignored by default).

## D-306 — Agent access is a provider contract over the existing editing engine

**Decision.** A Qt Core/Network-only `QindaTK::agent` library owns live JSON
state, formal action discovery, revision-gated invocation, application-set
per-provider grants and execution receipts. It borrows providers, runs on the
owner thread and exposes the same service through a private local Unix socket,
CLI and MCP stdio adapter. Models do not validate success or own document state.
Receipts are retained up to a hard session limit, never evicted and reapplied.

**Consequences.** QtQuick and native content engines can share one contract;
applications implement domain state and semantic actions instead of exposing
visual delegates. The v1 boundary is local same-user access with synchronous
operations and bounded event polling. Automatic control coverage, async tasks
and remote access are not implied. See [agent-interface.md](agent-interface.md)
and `tst_agent_host` for the contract and real IPC verification.

## D-305 — The ribbon squeezes group by group, from the right

**Context.** One `squeezeLevel` applied to every group: a tab a few pixels
too wide for level 0 whose level 1 still did not fit went icon-only
everywhere, leaving a third of the band empty (QindaNote's Home tab at
1280 px with one more item). **Decision.** `planSqueeze()` steps groups up
one level at a time from the rightmost group, finishing level 1 in every
group before any group goes icon-only, and stops as soon as the tab fits;
level 3 (wrap) remains the last resort. `squeezePlan.levels` carries the
per-group levels; `squeezeLevel` reports the deepest, so its 0–3 contract
and monotonic shrink behaviour hold. **Consequences.** The first groups
(most-used commands) keep their labels longest. Tests:
`tst_ribbon.qml::test_squeeze_goes_group_by_group_from_the_right`.

## D-307 — Terminal input modes and history cells belong to the session

**Problem.** Application surfaces manually encoded keys and pasted raw text,
losing application-cursor/keypad and bracketed-paste modes. Plain-text history
lost styling and double-width geometry, and unread final output could be dropped
when the child exited.

**Decision.** TerminalSession delegates keyboard, paste, mouse and focus input to
libvterm. It exposes terminal properties and retains raw history cells, including
palette indices and glyph widths, for cell-accurate rendering and extraction.
History eviction has a monotonic offset, resize can pop history, and final output
drains in bounded event-loop turns before the exit notification. Launch errors
are reported synchronously through a close-on-exec error pipe.

**Verification.** `tst_terminal_session` and `tst_terminal_modes` run real PTYs:
normal/application keys, modifiers/keypad, paste, SGR mouse/focus, alternate
screen, cursor/title, Unicode, palette history, resize, eviction and large final
output. QQ_Term supplies the surface, selection, search, shortcuts and clipboard.

## D-308 — Scoped read authority belongs to the resource owner

**Problem.** Cooperative same-user v1 grants cannot represent one authenticated
task reading one resource. Reusing an app-wide state getter can disclose other
documents even when the caller names a narrower resource.

**Decision.** Add a separate public scoped Read host and pure value/bounds
policy. The application retains volatile exact grants and supplies an explicit
resource adapter that validates generation/revision and captures only that
resource. Injected task/privacy ports are trusted, side-effect-free composition
boundaries. Admission precedes provider lookup and is rechecked after capture;
a transport must check again immediately before queued disclosure. Legacy v1
grants and protocol remain unchanged; no scoped grant calls a global setter.

**Consequences.** This SDK depends on Qt Core/Network only and has no desktop
private headers or authority lookup. Ports are not yet OS authentication:
desktop/session composition, real document adapters, consent and a scoped
wire transport remain separate production packets. This first host has no
mutation/proposal/receipt API. See [scoped access](agent-scoped-access.md),
`tst_scoped_agent_policy` and `tst_scoped_agent_host`; retain `tst_agent_host`
as the v1 compatibility gate.

## D-309 — Focus reveal keeps a tall item that already shows

**Problem.** D-021's focus reveal called `ensureVisible()` on the focused
item. For an item taller than the viewport — the page of a `Tk.Viewport`
document — that aligns its far edge: focus returning to the page from a
find field, a popover or a dialog scrolled a long document to its top or
bottom (QindaWrite, 2026-10-08).

**Decision.** The reveal leaves the scroll position alone on each axis where
the focused item is larger than the viewport and already intersects it;
items that fit, and items wholly out of view, are revealed as before. The
document view keeps showing its caret through its own ensure-visible
requests. `ensureVisible()` itself is unchanged for explicit callers.

**Consequences.** `tst_scroll_nested` `test_focusKeepsATallItemThatAlreadyShows`
guards it; the nested reveal tests are unchanged.

## D-310 — Raster display resolution is independent of editing coordinates

**Context.** QindaPhotoCraft retains its Rust editing engine while native QindaTK
controls display a reduced composite or a visible tile. Treating that display
texture as the document would scale brush and selection coordinates incorrectly.

**Decision.** Publish the pure QML `QindaTK.Raster` companion separately from
the core plugin. `documentSize` owns editing coordinates; `displayRect` locates
the current texture within them. Existing Viewport navigation and StylusHandler
samples keep their original coordinate system. A tiny checker texture bounds
background storage. The host owns pixels, profiles, commands, history and IPC.

**Consequences.** Existing core ABI and installed consumers need no rebuild.
Source installs can opt into the companion; Portage installs it independently
as `dev-libs/qindatk-raster`. `tests/raster/tst_raster.qml` checks proxy/tile
geometry, mouse pressure/positions, and the painting gate.

## D-311 — Form designs carry positioned controls

**Problem.** QindaBase needs Access-class forms — combo and list boxes,
subforms, command buttons, tab controls, labels, images, lines and
rectangles, and calculated controls — but `FormDesigner` only listed the
bound fields with a caption, an order and a "Shown" flag.

**Decision.** A design's form gains an optional `controls` list: each control
is `{id, type, name, x, y, width, height, ...}` in design units, with
type-specific properties (`caption`, `fieldName` for the bound types,
`expression` as an opaque calculated-control string, `items`/`rowSource`,
`formName` and link fields for a subform, `actionId` for a button, `pages`
for a tab control, `parentId`/`page` for a control on a tab page, `source`,
`slant`). `LocalDataDesignStore::checkStructure` checks the list and
`referencedFields` includes bound controls, so a control can never name an
unbound field; `bindTable` unbinds controls whose field it drops.
`LocalDataDesignDocument` edits controls with `addFormControl`,
`setFormControl`, `removeFormControl` and `setFormControls`, one undo step
each. `FormDesigner` draws them on `FormControlCanvas` (select, drag, eight
resize handles, keyboard nudge, all snapped to `gridSize`; a finished
gesture is one edit) with `FormControlSheet` as the property sheet, and runs
them live in preview and edit mode; record values still change only
through `setFieldValue`, and a host-owned design receives the whole next
list in `designChangeRequested("", {controls})`. The toolkit never
evaluates `expression` (the host's `expressionEvaluator` does) and never
resolves a subform (the host's `subformComponent` does).

**Consequences.** Designs without `controls` behave and look as before; the
toolbox shows only once a design has controls or the host sets
`controlsEnabled`. Coordinates are absolute in the form, also for controls
on a tab page; moving a tab control moves its pages' controls in the same
edit. `tst_local_data_design_document` (commands, refusals, undo,
rebinding) and `tst_form_designer_controls` (toolbox, canvas gestures,
property sheet, live preview/edit, host-owned path) guard it.

## D-312 — Group header rows are the host's projection

**Problem.** QindaPlan groups its task sheet and Gantt chart (MS Project's
Group By): a band per group with a summary, collapsible. The sheet
(`EditableGrid`) and the chart (`GanttView`) must keep showing the same rows
in the same order, and their models (`EditableGridModel`, `GanttTaskModel`)
are separate.

**Decision.** Collapsing is the host's re-projection, not view state. A
`GanttTaskModel` row with `kind: "group"` (`collapsed`, `summaryText`) is a
group header: it has no dependencies, nothing depends on it, and it is never
previewed or moved. `GanttView` draws it as a band with a chevron, name,
summary and a span bar, and reports `groupToggled(id, collapsed)` from the
chevron, Left/Right or Space. `EditableGrid.groupHeader(rowId, index)`
marks the grid's group rows; `GridGroupHeaders` draws the band (name,
summary, per-column values) above the cells, the grid never edits such a
row, and the chevron, Return or Space emit `groupToggled`. The host then
supplies both views the next rows.

**Consequences.** One source of truth for row order; no hidden-row geometry
in either view, so links, frozen panes and synchronized scrolling are
unchanged. `tst_gantt` (C++), `tst_gantt_planning` and
`tst_grid_group_rows` guard it.

## D-313 — Planning bars are presentation fields

**Decision.** `GanttTaskModel` accepts, validates and exposes as roles
`slackFinishUtc` and `freeSlackFinishUtc` (bars after the task),
`deadlineUtc` (an arrow, `danger` once the bar ends after it),
`progressUtc` (the progress point), `segments` (ordered, non-overlapping
split pieces) and a second baseline lane (`baseline2StartUtc` with
`baseline2FinishUtc` or `baseline2DurationMinutes`). `GanttView.progressLines`
(`[{utc, role}]`) draws MS Project progress lines through each task's
progress point. The values are the host engine's; the view only draws them,
and the reference scheduler drops `segments` with `finishUtc` when it moves
a task. Role values are appended, so existing role numbers are unchanged.
Lane geometry with one baseline is the D-303 geometry.

## D-314 — Completion is host-fed and reports the whole entry

**Decision.** `TextField.suggestions` is a list the host keeps current
(strings or `{label, detail, iconName, ...}`); a popup that never takes
focus lists it while the field has focus. Up/Down/PageUp/PageDown move
`suggestionIndex`, Return/Enter/Tab or a click emit
`suggestionChosen(entry)` with the whole entry, and the field never edits
its own text for a choice (a formula bar replaces the token being typed).
The keys are handled in the generic `Keys.pressed` stage only while the
popup is open, so `accepted` and consumers' own key handlers behave as
before otherwise. A `TokenField.validator` declaring two parameters
receives `(label, suggestion)`, so a chosen suggestion's value survives
validation; one-parameter validators are called as before.

## D-315 — A narrow MenuBar moves its trailing menus into "»"

**Decision.** When the bar is narrower than its menus, `MenuBar` moves the
trailing menus — the real `Menu` objects, as submenus — into its
`overflowMenu` ("»"), the bar's last entry, and moves them back in order
when it widens. Widths come from the real `MenuBarItem`s, re-measured only
when titles or the theme change, and the bar's implicitWidth stays the
natural width of every menu, so a layout never shrinks it because menus
moved. Menus move only with `takeMenu`/`insertMenu`/`addMenu`, never
`removeMenu`; host insertions are re-flowed, and a moved menu that its
host destroys leaves a null submenu that the next re-flow drops.
`overflowEnabled: false` keeps the old clipping behaviour.

**Consequences.** While menus overflow, `count`, `menuAt()` and `menus`
describe the bar as shown ("»" included); `overflowMenus` lists the moved
ones. `tst_menubar_overflow` guards it.

## D-316 — An implicit size assigned to a Flex wins

**Problem.** `Flex` always wrote its measured content size into its own
`implicitWidth`/`implicitHeight`, so a consumer's binding was silently
overwritten (AGENTS rule 5 told everyone to wrap in an `Item`).

**Decision.** `Flex` overrides the two properties: a value or binding
assigned from QML (or C++ through the property) is what it reports to its
parent; assigning `undefined` resets to the measurement. The content is
still laid out in the Flex's real size. `Grid` and `Stack` are unchanged.

**Consequences.** The two in-tree Flexes that already assigned an implicit
size (TerminalView's status bar, the grading example's readout) now get the
size their authors wrote. `tst_layout_intrinsic` `flexHonoursDeclaredImplicitSize`
guards it.

## D-317 — Consumers' state survives the toolkit's own updates

**Decision.** A `FormGroup` toggle (its header, or `focusFirstInvalid`
expanding it) first emits `toggled(collapsed)`; only when `collapsed` is
unchanged afterwards does the group assign it, so a consumer that binds
`collapsed` and answers `toggled` keeps its binding, and the header re-binds
to the group instead of keeping a stale value. `Ribbon` matches tabs by ID
on a freshly filtered list, so rebuilding `tabs` (new objects, same IDs)
keeps the current tab, and when the current contextual tab disappears it
returns to the tab that was current before the contextual one appeared
(the first tab when that is gone too).

## D-318 — "Automatic" and "No Color" are colour modes

**Decision.** `ColorButton` gains opt-in `automaticEntry` and
`noColorEntry` above its swatches and a `colorMode` (`color`, `automatic`,
`none`): a document stores "use the automatic colour" or "no fill", which no
`color` value can say. Choosing them emits `automaticSelected()` or
`noColorSelected()`; the swatch shows `automaticColor` or a struck-through
empty square. Without the entries nothing changes.

## D-319 — Imported HTML never names an image the engine would fetch

**Problem.** `RichTextHtml::toFragment` copied every `<img src>` into the
document, so pasted or imported HTML could make the engine read a local
`file:` image (the default renderer loads `file:` and `qrc:`) or hand a
remote URL to a host renderer; QTextDocument's own import could also load
local style sheets.

**Decision.** The import document overrides `loadResource` and loads
nothing. An image keeps its source only when `isImportableImageSource`
accepts it: a `data:image/...` raster URI (not SVG) or a scheme the
application registered with `registerImageScheme` (for example `asset`).
Everything else — `http(s):`, `file:`, `qrc:`, protocol-relative
`//host/...`, relative paths, `javascript:` — becomes a placeholder object
with its alt text and size and an empty source.

**Consequences.** Pasting HTML from another application keeps text and
layout but drops external pictures unless the app opts a scheme in.
`tst_rich_text_html_import` feeds hostile HTML (remote, `file:`,
protocol-relative, `srcset`, entity-encoded, SVG data) and proves a local
`<link rel=stylesheet>` is not read.

## D-320 — A focused rich text area owns its typing keys

**Decision.** `RichTextArea` accepts `ShortcutOverride` while it has active
focus and is editable for printable text without Ctrl, Alt or Meta and for
Backspace, Delete, the arrows, Home, End, Return and Enter without those
modifiers, as Qt's text inputs do. A host's one-letter shortcuts (QindaNote's
P, H, E, T) no longer steal typing; Alt mnemonics and Ctrl/Meta chords stay
the host's, and a read-only or unfocused area leaves every key to them.

## D-321 — Host-kept rich text history receives undo and redo

**Decision.** `RichTextDocument.requestUndo()`/`requestRedo()` are what the
area's `edit.undo`/`edit.redo` commands, Ctrl+Z and Ctrl+Shift+Z call. With
`externalUndo` they emit `undoRequested()`/`redoRequested()` (returning
true) for the host's history, and the commands stay enabled while the
document is writable; without it they are `undo()`/`redo()`. `undo()` and
`redo()` keep their meaning (a no-op returning false with `externalUndo`).
A request made while one is being handled is refused, so a host that
answers with `requestUndo()` cannot recurse.

## D-322 — A whole table deletes as one step

**Decision.** `RichTextDocument.deleteTable(id)` (QML `removeTable`) removes a
table block with its rows, cells, paragraphs and anchored objects as one
undoable `StepBlocks` transaction (refused while tracking changes; a lone
table leaves an empty paragraph), and the area offers it as the
`table.delete` command at the caret. `RichTextFormatState` also reports the
selection's highlight (`highlight`, `highlightMixed`), the value of
`text.highlight`.

## D-323 — Checkable menu items keep their icons

**Decision.** A checkable `MenuItem` with an `iconName` shows its icon and
marks the checked state with a frame around it (`menuItemCheckFrame`,
accent border on `accentSubtle`), as office suites do for Bold or Ruler;
only an item without an icon uses the check mark (or the dot of a `radio`
item) in the icon slot. The accessible checked state is unchanged.

## D-324 — A batch of records is one store change

**Problem.** A mail or calendar sync upserted record by record: each call
scanned its collection (O(n·k)) and emitted `dataChanged`, so QML
re-evaluated every binding on the store thousands of times.

**Decision.** `LocalProductivityStore.upsertMany(collection, records)` applies
a batch as one change (one revision, one `dataChanged`) through a stable-ID
index that is built lazily per collection and dropped on every
`dataChanged`, the one signal every mutation already emits; `indexOf` and
`record` expose the same O(1) lookup. A record without an ID refuses the
whole batch. `upsert` is unchanged.

## D-325 — Paragraph data is part of the paragraph's format

**Problem.** QindaNote keeps tags per paragraph and had nowhere in the rich
text model to put them that would follow the paragraph through undo, save,
copy and paste.

**Decision.** `ParagraphFormat` gains `data`, a JSON object the host owns
(at most 64 KiB compact). `RichTextDocument.setParagraphData` sets it as one
`StepParagraphFormat` (one undo step, applied directly even while tracking
changes) and `paragraphData` reads it. Because it is format, the codec saves
it (`format.data`), fragments carry it and a split copies it to the new
paragraph; nothing draws or exports it.

## D-326 — Tree rows report drops; owners move

**Decision.** `TreeRow` drag and drop is opt-in (`dragEnabled`,
`dropEnabled`) and keyed (`dragKey`), on Qt Quick's internal drag: the
dragged row shows a ghost on the window overlay, a target row shows a
before/after line or an inside frame (inside only when `acceptsChildren`),
and the drop is reported as `dropRequested(sourceId, position)`. Like
`ShapePalette` (D-302) the control never moves a model row; the owner
(a notebook's page tree, a task outline) applies the move as its own undoable
edit.

## D-327 — Galleries render host previews; the character map emits text

**Decision.** `Gallery` lays out tiles whose previews are the host's
`preview` Component (it receives the entry and the selected state), so a
Styles gallery shows the application's real styles and a Themes gallery its
real themes without the toolkit knowing either; hover and keys emit
`highlighted` for live preview, a choice emits `activated`, and `currentId`
stays the host's. `CharacterMap` lists Unicode blocks by code point (no
character-name database) and emits `characterChosen(text)`; the host
inserts it and keeps the recent list.

## D-328 — Toast widths include their message and remain inside the window

Notice exposes unconstrained text measurement independently of its wrapping
layout. Toast clamps that natural width by themed minima/maxima and window
anchor margins; a content-sized parent cannot create a width feedback loop.

## D-329 — Decimal number displays never re-enter their computed binding

NumberField synchronizes display text from the source value and the shared
clamp() function. Reading clampedValue from a configuration-change handler or
from its own notification re-enters that computed binding during initialization.
Direct derivation preserves immediate display updates and host value bindings.

## D-330 — Design history getters are pure

LocalDataDesignDocument history capability/text getters only inspect the
attached service. They never attach a service or emit notifications. Explicit
edits may recover local history after detachment; switching or loading a form
cannot start a recursive canRedo binding through historyAttached.

## D-331 — Reports derive independent interval keys for up to ten groups

An immutable QuerySource facade adds one private key per report level; repeated
source fields remain distinct levels. Prefix, date and numeric interval keys
feed the existing QueryEngine aggregation. A stable group ordering follows the
engine's detail sort without raising its general four-key query limit. Running
sums are report detail columns. Unknown group metadata and unavailable fields
remain in saved designs; invalid live schema references fail explicitly.

## D-332 — Report page controls are part of the generated document

Section breaks and keep constraints are generated with the report, shared by
preview, print and PDF. Whole-group spans are measured with RichTextLayout;
a span fitting one page moves to a new page when it otherwise crosses one.
Oversized groups flow. Applications need no knowledge of rich-text block shapes.

## D-333 — Value lists select the grid editor without owning cell data

EditableGrid.valueList supplies values and Limit To List from host schema.
GridCellEditor switches between the existing named text field and a toolkit
ComboBox. Validation happens before beginning the model transaction. The host's
cell model, selection and commit/cancel semantics remain authoritative.

## D-334 — Side docks constrain display geometry without changing saved sizes

DockHost computes both side zones against the available viewport, reserves
canvas space and proportionally shares excess width above a themed minimum.
Below the minimum floor panes share constrained space equally. Model lane
extents remain untouched and reappear when the host expands.

## D-335 — Border previews propose independent document-edge styles

BorderEditor emits a complete proposed edge map. Each click toggles just one
edge, using the current style, colour and document-point width for new edges.
The host owns undo and persistence. The icon catalog includes case-sensitive.

## D-336 — Task cards render host category colours and named extra fields

TaskBoard delegates optional metadata to TaskCardDetails, preserving the old
appearance when no metadata exists. Colours are record data, with a themed
fallback; names and values remain accessible text and wrap in narrow cards.

## D-337 — LayerTree can consume an independent hierarchical model

A host may provide a QAbstractItemModel with the published layer roles and
request methods. CanvasLayerTreeModel remains the default adapter when model
is null. The view never owns document or selection mutations.

## D-340 — IMAP keywords and message operations share provider jobs

ImapKeywords validates RFC atoms and provides the existing QindaMail category
mapping. Provider APIs expose flags, categories, chosen header fields and COPY;
PERMANENTFLAGS and read-only selection are enforced before keyword STORE.
Header blocks remain bytes. UIDVALIDITY guards remain in every message job.

## D-341 — Noncanonical mailbox encodings preserve server wire names

Modified UTF-7 runs encoding printable ASCII are accepted. LIST retains raw
names and the provider uses that server spelling for later selected, copied,
moved and appended mailboxes. Public mailbox names remain decoded text.

## D-342 — SMTP accepts prepared messages with an explicit envelope

sendPrepared validates envelope addresses, CRLF framing, header separation,
size and NULs, then applies only SMTP dot-stuffing. It never rebuilds headers or
body. Ordinary draft composition now writes Reply-To and importance headers.

## D-344 — Calendar categories are mapped text lists

VEVENT and VTODO map CATEGORIES to categories. Escaped commas/backslashes and
multiple properties round-trip; edits replace the managed property and affect
the CalDAV digest. Empty categories preserve compatibility with earlier hashes.

## D-345 — Recurrence overrides retain the established shared mapping

The existing RECURRENCE-ID/EXDATE codec and LocalCalendarProjectionModel already
map retimed, moved, retitled and cancelled instances. M98 uses those contracts
and their CalDAV, ICS and local-calendar regression gates rather than a second
recurrence implementation.

## D-346 — iMIP conversion never implies sender authentication or acceptance

CalendarMail builds REQUEST/REPLY/CANCEL as multipart/alternative with matching
calendar METHOD and MIME method. Parsing exposes event, organizer, attendee and
sequence metadata with authenticated=false. Replies change one existing
attendee's participation status and preserve scheduling identity. Sending and
trust/acceptance decisions remain explicit host operations.

## D-347 — Downloaded HTML remains opt-in untrusted content

IMAP and POP3 share MIME parsing and MailRecord output. keepHtml defaults false;
when enabled bounded raw HTML is marked untrusted-html for the host sanitizer.
Attachment metadata records Content-ID, inline usage and disposition. Calendar
parts, Reply-To and importance use the same record shape across both providers.

## D-343 — ManageSieve jobs isolate protocol state and enforce TLS

ManageSieveClient uses one Protocol::Channel worker session per request. It
refreshes capabilities after STARTTLS before AUTHENTICATE PLAIN; absence of
required TLS fails before credentials. Quoted strings and bounded literals are
parsed independently of line framing. Script bodies and authentication payloads
never enter transcripts. Response codes remain available to the host. No server
script is activated implicitly after upload.

## D-348 — Desktop notifications are optional and never start a server

DesktopNotifications uses asynchronous session-bus calls when QtDBus is present.
Availability follows the service owner; post is a no-op when absent. Calls disable
D-Bus auto-start, and neither constructing the helper nor receiving mail emits a
notification by itself. Tests run a fake server on a private dbus-run-session bus.

## D-338 — Filter lists, field wells and gallery sections keep host ownership

SearchChecklist edits a proposed checked-ID set, preserving selections hidden
by search. FieldWells emits assignment/removal requests from mouse drag or
keyboard selection. SectionedGallery wraps host previews in collapse-request
headers and never edits its section/selection snapshots. None of these controls
embeds spreadsheet, pivot, or slide document types.

## D-339 — Gantt group disclosure survives a hidden label column

GanttRow keeps one persistent ganttGroupChevron and reparents it into the chart
when labelWidth is zero. A companion task sheet may render the labels without
removing the chart's mouse/accessibility group toggle. The name and activation
signal remain unchanged; chart captions leave space for the control. A themed
opaque backing keeps its glyph visible over a same-colour summary bar.

## D-349 — Office follow-up keeps defaults and rebuilds ABI consumers together

Additive QML/provider APIs retain existing defaults; opt-in raw HTML stays
untrusted and mail mutations stay explicit. C++ layout additions require the
coordinated r18 subslot/reverse-dependency rebuild. Tests and pixel checks use
headless builds, and notifications tests use a private bus.

## D-350 — Applications can consume an uninstalled toolkit build coherently

The separate cmake-build package exports the live qindatk and agent targets,
QML module directory and generated public-header symlinks inside the build
folder. The installed config remains relocatable and unchanged. Consumers of
this development package compile and run against one build, with no home-prefix
installation or live system ABI replacement. The source/build directories must
remain present until those consumers are rebuilt against a packaged toolkit.

## D-351 — Stylus begin grabs stay within the canvas input branch

The underlying pointer-device handler must not take over a press already
claimed by an item in a different visual branch. Its CanTakeOverFromItems
permission permits canvas delivery interactions, not popup/dialog/overlay
buttons. A regression exercises modal and ordinary overlay actions above ink,
including modal background presses and resuming strokes after dismissal.

## D-352 — Print settings follow the settled host width

PrintPreview chooses one or two settings columns after the current layout
transaction. A parent Scroll can change the control's width synchronously in
response to its implicit height; evaluating the column predicate within that
transaction re-enters its own binding. Deferred, coalesced updates use the final
host width and respond to theme threshold changes without feedback warnings.

## D-353 — IMAP sync costs what the store keeps, not what the mailbox holds

A Gmail inbox of 158 000 messages took about a minute per sync, and longer
with two clients, before any message was stored: the sync asked for the flags
of `1:<last UID>` and, on a first sync, the size of every message, then
fetched each new body in its own round trip. Sync now asks for the flags of
the known UIDs only (in `ImapMessages::fetchFlags` chunks), lists only the
newest `fetchLimit` messages by sequence number when nothing is known yet (a
first sync or a UIDVALIDITY reset of a mailbox larger than the limit), and
fetches bodies in batches of up to 25 messages or 16 MiB. What is stored is
unchanged: the newest `fetchLimit` messages, then every new one. Older mail
stays on the server.

## D-354 — Android arm64 is a supported target; desktop-only backends are off there

QindaLink's phone UI is built with QindaTK, so the toolkit builds for Android
arm64-v8a with the installed Qt for Android 6.11 (`cmake --preset
android-arm64`, [android.md](android.md)). Four libraries the NDK lacks stay
required on desktop but become switches that default OFF on Android:
`QINDATK_ENABLE_SQLITE`, `QINDATK_ENABLE_LCMS2`, `QINDATK_ENABLE_LIBVTERM` and
`QINDATK_ENABLE_PLUGIN_HOST` (the CLAP host is a separate executable, which
Android cannot start). Each omitted library swaps in an "unavailable"
implementation of the same C++/QML API (`color_unavailable.cpp`,
`local_database_provider_unavailable.cpp`, `terminal_session_unavailable.cpp`),
so every QML file still loads and the type reports `Unavailable` rather than
failing to link; the alternative of vendoring SQLite, lcms2 and libvterm was
rejected because the phone UI uses none of those subsystems. `terminal_session.h`
exposes libvterm types, so a build without it defines `QINDATK_NO_LIBVTERM`
publicly on the exported target, and the installed package config only looks for
libvterm when it was built in. Qt for Android here has no SSL, so
`Protocol::Channel` falls back to a plain `QTcpSocket` and refuses TLS.
Two source fixes are not Android-conditional in behaviour: the schedule snapshot
slot and `memfd_create` ([android.md](android.md#source-compatibility-fixes-made-for-the-ndk)).
**Consequences.** Desktop defaults, behaviour and tests are unchanged. Android
tests, accessibility, input methods and GPU paths are not qualified. A consumer
must add `QindaTK_QML_IMPORT_PATH` to its `QT_QML_IMPORT_PATH` for
`androiddeployqt` to bundle the plugin. Portage packaging of the Android build
is a later milestone.

## D-355 — Android qmldir files list no scripts; a build guard checks the module

The first device run of an app importing QindaTK from the staged install failed
with `Script qrc:/android_rcc_bundle/qml/QindaTK/CadViewportCommands.js
unavailable`. The qmldir that `qt_add_qml_module` writes had a `Name 1.0 Name.js`
entry for each of the nine `.js` files in `src/qml`. androiddeployqt's
`android_rcc_bundle` holds only each module's `qmldir` and typeinfo (the
components themselves come from the module's compiled resources through the
qmldir's `prefer :/qt/qml/QindaTK/`), and the engine resolves a script entry
against the bundle path, not the `prefer`red one. The files were staged and
compiled into `libqindatk`, but the lookup never reaches them. Qt's own modules
list no scripts in their qmldir for the same reason. The module's QML imports
its scripts by relative path (`import "X.js" as X`), which resolves inside the
module resources, so on Android the script entries are skipped
(`QT_QML_SKIP_QMLDIR_ENTRY`); the desktop qmldir is unchanged.
`cmake/CheckQmlModuleBundle.cmake` runs as the `qindatk_check_android_bundle`
target of every Android build and fails when the qmldir names a missing file or,
for Android, any `.js`/`.mjs`.
**Consequences.** On Android an application cannot call `Tk.FormControls`-style
script names (none is documented or used outside the module); a future script
must be imported by relative path from a module QML file, or the guard fails.

## D-361 — Every Tk.MenuBar is published to the desktop global menu; no opt-out

On the QindaQt desktop an application's menus belong in the panel's global
menu. Until now only applications that wrote a second, C++ menu model for the
QindaQt AppShell (`ApplicationCoordinator` + `composeFirstPartyMenuExport`)
reached it; every other QindaTK application kept its menus in the window,
and an application author could forget the export entirely. The owner's
requirement is that a QindaTK application cannot be made without global-menu
support.
**Decision.** `Tk.MenuBar` owns a `Tk.GlobalMenuExport` that publishes the
bar's live menu tree as `com.canonical.dbusmenu`, announces it for the bar's
window, and collapses the bar while the QindaQt registrar confirms the panel
hosts it (`IsMenuHosted`/`MenuHostedChanged`). There is no switch to disable
it. The transport is implemented in QindaTK (LGPL), following the wire format
and handshake of container-wm's global-menu stack (`src/shell/global_menu`,
`src/app_shell/menu_export`), because the desktop's own applications depend on
QindaTK and the reverse dependency would be a cycle. Fail-closed throughout:
any failure keeps the in-window bar. Windows whose application already exports
its own desktop menu keep that export (the toolkit stands aside), so no window
ever publishes two menus (generalised to any application-owned
`com.canonical.dbusmenu` object, e.g. QindaOffice's own exporter, after the
live check found it).
**Consequences.** Every QindaTK application with a `Tk.MenuBar` shows its menus
in the panel after a toolkit update, with no application change or rebuild.
The library links Qt DBus where available and uses Qt GUI private API for the
Wayland announcement (already tied to the exact Qt build, D-096). Exported
shortcuts are display text; shortcut handling stays in the application. The
AppShell path remains for applications that want a C++-owned action model;
when container-wm adopts this transport the duplicated wire code can go.
Tests: `tst_global_menu` (private bus, fake registrar).

## D-362 — Semantic agent queries use the existing Read grant

QindaBooks needs parameterized registers, reports and payroll previews that
cannot fit in one inspect snapshot. An action descriptor's explicit boolean
`readOnly: true` admits those side-effect-free operations under Read or Edit.
Absent or malformed annotations keep the existing Edit requirement. Schema,
revision, request-ID and revocation checks still run. Queries return fresh
facts without using the 1,024-entry mutation receipt journal or publishing a
write-completed event; writes retain deduplication. This adds no virtual
methods or C++ layout changes and keeps legacy providers conservative.

`AgentServiceCatalog` supplies explicit public-invokable bindings with named
schemas, trailing defaults, const-only Read registrations and borrowed target
lifetimes. It cannot export inherited QObject APIs or pointer results. Apps
keep their domain validation and transactional posting code. CLI argument
files/stdin keep credentials out of process arguments.

## D-363 — TerminalSession answers agent-era terminal protocols in front of libvterm

Claude Code, Codex, Kimi Code and OpenCode probe for the kitty keyboard
protocol, synchronized output, colour queries and program status at startup,
draw faint text, and copy through OSC 52. libvterm 0.3.3 drops all of it: it
has no faint attribute, misreads the arguments of an underline colour as
separate attributes, and its fallback hooks never see private modes, SGR or
DECRQM. In practice Shift+Enter submitted a prompt instead of adding a line,
frames tore while streaming, hints rendered at full brightness, and copying
from a full-screen agent did nothing. libvterm's own legacy path also sent
Ctrl+I, Ctrl+J and Ctrl+M as `CSI u` to programs that never asked for it.
**Decision.** `TerminalSession` tokenizes child output before libvterm
(`terminal_stream_filter`), answers the sequences in
[terminal-session.md](terminal-session.md#protocols-beyond-libvterm) itself and
passes the rest through byte for byte. Faint text is carried in libvterm's
unused cell `font` attribute. Keys are encoded by `terminal_keyboard` when a
program negotiated the kitty protocol or modifyOtherKeys, and by traditional
bytes otherwise. Forking or vendoring libvterm was rejected: it would add a C
dependency to maintain outside Portage and change the cell layout hosts
compile against, for behaviour that fits in front of the parser. The session's
class layout is unchanged apart from one appended pointer.
**Consequences.** Hosts gain `bracketedPaste`, `keyboardFlags`, `cursorColor`,
`programStatus`, `sendKeyEvent()`, `setCellSize()` and the
`clipboardWriteRequested`, `notificationRequested`, `programStatusChanged` and
`modesChanged` signals; existing hosts keep working without a rebuild. Screen
updates are coalesced, so `screenChanged` no longer fires once per read.
Logical-line reflow, OSC 8 hyperlinks, grapheme-cluster widths (mode 2027) and
kitty graphics remain unsupported and are reported as such to programs.
Tests: `tst_terminal_protocol` (tokenizer, key and status vectors) and
`tst_terminal_agents` (PTY round trips).
