<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Volume imaging: voxel data, slices and transfer functions

`Tk.VoxelVolume` is a typed, spatially calibrated voxel source.
`Tk.ImageSliceView` shows one of its principal voxel planes: axial (X across,
Y down), coronal (X across, Z down) or sagittal (Y across, Z down). Give
related views the same `Tk.VolumeCursor` to link the crosshair and slice
position. `Tk.TransferFunctionModel` maps measured scalar values to color and
opacity; `Tk.TransferFunctionEditor` edits that settings model without
changing source voxels.
[`examples/volume-imaging/Main.qml`](../examples/volume-imaging/Main.qml)
shows a known sphere phantom with anisotropic voxels; the gallery's "Volume
imaging" section shows two linked planes.

## Voxel contract

Call `configure({sizeX,sizeY,sizeZ,voxelType,spacing,origin,orientation,unit,
valueUnit,channels,frames})` before filling data. Sizes are whole numbers
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
a snapshot: cache fills during reads do not emit `changed()`.

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
evaluates the measured-value transfer function directly. Missing samples are
transparent. Without a cursor the view shows its middle slice.

Planes are voxel-index planes. The view does not resample into world or
patient axes, so a flipped or rotated orientation shows the stored index
order; `worldPoint` and the readouts give physical positions.

A click moves the shared cursor to that point, clamped to the outer voxel
centres, and emits `pointSelected`. The wheel (one slice per 120 angle units;
horizontal scrolling passes through), Page Up/Down, Home/End and the header
chevrons step whole slices. Arrow keys move the crosshair one voxel within the
plane. A cursor without a volume adopts the view's volume. For overlays,
`ImageSliceItem.voxelAt(x, y)` and `positionForVoxel(x, y, z)` convert between
item pixels and continuous voxel indices.

## Transfer functions

`TransferFunctionModel` holds 2–256 stops with stable IDs, strictly
increasing measured values, opaque RGB colors and a separate 0–1 opacity.
Endpoints stay at the domain bounds; inner stops can be added, moved between
their neighbours, recolored, changed in opacity or removed. A rejected edit
changes nothing and sets `lastError`. `colorAt` and `opacityAt` interpolate
RGB and opacity linearly; values outside the domain clamp to an endpoint and
non-finite values are transparent. `saveSettings()` returns JSON-safe data
(`#rrggbb` colors) that `restoreSettings()` validates atomically. These are
view settings, kept apart from the immutable voxel data.

`TransferFunctionEditor` shows a gradient over the domain. Clicking the track
adds a stop at the current mapping, so the rendered result does not change
until the stop is edited. Drag or arrow-key a stop to move it; Delete or a
right-click removes it. The value field, opacity slider and color swatches
edit the selected stop exactly, and a rejected value snaps back to the stored
one. Hosts can connect `customColorRequested` to their color picker.

## Limits

This slice provides 2D principal-plane views and an out-of-core provider
boundary. Oblique reslicing, GPU volume raycasting (`VolumeView`), clipping,
camera and picking, asynchronous prefetch and cancellation, overlays with
geometry checks, undoable transfer transactions, histograms and the
scientific 3D scatter/surface/volume examples remain WP-086 work.
