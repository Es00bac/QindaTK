<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FND-07 — Touch targets and enlarged gallery chrome

**Scope:** a touch-density metric floor and one reachable large-text
gallery workflow. This is partial FND-07 evidence.

The authored Touch multiplier resolved `size.control` to 36px and
`size.row`/`size.menuItem` to 33px. D-022 now raises interactive vertical
roles to 48 logical pixels after density/text-scale resolution, leaving
44px for the inner button of a 2px bordered joined control. The authored
Compact and Comfortable sizes return when density changes back. Short
`Tk.Segmented` choices also have a `size.controlSm` width floor, so their
touch targets remain at least 48×44 in the inspected case.

The gallery header flows its controls onto additional rows. At maximum
supported `textScale: 3` in Touch density, the preset switch needs 1281px
inside a 1280px window. `Scroll#presetScroll` now shows a 1256×63 viewport
with horizontal overflow; the segmented control remains 1281×48, and the
scrollbar has its own space below the text. A QML test scrolls to the end
and verifies the last preset is fully reachable. `SearchField#sectionFilter`
remains 220×48. The dump reports `Box#galleryHeader 0,0 1280x162` and
`Scroll#galleryScroll 0,162 1280x638`, so the content begins below the
expanded header.

Focused `tst_theme` and `tst_qml` targets pass on Qt 6.11.1. The Touch
QML test checks Button, IconButton, ListRow and short Segmented geometry;
it checks actual inner-segment height/width as well as role values. Normal
Touch and 3× Touch software captures were opened, as was a 3× Touch
OpenGL RHI capture. The software and GPU images show the header controls
and scrollbar inside the window. Ignored captures are
`build/gallery-touch-48-segments.png`,
`build/gallery-touch-3x-scroll-padded.png`, and
`build/gallery-touch-3x-gpu.png` in the integration worktree.

This metric floor covers controls that bind to the named size roles.
`Slider` and `RangeSlider`, including their small variants, now bind their
template input heights to `Theme.size.sliderTarget` and `sliderTargetSm`.
The roles resolve to 16/14px in Compact, 19/17px in Comfortable, and at
least 48px in Touch; track and handle artwork stays centred at its authored
density size. QML and theme tests exercise the three densities, 1×/3× text,
and a preset switch/reset. In the integrated Touch 3× preview,
`Slider#reviewSlider 0,194 520x48` and
`RangeSlider#reviewRange 0,260 520x48` have tracks centred at y=23 and
handles at y=17. The ignored `build/wp007-controls-touch-3x.png` was opened
and inspected. Menu geometry, other custom handlers, narrower application
workflows and physical devices remain to check.

Small `CheckBox`, `Switch` and `RadioButton` rows now use the derived
`Theme.size.toggleTargetSm` role (16px Compact, 19px Comfortable, 48px
Touch), preserving their small desktop geometry. Theme and QML tests check
the role and control heights across densities and 1×/3× text. Integrated
software previews show `CheckBox#reviewSmallCheck 0,27 520x16`,
`Switch#reviewSmallSwitch 0,55 520x16` and
`RadioButton#reviewSmallRadio 0,83 520x16` in Compact. In Touch at 3× text,
the same controls are each 520×48, with 18px indicators centred at y=15.
`build/wp007-small-toggles-compact.png` and
`build/wp007-small-toggles-touch-3x.png` were opened and inspected.

`Ruler` marker hit radius uses `Theme.size.controlSm / 2` in Touch (24px)
while the Canvas marker remains at `Theme.size.rulerMarker`. Overlapping hit
regions resolve to the nearest marker, and ties retain model order. A
synthetic touch starts a drag from near the edge of that radius; QML tests
also cover overlap selection and double-click insertion on an empty region.
The opened Touch preview retains the existing marker artwork geometry.
Focused evidence is in [the ruler marker interaction slice](WP-007-FIX-F04-ruler-markers.md).

`ColorSwatches` cells now size their hit area to the larger of the authored
swatch-plus-spacing size and `Theme.size.controlSm`. Compact retains the
existing 16px artwork and 20px cell; Touch gives each cell at least 48×48px
while keeping the swatch centred at its density-scaled authored size. The
QML test checks Compact geometry, Touch target geometry, and a synthetic
touch on the outer edge of a cell emits the expected single selection.
`ctest --test-dir build/wp007-swatches -R '^tst_qml$'` passes on Qt 6.11.1;
`all_qmllint` exits successfully with the repository's nonfatal warnings.
The opened Compact dump reports `Item#swatch_0 0,0 20x20` with a
`Rectangle 2,2 16x16`; Touch reports `Item#swatch_0 0,0 48x48` with a
`Rectangle 12,12 24x24`. Captures are
`build/wp007-swatches-compact.png` and `build/wp007-swatches-touch.png` in
the swatch worker worktree. Physical touch hardware and native assistive
traversal remain unqualified.

At 480×320, Touch density and 3× text, the gallery header previously grew
to 333px, leaving `Scroll#galleryScroll 0,333 480x0`; its 549px density
switch also extended beyond the right edge. The header now scrolls once its
height reaches half the window, and the density choices have their own
horizontal Scroll. The repaired dump reports `Box#galleryHeader 0,0 480x160`,
`Scroll#galleryHeaderScroll 0,0 480x159` with 346px content,
`Scroll#densityScroll 12,189 456x63` inside that content, and
`Scroll#galleryScroll 0,160 480x160`. The Touch/high-contrast-dark PNG was
opened: the header's vertical scrollbar and the gallery content are both
visible. A focused QML test scrolls to the final density choice and the
filter field, verifying both remain reachable. Ignored preview artifacts are
`build/wp007-gallery-narrow-review.{qml,png}`.

A combined 480×320 Touch, 3× text and high-contrast-dark preview was also
dumped and opened. It reports `Box#galleryHeader 0,0 480x160`,
`Scroll#galleryHeaderScroll 0,0 480x159`, `SearchField#sectionFilter
12,255 220x48` inside the scrollable header content, and
`Scroll#galleryScroll 0,160 480x160`. The ignored artifacts are
`build/wp007-hc-touch-gallery.{qml,png,dump}` in the integration worktree.
This checks reachability of the combined example state, not every control's
contrast or native accessibility behavior.

The integration gallery was also captured at 480×320 in Touch density under
both high-contrast presets at 1× and 3× text. All four PNGs were opened.
At 3×, both preset captures show the header and gallery as separate visible
regions; the dump reports `Box#galleryHeader 0,0 480x160`,
`Scroll#galleryHeaderScroll 0,0 480x159`, `Scroll#presetScroll 12,83
456x63`, and `Scroll#galleryScroll 0,160 480x160`. The filter field lies at
y=255 inside the header's scrollable content and remains reachable by the
existing focused QML test. Section title/count text elides at this width;
this is an example workflow check rather than contrast certification for
every control. Ignored captures are `/tmp/qindatk-gallery-hc-{dark,light}-touch.png`
and `/tmp/qindatk-gallery-touch3-hc-{dark,light}.png`.

At integration source commit `1f30b69`, Qt 6.11.1 and 6.8.3 each passed the
full 19-target CTest suite and `all_qmllint`.

## Divider seams

`DockDivider` now reads the density-scaled `Theme.size.seamTarget` (16px in
Compact, 19px in Comfortable, and at least 48px in Touch) and derives the
`HoverHandler` and `DragHandler` margin from the seam's cross-axis thickness.
The painted Rectangle and its Flex basis remain bound to `Theme.size.seam`,
so hit expansion does not alter dock
geometry. `tests/qml/tst_touch_targets.qml` presses 20px outside the painted
vertical and horizontal seams, starts synthetic touch drags, and verifies
that each handler reports a nonzero resize delta. Theme tests check the target
role across Compact, Comfortable, Touch, a preset switch and reset.
After the horizontal cross-axis correction, the focused `tst_qml` and
`tst_theme` targets pass on Qt 6.11.1 and Qt 6.8.3. The
docking example dump shows Compact lane dividers at `4px` (`left_0 230,0
4x536`; `center_0 0,220 758x4`) and Touch dividers at the existing density
scaled `6px` (`left_0 230,0 6x504`; `center_0 0,220 752x6`). The `--grab`
images `build/wp007-divider-touch/docking-compact.png` and
`build/wp007-divider-touch/docking-touch.png` were opened; no target pixels
are painted beyond the seam.

`Splitter` now uses an expanded `MouseArea#splitterHitTarget` child and the
same child as its handle containment mask. The child gives Qt's pointer
delivery agent real bounds outside the painted seam; the template's native
child event filter then finds the handle through that mask. Native SplitView
continues to own resizing, pane constraints, and save/restore. The previous
mask-only experiment failed because it did not provide those delivery bounds;
its standalone failing repro is replaced by `tests/qml/tst_splitter_targets.qml`.

The tests cover both orientations and both sides of the seam at Compact,
Comfortable, and Touch densities, including a touch press just inside the
expanded boundary, no size jump on press, exact 40px movement, release, and
saved-state restoration. Mouse tests cover native minimum constraints and
ordinary button input in an adjacent pane outside the target.

The Touch dump shows `Rectangle#splitterHandle 160,0 6x340` with
`MouseArea#splitterHitTarget -21,0 48x340`, and the vertical equivalent
`Rectangle#splitterHandle 0,160 340x6` with
`MouseArea#splitterHitTarget 0,-21 340x48`. The first pane remains 160px;
the second starts at 166px. The opened `build/splitter-touch.png` in the
splitter worker worktree confirms that only the 6px seam is painted.
Compact dumps retain 4px seams with 16px targets, and Comfortable retains
5px seams with 19px targets; both density captures were also opened. Qt 6.11.1
and Qt 6.8.3 both pass the complete QML suite and `all_qmllint` (existing
nonfatal warnings).
Physical touch hardware and native assistive traversal remain unqualified.

## Docking example: small-window overflow recipe

The opened 480×320 Touch, 3× text, high-contrast-dark baseline
`build/wp007-docking-stress.png` showed left/right lanes only 9px high,
`Flex#dockLane_center_0 0,0 0x220`, and `Item#dockCanvas 0,229 0x0`.
The example now
uses a two-axis `Tk.Scroll#dockingViewport` around an Item with a 1280×800
implicit workspace minimum (D-024). This preserves the example's default
arrangement while exposing it through scrolling; it is not a generic
DockHost adaptive-layout guarantee.

Opened worker captures `build/narrow.png`, `build/canvas-narrow.png`, and
`build/normal.png` show the initial narrow viewport, the scrolled canvas,
and the normal Compact presentation. Narrow dump:

```
Scroll#dockingViewport  0,0 480x320
Flex#dockLane_left_0  0,0 230x489
Flex#dockLane_center_0  0,0 752x220
Item#dockCanvas  0,229 752x260
Flex#dockLane_right_0  6,0 280x489
```

Normal Compact remains `Item#dockCanvas 0,226 758x310` within the
1280×800 viewport. `tst_dock_overflow.qml` checks useful canvas geometry,
canvas and floating-panel reachability, keyboard Home/End scrolling, a
30px dock-seam drag and grouped-tab click after scrolling, then resizes
back to the normal viewport and verifies exact serialized arrangement
preservation. Arbitrary floating positions beyond the chosen workspace and
per-panel content reflow remain separate application responsibilities.

The full focused `tst_qml` target passes on Qt 6.11.1 and Qt 6.8.3 in the
worker's isolated `build/narrow` and `build/qt68` directories. Both include
the scrolled seam/tab interaction regression. `gen_catalog.py` was run;
no toolkit API or catalog entry changed.
After integration, the focused `DockOverflow` scene also passed on both Qt
versions. Resizing the focused Scroll to an unscrollable viewport exposed a
Qt focus warning; `Tk.Scroll` now retains its tab stop while focused and
releases it after focus leaves. The focused `ScrollNested` scene passed on
both versions, and the integrated `DockOverflow` run no longer emits that
warning.
