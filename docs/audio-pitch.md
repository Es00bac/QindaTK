<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Pitch analysis and correction

WP-068: `PitchEditor` edits real pitch. `AudioPitchSource` analyses a session
source on the thread pool and publishes the editor's snapshot; each gesture
becomes a `pitch` source edit in `session.edits`, previewed on a worker,
committed as one undo step, saved with the session and heard in playback and
offline render. Decisions
[D-194](decisions.md#d-194--pitch-is-analysed-by-yin-on-a-5-ms-grid-in-midi-cents) to
[D-197](decisions.md#d-197--a-pitch-edit-is-a-source-edit-and-the-analysis-holds-through-previews).
It builds on the editing stack in [audio-editing.md](audio-editing.md).

```
 AudioSession ── edits.buffer(resource) ─▶ AudioPitchSource ── YIN (pool, once per committed buffer)
                                              │ snapshot (view range)          ▲ sourceChanged
                                              ▼                                │
                                          PitchEditor ── preview/commit/cancel ┘
                                              │ editPreviewRequested …
                                              ▼
            pitch.previewEdit / commitEdit ─▶ edits.preview / commitPreview / apply ("pitch": TD-PSOLA)
```

Code: `src/core/audio_pitch.{h,cpp}` (analysis), `audio_pitch_correct.cpp`
(TD-PSOLA), `audio_pitch_edit.{h,cpp}` (the source-edit operation),
`audio_pitch_source*.cpp` (the QML host).

## Units and regions

| Item | Convention |
| --- | --- |
| Pitch | cents on the MIDI scale: 100 × MIDI note, A4 = 440 Hz = 6900 (PitchEditor's `pitchCents`) |
| Frame | frame *f* is centred on sample `f · hop`, hop = round(rate × 5 ms) (240 at 48 kHz, 221 at 44.1 kHz); partial analyses sit on the same grid and agree exactly with a whole-source one |
| Note | `[start, end)` on frame-cell edges (`centre − ⌊hop/2⌋`); `pitchCents` is the median of its voiced frames; ID `note-<start>` |
| Segment | a phrase: a voiced run bridging at most 6 unvoiced frames (30 ms); ID `segment-<start>` |
| Confidence | `1 − d′(τ)` of the chosen lag (0…1); the snapshot's `confidenceThreshold` is `1 − voicingThreshold` |
| Unvoiced | no dip below the threshold, or frame RMS ≤ −50 dBFS; digital silence reports 0 Hz and confidence 0 |
| Formant | a ratio relative to the note as it sounds now (1 = unchanged) |
| Samples | positions are source frames as exact decimal strings in QML, as everywhere in the audio stack |

Supported material: monophonic pitched sources (voice, solo instruments) at
60–1000 Hz by default (`minimumHz`, `maximumHz`; 20 Hz up to a quarter of the
rate). Polyphonic material gets one pitch per frame, which is not a
meaningful edit target.

## Analysis (D-194)

YIN (de Cheveigné & Kawahara, *J. Acoust. Soc. Am.* 111(4), 2002) per frame:

1. Difference function `d(τ) = e(0) + e(τ) − 2r(τ)` over an integration
   window W = maximum lag (⌊rate / minimumHz⌋), the cross term by FFT.
2. Cumulative-mean normalisation `d′(τ) = d(τ) · τ / Σ_{k≤τ} d(k)`.
3. The first lag from ⌊rate / maximumHz⌋ with `d′ < voicingThreshold`
   (default 0.15), followed to its local minimum; with none, the global
   minimum is reported and the frame is unvoiced.
4. `d` is re-evaluated at τ−1, τ, τ+1 with each window centred on the frame
   centre and a parabola gives the sub-sample period, so a glide is measured
   where the frame sits.

Notes split where the pitch stays more than `noteToleranceCents` (50) from
the note's running median for 4 voiced frames; a note too short to stand
alone re-references instead of splitting.

## Correction (D-195, D-196)

TD-PSOLA (Moulines & Charpentier, *Speech Communication* 9, 1990). The
region is re-analysed inside the edit, so the stored parameters are the
whole edit:

- **Marks.** In voiced runs, one mark per period on the waveform's extreme
  of one polarity, refined to a sub-sample position by a parabola; every hop
  in unvoiced material. All channels share the marks of their mean.
- **Synthesis.** Each output mark takes the grain of the analysis mark
  nearest its input time and is spaced by that mark's period divided by the
  pitch ratio. Grains are Hann windows at least as long as the spacing on
  either side and are resampled by the formant ratio on voiced marks
  (Catmull-Rom). The output is the grain sum divided by the window sum: with
  no change it reproduces the input to rounding.
- **Pitch.** `targetPitchCents` moves the median of the region's voiced
  frames; `correctionAmount` 0 keeps every deviation from it (vibrato,
  scoops), 1 flattens each voiced frame onto the target.
- **Time.** `anchorSample` is heard at `targetSample`; the two sides of the
  region stretch linearly and the region keeps its length.
- **Edges.** Samples outside `[startSample, endSample)` are never touched; the
  first and last 5 ms (at most a quarter of the region) crossfade from and
  back to the original.

| Limit (refused beyond) | Value |
| --- | --- |
| Region | ≥ 4 hops (20 ms) |
| Pitch change | ±700 cents from the region's median, also per frame |
| Formant ratio | 0.8 … 1.25 |
| Retiming stretch | 2/3× … 1.5× on each side |
| Correction amount | 0 … 1 |

A pitch or formant change over a region with no voiced frames is refused
("no voiced material"); retiming unvoiced material is allowed.

## Measured results (fixtures)

The tests assert these on synthetic signals with known pitch at 48 kHz
(`tst_audio_pitch`, `tst_audio_pitch_session`):

| Fixture | Asserted |
| --- | --- |
| Sines 65–990 Hz | every interior frame voiced, within 0.02 cents; confidence > 0.99 |
| Exponential glide 200→400 Hz in 1 s | every frame 0.1–0.9 s within 5 cents of the instantaneous pitch |
| Uniform noise, digital silence | no voiced frame, confidence < 0.5; silence 0 Hz |
| Legato A3→E4 harmonic tone | one phrase, two notes within 0.05 cents; boundary within 20 ms after the change |
| Pitch ±100, ±300, ±699 cents (207 Hz tone) | every re-analysed interior frame within 0.25 cents of the target; outside samples bit-identical; largest sample step at the edges ≤ 1.1 × the input's |
| No change (target = median) | output equals input within 1e-6 |
| ±30-cent vibrato, amount 1 / amount 0 +100 | flattened within 4 cents / every frame keeps its deviation within 4 cents |
| Formant 0.8 and 1.25 (1 kHz resonance, 150 Hz) | harmonic centroid scales by the ratio within 0.03, pitch unchanged within 0.25 cents |
| Retime a note start by +50 / −42 ms | the re-analysed boundary moves by the same amount within 2 hops; pitches unchanged within 0.05 cents |
| Session edit | offline render within 0.25 cents of the target; undo/redo/reopen render bit-identically |

**Limitations (measured, not general quality claims).** TD-PSOLA keeps the
spectral envelope: a pure sine has one harmonic, so a large shift of a sine
mostly moves its level, not its pitch (tests shift harmonic tones). Large
downward shifts and stretches repeat grains, which buzzes at the hop rate on
noise-like material. A repeated edit on the same note re-processes already
processed audio. Listening tests, real vocal fixtures and throughput are
not measured here (WP-088…WP-094).

## Lifecycle and threads (D-197)

| Work | Thread | Cancellation / staleness |
| --- | --- | --- |
| Analysis | thread pool, once per committed buffer and channel | token every 32 frames; a newer revision, channel or setting abandons it; a superseded result is never published |
| View range, zoom, scroll | GUI thread, re-publishes from the cached analysis | — |
| Preview | thread pool (`edits.preview`) | token per mark batch; a newer preview cancels the older one |
| Commit / apply / replay | GUI thread | — |
| Playback | device thread plays the compiled graph only | never runs pitch code and allocates nothing (`tst_audio_pitch_session`) |

- A ready preview makes `session.edits` return the previewed samples. The
  pitch source then **holds** (`holding`): it keeps its analysis of the
  committed samples, so note IDs survive a drag and a request never targets
  previewed pitch. It never analyses previewed samples.
- Any change to the committed samples (commit, apply, undo, redo, relink,
  another edit) marks the analysis stale at once (`current` false,
  `snapshot.stale` true); `editFor`/`previewEdit`/`commitEdit` refuse until
  the new analysis is published. Cancelling a preview keeps the analysis.
- `commitEdit` commits the ready preview when it is exactly the request
  (the undo step is what was heard); otherwise it cancels the preview and
  applies the edit synchronously.

## QML

```qml
Tk.AudioPitchSource { id: pitch; session: session; resourceId: vocal
                      columnCount: Math.max(1, editor.width); pitchSnapCents: 0; correctionAmount: 0 }
Tk.PitchEditor {
    id: editor
    sourceId: pitch.resourceId
    analysisSnapshot: pitch.snapshot
    editingEnabled: pitch.current
    onEditPreviewRequested: (gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) =>
        pitch.previewEdit(gesture, segment, note, operation, anchor, target, cents, ratio)
    onEditCommitRequested: (gesture, source, channel, segment, note, operation, anchor, target, cents, ratio) =>
        pitch.commitEdit(gesture, segment, note, operation, anchor, target, cents, ratio)
    onGestureCanceled: gesture => pitch.cancelEdit(gesture)
    onZoomRequested: (anchor, factor) => pitch.zoom(anchor, factor)
    onScrollRequested: fraction => pitch.scroll(fraction)
}
```

`AudioPitchSource` (an `AudioAnalysisSource`: `session`, `resourceId`,
`channel`, `startSample`/`endSample`, `columnCount`, `state`, `error`,
`zoom`/`scroll`/`showAll`/`cancel`/`refresh`) adds `minimumHz`,
`maximumHz`, `voicingThreshold` (analysis settings), `correctionAmount`,
`pitchSnapCents` (request settings), read-only `snapshot`, `current`,
`holding`, `noteCount`, `analysisRuns`, `detector` ("YIN"), `corrector`
("TD-PSOLA"), and `frameAt(sample)`, `noteAt(sample)`, `notes()`,
`editFor(…)` (the edit map without applying it),
`previewEdit`/`commitEdit`/`cancelEdit`, and `writeTestSignal(file, notes,
rate)` (harmonic reference tones, as the example uses).

The snapshot adds to PitchEditor's contract: `stale`, `revision`,
`detector`, `corrector`, `sampleRate`, `hopSamples`, `startSample`,
`endSample`, `maximumShiftCents`; notes carry `confidence` and
`voicedFrames`; contour points carry `sample`, `x`, `pitchCents`,
`confidence`, `voiced` (about one per column). `sampleBoundaries` are note
edges plus frame-cell edges (at most about 256), the targets of a time drag.

### The `pitch` edit

`session.edits.apply/preview(resourceId, { operation: "pitch", channel,
startSample, endSample, targetPitchCents, correctionAmount, formantRatio,
anchorSample, targetSample, minimumHz, maximumHz, voicingThreshold })`.
Omit `targetPitchCents` for no pitch change and both `anchorSample` and
`targetSample` for no retiming. `channel` −1 processes every channel with
shared marks. Labels read "Pitch to A#3", "Correct pitch to C4 (100%)",
"Formant ×1.20", "Move sample 24000 to 26400".
