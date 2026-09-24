<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Exact time and tempo map

`TimeMap` gives audio, video and timeline controls one exact conversion boundary.
It stores signed 64-bit sample positions, frame indices and musical ticks.
Every QML position and result crosses the API as a decimal string, so a QML
`Number` cannot round positions above 2⁵³. `setFrameRate(numerator,
denominator)` normalizes a positive rational rate. Sample rate defaults to
48,000 Hz, frame rate to 30,000/1,001, and ticks per quarter to 960.

| Operation | Input/output | Policy |
| --- | --- | --- |
| `frameToSample`, `sampleToFrame` | exact decimal string → `{ok,value}` or `{ok:false,error}` | Rational rate and sample rate; `originSample` is frame zero. |
| `tickToSample`, `sampleToTick` | exact decimal string → result map | Piecewise constant tempo, using integer microseconds per quarter. |
| `formatTimecode`, `parseTimecode` | frame decimal string ↔ `HH:MM:SS:FF` or `HH:MM:SS;FF` | Drop-frame only at 30,000/1,001 and 60,000/1,001; skipped labels are rejected. Negative labels are signed magnitude. |
| `compareExact` | two decimal strings → `-1`, `0` or `1` as a decimal string | No floating conversion. |
| `sampleToPixel`, `pixelToSample` | decimal origin/position and a finite positive scale | Subtract the visible sample origin before floating display math; reject spans beyond 2⁵³ samples or pixels. |

The rounding enum is `TowardZero`, `Floor`, `Ceil` or `NearestAway` (the
default). Rounding occurs once at the requested boundary. Overflow or an
invalid decimal returns `{ok:false,error}` rather than a truncated position.
`originSample` may shift a complete timeline without changing tempo segments.

`tempoSegments` is a nonempty list of maps. Each segment has a `tick` **decimal
string**, `microsecondsPerQuarter` integer or decimal string, and time
signature `numerator`/`denominator`. Segments start at tick zero, then use
strictly ascending nonnegative ticks. The first tempo extends backward for
negative ticks. A tempo change is instantaneous at its tick; there is no ramp
or discontinuity in absolute time. The meter is metadata for notation and bar
display; it does not alter the tick-to-time calculation. The service accepts
at most 4,096 segments, tempo from 1 to 60,000,000 microseconds per quarter,
sample rate 1 to 768,000, PPQ 1 to 1,000,000, and frame-rate parts 1 to
1,000,000. Invalid settings leave the prior map unchanged and emit `rejected`.

The implementation uses bounded, portable 128-bit intermediate arithmetic
from 32-bit limbs; it adds no Boost or compiler-extension dependency. The
library and tests compile on the available Linux Qt 6.8.3 and 6.11.1 runtimes.
Windows/MSVC compilation and platform timecode application workflows remain
qualification work; the arithmetic itself uses standard C++ and Qt types.

When a `TempoMapModel` drives a TimeMap it is the only writer of
`tempoSegments`: it publishes its committed events (and gesture previews) and
restores them on cancel. Edit tempo through that model, not by assigning
segments; see [tempo-automation.md](tempo-automation.md).

