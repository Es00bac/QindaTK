<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tracking and rotoscoping core

`Tk.TrackingRotoModel` holds accepted feature positions and animated polygon
masks. It consumes tagged `QImage` frames supplied by a C++ host with
`setFrame(frame, image)`, decodes them from video with `loadMediaFrames`, or
reads local images with `loadFrame(frame, localUrl)`. Source frames are converted to encoded sRGB grayscale proxies for
tracking and are never saved with the document. The host keeps the decoded
full-color frames for composition.

## Tracking contract

A track has a stable `trackId`, label, reference frame, seed point in source
pixel coordinates, patch radius and per-frame search radius. `tracks` publishes
accepted `points` and separate generated `previewPoints`, each with `frame`,
`x`, `y`, `rotation` (degrees, clockwise on screen), `scale`, `confidence`,
`state` and `source`. Point states are `tracked`, `lost` and `corrected`.
Track states are `idle`, `running`, `ready`, `lost` and `cancelled`. A track's
`motion` is `translation` (default) or `similarity`, which also solves the
patch's rotation and uniform scale against the reference.
`setTrackOptions(trackId, radius, searchRadius, motion)` changes the patch
radius, search radius and motion model as an undoable edit. Radii are
tracking-proxy pixels, which equal source pixels for frames up to 4 MP.

`startAnalysis(trackId, startFrame, endFrame, occlusionFrames = 0)` checks the
reference patch and every frame in the range, then runs on a worker thread
(`TrackingEngine`, D-143). An `endFrame` below `startFrame` tracks backwards.
The worker owns shared copies of the proxies and posts each sample back;
`cancelAnalysis()` returns at once and the job's later samples are ignored by
job ID. A texture guard rejects flat reference patches (standard deviation
below 6). Only one worker job (analysis or frame load) runs at a time.

Per frame the tracker searches for the lowest mean absolute grayscale patch
error around the last trusted point, then refines the integer match to
subpixel precision with a parabola through the squared-error neighbours (a
zero-error match is exact and is not refined). A fractional seed keeps its
offset. Confidence is `max(0, 1 - error/64)`; error above 16 reports `lost`.
A `similarity` track searches positions under the prior rotation and scale,
then runs a compass search over position, rotation and scale on bilinear
samples of the squared error, bounded to 20° and ×1.25 per frame and a total
scale of 0.25–4. The lost test uses the refined mean absolute error.

**Loss and occlusion.** A lost point keeps the last trusted position and its
measured confidence; the engine never extrapolates motion. With
`occlusionFrames` = N the worker keeps searching through up to N consecutive
lost frames, around the last trusted point with a radius that grows by one
search radius per lost frame (capped at 64 px), and resumes `tracked` when the
patch matches again. One more lost frame stops the run. The track ends `lost`
(with `lastError` naming the frame) when the run stopped or ended on a lost
frame, otherwise `ready`. `TrackingPanel` shows reacquired loss as a warning.

Measured accuracy (tests/cpp/tst_tracking_roto_model.cpp): integer shifts of a
bounded texture track exactly; smooth texture shifted by 1.3 and 0.6 px per
frame tracks within 0.2 px (simulation of the same algorithm: mean 0.05 px,
max 0.09 px). A smooth texture turning 3° and scaling ×1.025 per frame tracks
within 0.25 px, 0.5° and 0.01 scale (tests/cpp/tst_tracking_roto_motion.cpp).
Perspective is handled by a four-track corner pin on the mask (below), not by
a single patch. Rebase the reference or correct points when appearance drifts
beyond these models.

Generated points do not change accepted edits until `acceptAnalysis(trackId)`.
`correctPoint(trackId, frame, x, y)` inserts a user point at exact frame ID,
`offsetTrack(trackId, dx, dy, fromFrame, toFrame)` shifts accepted points in
the selected inclusive range, and `rebaseTrack(trackId, frame)` makes an
accepted, nonlost point the new reference patch and seed. A later analysis
starts from the accepted point just before its range when that point is
trusted. These actions participate in the model's bounded undo/redo history.

**Results stay separate from accepted edits.** Undo and redo restore accepted
data only: a live preview and the live analysis state are kept, so undoing an
edit made while a job ran never discards its finished results or leaves a
track `running`. Undoing an accept returns its results to preview. A preview
records the version of every frame it measured (the range and the reference).
Replacing any of those frames (`setFrame`, `loadFrame`, `loadMediaFrames`)
cancels a running job and discards a finished preview (state `cancelled`,
`lastError` names the frame); `acceptAnalysis` also refuses a preview whose
frames changed, so results measured on stale pixels never apply. A track holds
at most 4096 accepted points; accepting or correcting past that is refused,
and `saveToFile` checks the bytes it writes with the loader, so a saved
document always reopens.

## Tracking footage

Frame IDs are **source frame indices**: the presentation indices of the
decoded media. `loadMediaFrames(uri, firstFrame, count)` decodes up to 300
frames of a local video (path or file URI) with the WP-070 `MediaDecoder` on a
worker, at coded size (so tracking coordinates are source pixels), converts
each to a grayscale proxy there and stores it; `loadingFrames` and
`frameLoadProgress` follow it and `frameLoadFinished(ok, error)` reports the
end. A missing file, a range past the last frame, a corrupt (concealed) frame
or the proxy budget fails the load explicitly. `cancelFrameLoad()` stops it at
once; frames already stored remain and late frames never apply. `loadedFrames`
lists the stored frame IDs. `setFrame`/`loadFrame` still accept host images.
A frame above 4 megapixels (for example 3840×2160) is tracked on a smooth
downscale that fits 4 MP; the model keeps each frame's source size and maps
positions back to source pixels, so tracks and masks stay in source
coordinates (a UHD fixture tracks within 0.5 source px). Frames above 64 MP
are refused. Every frame of an analysis must share the reference's size.

## Mask contract

A mask has stable `maskId`, label, feather radius and ordered keyframes. Each
keyframe has `frame` and ordered `{pointId, x, y}` vertices normalized to
**[0,1] in the source frame**. Tracking points remain source pixel coordinates.
At an exact key, `evaluateMask` returns that path. Between
keys, it linearly interpolates when both ordered point ID lists match. A
topology change holds the prior shape and reports `heldTopology` rather than
inventing a point correspondence. Before the first key, evaluation fails.

`applyMask(maskId, frame, sourceImage)` rasterizes a closed polygon with an
antialiased edge and an optional bounded box feather, multiplies the source
pixels' premultiplied channels by the mask, and returns tagged encoded-sRGB
pixels. The operation fails if a key is missing or the frame exceeds 16
megapixels.

## Masks in composition

For a persisted composition, set a layer's `maskId` and set
`MotionFrameRenderer.rotoModel`. The renderer applies the mask to that layer's
effected source **before its transform and source-over blend**. The mask is
sampled at the frame the layer's pixels came from (D-142):

- the source frame index given to `setSourceImage(sourceId, image,
  sourceFrame)`; `MediaFrameFeed` passes the decoded frame of every layer it
  feeds (`sourceFrame(sourceId)` reads it back);
- otherwise `floor(localTicks / maskTicksPerFrame)`, the layer-local time
  after `timeOffsetTicks` and `timeRemap` (`maskTicksPerFrame` defaults to 1,
  so a host rendering frame-index ticks needs no mapping).

A missing mask model or key, or a negative layer time, fails the frame and
keeps the prior rendered frame. Composition time is never used for masks.

## Masks from tracking

Each mask key carries `source`: `user` (drawn, edited or accepted with
`setMaskKeyframe`) or `generated` (keyed by tracking, with the `trackIds` that
generated it). It is stored and reopened with the key; `evaluateMask` reports
`source: "generated"` when a generated key shapes the frame.

`previewTrackedMask(maskId, sourceFrame, targetFrame, trackId)` moves a source
path by the motion between two **accepted, nonlost** tracking points and
returns a preview path without editing the mask. A user or host can accept it
with `setMaskKeyframe`; a lost track cannot propagate a mask.

`previewPropagation(maskId, sourceFrame, trackIds, fromFrame, toFrame)`
returns the generated keys for a whole range (`keys`, `keyedFrames`,
`skippedFrames`, `keptFrames`) without editing anything; `applyPropagation`
with the same arguments commits them as `generated` keys in one undo step.
One track moves the path by its translation, plus rotation and scale about the
tracked point for a `similarity` track; four tracks (corners in order) corner-
pin it with the perspective map of their quad. Lost or missing frames are
skipped, so the mask interpolates across them from trusted keys; a frame with
a user key is kept, never overwritten, and rerunning refreshes generated keys.
`applyTrackToMask(maskId, sourceFrame, trackId, fromFrame, toFrame)` is the
one-track form of `applyPropagation`.

## Persistence and limits

`saveToFile(localUrl)` and `loadFromFile(localUrl)` use schema
`qindatk.tracking-roto` version 1 and atomic local writes. They preserve only
accepted track points (with pose), track options and mask keys (with source). A reopened model needs its source frames
rebound before a fresh analysis. Limits are 128 tracks, 128 masks, 512 keys per
mask, 256 vertices per key, 4096 accepted points per track, 300 analysis or
loaded frames per job, 30 occlusion frames, 4-megapixel tracking proxies of
source frames up to 64 megapixels,
256 MiB aggregate proxy storage, 16-megapixel mask output and a 4 MiB document.

## Host boundary

Tracking and masks are toolkit functions. A host application chooses clips
and frame ranges (decoding them with `loadMediaFrames` or supplying its own
images), decides how analysis is scheduled around its media cache and owns
output formats. The
toolkit's `TrackingPanel` and `RotoscopeEditor` display snapshots and emit
requests keyed by stable IDs; a host connects these requests to the model.
