<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-006 layout diagnostics

This is a baseline diagnostic, not an implementation acceptance report. The
scene `examples/layout-diagnostics/Main.qml` exercises mixed font metrics,
wrapping, invalid Grid tracks, logical direction, per-side borders and nested
Scroll viewports in one stable `qtk-preview` dump. It is intentionally
independent of the layout algorithm changes WP-006 still requires.

## Reproduction

Configured a local build with:

```sh
cmake -S . -B build/wp006 -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DQINDATK_BUILD_TOOLS=ON
portageq envvar MAKEOPTS  # -j24 -l24
ninja -C build/wp006 -j24 -l24 qtk-preview
build/wp006/tools/preview/qtk-preview examples/layout-diagnostics/Main.qml \
  --dump --size 960x760
build/wp006/tools/preview/qtk-preview examples/layout-diagnostics/Main.qml \
  --grab build/wp006/layout-diagnostics.png --size 960x760
```

The generated PNG was inspected with the software renderer. It is a local
build artifact and is not committed. The scene is also picked up by the
examples directory smoke-test glob after CMake reconfiguration.

## Observations

| Fixture | Observed output | Gap / interpretation |
| --- | --- | --- |
| `baselineRow` | `baselineSmall 0,0 41x15`; `baselineLarge 49,0 91x32` | Both items share y=0; their font sizes differ (12 and 26 px), so this does not align text baselines. `Tk.Flex.Baseline` currently acts as Start, matching the documented limitation. |
| `wrappingColumn` | `wrappedText 0,0 220x30`; next item y=30 | This case wraps and Flex advances after the resulting 30 px height. Add width-change and mixed-font assertions at both densities before treating intrinsic convergence as generally qualified. |
| `invalidTrackGrid` | `QindaTK.Grid: bad minmax() arguments`; cell `0,0 220x96` | Invalid grammar is warned about but the resulting fallback is not an explicit, documented error layout contract. Decide reject/empty, preserve valid tracks, or fallback before expanding grammar. |
| `rtlRow` | `rtlStartA 0,0 48x44`; `rtlStartB 48,0 48x44` with `LayoutMirroring.enabled` | Child order and `Start` placement remain left-to-right. Custom layout coordinates do not follow the scene mirroring flag. Logical start/end needs a deliberate direction contract across Flex/Grid/Stack. |
| `roundedSideBorder` | Dump contains full-width top edge and a 3 px left edge as plain rectangles; PNG shows square accent corners over a rounded background | Current per-side edges are not clipped to the radius. Either explicitly retain square edge geometry as the supported conditional choice, or clip/construct rounded per-side strokes and verify software and GPU raster paths. |
| `outerScroll` / `innerScroll` | Outer viewport is 216x184 with 228 px content; inner viewport is 216x76 with 120 px content | Both viewports become scrollable and nest in the expected geometry. This scene does not establish wheel ownership, keyboard routing, focus reveal or scroll chaining; those need event-level tests and a declared overscroll policy. |

The observations align with current source contracts: `docs/layout.md`
documents Baseline as Start and per-side edge rectangles without radius;
`src/qml/Scroll.qml` configures `Flickable.StopAtBounds`. The nested fixture
does not prove event behavior by geometry alone.

## Algorithm decisions to resolve, in order

1. Define Flex baseline semantics and baseline source per child type, then
   align line metrics using font baselines rather than item top edges. Include
   mixed font, text/control and multiple-line cases.
2. Define logical direction as layout input (including Start/End, reverse
   direction and Grid placement), independent of `LayoutMirroring` paint
   transforms; cover LTR and RTL at narrow and normal widths.
3. Decide the public invalid-track contract before adding auto-repeat or
   intrinsic track grammar. A parse error should be inspectable and produce a
   stable, documented layout result rather than a warning-only surprise.
4. Keep the proven intrinsic wrapping case and add explicit multi-pass
   convergence tests when text width changes. Assert both implicit extent and
   child geometry, including nested Flex/Grid/Box/Scroll.
5. Choose whether per-side borders preserve square edge ends or honor radius.
   If radius is promised, implement clipping/geometry and compare GPU and
   software screenshots at compact and comfortable density.
6. Specify nested scroll ownership for wheel, keyboard and focus reveal,
   including edge handoff/overscroll. Add event-level nested inspector,
   grid/table and canvas fixtures; retain the current horizontal overflow
   default unless a separately reviewed compatibility decision changes it.

No layout algorithm or public behavior was changed by this diagnostic work.

## Subsequent integrated baseline repair

The observations above remain the pre-change record. Sol later implemented
row baseline alignment in `Flex`: it uses each text/control item's Qt
`baselineOffset`, seats a plain Item's zero-baseline fallback at its bottom
edge, and gives each wrapped line enough combined ascent/descent. A focused
QtTest with 12px and 26px text plus a plain Item passes on Qt 6.8.3 and
6.11.1, including a runtime baseline-offset change. The diagnostic preview
now dumps `Label#baselineSmall 0,14 41x15` and
`Label#baselineLarge 49,0 91x32` under `Flex#baselineRow 12,12 420x54`.
Compact and comfortable 960×760 software grabs were opened; the text
baselines visibly align. Column baseline still follows Start, and the
other WP-006 gaps in this record remain open. Ignored captures are
`build/layout-diagnostics-baseline-{compact,comfortable}.png` in the
integration worktree.

## Subsequent integrated Flex direction repair

The pre-change `rtlRow` observation above remains the diagnostic baseline.
`Tk.Flex.layoutDirection: Qt.RightToLeft` now mirrors each final child x
position within the physical left/right padded content box. RowReverse
still reverses the logical main axis; column cross-axis Start also follows
the logical direction. Qt's `LayoutMirroring.enabled` does not affect
custom coordinates on its own (D-019).

The asymmetric-padding QtTest asserts a 400px RTL row's first 50px item
at x=335 and its second 70px item at x=255, then exercises RowReverse,
runtime LTR switching, and RTL column Start. `tst_layout` and the full
16/16 CTest suite pass on Qt 6.8.3 and 6.11.1. The updated diagnostic
`qtk-preview --dump` reports `Flex#rtlRow 492,82 220x44`,
`Rectangle#rtlStartA 172,0 48x44` and
`Rectangle#rtlStartB 124,0 48x44`. Compact and comfortable software
captures were opened; the cyan first item is visibly at the right edge
of the row. Ignored captures are
`build/layout-diagnostics-rtl-{compact,comfortable}.png` in the
integration worktree. RTL Grid and Stack support remain separate WP-006
work, so this is a Flex repair rather than a package acceptance.

## Subsequent Stack direction and Box support decision

`Tk.Stack.layoutDirection` now uses the same explicit RTL contract as Flex
(D-019). A QtTest starts with asymmetric 10px left/30px right padding and
40px/60px inset children at x=30/295, switches to RTL and asserts x=310/25,
then switches back. The Stack diagnostic uses RTL with physical padding and
left/right attached insets. The integration dump reports
`Stack#rtlStack 492,146 220x44`, `Rectangle#rtlStackLeft 160,0 40x24`
and `Rectangle#rtlStackRight 10,0 40x24`. Compact and comfortable
960×760 software captures were opened; the cyan left-inset child appears
on the physical right. Ignored captures are
`build/layout-diagnostics-stack-{compact,comfortable}.png`.

FND-04 takes its conditional documentation path: `Tk.Box` per-side rules
remain square rectangular overlays even when its background has a radius.
`clipContent` uses a rectangular padding box. `docs/layout.md` and the source
contract now state this limit and recommend `radius: 0` with per-side rules;
the diagnostic label describes square edges. The compact and comfortable
software captures visibly show the square cyan top-left corner. This records
the supported behavior without claiming rounded per-side clipping.
