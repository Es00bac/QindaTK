<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio measurement and calibrated controls

WP-061 builds on the engine in [audio-engine.md](audio-engine.md). The engine
measures its own output on the device thread, and GUI models turn those
measurements into meter and loudness snapshots.
`AudioParameterBinding` turns Knob and Fader gestures into engine parameter
transactions. Decisions: [D-100](decisions.md#d-100--a-held-control-is-heard-in-every-mode-static-values-are-the-parameter-default)
(parameter transactions), [D-101](decisions.md#d-101--a-db-gains-range-minimum-is-silence)
(silence), [D-102](decisions.md#d-102--meters-measure-10-ms-blocks-on-the-audio-thread-ballistics-run-on-audio-time)
(metering) and [D-103](decisions.md#d-103--loudness-follows-itu-r-bs1770-4-with-a-bounded-histogram-gate)
(loudness).

```
 device thread                              GUI thread (AudioEngine::poll, 15 ms)
 AudioEngineProcessor::process              drainMeters: pop every block
   render(graph) → output                     ├─▶ AudioMeterModel.consume → publish → levels
   AudioMeterTap.process(output) ──ring──▶    └─▶ AudioLoudnessModel.consume → publish → M/S/I, history
   (10 ms blocks, 1024-block SPSC ring)
```

## Quantities and windows

| Quantity | Definition | Window | Unit |
| --- | --- | --- | --- |
| Sample peak | max \|x\| per channel | one level block: `round(rate / 100)` frames (exactly 10 ms at every rate divisible by 100 Hz) | dBFS, 1.0 = 0 dBFS |
| RMS | sqrt(mean x²) per channel | sliding `rmsWindowMilliseconds` (default 300 ms, rounded to whole blocks) | dBFS |
| Peak hold | the highest peak, held `holdMilliseconds` (default 1500), then falling at `fallDbPerSecond` (24) | audio time | dBFS |
| Display peak | instant attack, falling at `fallDbPerSecond` | audio time | dBFS |
| Max peak | highest peak since `resetClip()` | since reset | dBFS |
| Momentary loudness | BS.1770-4 K-weighted mean square, channel weighted | last 400 ms (4 sub-blocks of 100 ms), one reading per 100 ms | LUFS |
| Short-term loudness | the same | last 3 s (30 sub-blocks) | LUFS |
| Integrated loudness | gated mean of 400 ms blocks (75 % overlap): absolute gate −70 LUFS, relative gate 10 LU below the mean of the blocks above it | since reset | LUFS |

Sample peak is not intersample true peak, and RMS is not loudness. dBFS
readings are floored at −120 with a separate `silent` flag, so zero is never
confused with a very small signal. Loudness is `NaN` until measurable (under
400 ms or 3 s, or no block above the absolute gate) and `−inf` for digital
silence. Channel weights follow BS.1770-4 Table 3: L, R and C 1.0, Ls and Rs
1.41, LFE excluded; Mono is one channel at 1.0. The engine's layouts are Mono
and Stereo. The metric is named `ITU-R BS.1770-4` and checked with sine
reference signals and the EBU Tech 3341 gating cases. It is not a certified
meter.

## The realtime tap

`AudioMeterTap` runs in `AudioEngineProcessor::process()` right after the
graph rendered a period, so it measures exactly what the device receives,
silence included. Per sample and channel it updates the peak, the sum of
squares and the sum of squares after the two K-weighting biquads. The shelf
and the RLB high-pass are derived from the standard's analog prototypes with
the bilinear transform, so every rate gets its own filter; at 48 kHz they
equal BS.1770-4 Tables 1 and 2 to 1e-12. A closed block (`AudioMeterBlock`:
end frame, frames, per-channel peak/squares/weighted sums and non-finite
counts) is pushed into a 1024-block SPSC ring. The tap never allocates,
locks or waits: a full ring drops the block and counts it in
`droppedBlocks`. NaN and infinite samples are measured as silence and
counted, so they cannot poison the filter state. Filter state below 1e-20 is
flushed at block ends, which keeps silence out of denormals.

`AudioEngine` drains the ring on every 15 ms poll and after a stream stops.
Every block goes to both models, followed by one `publish()` per model.
`outputMeter` and `outputLoudness` are CONSTANT properties. A new stream with
a different rate, channel count, layout or block length reconfigures them,
which resets them. A stop leaves the meter with no channels, and the loudness
result stays readable.

## AudioMeterModel

`configure(rate, channels, layout, blockFrames)` sets the stream. The engine
calls it; a C++ host with its own tap can too. `consume(block)` advances every
channel by the block's own audio duration. `publish(dropped)` emits
`levelsChanged` once. The ballistics depend only on the blocks, so a GUI that
publishes every 10 ms and one that publishes every 70 ms show identical
snapshots. A block from another layout (drained late after a reconfiguration)
is ignored. `levels` rows are `{channel, name, peakDbfs, rmsDbfs,
displayPeakDbfs, heldPeakDbfs, maxPeakDbfs, clipped, silent,
invalidSamples}`. Names come from the layout (`M`; `L`, `R`; otherwise
`1…n`). `integrationWindow` states the block, RMS window, hold and fall in
words. `measuredSeconds` is the snapshot timestamp in stream time. A peak at or
above 0 dBFS latches `clipped` until `resetClip()`, which also clears the
maxima and the invalid-sample counts.

`submitLevels(peaks, rms, elapsedMilliseconds)` and
`submitInterleaved(samples, channels, elapsedMilliseconds)` remain the
fixture and offline path. They run the same hold and fall rules, timed by the
caller's elapsed time. `submitInterleaved` accepts up to one million values
and is not a realtime API.

## AudioLoudnessModel

It consumes the same blocks: ten make a 100 ms sub-block, and each closed
sub-block updates momentary, short-term, the maxima and the history. Integrated
loudness keeps every gating block above −70 LUFS in a 0.01 LU histogram with
its exact energy sum. The relative threshold uses the exact energy mean, so
memory stays bounded for any duration. Only blocks within 0.005 LU of the
threshold are classified at bin resolution. `history` holds the last
`historySeconds` (default 60) of `historyMetric` readings (`short-term` by
default, or `momentary`) as `{time, value}`, where time is seconds since the
reset. `historyMetricId` (`bs1770.short-term` / `bs1770.momentary`) and
`historyWindow` label them. `reset()` clears the measurement, maxima and
history; the layout stays. `report()` returns
`qindatk.loudness-report/1`: algorithm, rate, layout, channel weights, block
and gating parameters, the five metrics with ids, units and windows, the
history, and a qualification note. `submitInterleaved(samples, channels,
rate, layout)` runs a tap for QML fixtures and offline use.

## Parameters: AudioParameterBinding

An engine parameter is an `AutomationModel` parameter of an `AudioSession`
(for example `track:<id>/gain` from `ensureGainParameter`). The engine reads
it by ID through `EngineScheduleSnapshot`, and `value`/`silent` are what the
engine plays at the transport position. The binding reports the descriptor
(`unit`, `minimum`, `maximum`, `step`, `taper`, `silenceFloor`,
`staticValue`, `resetValue`) and owns an `AudioParameterMap`. A dB gain gets
`gainDb` with unity at `unityPosition` (0.75); a frequency with a positive
minimum gets `logarithmic`; anything else gets `linear`. It also reports
`automationMode`, `automationState` (`touch, writing`, `read, held`, …),
`touched` and `writing`.

`begin(value, silent)` → `preview(value, silent)`* → `commit(value, silent)` or
`cancel()` is one transaction:

| While held | On commit | Result |
| --- | --- | --- |
| The engine plays the gesture value in every mode and with the transport stopped (D-100) | a writing pass (touch, latch or write during playback) holds it; touch ends the pass on release, latch and write when the transport stops | `recorded`, one history step per pass |
| | mode `off` or a lane without points: the static value (`defaultValue`) changes in one history step labelled `Set <name>`, applied before the release so the device never hears the old value in between | `static` |
| | read mode over a curve: nothing is stored; the curve plays again | `live` |
| | the value did not change | `unchanged`, nothing stored |

`cancel()` stores nothing. It drops a pass the gesture opened. A pass that
was already running (latch or write) gets the start value back from that
moment, since what it wrote was already heard. A second `begin` joins the
open transaction, as the double-click reset does. Destroying a binding
mid-gesture releases it. Automation undo and redo change what the engine
plays, and `save`/`open` keep static values. A dB gain's range minimum is
silence: the graph multiplies by exactly 0 there, `silent` is true, and
`format()` prints `−∞ dB` (D-101). The lowest finite level lies above the
floor.

`AudioParameterMap` converts canonical values to normalized travel. Its
descriptor names `linear`, `logarithmic` (positive bounds) or `gainDb`
(negative minimum, positive maximum and a unity position inside the travel).
For gain, position zero alone means silence, the lowest finite position
begins at 0.001, and the unity position is exactly 0 dB. It returns
`{value, silent}`, so controls never substitute a finite number for negative
infinity. An invalid descriptor leaves the previous mapping in place.

## Controls

`Knob` and `Fader` take `binding: Tk.AudioParameterBinding`. They then read
the value, silence, range, reset value, unit, taper and automation state
from it, send every gesture through it, and never assign `value` themselves.
Without a binding, the host handles the same `gestureStarted`, `valuePreview`,
`gestureCommitted` and `gestureCanceled` signals. The following are one
gesture each: a pointer drag (relative, never jumping on press), a held arrow
key (auto-repeat until release), a wheel burst (ended by
`Tk.Theme.motion.gestureIdle` of quiet, or by any other input), a numeric
entry, Home/End and a reset. A double-click resets inside the open pointer
gesture, so it is one transaction, and Escape cancels. The Fader's scale marks
and unity mark sit where the mapper puts those values under the thumb
centre. With a silence floor, the mark at the range minimum reads −∞.

`AudioMeter` takes `meter: Tk.AudioMeterModel` (or provider `levels` rows).
It shows channel names, the RMS bar, the display peak (warning colour at or
above `warningDbfs`, default −6), the held-peak line and numeric readout, a
dBFS scale row, and the clip latch with a **Reset clip** request. Its
accessible description lists peak, RMS, hold, max and clip for every channel.
`LoudnessHistory` takes `loudness: Tk.AudioLoudnessModel` (or provider
`samples`). It shows the history, a labelled level scale, the M/S/I readout,
**Reset history**, and **Export report**, which also emits
`reportExported(report)`.

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_audio_meter_tap` | K-weighting against BS.1770-4 Tables 1–2; exact tone peaks and RMS in 10 ms blocks; silence and NaN/inf; hold (1490 ms held, 1500 ms falls) and fall on audio time with identical snapshots at 10 ms and 70 ms publish cadences; clip latch and reset; layout changes; a full ring drops without blocking |
| `tst_audio_loudness` | −23/−33 dBFS stereo tones within 0.001 LU of the analytic level (EBU Tech 3341 cases 1–2), mono −3.01 LU, 44.1 kHz; EBU cases 3 and 5 within ±0.1 LU; absolute gate; silence; history, reset, report and layout reset |
| `tst_audio_parameter_binding` | descriptor and mapper; static, live, recorded, unchanged and canceled transactions checked through the rendered graph; undo/redo; exact silence; save/reopen; read/off/touch/latch; destroyed binding |
| `tst_audio_engine_meter` | on the running `builtin:null` device: meter peak/RMS and momentary loudness of a known tone; a held, canceled, committed, undone and silenced fader heard in the meters; layout change to Mono |
| `tst_audio_engine` | its device-thread allocation probe now also covers the tap (every stereo period is measured) |
| `tst_audio_controls.qml` | bound Fader: keyboard step, Escape-canceled drag and double-click reset as engine transactions; bound Knob wheel burst as one transaction; AudioMeter and LoudnessHistory over real models |

Not covered here: performance measurement, screen-reader runs, other platforms
and output-fidelity comparison (WP-088…WP-094); true-peak, loudness range
(LRA) and certified loudness compliance. Per-track and bus meters and channel
strips on the mixer graph are in [audio-mixer.md](audio-mixer.md) (WP-062).
