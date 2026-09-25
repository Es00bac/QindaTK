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
`x`, `y`, `confidence`, `state` and `source`. Point states are `tracked`, `lost`
and `corrected`. Track states are `idle`, `running`, `ready`, `lost` and
`cancelled`.

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
max 0.09 px). Rotation, scale and perspective change are not modelled; rebase
the reference or correct points when appearance drifts.

Generated points do not change accepted edits until `acceptAnalysis(trackId)`.
`correctPoint(trackId, frame, x, y)` inserts a user point at exact frame ID,
`offsetTrack(trackId, dx, dy, fromFrame, toFrame)` shifts accepted points in
the selected inclusive range, and `rebaseTrack(trackId, frame)` makes an
accepted, nonlost point the new reference patch and seed. A later analysis
starts from the accepted point just before its range when that point is
trusted. These actions participate in the model's bounded undo/redo history.
Multi-resolution optical flow and camera solving are later adapter work.

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
Frames larger than 4 megapixels are refused rather than proxied at a different
scale.

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

`previewTrackedMask(maskId, sourceFrame, targetFrame, trackId)` translates a
source path by the normalized displacement between two **accepted, nonlost** tracking
points. It returns a preview path without editing the mask. A user or host can
accept it with `setMaskKeyframe`; a lost track cannot propagate a mask.
`applyTrackToMask(maskId, sourceFrame, trackId, fromFrame, toFrame)` keys the
translated path on every frame of the range with an accepted, nonlost point as
one undo step and returns `keyedFrames` and `skippedFrames`; lost frames are
never keyed, so the mask interpolates across them from trusted keys and the
host can see which frames need a manual key.

`saveToFile(localUrl)` and `loadFromFile(localUrl)` use schema
`qindatk.tracking-roto` version 1 and atomic local writes. They preserve only
accepted track points and mask keys. A reopened model needs its source frames
rebound before a fresh analysis. Limits are 128 tracks, 128 masks, 512 keys per
mask, 256 vertices per key, 300 analysis or loaded frames per job, 30
occlusion frames, 4-megapixel tracking proxies,
256 MiB aggregate proxy storage, 16-megapixel mask output and a 4 MiB document.

## Host boundary

Tracking and masks are toolkit functions. A host application chooses clips
and frame ranges (decoding them with `loadMediaFrames` or supplying its own
images), decides how analysis is scheduled around its media cache and owns
output formats. The
toolkit's `TrackingPanel` and `RotoscopeEditor` display snapshots and emit
requests keyed by stable IDs; a host connects these requests to the model.
