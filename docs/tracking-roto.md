<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tracking and rotoscoping core

`Tk.TrackingRotoModel` holds accepted feature positions and animated polygon
masks. It consumes tagged `QImage` frames supplied by a C++ host with
`setFrame(frame, image)`; `loadFrame(frame, localUrl)` is a local file
convenience. Source frames are converted to encoded sRGB grayscale proxies for
tracking and are never saved with the document. The host keeps the decoded
full-color frames for composition.

## Tracking contract

A track has a stable `trackId`, label, reference frame, seed point in source
pixel coordinates, patch radius and per-frame search radius. `tracks` publishes
accepted `points` and separate generated `previewPoints`, each with `frame`,
`x`, `y`, `confidence`, `state` and `source`. Point states are `tracked`, `lost`
and `corrected`. Track states are `idle`, `running`, `ready`, `lost` and
`cancelled`.

`startAnalysis(trackId, startFrame, endFrame)` checks the tagged reference and
all frames in the range, then processes one frame per event-loop turn so
`cancelAnalysis()` can stop it. A texture guard rejects flat reference patches.
The CPU tracker searches for the lowest mean absolute grayscale patch error
around the previous point. Confidence is `max(0, 1 - error/64)`; error above 16
reports `lost` with its measured confidence and stops. The engine does not
silently extend a lost trajectory. Cancellation clears previews. An image
replacement during analysis cancels the running job.

Generated points do not change accepted edits until `acceptAnalysis(trackId)`.
`correctPoint(trackId, frame, x, y)` inserts a user point at exact frame ID,
and `offsetTrack(trackId, dx, dy, fromFrame, toFrame)` rebases accepted points
in the selected inclusive range. These actions participate in the model's
bounded undo/redo history. Pixel decoding, multi-resolution optical flow and
camera solving are host or later adapter responsibilities.

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
megapixels. For a persisted composition, set a layer's `maskId` and set
`MotionFrameRenderer.rotoModel`. The renderer applies the mask to that layer's
effected source **before its transform and source-over blend**. A missing mask
model/key or a noninteger render time fails the frame, preserving the prior
rendered frame. Hosts using another tick timebase must map it to integer frame
indices before calling this renderer.

`previewTrackedMask(maskId, sourceFrame, targetFrame, trackId)` translates a
source path by the normalized displacement between two **accepted, nonlost** tracking
points. It returns a preview path without editing the mask. A user or host can
accept it with `setMaskKeyframe`; a lost track cannot propagate a mask.

`saveToFile(localUrl)` and `loadFromFile(localUrl)` use schema
`qindatk.tracking-roto` version 1 and atomic local writes. They preserve only
accepted track points and mask keys. A reopened model needs its source frames
rebound before a fresh analysis. Limits are 128 tracks, 128 masks, 512 keys per
mask, 256 vertices per key, 300 analysis frames, 4-megapixel tracking proxies,
256 MiB aggregate proxy storage, 16-megapixel mask output and a 4 MiB document.

## Host boundary

Tracking and masks are toolkit functions. A host application chooses clips,
decodes frames, maps project time to integer frame indices, decides how
analysis is scheduled around its media cache and owns output formats. The
toolkit's `TrackingPanel` and `RotoscopeEditor` display snapshots and emit
requests keyed by stable IDs; a host connects these requests to the model.
