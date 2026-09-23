<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-006 / FND-01 — intrinsic wrapping convergence

## Scope

Runtime coverage for width-dependent intrinsic sizing through Flex, Grid,
Box, and Scroll. This slice checked live width changes, mixed text sizes, and
nested containers. It did not change a layout algorithm.

## Findings

- A row `Flex` with wrapped Text children updates its implicit height after
  narrowing, and an enclosing `Box` reports the changed implicit height.
- Changing a Text font size after narrowing propagates through the same
  `Flex` → `Box` chain.
- A `Grid` using a flexible column updates its auto row height when wrapped
  text receives a narrower cell.
- A `Scroll` viewport with hidden horizontal overflow reflows a nested
  column and grows its scroll content after narrowing.
- `Flex` and `Grid` connect child `implicitWidthChanged` and
  `implicitHeightChanged` to layout invalidation. `Box` and `Scroll` derive
  their content host's implicit size from child implicit sizes. These
  mechanisms support the cases above without extra iterations or a polish
  loop.

The supported boundary is that a measured child must publish the relevant
implicit size. `Box` and `Scroll` intentionally do not treat arbitrary child
geometry as intrinsic content size; geometry-only children need a wrapper
with an explicit implicit size when a parent must measure them. This boundary
is documented in `docs/layout.md`.

## Verification

Added `tests/cpp/tst_layout_intrinsic.cpp` and registered `tst_layout_intrinsic`
as a separate target so the existing `tst_layout.cpp` remains below the
500-line repository limit.

```text
cmake --build build/wp006-intrinsic --target tst_layout_intrinsic
ctest --test-dir build/wp006-intrinsic -R '^tst_layout_intrinsic$' --output-on-failure
1/1 Test #6: tst_layout_intrinsic ............. Passed
100% tests passed, 0 tests failed
```

No direct build parallelism flags or `MAKEOPTS` override were used.
