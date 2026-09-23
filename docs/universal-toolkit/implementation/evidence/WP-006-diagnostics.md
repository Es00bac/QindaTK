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
