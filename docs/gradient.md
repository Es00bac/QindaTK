<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Numerical RGB gradients

`gradient_evaluator.h` defines a stateless C++ evaluator for document gradient
stops. The supported policy is linear-light sRGB interpolation with
premultiplied alpha, padded edges, and an explicit target RGB ICC profile.
The evaluator borrows a caller-owned [ColorEngine](color.md) for conversions;
it has no live document, preview, cancellation or transaction state.

`GradientEditorModel` connects this evaluator to `GradientEditor`'s QML
preview and one-gesture document transactions. It does not make canvas or
GPU output color managed; a rendering host must explicitly consume typed
gradient definitions and select its composition/output profile.

## Stop identity and ordering

`GradientDefinition` contains a nonempty string ID, an ordered vector of
`GradientStop` records, `GradientInterpolation::LinearSRgb`, and
`GradientAlphaPolicy::Premultiplied`. Each stop has:

- A stable nonempty string `id`, unique within the gradient.
- A finite normalized `position` in `[0,1]`.
- A straight-alpha, tagged `ColorValue` and its resolved immutable
  `ColorProfile`. The value's profile and transfer IDs must match that profile.

One through 4,096 stops are accepted. Positions must be nondecreasing. Equal
positions are valid and their supplied array order is significant; evaluation
never sorts, merges, drops or reorders stops. IDs do not break position ties.
The host preserves IDs and the intended tie order when editing or persisting.

| Query location | Selected color or interval |
| --- | --- |
| Before the first stop | First array stop, including when its position is duplicated |
| Exactly at a stop position | Last array stop at that position |
| Between distinct positions | Last stop at the left position to first stop at the right position |
| After the last stop | Last array stop |

This gives a left approach toward the first member of a duplicate group and
an exact/right-side value from its last member. A group can therefore encode
a hard discontinuity. If every stop has the same position, the first color
fills the left side and the last fills the exact position and right side.
A single stop is constant. The query may be any finite double, including
outside `[0,1]`; edges pad, with no repeat, reflection or linear extrapolation.

## Color and alpha pipeline

`evaluate(gradient, position, targetProfile, engine)` requires a resolved target
profile. It selects at most two stops, converts their straight RGB values
through ColorEngine to `ColorProfile::linearSRgb()`, and interpolates there.
The interpolation encoding is fixed; the target profile never changes it.

For interval fraction `t`, converted linear-light colors `C0`, `C1`, and
straight alphas `a0`, `a1`, the mathematical operation is:

```text
a = (1-t)*a0 + t*a1
P = (1-t)*a0*C0 + t*a1*C1
C = P/a                         when a > 0
C = (0,0,0)                     when a = 0
```

The implementation uses equivalent normalized coverage weights to avoid
underflow in `P` when both alphas are tiny. Alpha and the weighted interpolation
use doubles. RGB conversion at each ColorEngine boundary uses Float32; output
precision is therefore reported as `ColorPrecision::Float32`.

The straight linear-light result is converted through ColorEngine into the
target profile, and returned as a straight-alpha ColorValue with that target's
profile/transfer IDs. ColorEngine copies alpha without applying transfer
functions. All selected stop colors, including exact stops and padded edges,
pass through this declared pipeline, so profile roundtrips may incur the
documented Float32/ICC precision loss. Exact position selection does not imply
bit-exact preservation of stored channel values.

**Zero-alpha rule:** a gradient result with zero alpha has canonical positive
zero RGB and alpha in the final target encoding, including exact stop and
padded-edge queries. Stored stop colors remain unchanged. This intentionally
differs from standalone ColorEngine conversion, which preserves hidden RGB
at zero alpha. Transparent hidden color cannot tint a partially opaque gradient
sample. Nonzero subnormal alpha is not classified as zero by an epsilon.

Both source-to-linear and linear-to-target transformations use relative
colorimetric intent with black-point compensation off. This API exposes no
alternate intent or proof operation. CMYK, Gray, Lab and other non-RGB input
profiles are unsupported by ColorProfile; proofing, BPC, perceptual/saturation/
absolute intents, encoded-sRGB interpolation, perceptual interpolation spaces,
and straight-alpha interpolation are outside this slice. Unknown interpolation
or alpha enum values return `Unsupported`.

## Errors, precision and ownership

`validate()` checks all stop metadata, including stops not selected by a
particular query: IDs/order/positions, resolved profiles, finite Float32-range
RGB, alpha in `[0,1]`, and matching color encoding tags. It returns the first
`OperationError`, with backend ID `gradient-cpu` and an indexed field path.
Successful metadata validation does not compile every possible ICC transform.
Only the selected source profiles and target transform are compiled during
evaluation; their ColorEngine failures retain the `lcms2` backend ID and gain
a `stops[index]` or `target` field-path prefix.

The result contains either a color or an error, plus precision, gamut status
and `outsideNominalRange`. Invalid values/query coordinates return
`InvalidArgument`; malformed IDs/order/tags return `Validation`; unresolved
profiles return `Unavailable`; unsupported policies return `Unsupported`.
ColorEngine transform failures propagate without substituting a plausible color.
Finite extended RGB is not clamped by this evaluator; ICC profiles may still
limit their own domains. Gamut status remains `Unknown`, and channel-range
reporting is not an assertion about gamut or clipping.

Definitions are supplied by const reference. Concurrent evaluation is safe for
immutable definitions using a valid shared ColorEngine, whose calls serialize,
or one engine per worker. Hosts own engine lifetime and result revision checks.
Each call validates in O(n), searches in O(log n), and makes at most three
ColorEngine conversions. The engine owns its bounded transform cache; this
evaluator keeps no stop cache, prepared samples or cancelable preview state.

## Reference fixtures and remaining integration

`tst_gradient_evaluator` compiles the production gradient/color sources directly
against Qt Core/Gui/Test and LCMS, without building QML. Fixtures verify:

- Opaque black/white midpoint: linear RGB `0.5`, encoded sRGB approximately
  `0.7353569831` (not encoded-channel `0.5`).
- Red at alpha `.25` to blue at alpha `.75`: midpoint alpha `.5`, straight
  linear RGB `(.25,0,.75)`.
- Transparent hidden colors, exact zero canonicalization, tiny nonzero alpha,
  mixed sRGB/linear-sRGB stops, duplicate positions and padded edges.
- Invalid tags, order, IDs, values and policies, plus repeated evaluation of
  original/candidate definitions with no mutation or cancellation requirement.

Linear-light fixtures use absolute `2e-6` RGB tolerance. Ideal sRGB references
use `1e-5` for ICC signed 16.16 transfer-coefficient quantization. Mixed-profile
tests use the fixed profiles and provenance in
[tests/fixtures/color](../tests/fixtures/color/README.md). Alpha checks are exact.

Host/QML adaptation, shared transactions/undo, color-space editing, separate
opacity-stop tracks, spatial gradient geometry, alternate spread/interpolation
policies, adaptive sampling and managed renderer integration remain open.

## QML editing model

`GradientEditorModel` is the host-facing owner of a typed
`GradientDefinition`. It exposes stable stop IDs/order as QML role maps and
uses `GradientEvaluator` for 64 target-sRGB preview samples. Gesture updates
are transient `EditTransaction` previews; pointer release commits one
`UndoService` entry and Escape/grab cancellation restores the committed
snapshot. Add, delete, and color change each commit as a single undo step.
Positions are stable-sorted after a move, preserving array order for ties.
`GradientDefinitionCodec` stores IDs, full ICC bytes, and double precision
channel/position values in a versioned JSON payload. The example's visible
ramp is painted from these evaluator samples, rather than browser gradient
interpolation.

The model currently edits tagged RGB ICC profiles through sRGB hex color
inputs and previews to sRGB. UI color conversion controls and application
managed display output remain host responsibilities.
