<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Typed numerical curves

`curve_evaluator.h` provides a stateless C++ definition, constraint validator
and evaluator for host-owned curves. It shares normalized geometry with
`Tk.CurveEditor`; it does not mutate a document, repair a proposal, own a
transaction, or choose editor sampling density.

## Values and units

`CurveDefinition` contains a nonempty curve ID, typed x/y axis domains, ordered
knots, and independent before/after extrapolation policies. An axis declares
`UnitDimension`, an exact unit ID, and finite increasing minimum/maximum values.
The difference between those bounds must also be finite. Normalized geometry
maps affinely onto these domains; no logarithmic, frequency, timebase or unit
conversion is implicit. Defaults are scalar `normalized` axes covering 0–1.

`CurveKnot` has a stable nonempty string `id`, normalized double `x` and `y`,
optional absolute normalized `inHandle`/`outHandle` points, and `interpolation`
for its outgoing segment. Handles are positions, never offsets or slopes.
The equivalent host/QML shape is:

```js
{ id: "shadow", x: 0.25, y: 0.15,
  inHandle: { x: 0.18, y: 0.10 },
  outHandle: { x: 0.38, y: 0.21 } }
```

The host maps its interpolation representation to `CurveInterpolation`; the
presentation control does not select or evaluate that policy. Moving a knot
does not implicitly move, align or mirror its handles. A host applies any
desired tangent constraints to a candidate definition and validates it.

`evaluate(curve, UnitQuantity)` requires the query's dimension and unit ID to
match the x domain exactly, then normalizes its value. `evaluateNormalized`
accepts an already normalized double query, including finite values outside
0–1. Both return `CurveEvaluation`: either a `CurveSample` or `OperationError`.
A sample includes the original normalized query x, normalized y, and a typed
output `UnitQuantity` in the y domain. Repeat never replaces the recorded query
x with the wrapped coordinate. No output value is returned on failure.

These are double domains, not exact sample/tick domains. A timeline adapter
must retain its exact integer/rational positions and define the projection
before using this evaluator; it must not treat normalized doubles as stored
timeline positions or silently convert seconds to ticks.

## Validation and interpolation

`validate()` returns the first structured `Validation` error with a field path
and backend ID `curve-cpu`; success has no error. Evaluation validates before
doing numerical work. Inputs are never sorted, clamped, deduplicated or changed.

- One through 65,536 knots, unique nonempty IDs, finite x/y in `[0,1]`.
- Knot x coordinates strictly increase by at least `1e-12`, measured on the
  supplied doubles. This is the minimum supported normalized segment width.
- Every supplied handle is finite and in `[0,1]` on both axes. Its x must lie
  between its knot and that side's neighboring knot. An unused first incoming
  or last outgoing handle may extend toward 0 or 1 respectively.
- Each cubic segment requires `left.x <= out.x <= in.x <= right.x` after
  resolving missing handles. This sufficient monotonicity constraint gives a
  unique x inverse; it intentionally rejects some more general Bézier shapes.
- Unknown enum values and malformed typed domains fail validation. A single
  knot cannot use repeat, and linear edge extrapolation requires a finite
  endpoint slope.

The left knot selects a segment's interpolation. The last knot's interpolation
is retained and validated but has no outgoing segment.

| Interpolation | Value inside a segment |
| --- | --- |
| `Step` | Left knot y until the next knot; at an exact knot x, that knot's own y wins |
| `Linear` | Linear interpolation between endpoint y values |
| `CubicBezier` | Cubic parametric x/y through the endpoints and two handles, evaluated at the unique parameter matching query x |

Each missing cubic handle independently defaults to the chord's one-third or
two-thirds point. Thus two missing handles produce a straight line. Stored
handles are preserved even on step/linear segments, but do not affect their
evaluation. Y need not be monotone. In-domain cubic values remain in `[0,1]`
because all four control y values are normalized.

Cubic x inversion uses 64 bounded bisection iterations in local segment
coordinates, ending earlier if the double interval can no longer shrink.
De Casteljau interpolation evaluates both coordinates. This handles zero
dx/dt at endpoints without a Newton division by zero. Exact knot queries
bypass solving and return the stored y exactly.

## Edge policies

Edges are the first/last knot x values, which need not be 0/1. The selected
policy applies only strictly outside that closed interval.

| Policy | Meaning |
| --- | --- |
| `Hold` | Keep the nearest endpoint's y |
| `Linear` | Extend a line from the endpoint with that segment's limiting slope |
| `Repeat` | Wrap by the first-to-last x period into `[first.x,last.x)` |

For linear extrapolation, step slopes are zero and linear slopes are the
endpoint secant. A cubic slope comes from the first noncoincident control
point encountered from that endpoint. Coincident first handles therefore
use the next control; a noncoincident vertical control gives no finite slope
and is rejected when that edge uses `Linear`. Vertical tangents remain valid
for in-domain evaluation and hold/repeat. A single-knot curve is constant
under both hold and linear extrapolation.

The actual last knot preserves its y. A later repeat seam wraps to the first
knot, so unequal first/last y values intentionally cause a discontinuity.
Negative queries wrap in the same direction using a positive remainder.
The input double's precision limits phase accuracy at very large query
magnitudes; repeat does not provide exact rational time cycling.

Linear extrapolation may produce y outside `[0,1]` and outside the typed output
range. There is no output clamp. Nonfinite queries, mismatched units and
unrepresentable extrapolated/mapped results return `InvalidArgument` errors.

## Ownership and reference fixtures

The implementation has no mutable state or QObject dependency. Concurrent
calls are safe with immutable definitions. Every evaluation validates in O(n)
time and searches the segment in O(log n), followed by at most 64 cubic solve
steps. Hosts should bound sample counts; there is no prepared-curve cache or
realtime allocation guarantee in this slice.

`tst_curve_evaluator` compiles the same production numerical sources directly
against Qt Core/Test, without rebuilding unrelated QML. Its analytical
fixtures include identity lines, typed seconds-to-dB mapping, step boundary
ownership, `y=3x²−2x³`, `y=cuberoot(x)` with a vertical tangent, and a shifted
segment domain. Cubic comparisons use absolute `1e-12` tolerance on those
fixtures. Additional cases cover negative/positive repeat seams, limiting
endpoint slopes, constant single knots, malformed constraints, unit mismatch
and output overflow. The fixture formulas are independent of the evaluator.

Automatic/mirrored tangents, monotone-y transfer-curve constraints, multi-knot
editing rules, QML codecs, adaptive display sampling, exact time projection,
transactions, undo and persistence remain host or later WP-043 work.
