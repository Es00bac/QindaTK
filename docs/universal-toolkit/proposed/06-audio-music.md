<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio, timelines and music

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

[Waveform, TimeRuler and Filmstrip](../current/08-media-canvas.md) draw supplied
information; [Meter/Graph](../current/07-data-telemetry.md) are generic telemetry.
The following add editing and musical semantics. EditTimeline, TimeMap and curve
editing should also serve video/motion applications. Audio computation, device
callbacks and plugin processing remain outside QML and outside control painting.

<a id="aud-01"></a>
## AUD-01 — TimeMap

**Priority:** P0. **Purpose:** Exact mapping between samples, frames, beats and display positions.

**Contract sketch:** Describe sample rate, rational frame rate, tempo/signature segments and origin; convert with explicit rounding and format timecode separately.

**Dependencies and boundary:** Authoritative session/engine time model; large integer/rational positions must retain precision through the UI facade.

**Completion check:** Roundtrip long-duration sample/frame/beat positions across tempo changes; validate supported timecode/drop-frame boundary fixtures.

<a id="aud-02"></a>
## AUD-02 — TransportModel

**Priority:** P1. **Purpose:** GUI facade for playback/record state synchronized to an engine.

**Contract sketch:** Expose position, play/record state, loop/punch ranges, rate and capabilities; request play/stop/seek/record with acknowledged errors.

**Dependencies and boundary:** Realtime engine owns clock/state; queued snapshots update the GUI at a bounded cadence.

**Completion check:** Seek/play/record state remains correct under slow UI frames and device errors; UI never becomes the timing authority.

<a id="aud-03"></a>
## AUD-03 — TransportBar

**Priority:** P1. **Purpose:** Reusable transport controls and time readout.

**Contract sketch:** Bind TransportModel, TimeMap, command IDs and display mode; expose loop/record/locate controls with keyboard equivalents.

**Dependencies and boundary:** TransportModel and existing Button/NumberField styles; audio feedback is engine-owned.

**Completion check:** Buttons/shortcuts reflect actual acknowledged state, and unsupported recording/seek operations explain why they are unavailable.

<a id="aud-04"></a>
## AUD-04 — EditTimeline

**Priority:** P1. **Purpose:** Virtualized tracks/clips surface for audio, video and animation regions.

**Contract sketch:** Track/clip IDs, visible time/track ranges, time transform, selection and edit-policy adapter; issue move/trim/split/ripple requests.

**Dependencies and boundary:** TimeMap, EditTransaction, model spatial/time index and AssetProvider; domain chooses legal overlap/edit rules.

**Completion check:** Move/trim many clips with snapping, auto-scroll, cancel and undo; offscreen clips do not each create live QML objects.

<a id="aud-05"></a>
## AUD-05 — TrackHeader

**Priority:** P1. **Purpose:** Synchronized controls for one timeline track.

**Contract sketch:** Track ID/name/type, height, mute/solo/arm/lock/visibility and routing summaries; property/command requests target the model.

**Dependencies and boundary:** Track model and PropertyBinding; synchronized vertical scroll with EditTimeline.

**Completion check:** Resizing/reordering tracks keeps headers, clips and accessibility identity aligned through virtualization.

<a id="aud-06"></a>
## AUD-06 — ClipItem

**Priority:** P1. **Purpose:** Reusable visible clip presentation and edit affordances.

**Contract sketch:** Clip ID, exact interval/source offset, selection/lock state, label, peaks/frames and permitted handles; emit semantic gesture intents.

**Dependencies and boundary:** EditTimeline and media providers; actual trimming, fades and time stretch belong to the domain.

**Completion check:** Move/resize under zoom while hit regions remain usable and visuals reflect a single committed model state.

<a id="aud-07"></a>
## AUD-07 — TimeSelection

**Priority:** P1. **Purpose:** Loop, punch and edit-range overlay with precise endpoints.

**Contract sketch:** Start/end/anchor and time domain, allowed constraints and drag handles; keyboard/numeric entry share transactions.

**Dependencies and boundary:** TimeMap and timeline/transport selection policy.

**Completion check:** Endpoint crossing, empty ranges and offscreen auto-scroll have documented behavior; cancellation restores the original range.

<a id="aud-08"></a>
## AUD-08 — MarkerLane

**Priority:** P1. **Purpose:** Editable cues, regions and navigation markers above tracks.

**Contract sketch:** Marker IDs, exact positions/ranges, labels/colors/types and locked state; add/move/delete/navigation requests.

**Dependencies and boundary:** Session marker model and TimeMap; colors may be document data.

**Completion check:** Move markers across rate/tempo views and preserve their declared time anchoring after save/reopen.

<a id="aud-09"></a>
## AUD-09 — TempoLane

**Priority:** P2. **Purpose:** Edit musical tempo, meter and project-global events.

**Contract sketch:** Segment/event IDs, musical positions, tempo curves, time/key signatures and anchoring choices; preview downstream timing changes.

**Dependencies and boundary:** Musical TimeMap/engine owns conversion and scheduling.

**Completion check:** Insert a tempo change and verify sample-anchored versus beat-anchored clips follow the chosen policy and undo exactly.

<a id="aud-10"></a>
## AUD-10 — Knob

**Priority:** P1. **Purpose:** Continuous parameter dial with fine control, reset and typed entry.

**Contract sketch:** Parameter descriptor, canonical/normalized value, taper, default, bipolar/mixed/automation state; use gesture transactions.

**Dependencies and boundary:** PropertyBinding; parameter engine defines logarithmic or custom mapping.

**Completion check:** Mouse/keyboard/fine modifiers reach identical values; reset and canceled drags do not introduce extra undo operations.

<a id="aud-11"></a>
## AUD-11 — Fader

**Priority:** P1. **Purpose:** Vertical or horizontal slider for calibrated parameters.

**Contract sketch:** Parameter value/taper, tick labels, unity/default mark, bounds and automation state; touch/release gestures are explicit.

**Dependencies and boundary:** PropertyBinding and audio parameter mapper; can reuse template slider behavior with new visuals/contracts.

**Completion check:** A known gain map places unity correctly and preserves negative-infinity/silence semantics without arbitrary numeric substitution.

<a id="aud-12"></a>
## AUD-12 — AudioMeter

**Priority:** P1. **Purpose:** Audio-specific multichannel level display.

**Contract sketch:** Snapshot includes peak/RMS or named metric, unit, channels, clip latch and timestamps; display scale, hold and decay policy are explicit.

**Dependencies and boundary:** Analysis/meter engine supplies measurements; UI snapshots never imply processing every audio sample.

**Completion check:** Reference signals verify displayed units, peak hold/decay and clip reset at different UI refresh rates.

<a id="aud-13"></a>
## AUD-13 — ChannelStrip

**Priority:** P2. **Purpose:** Mixer channel composition of meter, fader, pan, sends and inserts.

**Contract sketch:** Channel ID, parameter descriptors, mute/solo/arm state, routing and plugin slots; all commands target stable IDs.

**Dependencies and boundary:** Audio session graph, AudioMeter, Fader, PanningControl and PluginRack.

**Completion check:** Operate a strip while tracks reorder; automation and hardware feedback update the same parameter model.

<a id="aud-14"></a>
## AUD-14 — MixerView

**Priority:** P2. **Purpose:** Virtualized collection of channel strips with grouping and banks.

**Contract sketch:** Channel order/visibility, groups, selected channel, bank/scroll state and compact/detail modes.

**Dependencies and boundary:** Channel model and ChannelStrip; no audio routing is stored in delegates.

**Completion check:** Large sessions create only needed strip visuals and preserve parameter gestures while banks/visibility change.

<a id="aud-15"></a>
## AUD-15 — PanningControl

**Priority:** P2. **Purpose:** Mono/stereo and spatial panning UI with explicit layouts.

**Contract sketch:** Source position/balance/width, speaker layout, constraints and parameter IDs; pad/numeric control share transactions.

**Dependencies and boundary:** Panning engine defines law, channel order and coordinate semantics.

**Completion check:** Known positions map to correct canonical coordinates and accessible numeric editing matches pointer movement.

<a id="aud-16"></a>
## AUD-16 — RoutingMatrix

**Priority:** P2. **Purpose:** Inspect and edit track/bus/device connections.

**Contract sketch:** Stable input/output port IDs, channel layouts, connection state, availability and proposed routing changes.

**Dependencies and boundary:** Audio graph validates cycles, compatibility and device availability; edits apply at safe engine boundaries.

**Completion check:** Rejected feedback/incompatible routes are explained; device removal does not silently connect another port by index.

<a id="aud-17"></a>
## AUD-17 — PluginRack

**Priority:** P2. **Purpose:** Ordered instrument/effect chain with bypass and parameter access.

**Contract sketch:** Plugin instance IDs, slots, bypass/state/latency/error and editor-open requests; move/replace commands preserve parameters.

**Dependencies and boundary:** PluginHostAdapter and session graph; processing and plugin windows have independent lifecycles.

**Completion check:** Reorder/bypass/save/reload a chain and preserve parameter IDs; plugin failure leaves the host session recoverable.

<a id="aud-18"></a>
## AUD-18 — PluginBrowser

**Priority:** P2. **Purpose:** Search installed effects/instruments/presets by capability.

**Contract sketch:** Plugin descriptors include vendor/category/layout/availability and scan status; instantiate and preset-preview requests are jobs.

**Dependencies and boundary:** Plugin registry/scanner and AssetProvider; loading an entry is explicit.

**Completion check:** Unavailable or incompatible plugins remain identifiable in saved sessions and cannot crash the browser during scanning.

<a id="aud-19"></a>
## AUD-19 — AutomationLane

**Priority:** P1. **Purpose:** Editable parameter values over time.

**Contract sketch:** Curve/parameter ID, exact time coordinates, value mapping, interpolation and read/write/touch/latch state; uses CurveEditor interaction.

**Dependencies and boundary:** Automation engine records/evaluates data and owns mode semantics; UI only requests changes.

**Completion check:** Edit breakpoints and record a gesture through a mock engine; playback values, undo and write-mode transitions agree.

<a id="aud-20"></a>
## AUD-20 — WaveformEditor

**Priority:** P2. **Purpose:** Multichannel sample/region editing with scalable waveform display.

**Contract sketch:** Media/source IDs, exact sample range, channel layout, peak-resolution provider and selection/fade/gain handles.

**Dependencies and boundary:** Audio editing engine, peak pyramid/cache and TimeMap; existing Waveform can be a simple display path.

**Completion check:** Zoom from full file to samples, trim/fade/undo, and verify boundaries against known audio without dropping peaks during reduction.

<a id="aud-21"></a>
## AUD-21 — SpectrogramView

**Priority:** P2. **Purpose:** Time/frequency display and region selection for spectral work.

**Contract sketch:** Tile provider, time/frequency axes, window/normalization metadata, channel selection and paint/lasso masks.

**Dependencies and boundary:** FFT/analysis and spectral-processing engine; time/frequency edits are domain commands.

**Completion check:** Known tones land at expected bins; zoom preserves selection coordinates and processing previews are cancellable.

<a id="aud-22"></a>
## AUD-22 — PianoRoll

**Priority:** P2. **Purpose:** MIDI/note editor with pitch rows and musical timing.

**Contract sketch:** Note IDs, start/duration/pitch/velocity/expression, scale display and selection; insert/move/resize/quantize requests.

**Dependencies and boundary:** MIDI/note model, TimeMap and instrument audition adapter.

**Completion check:** Edit overlapping notes with keyboard/pointer, undo and roundtrip exact timing; audition never leaves a stuck note.

<a id="aud-23"></a>
## AUD-23 — MidiControllerLane

**Priority:** P2. **Purpose:** Edit velocity, controllers, pitch bend and per-note expression.

**Contract sketch:** Typed event/controller descriptors, resolution/range and note associations; draw/scale/delete requests use stable IDs.

**Dependencies and boundary:** MIDI/event engine defines encoding and mapping to notes/channels.

**Completion check:** Edit supported high-resolution/per-note events without losing precision or detaching them from their notes.

<a id="aud-24"></a>
## AUD-24 — StepSequencer

**Priority:** P2. **Purpose:** Pattern grid with per-step parameters and probability.

**Contract sketch:** Pattern/row/step IDs, length, resolution, enabled/gate/velocity/probability and parameter lanes; edits remain discrete semantic commands.

**Dependencies and boundary:** Sequencer engine owns playback clock, randomization seed policy and instrument mapping.

**Completion check:** Polymetric patterns and edited steps play via a mock clock as modeled; undo restores the complete pattern.

<a id="aud-25"></a>
## AUD-25 — ClipLauncher

**Priority:** P2. **Purpose:** Scene/clip grid for performance triggering.

**Contract sketch:** Slot IDs, clip state, launch quantization, queued/playing/record state and stop actions; expose engine acknowledgments.

**Dependencies and boundary:** Performance/session engine owns launch timing and recording.

**Completion check:** Rapid launches under a delayed UI remain quantized by the engine; queued versus active state is visibly distinct.

<a id="aud-26"></a>
## AUD-26 — TakeCompView

**Priority:** P2. **Purpose:** Choose and combine regions from alternate recordings.

**Contract sketch:** Take/lane IDs, source intervals, active comp segments and crossfade boundaries; audition and comp edits reference original media.

**Dependencies and boundary:** Nondestructive audio/session model and preview engine.

**Completion check:** Build a comp from takes, undo boundaries and save/reopen while source recordings remain unchanged.

<a id="aud-27"></a>
## AUD-27 — ScoreView

**Priority:** P2. **Purpose:** Editable musical notation with score and part navigation.

**Contract sketch:** Semantic note/rest/voice/measure IDs, selection, symbols, page layout and playback cursor; note-entry commands target the score model.

**Dependencies and boundary:** Notation/engraving/playback engine and font support; piano-roll timing is not sufficient notation data.

**Completion check:** Edit voices/ties/tuplets in known fixtures, generate parts and verify semantic roundtrip and accessible reading order.

<a id="aud-28"></a>
## AUD-28 — PitchEditor

**Priority:** P3. **Purpose:** Pitch/time correction view for analyzed audio notes.

**Contract sketch:** Analysis segment IDs, source times, pitch curves, formant controls and preview status; edits retain source references.

**Dependencies and boundary:** Pitch analysis/resynthesis engine with versioned results and cancellation.

**Completion check:** Analyze then edit a note; stale analysis cannot replace newer edits and rendered results correspond to saved parameters.

<a id="aud-29"></a>
## AUD-29 — LoudnessHistory

**Priority:** P2. **Purpose:** Loudness, peak and spatial measurement history with provenance.

**Contract sketch:** Metric IDs/units, channel layout, integration interval, reset history and target profile; export a measurement report.

**Dependencies and boundary:** Calibrated analysis backend owns standards-specific calculations and reset semantics.

**Completion check:** Reference fixtures verify reported units/integration windows; no generic Meter value is mislabeled as calibrated loudness.
