<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 — Interaction and accessibility progress

**Status: active.** The [source inventory](WP-007-inventory.md) identified
pointer-only SectionHeader disclosure and TimeRuler scrubbing. Those two
repairs are integrated; WP-007 still owns the remaining F04 audit and
FND-07 through FND-10.

## Integrated repairs

- `SectionHeader` and `PropertyGroup` share a guarded disclosure operation
  for tap, Space, Return, Enter and `Accessible.pressAction`. Collapsible
  enabled headers can receive Tab focus and show a theme-derived focus ring.
  Accessible name and description report title and current state. Disabled
  and noncollapsible headers do not toggle; programmatic group state remains
  synchronized. Stable `sectionTitle`, `groupHeader` and `groupBody`
  objectNames remain.
- `TimeRuler` exposes its visible range and value as read-only QML properties,
  an accessible slider role and description, keyboard Left/Right/Home/End,
  and accessible increase/decrease actions. All user scrubs use one bounded
  operation. External playhead updates do not emit `scrubbed`; disabled
  rulers ignore scrubs.
- Two high-contrast presets use opaque derived borders, focus and control
  surfaces. Resolved representative text pairs meet 4.5:1 and control
  boundaries 3:1 in `tst_theme`; switching back restores standard disabled
  opacity. These pairs are a focused palette check, not a whole-toolkit
  accessibility certification.
- `Tk.Density.textScale` now independently scales the font ramp from 0.5 to
  3.0, composing with density font scaling when enabled. Tests cover default,
  reset, clamping, non-finite inputs and JSON/QST font-base preservation.
- An open `Tk.Popover` now follows direct anchor x/y/size, scrolling on its
  ancestors, host size and placement changes; direct-movement and scrolling
  regressions failed on the old source and passed after repair. Oversized
  content now stays within the host and scrolls on both axes; the 220×160
  preview and keyboard reachability regression passed.
  [Focused evidence](WP-007-FND-09-popover.md).
- Popup focus entry/return and small-host bounds now have a focused matrix
  across Menu, Dialog, Popover and CommandPalette. In a constrained nested
  Menu, keyboard selection now scrolls the current row into view, Escape
  returns focus through the parent submenu, and the root menu remains within
  the host. A 2× text palette overflow was reproduced and repaired.
  [Focused evidence](WP-007-FND-09-focus.md).
- `Tk.NumberField` now has reset/default, mixed and invalid-draft semantics
  with user-only change notification and consistent read-only/disabled edit
  gating. [Focused evidence](WP-007-FND-10-numberfield.md).
- Touch density now has a 48px interactive row floor and short Segmented
  choices retain a 48×44 target. The gallery's preset switch scrolls at
  maximum text zoom, with the scrollbar below the labels. At 480×320, the
  header itself scrolls to keep the gallery content visible, and density
  choices scroll horizontally. DockDivider seams now route touch drags through
  a 48px target. `Tk.Splitter` routes touches through a matching expanded
  handle child while native SplitView retains pane sizing and constraints.
  Both axes and all three densities have focused synthetic touch coverage.
  [Focused evidence](WP-007-FND-07-touch.md).
- `Ruler` marker hits use a theme-derived 24px Touch radius while marker
  artwork stays at `size.rulerMarker`; overlapping targets choose the nearest
  marker and exact ties follow model order. Touch edge-drag and insertion tests
  are recorded in [ruler marker evidence](WP-007-FIX-F04-ruler-markers.md).
  Marker keyboard and native accessibility actions remain an open F04 gap.
- `TextField` blocks its custom clear paths when read-only or disabled;
  `SearchField` now debounces user edits while programmatic synchronization
  remains silent. `TextField` and `TextArea` describe read-only/error state.
  [Focused evidence](WP-007-FND-10-text-inputs.md).
- `RangeSlider` has keyboard focus, endpoint controls, a visible focus ring,
  accessible name/actions and user-only change notifications. Its default
  keyboard step now works for fractional ranges. `Slider` has an accessible
  fallback name and formatted description. `TreeRow` supports focus,
  keyboard/assistive expansion and F2 rename, with disabled guards. Plain
  leaves share their inherited click operation across pointer, key and
  accessible activation without emitting twice.
  [RangeSlider evidence](WP-007-FIX-F04-range-slider.md) and
  [TreeRow evidence](WP-007-FIX-F04-tree-row.md).
- `ColorSwatches` now routes tap, keyboard and accessible press through one
  disabled-aware selection operation. Owner updates to `current` are silent.
  Cells retain authored swatch artwork while expanding to 48×48px targets in
  Touch; a synthetic edge touch is covered in the touch-target test. Focused
  Qt 6.11.1 QML tests and QML lint pass; Compact/Touch previews confirm target
  and artwork bounds. [Touch evidence](WP-007-FND-07-touch.md).
  [Focused evidence](WP-007-FIX-F04-swatches.md).
- `ColorButton` now exposes enabled focus state and an accessible press path
  through its template click. The tested Space and accessible paths open the
  swatch popover, disabled input is guarded, and owner colour sync is silent.
  [Focused evidence](WP-007-FIX-F04-color-button.md).
- `CheckBox`, `Switch` and `RadioButton` now expose live accessible checked
  and focus state, plus an accessible press path through their template
  `click()` operation. Mixed CheckBox state is described.
  [Focused evidence](WP-007-FIX-F04-toggles.md).
- `Segmented`, `PillSwitcher` and `TabStrip` now expose disabled model entries,
  selected PageTab state and guarded keyboard/assistive selection. TabStrip
  focus follows the selected tab.
  [Focused evidence](WP-007-FIX-F04-selection-composites.md).
- `DataTable` now names the table and realized rows for accessibility, shows
  selected row state, and shares a guarded user activation path across tap,
  Return/Enter and accessible press. Focusable column headers support
  keyboard/assistive sort and fixed-width resize. Arrow navigation and owner
  selection remain silent. [Focused evidence](WP-007-FIX-F04-data-table.md).
- `ColorField` now retains invalid hex drafts, exposes `inputInvalid`, and
  separates silent owner updates from changed user commits. Its swatch has
  keyboard and accessible picker actions, with read-only and disabled gates.
  [Focused evidence](WP-007-FND-10-colorfield.md).
- `ListRow` now supports Tab focus, a visible focus ring, Space and accessible
  press through its template click path. Owner `selected` updates stay silent.
  [Focused evidence](WP-007-FIX-F04-list-row.md).
- `ComboBox` now exposes selected popup rows and fits a long, text-scaled
  list above or below a control in a small host. Keyboard and pointer choice
  emit `activated`, while owner index assignments remain silent.
  [Focused evidence](WP-007-FND-09-combo-box.md).
- `ZoomControl`'s percentage readout is now keyboard focusable and exposes a
  guarded accessible reset action. Owner zoom synchronization stays silent;
  disabled user operations are ignored.
  [Focused evidence](WP-007-FIX-F04-zoom-control.md).
- `MenuItem` now exposes its mnemonic-clean name, checked state and disabled
  focusability to accessibility clients, with `Accessible.pressAction` routed
  through its enabled template click. `MenuBar` and menu entries expose
  explicit roles and names. Space, Return and Alt mnemonics emit `triggered`
  on accepted user actions; owner checked-state changes stay silent.
  [Focused evidence](WP-007-FIX-F04-menu-actions.md).
- `CommandPalette` command rows expose accessible names and selected state;
  disabled entries are skipped and cannot activate by keyboard, pointer or
  accessible press. `Dialog` names its dialog body and its disabled primary
  action rejects keyboard and accessible acceptance.
  [Focused evidence](WP-007-FIX-F04-dialog-palette.md).
- `DockTabStrip` now exposes PageTabList/PageTab semantics, title names,
  selected/focused state and a visible focus ring. Left/Right plus Home/End
  navigate and activate through `DockModel`; Space/Return/Enter, pointer and
  accessible press use the same guarded activation signal. Owner model updates
  remain silent. Render-settled drag tests also confirm inactive-tab reorder
  without activation and the begin/update/end tear-off lifecycle. The model
  recreates the strip after reorder, so tests reacquire it before asserting.
  [Focused evidence](WP-007-FIX-F04-dock-tabs.md) and
  [drag evidence](WP-007-dock-tab-drag.md).
- `DockDivider` seams now expose a focusable Splitter role. Arrow keys and
  accessible increase/decrease send seam-axis deltas through the existing
  `DockZone` callbacks into `DockModel`; disabled host ancestry blocks them.
  The themed focus ring leaves seam geometry unchanged.
  [Focused evidence](WP-007-FIX-F04-dock-dividers.md).
- `Viewport` now has a named Pane focus stop and focus ring. Pan keys observe
  hard bounds and may pass an edge key to an ancestor Scroll; zoom keys and
  accessible increase/decrease use the same guarded operation while owner
  `zoom` assignment stays silent. Document children keep their keyboard input.
  [Focused evidence](WP-007-FIX-F04-viewport.md).
- `StatusField` now has an explicit static mode and an actionable template
  Button mode. Actionable fields expose a text/tooltip name, keyboard and
  accessible activation, a visible themed focus ring and one guarded click
  signal. Touch status fields receive the existing 48px control target with
  their bar's top border accounted for; Compact geometry stays 22px.
  [Focused evidence](WP-007-FIX-F04-status-field.md).
- [D-023](../../../decisions.md#d-023--separate-owner-state-from-user-intent-notifications)
  and the [control interaction baseline](../../../control-interaction.md)
  now define owner assignment, accepted user signals, draft commit/cancel,
  applicability of mixed/default/invalid states, focus and assistive paths
  for later controls. The current-control audit is still in progress.

## Verification

On both Qt 6.11.1 and isolated Qt 6.8.3, integrated Debug builds, all
19 current CTest targets and `all_qmllint` exited 0 at integration source
commit `a500767`. The lint targets still report existing nonfatal warnings,
especially under Qt 6.8.3; their exit status is the gate recorded here.
The new QML tests cover
pointer/keyboard/assistive-action signal paths, disabled behavior and group
state synchronization. These signal tests do not establish native screen-reader
delivery or a platform value interface.

The focused SectionHeader preview was dumped and grabbed under compact and
comfortable density, and both PNGs were opened for pixel inspection. The
compact dump showed `SectionHeader#focusedSectionHeader 0,0 440x22` with a
visible focus-ring `Item 0,0 440x22` and four 2px themed edge rectangles;
the plain header remained `0,26 440x22` with its ring hidden. Comfortable
density showed the focused header `0,0 440x26` and the plain header
`0,31 440x26`. The focus ring was visible around the focused row in both
captures, with the title, count and disclosure icon readable. The ignored
captures are `build/section-header-focus-{compact,comfortable}.png` in the
integration worktree.

The gallery under `high-contrast-dark` and `high-contrast-light` was dumped
and grabbed at 1280×800, then both images were opened. The dump showed
`Box#galleryHeader 0,0 1280x40` and the two new preset choices as
`AbstractButton#segment_3 224,0 123x22` and `segment_4 347,0 120x22`.
The visible gallery text, selected choice, focus/accent colours and widget
borders were legible in both captures. The ignored PNGs are
`build/gallery-high-contrast-{dark,light}.png` in the integration worktree.

A separate `textScale = 2` gallery capture and dump first exposed a vertical
layout defect: `AbstractButton#segment_0 0,0 147x22` contained a
`Label 0,-4 131x30`. The new resolved minimum-size rule raised that segment
to `0,0 147x34` and placed the label at `0,2 131x30`; the gallery header
grew from 40 to 52px. Both before and after PNGs were opened. The after
capture shows the segment text fitting vertically; some fixed horizontal
chrome still crowds or elides at this scale, so FND-07 and WP-007 remain
open. Ignored captures are `build/gallery-text-scale-2.png` and
`build/gallery-text-scale-2-rows.png`.

The same 2× gallery then exposed a horizontal header defect: `SearchField#sectionFilter`
was `1285,8 0x36` inside a 1280px window. The gallery header now wraps its
controls and grows with its rows. The repaired dump shows
`Box#galleryHeader 0,0 1280x79`, `SearchField#sectionFilter 383,40 220x36`,
and `Scroll#galleryScroll 0,79 1280x721`; a focused QtTest checks that the
search field remains inside the header. At 3× text the header grows to 142px,
the search field remains `220x48`, and the content Scroll starts at y=142.
Both 2× and 3× software captures were opened. This repairs the gallery
workflow example, while broader control and hit-target qualification remains
open.

The integrated moving-anchor popover preview was dumped and grabbed after
the popup opened and the anchor moved in a 320×200 window. The dump showed
`Rectangle#movingAnchor 275,170 40x24` and, under `overlay:`,
`PopupItem#Popover 208,138 110x60`, within the host's 2px margin. The
`build/popover-moving-anchor.png` capture was opened and showed the whole
popover visible at the lower-right boundary.

## Still open

Audit the remaining custom composites for F04, including native assistive
bridge semantics where applicable. FND-07 still requires broad high-contrast,
text-scale and hit-target workflow coverage;
[D-016](../../../decisions.md#d-016--keep-toolkit-theme-ownership-process-wide)
records the conditional FND-08 decision to retain process-wide theming;
FND-09 requires monitor/work-area and dynamic ComboBox popup qualification;
[the monitor gap audit](WP-007-FND-09-monitor.md) identifies the per-screen
geometry bridge and native two-screen check still needed;
FND-10 requires uniform
editing state and notification contracts across remaining controls. No
WP-007 acceptance is claimed.
