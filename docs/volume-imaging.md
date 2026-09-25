<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Volume imaging: voxel data, slices, volume rendering and transfer functions

`Tk.VoxelVolume` is a typed, spatially calibrated voxel source.
`Tk.ImageSliceView` shows one of its principal voxel planes: axial (X across,
Y down), coronal (X across, Z down) or sagittal (Y across, Z down), or an
oblique world-space reslice through the cursor. `Tk.VolumeView` renders the
whole volume (composite or maximum intensity) on the GPU, or with the exact
CPU reference renderer when the GPU cannot. Give related views the same
`Tk.VolumeCursor` to link the crosshair, slice position and 3D picking.
`Tk.TransferFunctionModel` maps measured scalar values to color and opacity;
`Tk.TransferFunctionEditor` edits that settings model, one undoable step per
gesture, without changing source voxels.
[`examples/volume-imaging/Main.qml`](../examples/volume-imaging/Main.qml)
shows a known sphere phantom with anisotropic voxels in three principal
planes, an oblique plane and a volume rendering, and saves the cursor, view
and transfer settings to a settings file that a later run restores; the
gallery's "Volume imaging" section shows two linked planes and a volume view.

## Voxel contract

Call `configure({sizeX,sizeY,sizeZ,voxelType,spacing,origin,orientation,unit,
valueUnit,channels,frames,datasetId,frameOfReference,axisIds})` before filling
data. Sizes are whole numbers
1–8192 per axis, channels 1–64 and frames 1–4096. `voxelType` is `uint8`,
`uint16` or `float32`. A rejected description changes nothing and sets
`lastError`. An accepted one drops all voxels and any attached source, so
attach the source afterwards.

Chunks are 32×32×32, tightly packed in X-fastest order; multibyte values are
little-endian. C++ hosts supply complete chunks through `putChunk()` or attach
a `VoxelChunkSource` for synchronous on-demand reads. A source may be called
on the render thread while the GUI thread is blocked and must not call back
into the volume. `maximumResidentBytes` (default 64 MiB, at least one chunk)
bounds owned chunks plus the provider read cache. Cached provider chunks are
evicted least recently used first; owned chunks never are. `residentBytes` is
a snapshot: cache fills during reads do not emit `changed()`. `copyChunk()`
reads one whole chunk through the same cache; the GPU upload uses it.

Unwritten owned voxels read as zero. With a provider, a failed read, or a
chunk that does not fit beside the owned chunks, is missing. `setVoxel`
rounds integer types half away from zero and rejects non-finite values and
values outside the type's range. Writing into a provider chunk copies that
chunk first, so its other voxels keep their provider values.

`spacing` and `origin` each have three finite values; spacing is positive.
`orientation` is a row-major 3×3 orthonormal matrix (tolerance 1e-5), so
rotations and reflections (determinant ±1) are valid. Spatial coordinates
follow `world = origin + orientation × (voxelIndex × spacing)` in `unit`. Use
`worldFromVoxel` and `voxelFromWorld` to convert.

Identity ([D-260](decisions-domains.md#d-260--linked-views-check-geometry-saved-views-name-their-dataset)):
`datasetId` names the dataset, `frameOfReference` the spatial frame its world
coordinates belong to, and `axisIds` the three axes (default `x`, `y`, `z`;
three distinct non-empty names). `identity()` returns them with the unit,
value unit, type, shape, channel and frame counts and the revision.
`identityMismatch(identity)` is empty when saved settings belong to this
volume (same dataset, frame of reference and spatial unit); a reload of the
same dataset, even at another resolution, keeps them valid. The revision is
recorded, never compared. `geometryMismatch(other)` is empty when another
volume has the same grid, spatial unit, frame of reference, axis IDs,
spacing, origin and orientation (relative tolerance 1e-6); otherwise it names
the first difference.

Each channel and frame is its own layer of chunks: a provider is asked for
`(chunk, channel, frame)` and the cache keys on all five. `histogram(bins,
minimum, maximum, channel, frame)` counts one layer in equal bins, plus
explicit `below`, `above` and `missing` counts, reading chunk by chunk
through the bounded cache.

## Sampling

A voxel index is the centre of a cell one spacing wide ([D-086](decisions-domains.md#d-086--voxel-indices-are-cell-centres-slices-show-whole-cells)).
`sample(x, y, z, channel, frame, interpolation)` returns `{valid, value,
unit}`. A point is in bounds when every continuous index lies within
−0.5 … size − 0.5. `Nearest` picks the containing cell (a tie rounds away from
zero). `Linear` is trilinear between the eight surrounding centres and clamps
to the outer centres inside the edge half cell. Out-of-bounds points, invalid
channels or frames, missing chunks, NaN float voxels and a `Linear` sample
with a missing corner of non-zero weight are `valid: false`. No implicit
intensity normalization or unit conversion occurs.

`fillSpherePhantom(radiusFraction = 0.32)` writes the known phantom used by
tests and examples into a single-channel, single-frame volume. Its centre is
index (size − 1)/2 and r = fraction × the smallest physical extent
(size × spacing). Voxels within 0.55 r (spatial units) hold the peak, voxels
within r half of it, the rest zero. The peak is 220 (`uint8`), 55000
(`uint16`) or 1 (`float32`).

## Slice view

`ImageSliceView` exposes volume, cursor, transfer, plane, channel, frame,
interpolation and window center/width. The image rectangle spans whole cells
(size × spacing), fitted and centred in the viewport, so the in-plane
physical aspect is exact. `Nearest` generates at most one sample per voxel
and scales it without smoothing; `Linear` samples every item pixel, so values
are interpolated before they are mapped to colors. The generated image is
capped at 2048 pixels per side. Without a transfer model the view maps
`windowCenter ± windowWidth / 2` linearly to black … white; with one it
evaluates the measured-value transfer function directly, including its
below/above/missing colors; without one, missing samples are transparent.
Points outside the volume are always transparent. Without a cursor the view
shows its middle slice.

Planes are voxel-index planes. The view does not resample into world or
patient axes, so a flipped or rotated orientation shows the stored index
order; `worldPoint` and the readouts give physical positions.

A click moves the shared cursor to that point, clamped to the outer voxel
centres, and emits `pointSelected`. The wheel (one slice per 120 angle units;
horizontal scrolling passes through), Page Up/Down, Home/End and the header
chevrons step whole slices. Arrow keys move the crosshair one voxel within the
plane. A cursor without a volume adopts the view's volume.
`ImageSliceItem.voxelAt(x, y)` and `positionForVoxel(x, y, z)` convert between
item pixels and continuous voxel indices.

`saveSettings()` returns the plane, reslice orientation, window, channel,
frame, interpolation and overlay opacity with the volume's `identity()`,
JSON-safe. `restoreSettings()` validates everything first and changes nothing
when any part is invalid, when the settings name another dataset, or when the
saved channel or frame is outside the volume; `lastError` says why.

## Linked views and overlays

Views read the shared cursor in their own voxel indices, so a cursor is only
linked when it is bound to the view's volume or to one with the same geometry
(`geometryMismatch` is empty). Otherwise the view ignores it: it shows its
middle slice, draws no crosshair, never moves that cursor, and `linkError`
(shown under the view) names the difference. A transfer function whose `unit`
differs from the volume's `valueUnit` (both non-empty) is ignored the same
way; the view falls back to its window ramp.

`ImageSliceView.overlay` draws a second volume on the same grid (a label map,
a second modality) through `overlayTransfer`, composited over the base image
at `overlayOpacity` (0–1, default 0.5). It samples the overlay's channel 0 at
the view's frame (frame 0 when the overlay has fewer frames) with the view's
interpolation. An overlay with another geometry, without a transfer
function, or with a transfer function in another unit is not drawn, and
`overlayError` says why.

## Oblique reslicing

`plane: Tk.ImageSliceItem.Oblique` samples the world-space plane through the
cursor ([D-149](decisions-domains.md#d-149--oblique-reslices-are-square-world-frames-through-the-cursor)).
`resliceOrientation` is a row-major, orthonormal 3×3 world matrix whose
columns are the image's right, down and normal directions; the identity
shows world X across and world Y down, whatever the volume's own
orientation, so a rotated or reflected volume appears in world axes. The
image is a square one volume diagonal wide, centred on the volume centre
projected onto the plane and fitted into the item, sampled once per item
pixel with the view's interpolation (capped at 2048 per side). Clicks,
`voxelAt`, `positionForVoxel` and the crosshair use that frame. Page Up/Down,
the wheel and the chevrons step the cursor by the smallest voxel spacing
along the normal (`sliceCount` such steps span the diagonal); arrow keys move
it by the same step within the plane. `stepSlice(n)` and `moveCrosshair(dh,
dv)` do the same from code for every plane. A rejected orientation keeps the
current one and sets `lastError`.

## Volume rendering

`Tk.VolumeView` wraps `Tk.VolumeViewItem`. The camera is orthographic and
orbits the volume centre: `yaw` turns about world Y, `pitch` about the
camera's right axis (yaw 0, pitch 0 looks down world −Z with +X right and +Y
up; defaults 30° and 20°), and `zoom` 1 fits the volume diagonal to the
shorter side. `mode` is `Composite` (front-to-back accumulation of transfer
color and opacity) or `MaximumIntensity` (the transfer color of the largest
sample on each ray). `samplingRate` is samples per smallest voxel spacing
(0.25–8, default 2); opacity is corrected so the result does not depend on
it. `clipLow`/`clipHigh` crop the volume per voxel axis (fractions 0–1).
Without a transfer model the window ramp (`windowCenter ± windowWidth/2`)
maps values to gray with matching opacity. `interpolation` is explicit:
`Linear` (default) is D-086's clamped trilinear sample, `Nearest` its
containing cell.

The algorithm is specified by the CPU reference renderer
([D-146](decisions-domains.md#d-146--one-raycasting-algorithm-specified-by-a-cpu-reference));
the GPU shader runs the same steps. `renderPath` says which one drew the last
frame and `fallbackReason` why the GPU did not, in this order: reference
requested, no volume, no RHI scene graph (the software renderer), shaders not
built, no 3D textures, voxel format not sampleable, a side over the texture
limit (at most 2048), over `maximumTextureBytes` (256 MiB), channel or frame
out of range, missing chunks
([D-147](decisions-domains.md#d-147--the-gpu-volume-path-falls-back-to-the-reference-image-and-says-why)).
The reference image is at most 192 × 192 rays, scaled to the item.
`renderReference(w, h)` returns the exact reference image for the current
settings.

Drag to orbit, scroll to zoom, and click without dragging to pick: the ray
under the pointer stops where accumulated opacity reaches 0.5 (or at its
maximum sample) and moves the shared cursor there, so the slice views follow.
Arrow keys orbit by `orbitStep`, +/- zoom, Home resets and Escape cancels an
orbit drag back to where it started. `saveSettings()`/`restoreSettings()`
persist the camera, mode, interpolation, sampling, clip box, channel, frame
and window as JSON-safe values with the volume's `identity()`, validated
atomically, apart from the voxels. Settings for another dataset, or naming a
channel or frame the volume does not have, are rejected. `busy` is true while
volume work is in flight; `linkError` says why a cursor or transfer function
is ignored (see [linked views](#linked-views-and-overlays)).

GPU residency: the upload packs one channel and frame chunk by chunk through
the volume's bounded cache and drops its staging copy once uploaded, so CPU
memory stays within `maximumResidentBytes` plus one texture-sized transient
copy bounded by `maximumTextureBytes`. The reference path reads through the
cache only.

The GPU path is tested against the reference image on a real display
(`tst_volume_render_rhi`, rows for R8, R16 and R32F textures, maximum
intensity and composite, nearest and linear). It needs a hardware scene
graph; the offscreen platform has no 3D textures and skips it.

## Large data and cancellation

Volume reads never run in `updatePaintNode`
([D-261](decisions-domains.md#d-261--volume-reads-run-on-a-worker-obsolete-frames-are-rejected)).
The reference image and the GPU texture pack run on a one-thread worker with
copies of the camera, transfer table and geometry, so the GUI and scene-graph
threads keep running while a slow provider is read. Every change that alters
the picture starts a new generation: the running job of that kind is
cancelled at its next ray or chunk, and a result for an older generation is
rejected, so an obsolete frame is never shown. Until a current frame arrives
the view shows the last completed one (the GPU path draws nothing rather than
another volume's texture). `cancelRendering()` (and Escape when no orbit is
active) stops the running work and keeps the last frame; the next change
starts again. `renderStatistics()` reports jobs started, completed, rejected
and cancelled. Voxel reads and writes hold the volume's lock, so a host may
write voxels while a frame renders; a volume deleted mid-render cancels and
waits for the worker first.
## Transfer functions

`TransferFunctionModel` holds 2–256 stops with stable IDs, strictly
increasing measured values, opaque RGB colors and a separate 0–1 opacity.
Endpoints stay at the domain bounds; inner stops can be added, moved between
their neighbours, recolored, changed in opacity or removed. A rejected edit
changes nothing and sets `lastError`. `colorAt` and `opacityAt` interpolate
RGB and opacity linearly. `saveSettings()` returns JSON-safe data (`#rrggbb`
colors) that `restoreSettings()` validates atomically. These are view
settings, kept apart from the immutable voxel data.

Out-of-range and missing values are explicit
([D-263](decisions-domains.md#d-263--out-of-range-and-missing-transfer-values-are-explicit-settings)).
`classify(value)` answers `inside`, `below`, `above` or `missing` (NaN or
infinite). `belowRange` and `aboveRange` are `{mode, color, opacity}`: mode
`clamp` (the default) uses the end stop, mode `color` its own color and
opacity, set with `setRangeColor(which, color, opacity)` and cleared with
`clampRange(which)`. `missingValue` always has its own color (transparent
black by default). The slice views, both volume render paths and the export
use the same entries; in maximum intensity a missing sample never wins.

`presets()` lists built-in known ramps (`grayscale`, `hot`, `viridis`,
`cool-warm`); `applyPreset(id)` maps one onto the current domain as one undo
step and sets `presetId`, which any stop edit clears. `exportRamp(count)`
returns `count` samples over the domain (`{value, color, opacity}`) plus the
three special entries, the same values `colorAt`/`opacityAt`, the preview and
the render table use. Settings saved before these fields existed restore as
clamped with transparent missing values.

Edits are typed history steps
([D-148](decisions-domains.md#d-148--transfer-edits-are-typed-history-steps-gestures-are-transactions)):
each committed edit records the stop list before and after in the model's
`undoService`, which a document session can adopt. `undo()`, `redo()`,
`canUndo`, `canRedo`, `undoText` and `redoText` follow it. `beginEdit(label)`
opens a gesture: until `commitEdit()` edits are previews that update views
but record nothing, and `cancelEdit()` restores the start without a step.
Undo and redo are refused while a gesture is open. `configure()` and
`restoreSettings()` are loads and reset the history; `applySettings()` applies
saved settings as one undoable "Restore transfer settings" step instead
([D-262](decisions-domains.md#d-262--restoring-a-saved-view-is-an-undoable-transfer-step)),
which is what a host's "restore view" should use. A step records the whole
transfer state (domain, unit, stops, special colors, preset). `committed()` fires after
every recorded step, undo, redo or load, never for previews.

`TransferFunctionEditor` shows a gradient over the domain. Clicking the track
adds a stop at the current mapping, so the rendered result does not change
until the stop is edited. Drag or arrow-key a stop to move it; Delete or a
right-click removes it. The value field, opacity slider and color swatches
edit the selected stop exactly, and a rejected value snaps back to the stored
one. Hosts can connect `customColorRequested` to their color picker. A stop
drag, a keyboard nudge or an opacity slider press is one undo step; Escape
cancels a drag, and Ctrl+Z / Ctrl+Shift+Z or the header buttons undo and
redo. The editor closes the gestures it opened: changing its `model` or
destroying it mid-gesture cancels the gesture, so the model never stays in
`editing`. It leaves a gesture another client opened alone. The header's
preset box applies a built-in preset; the range row sets the below, above and
missing colors (the unlink button clamps to the end stop, the eye button
makes missing values transparent). Setting `histogram` to a
`VoxelVolume.histogram()` result draws the distribution (log-scaled bars) above the gradient
and shows the below/above/missing counts.

## Scientific 3D examples

[`examples/scientific-3d-family`](../examples/scientific-3d-family/Main.qml)
joins the scientific 3D family in one window: the sampled surface with 3D
probe points from
[`examples/scientific-plot-3d`](../examples/scientific-plot-3d/Main.qml)
(WP-085) and the sampled volume from
[`examples/volume-imaging`](../examples/volume-imaging/Main.qml) (slices,
reslice, volume rendering and saved views). Both pages stay loaded, so
switching keeps each page's state. `tests/qml/tst_volume_example.qml` checks
both pages show their known data, and that the volume page's saved view
survives a restart, is refused for another dataset and restores the transfer
function as one undo step. All keep their view settings apart from the
immutable source data.

## Limits

Slice images are still generated in the paint call (one plane, at most
2048² samples); only the volume view's work is in the background, and there
is no prefetch. The volume camera is orthographic without lighting or
gradient shading, and one channel is rendered at a time. Overlays show one
channel of a volume on the same grid; there is no resampling between grids.
The transfer table has 256 entries and the preset list is built in.
Qualifying the GPU path across drivers and platforms and measuring its
performance belong to WP-088 to WP-094.
