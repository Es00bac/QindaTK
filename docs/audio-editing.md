<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Waveform and spectral editing

WP-063: sample editing and spectral analysis/processing on the real session
audio. `WaveformEditor` and `SpectrogramView` stay pure views; the data and
the edits live in C++ next to the engine ([audio-engine.md](audio-engine.md)).
Decisions [D-134](decisions.md#d-134--source-edits-are-an-operation-list-per-resource-with-their-own-history)
to [D-137](decisions.md#d-137--spectral-processing-is-an-stft-gain-with-exact-patch-bounds-and-provisional-previews),
[D-186](decisions.md#d-186--spectral-masks-are-samplehz-polygons-applied-per-analysis-frame) (masks) and
[D-187](decisions.md#d-187--committed-edits-hold-parameters-spectral-patches-are-a-bounded-cache) (edit memory).

```
 AudioSession ─ decode ─▶ AudioSourceEditModel (session.edits)
   mediaBuffer()  ◀── buffer(resourceId) = original + edits (or a ready preview)
   renderToFile / engine graph                        │ sourceChanged(resourceId, revision)
                                                      ▼
 AudioWaveformSource ── pyramid (worker) ──▶ columns ──▶ WaveformEditor ── editRequested ─▶ edits.apply
 AudioSpectrogramSource ─ tiles (worker) ──▶ tiles, sampleBoundaries ─▶ SpectrogramView ─ processRequested ─▶ edits.preview/commitPreview
```

## Coordinates and selection

- **Samples.** Every position is a source frame index at the source's own
  rate, carried as an exact decimal string in QML. Ranges are half-open
  `[start, end)`. A range always refers to the source *as it stands* at that
  moment (after the edits before it).
- **Channels.** `channel` −1 (or `"all"`, or empty) is every channel; `n` is
  channel *n*. Gain, fades, silence and spectral gain can target one channel;
  delete and crop always apply to every channel. `AudioWaveformSource`
  with `splitChannels: true` also gives `channelColumns` (one column list
  per channel) and `channelLayout` (`[{id, label}]`, ids are the edit
  channel indices, labels L/R for stereo); a `WaveformEditor` given both
  draws one band per channel, and a drag inside one band selects that
  channel (across bands: `"all"`).
- **Columns.** For a view range `[s, e)` of length `L` and `n` columns
  (`n = min(columnCount, L, 8192)`), column *i* covers
  `[s + ⌊i·L/n⌋, s + ⌊(i+1)·L/n⌋)`. When `L ≤ columnCount` each column is one
  sample, so a gesture at sample zoom selects single samples. A selection
  gesture snaps to column boundaries, which are exact sample numbers.

## Peak pyramid (D-135)

`AudioPeakPyramid` holds min/max pairs for 16-sample blocks and each ×4
coarser level until a level has at most four blocks: about ⅔ byte per sample
per channel, built once per source revision on the thread pool (cancellable
between blocks). `extrema(channel, start, end)` combines whole blocks from
the coarsest level that fits and reads raw samples only at the partial ends,
so every column's extrema are the exact min/max of its samples at any zoom;
no peak is ever lost to reduction.

## Spectral conventions (D-136)

| Item | Convention |
| --- | --- |
| Frame | frame *f* at hop *h* is centred on sample `f·h`, reads `[f·h − N/2, f·h + N/2)`; samples outside the source are zero |
| Window | periodic Hann `w[j] = 0.5 − 0.5·cos(2πj/N)`, `w[N/2] = 1` on the centre sample, `Σw = N/2` |
| Bin | bin *k* (0 … N/2) is `k · rate / N` Hz |
| Scaling | amplitude: `2|X[k]|/Σw` (DC and Nyquist `|X[k]|/Σw`); a full-scale sine on a bin reads 1.0 = 0 dBFS, its Hann neighbours −6.02 dB |
| FFT sizes | powers of two from 64 to 16384; hop 1 … N |
| Display | cell = max amplitude over the frames centred in its column (or the frame nearest the column centre) and the bins whose frequency lies in `[e_r, e_{r+1})` (the top band includes its upper edge; a band without a bin takes the nearest one); log or linear band edges; intensity = `(dB − floorDb)/−floorDb` clamped to 0 … 1, cells at or below the floor are not sent |
| Bounds | at most 12 000 cells per request (`columns × rows`, rows ≤ 512); a range with more than `maxFrames` (default 1024) frames is analysed at a power-of-two multiple of the hop (`stride`, `effectiveHop`), whose frames are frames of the unstrided analysis |

Frames are computed in tiles of 32 consecutive frames per (revision,
channel, N, hop); a frame depends only on its own samples, so separately
computed tiles join without seams. `AudioSpectrogramSource` keeps a 32 MiB
least-recently-used tile cache and computes missing tiles on the thread pool.

## Edits (D-134)

`session.edits` is the `AudioSourceEditModel`. Operations
(`edits.capabilities(resourceId)` lists them):

| Operation | Parameters | Result |
| --- | --- | --- |
| `gain` | `gainDb` (≤ +48; ≤ −150 is exact silence) | samples × 10^(dB/20) |
| `fadeIn` / `fadeOut` | `shape`: `linear` or `equalPower` | fade in: sample *i* of *n* × `i/n` (or `sin(π/2 · i/n)`); fade out uses `n − 1 − i`, so the first faded-in and last faded-out samples are 0 |
| `silence` | — | zeros |
| `delete` | every channel | removes the range; later samples move earlier |
| `crop` | every channel | keeps only the range |
| `spectralGain` | `lowHz`, `highHz`, `gainDb` (default −150: removal), `fftSize` (default 2048), optional `mask` | see below |

`apply(resourceId, edit)` validates, computes and records one undo step
synchronously; `undo()`/`redo()` step through the model's own history
(per-owner undo, like the mixer). Each change bumps the source revision and
emits `sourceChanged`: the session drops resampled copies and recompiles the
engine graph (playback and `renderToFile` use the edited samples), and every
analysis source for that resource drops its pyramid or tiles and recomputes.
The session saves the edit list as its `edits` part and replays it on open;
previews and history are not saved.

**Memory (D-187).** Gain, fades and silence store only their parameters;
their samples are recomputed from the base whenever the list is replayed.
Spectral edits keep their computed patch as a cache bounded by
`edits.patchBudgetBytes` (64 MiB; `edits.patchBytes` reports what is held):
past it the oldest patches are dropped and recomputed bit for bit when a
replay or redo needs them. A replay edits one private copy in place, so
undo costs one buffer copy plus the edits' own ranges.

**Clips.** Clips reference sources by ID and keep their timeline position
and length. After a delete or crop, a clip reads the shorter source by source
sample index: later material moves earlier inside the clip and the part of
the clip past the new end plays silence. Moving clips with the edit is a
timeline operation, not a source edit.

**Relink.** A relinked original replays the edit list; an edit that no
longer fits is skipped and listed by `edits.problems(resourceId)`.

## Spectral processing and previews (D-137)

`spectralGain` is short-time Fourier processing at hop N/4 with Hann analysis
and synthesis windows (`Σw² = 1.5`). Frames centred in `[start, end)` have the
bins with `lowHz ≤ k·rate/N ≤ highHz` multiplied by the gain; only the
difference those frames make is added, so every sample outside
`[f0·h − N/2, (f1−1)·h + N/2)` is bit-identical and unprocessed bins
reconstruct to rounding. A range shorter than one hop between frame centres
is refused.

**Masks (D-186).** `mask: [[{sample, hz}, …], …]` replaces the band: a
frame centred on sample *c* changes the bins whose frequency lies inside
any polygon on the line `x = c` (even-odd per polygon, union across
polygons); frames the mask misses change nothing. Points must lie inside
`[startSample, endSample]` and 0 … rate/2; at most 4096 points. Without
`lowHz`/`highHz` the band is the mask's frequency extent (labels). A lasso
is one polygon; a brush stroke is one octagon per dab.

`edits.preview(requestId, resourceId, edit)` runs the processing on the
thread pool and answers with `previewReady`, `previewFailed` or
`previewCanceled`. `cancelPreview` stops it between frames and nothing is
published. A ready preview is provisional: `edits.buffer()` (and so playback,
renders and analysis) returns the previewed samples until `commitPreview`
records it as one undo step or `cancelPreview` restores the committed
samples. Any other change to that source cancels the preview first.

## Threads

| Work | Thread | Cancellation |
| --- | --- | --- |
| Peak pyramid | thread pool, once per source revision | token checked every 4096 blocks; superseded builds are never published |
| Spectrum tiles and reduction | thread pool, per request | token checked per frame and column; a setting change or `cancel()` abandons the request |
| Spectral preview | thread pool | token checked per frame |
| Edit fold/apply (linear edits, replaying saved edits) | GUI thread; one working copy per replay | — |
| Recomputing a spectral patch (after open, or dropped by the budget) | GUI thread during a replay or redo, bounded by the edit's range | — |
| Playback | device thread reads the compiled graph only | the graph holds immutable buffers; no edit code runs there |

The device thread never sees an edit: edits produce new immutable buffers and
a new graph, adopted at a period boundary like any other recompile.

## QML

```qml
Tk.AudioWaveformSource { id: peaks; session: session; resourceId: clipResource; splitChannels: true
                         columnCount: Math.max(1, editor.width) }
Tk.WaveformEditor {
    id: editor
    sourceId: peaks.resourceId; columns: peaks.columns
    channelLayout: peaks.channelLayout; channelColumns: peaks.channelColumns
    selectionChannelId: hostSelectionChannel // from onSelectionCommitted's channelId
    // parameters: {gainDb} from the gain handle / G, {shape} from the fade handles / F.
    onEditRequested: (id, source, channel, operation, start, end, parameters) =>
        session.edits.apply(source, Object.assign({ operation, channel, startSample: start,
                                                    endSample: end }, parameters))
    onZoomRequested: (anchor, factor) => peaks.zoom(anchor, factor)
    onScrollRequested: fraction => peaks.scroll(fraction)
}
Tk.AudioSpectrogramSource { id: spectrum; session: session; resourceId: clipResource
                            rows: 64; columnCount: 256; frequencyScale: "log" }
Tk.SpectrogramView {
    tiles: spectrum.tiles; sampleBoundaries: spectrum.sampleBoundaries
    minimumHz: spectrum.minimumHz; maximumHz: spectrum.resolvedMaximumHz
    fftSize: spectrum.fftSize; hopSize: spectrum.effectiveHop
    fftWindow: spectrum.window; fftNormalization: spectrum.normalization
    maskPolygons: hostMask // set from onMaskCommitted (lasso: L, paint: B)
    onMaskCommitted: (id, source, channel, polygons) => hostMask = polygons
    onProcessRequested: (id, source, channel, operation, start, end, low, high, parameters) =>
        session.edits.preview(id, source, Object.assign({ operation, channel, startSample: start,
                                                          endSample: end, lowHz: low, highHz: high }, parameters))
}
```

Source states: `empty` (no session or resource), `loading`, `ready`,
`failed` (`error` says why), `canceled`. A change sets `loading` at once, so
a reader never takes the previous result for the answer.

## Not covered here

Performance measurement of large files, other platforms and screen-reader
runs are WP-088…WP-094. Time stretch, pitch shift and repair tools beyond
band and mask gain are not part of the reference engine.
