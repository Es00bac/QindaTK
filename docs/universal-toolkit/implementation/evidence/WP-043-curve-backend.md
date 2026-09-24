<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-043 — Typed numerical curve slice

**State: bounded backend slice; WP-043 not accepted.**

`CurveEvaluator` validates immutable host definitions and evaluates typed
double domains independently of document transactions. Stable string knot IDs,
normalized absolute coordinates and optional absolute `inHandle`/`outHandle`
points match the CurveEditor presentation contract. The host maps its editor
records to the C++ types; this slice supplies no QML codec or live model.

Supported interpolation is step, linear and cubic Bézier with ordered x
controls and bounded monotone inversion. Edges independently hold, extend a
finite limiting slope, or repeat over the knot interval. Exact knot values,
step discontinuities, repeat seams, missing handles and coincident endpoint
controls have explicit [documented semantics](../../../curve.md).

**Focused verification:** Qt 6.11.1 and Qt 6.8.3 each passed **1/1** targeted
CTest and **9/9** QtTest checks in `tst_curve_evaluator`. The target compiled
the production evaluator and validator against Qt Core/Test. Both builds used
the actual configured `-j24 -l24` limits; no full QML build was run. Analytical fixtures cover straight lines,
typed seconds/dB mapping, smoothstep, cube-root x inversion, step endpoint
ownership, hold/linear/repeat edges, degenerate tangent limits and invalid
constraints/queries. Cubic absolute tolerance is `1e-12` for those fixtures.
Catalog regeneration remained **265 types**, with no generated diff.

Reproduction after configuring a build directory for the chosen Qt:

```sh
cmake --build build/qt611 --target tst_curve_evaluator -- -j24 -l24
ctest --test-dir build/qt611 -R '^tst_curve_evaluator$' --output-on-failure
```

Use the current `portageq envvar MAKEOPTS` values if this machine's build
configuration changes. The matching Qt 6.8 run used `build/qt68`.
Luna confirmed that the tangent UI uses these same absolute optional handles,
with `in`/`out` side IDs, and leaves interval/monotonic constraints to the host.

This work does not connect the evaluator to WP-033 transactions, mutate host
knots, implement tangent gestures, constrain monotone y, provide adaptive
display sampling, project exact timeline ticks, or supply undo/persistence.
Those boundaries remain open for host integration and later WP-043 slices.
