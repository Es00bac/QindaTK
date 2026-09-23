<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Range slider interaction slice

**Scope:** `RangeSlider` keyboard and assistive operation, focus indication,
and `Slider` accessible name/value description. This is one control-family
repair, not the whole F04 or FND-07 audit.

`RangeSlider` is one tab stop. Left/Right changes the upper endpoint;
Shift+Left/Right changes the lower endpoint. Home/End moves the selected
endpoint to a bound or collapses the band. The assistive increase/decrease
actions change the upper endpoint. A focused endpoint has a themed ring.
Programmatic endpoint assignments remain silent; user operations emit
`rangeModified`. A zero `stepSize` falls back to one hundredth of the range,
including fractional ranges. Disabled input is ignored. The accessible name
uses `tooltip` or “Range slider” and its description reports both endpoints.
`Slider` has a “Value” fallback name and a formatted value description.

Focused QML regressions cover keyboard, signal counts, accessible text,
disabled behavior and the fractional fallback step. Both integrated Qt
6.11.1 and isolated Qt 6.8.3 passed 19/19 CTest targets and `all_qmllint` at
commit `7b3c898`. An offscreen software preview at 480×160 showed
`RangeSlider#rangeSlider 0,27 480x16`, with
`rangeSliderSecondFocus -2,-2 14x14` visible around the second handle;
`build/wp007-range-review.png` was opened and inspected.

The horizontal 16px visual row is retained. Touch hit-target qualification
and native screen-reader behavior remain open.
