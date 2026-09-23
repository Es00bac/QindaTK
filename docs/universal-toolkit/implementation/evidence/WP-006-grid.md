<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-006 Grid responsive track slice

Implemented against `b6ab653` in isolated `work/wp006-grid`; this is partial
WP-006 evidence, not acceptance of the whole layout/scroll work package.
Decision D-020 and `docs/layout.md` define the accepted grammar and limitations.

## Implemented

- A sole `repeat(auto-fill, minmax(...))` or `repeat(auto-fit, minmax(...))`
  in columns/rows, resolved from current inner dimensions, gaps and track bounds.
- Auto-fit collapses only unoccupied repeated tracks after placement, including
  their gaps. Spans and zero-sized items still occupy tracks; indices and explicit
  counts stay stable. Ordinary implicit tracks beyond that region remain intact.
- Finite minmax ranges grow to their upper bounds before flexible sharing;
  spanning contributions cannot grow such tracks past those bounds.
- Plain `1fr` preserves its zero floor and `minmax(auto, 1fr)` preserves its
  implicit-content floor. Existing intrinsic keyword aliases stay explicit.
- Negative/non-finite lengths are rejected. Integer-repeat expansion and nesting
  have deterministic limits. Invalid specifications retain the valid prefix and
  report `Grid.error` as before.

## Verification

Configured and built with system Qt 6.11.1, GCC 15.3.0, Debug/Ninja in
`build/grid`. Live `portageq envvar MAKEOPTS` returned `-j24 -l24`; direct Ninja
used those exact configured options. No Portage configuration or environment
limits were changed.

`ctest --test-dir build/grid --output-on-failure`: **17/17 passed**, including
all C++/QML suites and the new `example_responsive-grid` smoke test.
Focused `tst_grid_tracks`/`tst_layout` tests cover fitting thresholds, resize,
visibility, auto-fit holes and spans, zero-sized occupancy, empty fit grids,
percentage minima, fixed maxima, indefinite axes, row flow, invalid fallback,
legacy intrinsic aliases, content floors, and existing layout regressions.
`tools/scripts/gen_catalog.py` regenerated the same 101-entry catalog.

`qtk-preview examples/responsive-grid/Main.qml --size 360x180 --dump --grab ...`
was run with offscreen/software rendering at compact and comfortable density.
Both PNGs were visually inspected: retained slot space in the first row and
wider occupied slots in the second, with the same intended gap. Both dumps show:

```text
Grid#fillGrid  20,30 320x40
  Rectangle#fillA  0,0 100x40
  Rectangle#fillB  110,0 100x40
Grid#fitGrid  20,110 320x40
  Rectangle#fitA  0,0 155x40
  Rectangle#fitB  165,0 155x40
```

Local artifacts: `build/grid/responsive-compact.png`,
`build/grid/responsive-comfortable.png` and matching `.dump` files.
The tracked QML example reproduces the artifacts; build outputs are ignored.

## Explicit limits

Automatic repeat is a sole expression containing one minmax track, with positive
fixed/percent minimum and fixed/percent/fr maximum. Surrounding tracks, multiple
pattern tracks, nested automatic repeats and responsive implicit tracks are
rejected. One repetition is used on indefinite axes; expansion is capped at 4096
and fitting breadth is floored at one pixel.

QQuickItem's one implicit size does not provide distinct min/max-content
measurements. Those keywords remain aliases, and spanning fr-track intrinsic
expansion remains unsupported. No named lines, fit-content or subgrid support
is claimed. GPU and Qt 6.8 qualification are left to integration; this slice
provides Qt 6.11 software evidence only. RTL integration, nested scroll ownership
and the remaining WP-006 contracts are separate work.

## Integration qualification

Sol integrated this slice with the D-019 Flex/Stack/Grid direction work.
The combined tree builds and passes all 17 CTest cases on Qt 6.8.3 and
6.11.1, including responsive parser/geometry tests and a runtime Grid
RTL direction test. `all_qmllint` passes on Qt 6.11.1. The compact and
comfortable responsive Grid captures above were inspected in the isolated
worktree; Grid RTL dumps and inspected integration captures are recorded in
`WP-006-diagnostics.md`. GPU rendering and nested scroll work are still
separate WP-006 gates.
