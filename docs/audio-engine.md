<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio engine and session

WP-060 reference engine: real device I/O, a session that records, imports,
plays, edits, saves, reopens and renders audio, and measured callback
behaviour. Decision [D-088](decisions-domains.md#d-088--a-queue-driven-realtime-engine-with-built-in-silent-devices-and-optional-alsa)
records the backend choice. Later audio packages (WP-061…068) build on the
seams below instead of adding their own clocks, graphs or device code.

```
 GUI thread                          worker (QtConcurrent)        device thread
 AudioSession ─ compile ─▶ AudioRenderGraph (immutable) ─ offer ─▶ AudioEngineProcessor
   TimelineModel · AutomationModel/EngineSchedule                   process(in, out, frames)
   ResourceResolver · WAV codec                                     │ commands ◀─ SPSC ring (64)
 AudioEngine (TransportAdapter) ◀─ events/acks ─ SPSC ring (256) ◀──┘ events ─▶
   TransportModel · DeviceSettingsPanel host      open/stop/close   capture ─▶ SPSC ring ─▶ take writer thread ─▶ WAV
   outputMeter · outputLoudness ◀─ meter blocks ─ SPSC ring (1024) ◀─ AudioMeterTap(output)
```

## Threads and queues

| Thread | Owns | Never does |
| --- | --- | --- |
| GUI | `AudioSession`, `AudioEngine`, models, graph compilation, telemetry (15 ms poll) | block on a device |
| Worker | `AudioDeviceBackend::open`, stream stop/join/close during reconfiguration | touch QML or models |
| Device | `AudioEngineProcessor::process()` once per period | allocate, free, lock, block, do I/O, call Qt |
| Take writer | drains the capture ring into a float32 WAV | touch the device |

`AudioSpscRing<T>` (`audio_ring.h`) is the only cross-thread channel into or
out of the device thread: bounded, allocated once, lock-free, rejecting when
full. The output meter tap is one more ring: 10 ms measurement blocks,
drained by the GUI on every poll ([audio-measurement.md](audio-measurement.md)). Commands (`Play`, `Stop`, `Seek`, `Record`, `Loop`) carry a serial and
are applied at the start of the next period; each produces one acknowledgment
event with the transport sample at which it took effect. The engine keeps at
most 64 commands in flight, so the 256-entry event ring cannot overflow. The
capture ring holds 4 s; a full ring drops whole periods and reports them as
`droppedFrames` of the take. `tst_audio_engine` interposes the allocator and
asserts **zero** heap calls on the device thread while it renders clips,
per-sample automation and loop wraps.

## Graph lifetime and scheduling

`AudioRenderGraph::compile()` turns the committed timeline, decoded media and
an `EngineScheduleSnapshot` into an immutable program at one sample rate. The
GUI keeps every offered graph alive; the processor adopts the newest pointer
at a period boundary and publishes its generation; older graphs are released
only after a newer generation is adopted (`AudioEngine::retireGraphs`). A
graph at another rate is held, never played. `render(start, frames, …)` is the
only mixing code: the device and `AudioSession::renderToFile` execute the same
function, so device output and offline renders are identical samples.

- Clip edges: exact tick → sample conversion (`TimelineTime::rescale`,
  nearest-away), so abutting clips never gap or overlap.
- Automation: `track:<track id>/gain` binds to the track (unit `dB` →
  10^(v/20), any other unit is a linear factor), evaluated **per sample** at
  that sample's exact tick through `AutomationCurve::valueAt`; cubic segments
  are flattened to 32 linear pieces at compile time (declared tolerance).
  A held control or a writing pass plays its live value, mode `off` or a lane
  without points plays the static value (`defaultValue`), otherwise the curve
  plays (D-100). A dB gain's range minimum is exact silence (D-101).
  `AudioParameterBinding` turns Knob and Fader gestures into these parameter
  transactions ([audio-measurement.md](audio-measurement.md)).
- Loops wrap inside a period at the exact loop end; seeks, play/stop and
  record start/stop take effect at the next period boundary.
- Overlapping clips on a track sum. Channels, buses, sends, pan laws,
  mute/solo (track flags from `TimelineTrack`, solo in place), output ports
  and per-channel meters are the mixer's ([audio-mixer.md](audio-mixer.md),
  WP-062); with the default mix every track feeds the master and the master
  feeds `main`, so a mono file still reaches the device bit-exactly.
- Media are converted to the engine rate off the device thread (Blackman
  windowed sinc, 32 zero crossings; 1 kHz 48→44.1 kHz error < 2·10⁻³).

## Devices and reconfiguration

Device IDs are `<backend>:<name>` and never indices. `builtin:null` (discards
output, captures silence) and `builtin:loopback` (captures the previous
period's output: round trip one period) are always built, always silent and
paced by the monotonic clock, so their deadlines are real wall-clock
deadlines. With `QINDATK_ENABLE_ALSA` (default ON, used only when
`libasound` links) every ALSA PCM hint is listed as `alsa:<pcm>`: rates and
buffer sizes are offered candidates, and `hw_params` negotiation at open
decides; null/file PCMs have no converter clock and are paced like the
built-in devices. Test fixtures pass ALSA configuration text to
`createAlsaAudioBackend()` (for example a `file` PCM with an `infile`) to
exercise real alsa-lib I/O silently.

`AudioEngine.applyConfiguration(requestId, configuration)` always answers with
`configurationApplied(requestId, state, activeConfiguration, message)`:

| Situation | Answer | Engine state |
| --- | --- | --- |
| Opened | `accepted`, active = actual period size | `running` |
| New device refused, previous one reopened | `rejected` with the device's reason | `running` (previous) |
| Refused with nothing running (first open, still unplugged) | `rejected` | unchanged (`stopped`/`lost`) |
| Refused and the previous device cannot reopen | `error` | `error` |
| While recording, or during another apply | `rejected` immediately | unchanged |
| During a stop/loss teardown | queued, answered after the teardown | — |

`stop()` closes the device and clears `activeConfiguration`.

Hot-unplug (ALSA `-ENODEV`, or `setBuiltinDeviceConnected(id, false)`) ends
an open take at the last captured sample, reports `lost` with the reason,
keeps the lost device listed by its ID (`available: false`) and keeps it as
the active configuration. Nothing falls back to another device; recovery is
an explicit apply of the same (or another) stable ID, which
`DeviceSettingsPanel` offers as **Reconnect** when the host passes
`engineState`/`engineMessage`.

A sample-rate change stops the stream, converts the transport position and
loop range so they keep their time, sets the session `TimeMap` rate (the
session clock used by timecode readouts and schedule snapshots), recompiles
the graph at the new rate and restarts.

## Transport, recording and latency

`AudioEngine.transport` is a `TransportModel` whose adapter is the engine:
acknowledged states only, positions as exact decimal sample strings. Play and
record require a running device; record also requires input channels and an
armed audio track (`AudioSession` reports it). Recording is linear: loop wraps
keep writing the same take (per-pass takes are WP-065). Punch recording is
reported unavailable.

Latency is nominal from the driver configuration: ALSA output = buffer
frames, input = one period; loopback output = one period. A finished take is
placed at `recordedSample − (input + output latency)`, so what was recorded
lines up with what was heard; samples compensated before 0 become a source
offset. `recordingFinished` carries `path, startSample, recordedSample,
frames, sampleRate, channels, droppedFrames, latencyFrames, deviceLost,
error`.

## Session persistence

`AudioSession` owns `timeline` (clips, edits, undo), `tempo`, `automation`,
`schedule`, `resources`, `timeMap`, `mixer` and `edits` (source sample edits,
saved as the `edits` part). `importMedia(file, trackId,
startTicks)` decodes a RIFF/WAVE file (PCM 8/16/24/32, float 32/64,
extensible), registers a resource (`media:<uuid>`) and inserts one undoable
clip. `save(file)` writes `qindatk.audio-session/1` JSON atomically
(`QSaveFile`): tempo, automation and timeline content plus media entries with
URIs relative to the session file and their SHA-256. `open(file)` restores
them; unavailable media stay referenced by ID in `missingMedia`, play and
render as silence, and `relinkMedia(resourceId, file)` validates a decodable
replacement before `ResourceResolver.relink`. `renderToFile(file, startTicks,
endTicks, sampleRate, channels)` writes float32 WAV synchronously on the
calling thread.

## Measurements

`AudioEngine.statistics`: `callbacks`, `xruns`, `lateCallbacks` (finished after
the period deadline), `maxCallbackUs`, `meanCallbackUs`, `budgetUs`,
`maxWakeLatenessUs` (paced devices), `realtimePriority` (SCHED_FIFO granted),
`hardwareClock`, rates and latencies. The engine requests SCHED_FIFO 70 and
continues at normal priority when the system refuses it.

`AudioEngine.outputMeter` (`AudioMeterModel`: sample peak, RMS, hold, clip
per channel) and `AudioEngine.outputLoudness` (`AudioLoudnessModel`: ITU-R
BS.1770-4 momentary, short-term, integrated, history, report) measure what
the device receives. Both are reconfigured by the stream's rate and output
layout; see [audio-measurement.md](audio-measurement.md).

## Not covered here

Hardware/OS matrices (PipeWire native, JACK, WASAPI, CoreAudio, real
interfaces, driver hot-plug notifications) are WP-088 platform qualification;
formats beyond WAV belong to format adapters; resampling at playback time,
clip crossfades and per-clip gain are not modelled; plugins belong to
WP-066. Source edits (trim, fades, gain, spectral processing) are
`AudioSession.edits` ([audio-editing.md](audio-editing.md), WP-063): the
graph plays the edited buffers. Sends, buses and per-track and bus meters are in
[audio-mixer.md](audio-mixer.md).
