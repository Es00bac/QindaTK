<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 05 — Audio and music workflows

**IN PROGRESS — WP-060 has a device settings control and WP-061 has measurement and parameter slices.**

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-060"></a>
## WP-060 — Integrate a real audio engine and device settings

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-015](phase-01-services.md#wp-015), [WP-019](phase-01-services.md#wp-019), [WP-047](phase-03-authoring.md#wp-047), [WP-048](phase-03-authoring.md#wp-048), [WP-049](phase-03-authoring.md#wp-049)

**Proposal scope:** [PLT-19 DeviceSettingsPanel](../proposed/08-platform-integrations.md#plt-19)

**Change area:** Audio/session adapter, device I/O, offline renderer and DeviceSettingsPanel.

**Additional obligations:** [ENG-08](supporting-scope.md#eng-08).

1. Define realtime versus GUI/worker boundaries, bounded control/telemetry queues, graph/configuration lifetime and sample-accurate scheduling.
2. Choose/build a functional engine using record/play/seek/loop/offline-render fixtures, including device loss and sample-rate change.
3. Implement device enumeration/settings, acknowledged reconfiguration and session clock integration; keep allocations, GUI calls and unbounded locks out of callbacks.
4. Connect real transport/timeline/automation behavior and native audio-session persistence with missing-media relink.
5. Measure callback deadlines/xruns under UI load and verify actual recorded/rendered samples and latency reporting; a fake clock cannot close this package.

**Accept when:** A real session records/imports, plays, edits, saves/reopens and renders audio, with measured device and callback behavior.

**Delegate:** Luna owns device/settings UI and offline fixtures; Sol owns engine, threading and scheduling.

**Astra trigger:** Astra is justified for a demonstrated realtime race, graph-swap lifetime or exact scheduling defect after profiling and minimization.

<a id="wp-061"></a>
## WP-061 — Implement calibrated audio controls and meters

**Implementation lead:** Sol. **Status:** active. **Depends on:** [WP-022](phase-02-controls-data.md#wp-022), [WP-030](phase-02-controls-data.md#wp-030), [WP-060](phase-05-audio.md#wp-060)

**Proposal scope:** [AUD-10 Knob](../proposed/06-audio-music.md#aud-10), [AUD-11 Fader](../proposed/06-audio-music.md#aud-11), [AUD-12 AudioMeter](../proposed/06-audio-music.md#aud-12), [AUD-29 LoudnessHistory](../proposed/06-audio-music.md#aud-29)

**Change area:** Knob, Fader, AudioMeter, LoudnessHistory and measurement/ballistics providers.

1. Define parameter taper/units, touch/automation state, channel layouts, peak/RMS/loudness quantities and declared measurement windows.
2. Implement knob/fader pointer, wheel, text and keyboard gestures through one parameter transaction with double-click/reset semantics.
3. Implement actual meter/loudness computation in the audio/analysis layer and bounded GUI snapshots; define hold/decay/clip reset.
4. Implement readable calibrated scales, overload state and accessible numeric/summary alternatives.
5. Verify known-amplitude/test-tone signals, silence/non-finite input, channel changes, ballistics timing and parameter undo/automation.

**Accept when:** Meters report defined audio quantities against known signals; controls manipulate actual engine parameters consistently.

**Delegate:** Luna owns themed knob/fader/meter visuals; Sol owns measurement and taper/ballistics correctness.

<a id="wp-062"></a>
## WP-062 — Implement mixing, panning and routing

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-027](phase-02-controls-data.md#wp-027), [WP-060](phase-05-audio.md#wp-060), [WP-061](phase-05-audio.md#wp-061)

**Proposal scope:** [AUD-13 ChannelStrip](../proposed/06-audio-music.md#aud-13), [AUD-14 MixerView](../proposed/06-audio-music.md#aud-14), [AUD-15 PanningControl](../proposed/06-audio-music.md#aud-15), [AUD-16 RoutingMatrix](../proposed/06-audio-music.md#aud-16)

**Change area:** ChannelStrip, MixerView, PanningControl, RoutingMatrix and audio graph commands.

1. Define channel/bus identities, supported layouts, gain/pan laws, sends, mute/solo groups and valid routing graph constraints.
2. Implement persistent mixer/routing operations in the engine adapter with safe graph reconfiguration and cycle policy.
3. Implement virtualized mixer strips and routing matrix with stable selection and keyboard/numeric access.
4. Connect parameter automation, meters and undo without using delegates as engine state.
5. Verify known panning/mix output, mute/solo interactions, invalid feedback routes, route changes during playback and session reopen.

**Accept when:** The mixer/routing UI changes a real audio graph and produces verified output while remaining responsive.

**Delegate:** Luna implements strips/matrix controls and routing fixtures; Sol owns DSP/graph semantics.

<a id="wp-063"></a>
## WP-063 — Implement waveform and spectral editing

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-043](phase-03-authoring.md#wp-043), [WP-048](phase-03-authoring.md#wp-048), [WP-060](phase-05-audio.md#wp-060)

**Proposal scope:** [AUD-20 WaveformEditor](../proposed/06-audio-music.md#aud-20), [AUD-21 SpectrogramView](../proposed/06-audio-music.md#aud-21)

**Change area:** WaveformEditor, SpectrogramView, peak/tile caches and waveform/spectral processing backend.

**Additional obligations:** [ENG-09](supporting-scope.md#eng-09).

1. Define sample/channel selection, peak pyramid reduction, FFT window/hop/scaling, spectral coordinates and edit capabilities.
2. Implement bounded waveform and spectrogram tile providers with cancellation, accurate extrema and known-frequency interpretation.
3. Implement real trim/fade and supported spectral selection/processing preview/commit operations in the audio domain.
4. Connect sample-accurate gestures, undo, cache invalidation and rendered audio output.
5. Verify impulses/tones, extrema retention, FFT bin/window conventions, tile seams, sample boundaries and canceled processing.

**Accept when:** Both editors manipulate or analyze actual audio with numerically verified coordinates and reversible supported operations.

**Delegate:** Luna owns editor overlays and signal fixtures; Sol owns DSP, sample indexing and caches.

**Astra trigger:** Use Astra only for unresolved spectral normalization, reconstruction or tile/precision issues with reference signals.

<a id="wp-064"></a>
## WP-064 — Implement MIDI, patterns and clip launching

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-049](phase-03-authoring.md#wp-049), [WP-060](phase-05-audio.md#wp-060), [WP-061](phase-05-audio.md#wp-061)

**Proposal scope:** [AUD-22 PianoRoll](../proposed/06-audio-music.md#aud-22), [AUD-23 MidiControllerLane](../proposed/06-audio-music.md#aud-23), [AUD-24 StepSequencer](../proposed/06-audio-music.md#aud-24), [AUD-25 ClipLauncher](../proposed/06-audio-music.md#aud-25)

**Change area:** PianoRoll, MidiControllerLane, StepSequencer, ClipLauncher and MIDI/sequencer backend.

**Additional obligations:** [ENG-10](supporting-scope.md#eng-10).

1. Define note/event IDs, ticks, channels, velocity, supported high-resolution/per-note expression and pattern probability seed policy.
2. Implement actual note/controller scheduling, audition and all-notes-off lifecycle with import/export for declared MIDI capabilities.
3. Implement insert/move/resize/quantize and event lane editing as exact reversible operations.
4. Implement polymetric patterns and quantized clip launching tied to the engine clock, including stop/cancel transitions.
5. Verify overlapping notes, expression precision, tempo changes, deterministic patterns, launch boundaries and no stuck notes on close/device loss.

**Accept when:** Music editors schedule real events at exact positions and preserve data through undo and supported session/MIDI roundtrips.

**Delegate:** Luna owns note/pattern/launcher visuals and deterministic sessions; Sol owns MIDI encoding, timing and audition.

<a id="wp-065"></a>
## WP-065 — Implement take comping

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-048](phase-03-authoring.md#wp-048), [WP-060](phase-05-audio.md#wp-060), [WP-063](phase-05-audio.md#wp-063)

**Proposal scope:** [AUD-26 TakeCompView](../proposed/06-audio-music.md#aud-26)

**Change area:** TakeCompView and non-destructive comp segment/session operations.

1. Define takes, source offsets, comp ranges, overlap/crossfade policy and source lifetime.
2. Implement real comp selection/split/merge and playback/render resolution from source media without destructive source replacement.
3. Implement lane presentation, swipe/numeric/keyboard selection and comparison/audition actions.
4. Persist comp decisions and reconnect undo/redo and missing-source relink.
5. Verify boundary samples, crossfades, adjacent/overlapping selections, canceled gestures and rendered comp equivalence after reopen.

**Accept when:** Comp edits select actual source audio and produce verified reversible playback/render results.

**Delegate:** Luna owns take-lane visuals and comp fixtures; Sol owns media interval and render operations.

<a id="wp-066"></a>
## WP-066 — Integrate plugin hosting and plugin controls

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-019](phase-01-services.md#wp-019), [WP-060](phase-05-audio.md#wp-060), [WP-062](phase-05-audio.md#wp-062)

**Proposal scope:** [AUD-17 PluginRack](../proposed/06-audio-music.md#aud-17), [AUD-18 PluginBrowser](../proposed/06-audio-music.md#aud-18), [PLT-11 PluginHostAdapter](../proposed/08-platform-integrations.md#plt-11)

**Change area:** PluginHostAdapter, PluginRack, PluginBrowser and versioned plugin format adapters.

**Additional obligations:** [ENG-16](supporting-scope.md#eng-16).

1. Define supported plugin ABI/SDK capabilities, discovery metadata, parameter/state contracts, editor lifecycle and crash containment.
2. Prototype scan/load/process/save-state/unload with a real test plugin, including a deliberately failing plugin in an isolated fixture.
3. Implement discovery/cache, instantiation, processing graph integration, state recall and supported latency compensation.
4. Implement rack/browser UI, parameter mapping/automation and native editor embedding with focus/window ownership.
5. Verify plugin failure, unavailable plugin recall, state roundtrip, unload during shutdown and actual processed audio. Track each SDK/format independently.

**Accept when:** At least one declared real plugin format passes lifecycle/audio/state fixtures; every advertised format has equivalent evidence.

**Delegate:** Luna owns browser/rack views and metadata fixtures; Sol owns ABI, host process and audio integration.

**Astra trigger:** Astra may address unresolved plugin isolation, native editor lifetime or delay-compensation correctness with a reproducible host fixture.

<a id="wp-067"></a>
## WP-067 — Implement score and notation editing

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-024](phase-02-controls-data.md#wp-024), [WP-050](phase-04-office.md#wp-050), [WP-064](phase-05-audio.md#wp-064)

**Proposal scope:** [AUD-27 ScoreView](../proposed/06-audio-music.md#aud-27)

**Change area:** ScoreView and notation layout/music event adapter.

**Additional obligations:** [ENG-11](supporting-scope.md#eng-11).

1. Define supported score objects, voices, ties, tuplets, spelling, articulations, part layout and linkage to MIDI/musical time.
2. Select/implement a functional notation layout backend using dense multi-voice measures and page-break fixtures.
3. Implement note/rest/rhythm entry, selection and notation-specific edits through a persistent score model.
4. Connect playback/audition, print/page output and declared notation interchange while retaining expressive information outside MIDI where needed.
5. Verify rhythmic totals, ties/tuplets, accidental context, pagination, undo and native/interchange roundtrip for the declared feature set.

**Accept when:** ScoreView edits and renders real notation with correct rhythm and persistent score semantics.

**Delegate:** Luna owns palettes/controls and score fixtures; Sol owns notation model, layout and music mapping.

**Astra trigger:** Use Astra for a minimized notation layout or rhythmic representation problem that blocks the agreed feature contract.

<a id="wp-068"></a>
## WP-068 — Implement pitch analysis and correction editing

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-043](phase-03-authoring.md#wp-043), [WP-063](phase-05-audio.md#wp-063), [WP-064](phase-05-audio.md#wp-064)

**Proposal scope:** [AUD-28 PitchEditor](../proposed/06-audio-music.md#aud-28)

**Change area:** PitchEditor and pitch analysis/correction provider.

**Additional obligations:** [ENG-12](supporting-scope.md#eng-12).

1. Define analysis regions, confidence/unvoiced handling, pitch/note units, formant/time constraints and supported material.
2. Choose/integrate actual pitch detection and correction from synthetic-tone/glide and voiced fixture evidence.
3. Implement pitch contour/note editing, preview/cancel and non-destructive parameter storage.
4. Connect worker processing, stale-result cancellation, cache invalidation and real rendered audio output.
5. Verify frequency targets, unvoiced regions, boundary artifacts and undo/reopen; state measured limitations instead of general pitch-quality claims.

**Accept when:** PitchEditor performs actual declared analysis/correction and passes numerical/audio-output fixtures.

**Delegate:** Luna owns contour/control presentation and synthetic fixtures; Sol owns DSP and analysis lifecycle.

**Astra trigger:** Astra may resolve a specific pitch-estimation, phase continuity or correction artifact after objective and listening fixtures isolate it.
