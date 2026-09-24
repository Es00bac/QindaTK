<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-043 — Numerical gradient slice

**State: bounded backend slice; WP-043 not accepted.**

`GradientEvaluator` evaluates host-owned stable-ID stops without document,
gesture or transaction state. It validates nondecreasing normalized positions
and tagged RGB ICC values, preserves duplicate array order, and converts
selected stops to linear sRGB before premultiplied-alpha interpolation. It
returns straight-alpha values tagged with an explicit target RGB ICC profile.
Relative colorimetric intent/BPC off and padded edges are fixed policies.

At an exact duplicate position the last stop wins; its left approach targets
the first duplicate. Zero-alpha gradient output has canonical zero channels,
while stored stop values and standalone ColorEngine hidden-RGB behavior remain
unchanged. Scaled coverage weights retain colors at tiny nonzero alpha.
The [gradient contract](../../../gradient.md) documents these semantics and
the precise distinction between metadata validation and transform execution.

**Focused verification:** Qt 6.11.1 and Qt 6.8.3 each passed **1/1** targeted
CTest and **10/10** QtTest checks against LCMS 2.19. The direct-source target
built only the gradient/color numerical sources and tests, with the actual
configured `-j24 -l24` limits; no full QML rebuild was run.
Analytical fixtures cover the light-space midpoint, unequal alpha, transparent
colors, mixed-profile input, exact duplicate positions/edges, malformed input
and repeated original/candidate evaluation. Tolerances are `2e-6` for linear
RGB and `1e-5` for ideal sRGB references after ICC coefficient quantization.
The opaque black/white midpoint is linear RGB `.5` or approximately encoded
sRGB `.735357`; unequal-alpha red/blue gives straight linear RGB `(.25,0,.75)`
at alpha `.5`. Canonical positive zero and nonzero subnormal alpha both passed.
Catalog regeneration remained **265 types**, with no generated diff.

After integration at `341a762`, the focused CTest target passed **1/1** on
both supported Qt versions, and the shared `qindatk` library built with the
new source on both. The rest of the repository suites were not rerun for this
isolated numerical addition.

Reproduction after configuring for each Qt version:

```sh
cmake --build build/qt611 --target tst_gradient_evaluator -- -j24 -l24
ctest --test-dir build/qt611 -R '^tst_gradient_evaluator$' --output-on-failure
```

The Qt 6.8 run used `build/qt68`. Use the current values from
`portageq envvar MAKEOPTS` if the machine's build configuration changes.

This slice does not wire the GradientEditor preview to the evaluator, mutate
canonical stop data, implement WP-033 transactions, add separate opacity
tracks, or provide geometry, proofing, alternate intents/interpolation/spread
policies, GPU display management, persistence or undo.
