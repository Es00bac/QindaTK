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
its routing. Undo is per owner (mixer, timeline, automation) until a host
joins those histories.

## D-111 — Pan laws, layouts and conversions

**Context.** AUD-15 needs defined laws and coordinates, and the engine must
not change what existing sessions play. **Decision.** Channels are Mono or
Stereo; the master is Stereo. A mono source into a stereo destination follows
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
centre. Surround layouts are outside the reference engine.

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
