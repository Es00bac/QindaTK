<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Video scopes

`VideoScopesModel` measures a frame and publishes an immutable snapshot;
`VideoScopes` paints it as a waveform, RGB parade, histogram or vectorscope.
The analysis is a bounded CPU pass (16 megapixels at most) in
`VideoScopeAnalysis::analyze` (`src/core/video_scope_analysis.cpp`).

## Conventions (D-127)

Scopes measure the encoded (non-linear) signal, never linear light, under
three stated choices, each a model property and each echoed in the snapshot:

| property | values | meaning |
|---|---|---|
| `matrix` | `bt709` (default), `bt601`, `bt2020` | Y′CbCr coefficients: Kr/Kb 0.2126/0.0722, 0.299/0.114, 0.2627/0.0593 (non-constant luminance) |
| `signalRange` | `full` (default), `limited` | 8-bit codes: full maps normalized V to `255V` and chroma C to `128 + 255C` (clamped); limited maps to `16 + 219V` and `128 + 224C` |
| `analysisSpace` | `srgb` (default), `signal` | `srgb` converts a tagged frame to encoded sRGB first and takes an untagged one as encoded sRGB; `signal` measures the delivered encoded values unchanged and reports the tag's transfer (`srgb`, `linear`, `pq`, `hlg`, `gamma N`, ...) |

Decoded video reaches the scopes as full-range R′G′B′: the decoder converts
Y′CbCr with the stream's own matrix and range (docs/media-playback.md
"Decoder"), and SDR video is tagged encoded sRGB. So `signalRange: limited`
shows the codes the frame would carry as a legal-range signal; it does not
re-read the source file's codes.

## Snapshot

Every snapshot has `valid`, `frameId`, `width`, `height`, `sampleCount`
(transparent pixels skipped and counted), the conventions (`matrix`,
`signalRange`, `analysisSpace`, `transfer`, `profileConvertedToSrgb`,
`colorSpaceAssumption`, `rangeConvention`, `lumaConvention`,
`vectorscopeConvention`) and `codeScale` (`black`, `white`, `chromaZero`,
`chromaMin`, `chromaMax`). Measurements:

- `waveform`: 128 columns × 64 rows of Y′ counts (column-major); rows index
  8-bit codes, row 0 = code 255, four codes per row. `waveformMin`/`Max`: the
  exact Y′ code extent of each column (−1 when empty).
- `parade`: the same grid for R′, G′ and B′ in turn; `paradeMin`/`Max`.
- `histogram` (Y′) and `histogramRgb` (R′, G′, B′): 128 bins of two codes.
- `vectorscope`: 64 × 64 Cb (horizontal) / Cr (vertical, up) counts;
  `vectorTargets`: the 75% and 100% primaries and secondaries as codes and
  normalized positions under the convention (the control draws them).
- `levels`: frame-wide `yMin`…`bMax` code extents of Y′, Cb, Cr, R′, G′, B′.

Known answers (`tst_video_finishing`
`scopesMeasureDecodedBarsUnderStatedConventions`, on the test signal's 75%
bars encoded to FFV1 and decoded): Rec.709 limited Y′ 180, 168, 145, 133, 63,
51, 28; Cb 128, 44, 147, 63, 193, 109, 212; Cr 128, 136, 44, 52, 204, 212,
120; Rec.601 limited Y′ 180, 161, 131, 112, 84, 65, 35 (the 75% level is code
191, so values are those of 191/255, not of exactly 0.75).

## Demand and frame identity

A snapshot is computed only on demand: `analyzeFrame(image, frameId)`, a
convention change (which re-measures the last frame), or a newly presented
frame of the bound `player` (`MediaPlayer`) while `active`. Repaints and
repeated notifications of the same presented frame never re-analyze
(`analysisCount` counts analyses). Bound to a player, `frameId` is the
player's `presentedFrameKey` for the exact image measured, after the
program's finishing, so scrubbing never shows one frame's scope under another
frame's identity. An inactive model skips frames and measures the frame on
screen when it becomes active again.

## Limits

Analysis runs synchronously on the GUI thread, bounded by the 16-megapixel
limit; its cost during playback is WP-091's measurement. HDR frames under
`signal` are measured as delivered code values; tone-mapped or nit-scaled
HDR scopes are not provided.
