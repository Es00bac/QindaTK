<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Volume imaging: voxel data, slices and transfer functions

`Tk.VoxelVolume` is a typed, spatially calibrated voxel source for the three
principal planes. `Tk.ImageSliceView` shows its axial (XY), coronal (XZ) or
sagittal (YZ) plane. Give related views the same `Tk.VolumeCursor` to link the
crosshair and slice position. `Tk.TransferFunctionModel` maps measured scalar
values to color and opacity; `Tk.TransferFunctionEditor` edits that settings
model without changing source voxels. See
[`examples/volume-imaging/Main.qml`](../examples/volume-imaging/Main.qml) for
a known sphere phantom.

## Voxel contract

Call `configure({sizeX,sizeY,sizeZ,voxelType,spacing,origin,orientation,unit,
valueUnit,channels,frames})` before filling data. Dimensions are X-fastest and
limited to 8192 per axis, 64 channels and 4096 frames. `voxelType` is `uint8`,
`uint16` or `float32`. Chunks are 32×32×32, tightly packed in X-fastest order;
multibyte values are little-endian. C++ hosts can supply complete chunks through
`putChunk()` or attach a `VoxelChunkSource` for on-demand reads. The default
resident limit is 64 MiB; `maximumResidentBytes` bounds owned chunks plus the
read cache. Provider chunks use least-recently-used eviction. Unwritten owned
voxels read as zero; failed provider reads are missing.

`spacing` and `origin` each have three finite values. Spacing is positive.
`orientation` is a row-major 3×3 orthonormal matrix, including valid axis
flips. Spatial coordinates follow
`world = origin + orientation × (voxelIndex × spacing)` in `unit`. Use
`worldFromVoxel` and `voxelFromWorld` to convert. `sample` returns `{valid,
value,unit}`; out-of-bounds or missing values have `valid:false`. `Nearest`
rounds voxel coordinates and `Linear` is trilinear interpolation. No implicit
intensity normalization or unit conversion occurs in the data model.

## Slice and transfer controls

`ImageSliceView` exposes volume, cursor, transfer, plane, channel, frame,
interpolation and window center/width. It fits the plane by physical spacing,
caps its generated image at 2048 pixels per side, and lets users click to move
the shared cursor or wheel/arrow through planes. With no transfer model it uses
window/level grayscale; with a transfer model it evaluates its measured-value
domain directly. Missing samples remain transparent. The crosshair is in
continuous voxel coordinates; slice navigation uses integer indices.

`TransferFunctionModel` holds 2–256 ordered points with stable IDs, measured
value, color and opacity. Endpoints stay at the domain bounds; inner points can
be added, moved, recolored, changed in opacity or removed. `saveSettings()` and
`restoreSettings()` serialize that view setting separately from immutable voxel
data. The editor provides the gradient drag surface, precise value/opacity
fields and color swatches. Hosts can connect `customColorRequested` to their
color picker. Transfer colors linearly interpolate RGB and opacity; values
outside the domain clamp to an endpoint, and non-finite values are transparent.

This slice provides 2D reslices and an out-of-core provider boundary. GPU
volume raycasting, clipping, camera/picking and scientific 3D examples belong
to the separate `VolumeView` slice of WP-086.
