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

## D-026 — Gantt dates project through a working calendar

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

## D-030 — Polar plots declare bearing and radial semantics outside Cartesian axes

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

## D-041 — Canvas guides are document content

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
(only for non-flat layers), like the guides of D-041. **Consequences.**
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
lost frames are visible in data and UI and never move a mask. Translation is
the motion model: rotation, scale and perspective need rebasing
(`rebaseTrack`) or corrections, and multi-resolution flow or camera solving
remain later adapters.

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
tracking; a paint stroke is never split. A native session codec for the mesh
schema is not registered yet; hosts save through `saveToFile`.

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
before the junction and half the left segment, `y` by the left take's audio
after it and half the right segment, a shortfall on one side moving to the
other. Weights use `t = (s − a + 0.5)/(c − a)`: linear or equal power
(sin/cos). Same-take junctions, gaps and comp edges are exact cuts. The
graph plays each resolved piece as an ordinary clip with a frame limit and
fade regions evaluated per sample. **Consequences.** Boundary samples are
exact and testable, both fades of a segment never overlap, merges and
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
`compProposalPreviewCanceled`. Audition and comparison change what plays,
not the comp, and are neither saved nor undone. **Consequences.** Swipes
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
turned into a comp by one source switch, or in a new comp. **Consequences.**
Every pass plays exactly what was heard at its position, pre- and post-roll
stay available, the event ring's acknowledgment guarantee is untouched, and
a recording with lost jumps falls back to one linear clip.

## D-186 — Spectral masks are sample/Hz polygons applied per analysis frame

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

## D-187 — Committed edits hold parameters; spectral patches are a bounded cache

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
