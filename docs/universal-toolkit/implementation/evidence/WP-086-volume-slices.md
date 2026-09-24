<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-086 — Voxel volumes, calibrated slices and transfer functions

**State: ready for integration; package not accepted.** This record covers
the principal-plane slice of WP-086: the typed voxel provider, `ImageSliceView`
([EXT-07](../../proposed/09-specialized-domains.md#ext-07)) and
`TransferFunctionEditor` ([EXT-09](../../proposed/09-specialized-domains.md#ext-09)).
`VolumeView` ([EXT-08](../../proposed/09-specialized-domains.md#ext-08)) and
the 3D examples are not part of it.

Lane: `QindaTK-wp086-voxel-slices`, branch `work/wp086-voxel-slices`, base
`0174983`. The Sol sub-agent's uncommitted work is committed unchanged as
`9ab5a8d`. The review fixes, tests and docs are `880471b`; this record and
its captures follow.

## What the slice implements

- `Tk.VoxelVolume`: `uint8`, `uint16` or `float32` voxels with 1–64 channels
  and 1–4096 frames, whole-number sizes up to 8192 per axis, spacing, origin,
  a row-major orthonormal orientation (rotations and reflections) and units.
  Data lives in X-fastest 32³ little-endian chunks from `setVoxel`,
  `putChunk` or an on-demand `VoxelChunkSource`. Owned chunks plus an O(1)
  least-recently-used provider cache stay under `maximumResidentBytes`
  (default 64 MiB). A write into a provider chunk copies it first.
- Sampling follows decision D-086 (new file `docs/decisions-domains.md`):
  indices are cell centres, points sample within −0.5 … size − 0.5, `Nearest`
  picks the cell and `Linear` is clamped trilinear. Out-of-bounds, missing
  chunks, NaN voxels and missing `Linear` corners are `valid: false`.
- `Tk.VolumeCursor`: one continuous voxel point shared by all views, with
  `worldPoint`. It recentres when its volume takes a new shape.
- `Tk.ImageSliceItem`/`Tk.ImageSliceView`: axial, coronal and sagittal
  voxel-index planes over whole cells fitted by physical spacing, window/level
  or transfer mapping, linked crosshair, click, wheel, Page Up/Down, Home/End,
  arrow-key and chevron navigation, and item/voxel conversion for overlays.
- `Tk.TransferFunctionModel`/`Tk.TransferFunctionEditor`: 2–256 stable stops
  with opaque colors and separate opacity, `colorAt`/`opacityAt`, JSON-safe
  atomic settings, and an editor for adding, moving, removing and exactly
  editing stops. Edits never touch voxels.
- `examples/volume-imaging/Main.qml` (41 × 41 × 21 phantom, 0.8 × 0.8 × 1.6 mm
  voxels, centre at world 0) and the gallery's "Volume imaging" section.

## Defects fixed in review

- **Slice geometry.** The image rectangle spanned size × spacing, but pixels
  were mapped between the outer voxel centres. Edge voxels drew at half width
  and the in-plane aspect was off by up to one voxel per axis (2.5% for a
  40-slice coronal plane). Sampling, `voxelAt`, the new `positionForVoxel` and
  the crosshair now share the cell-centred mapping (D-086).
- **Editor layout.** A fixed 68 px height squeezed the gradient to 22 px and
  overlapped the value/opacity row. The editor is now sized to its content.
  Its swatch list passed bare colors to `ColorSwatches`, which logged
  `Unable to assign [undefined] to QColor`; it now passes `{name, color}`.
- **Provider writes.** `setVoxel` on a provider-backed volume created a zero
  chunk that hid the other 32,767 provider voxels. It now copies the chunk.
- **Validation.** Writes beyond the float32 range stored ±inf. Integer range
  checks ran before rounding. Fractional sizes and counts were silently
  rounded. `putChunk` multiplied unchecked chunk indices (signed overflow).
  A limit below one chunk was ignored. All of these are now rejected with
  `lastError`, and configure refuses a limit smaller than one chunk.
- **Cache cost.** Every cache hit did an O(n) list removal. Hits are now O(1).
- **Cursor.** A cursor bound before `configure` stayed at voxel (0, 0, 0).
  Every voxel edit also re-notified all linked views.
- **Navigation.** The documented arrow-key navigation did not exist. A
  horizontal wheel stepped a slice backwards, and each touchpad event stepped
  a whole slice.
- **Transfer model.** Stop colors kept an alpha that `colorAt` ignored while
  swatches showed it. A clicked-in stop reset opacity to 0.5. Saved settings
  held `QColor` values that JSON cannot carry. Rejected edits set no error,
  and the value field kept values the model had rejected.
- **qmllint.** `ImageSliceView.qml` had 67 findings: unqualified `Q_PROPERTY`
  types left its aliases untyped. The types are now namespace-qualified.
- **Repaint cost.** A `Nearest` repaint sampled every view pixel. It now
  samples each voxel once, and a cursor move regenerates only the planes whose
  slice changed. `Linear` still samples per pixel, so values are interpolated
  before color mapping; a QML pixel test guards this.

## Verification

Build and test environment: Qt 6.11.1, GCC 15.3, Debug, `-Wall -Wextra
-Wpedantic`, four build jobs as instructed for this run;
`QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software`.

- `cmake -S . -B build/wp086-voxel-slices` and
  `cmake --build build/wp086-voxel-slices --parallel 4`: exit 0 with no
  compiler warnings.
- `build/wp086-voxel-slices/tests/tst_volume_imaging`: 31/31 (13 functions,
  17 invalid-configuration rows, init/cleanup). The known answers cover
  world/voxel round trips for a rotation and a reflection, uint16 and float32
  little-endian bytes and range limits, sphere values at world points
  (220, 110, 0, and just beyond the volume), trilinear values, edge half
  cells, missing provider chunks, LRU eviction order, the resident cap,
  copy-on-write and cursor recentring.
- `build/wp086-voxel-slices/tests/tst_volume_slices`: 8/8. It covers whole-cell
  pixel ↔ voxel mapping, per-cell pixel values, the 2048 px cap, the physical
  aspect for 0.5 × 0.5 × 3 mm voxels, one cursor driving coronal and sagittal
  images, transparent missing samples, transfer interpolation, clamping and
  validation, and a JSON round trip.
- `build/wp086-voxel-slices/tests/tst_qml -input tests/qml/tst_volume_imaging.qml`:
  8/8. It checks documented sizes, parts and accessibility, cursor adoption,
  a click moving the shared cursor and the linked coronal slice, keys, wheel
  accumulation, ignored horizontal scroll and chevrons. Its pixel test shows
  a `Linear` midpoint mapped from value 165 (red), not a blend of the core
  and shell colors. Editor edits through the gradient, value field, opacity
  slider, swatch popover, custom-color signal and Delete leave
  `VoxelVolume.revision` unchanged.
- `ctest --test-dir build/wp086-voxel-slices -R volume`: 3/3
  (`tst_volume_imaging`, `tst_volume_slices`, `example_volume-imaging`).
- Related checks: `example_gallery`, `tst_layout`, `tst_layout_intrinsic` and
  `tst_theme` passed. QML runners passed `tst_gradient_editor` 9/9,
  `tst_gallery_section_matrix` 4/4, `tst_controls_office` 12/12 and
  `tst_controls_input` 54/54.
- Full suite, `ctest --test-dir build/wp086-voxel-slices -j4`: 252/253. The
  one failure is inside `tst_qml` (768 passed, 1 failed):
  `FormatDialogs::test_operation_scoped_formats_and_safe_preflight` expects
  2 formats and gets 3. The lane base `0174983` already contains the
  provider registration (2c9539d) and that stale expectation. Integration
  commit `05c0314` fixes the test, and this lane touches neither file.
- `cmake --build build/wp086-voxel-slices --target all_qmllint --parallel 4`:
  no findings in `ImageSliceView.qml` or `TransferFunctionEditor.qml` (67 and
  0 as found). The 2485 remaining module findings are in other files and did
  not change. The example, gallery section and QML test each lint clean with
  `qmllint --bare -I build/wp086-voxel-slices/qml -I /usr/lib64/qt6/qml`.
- `python3 tools/scripts/gen_catalog.py`: wrote `docs/catalog.json`
  (449 types).

## Visual evidence

Captured with
`build/wp086-voxel-slices/tools/preview/qtk-preview examples/volume-imaging/Main.qml --offscreen --size 1280x800 [--density comfortable] --wait 1000 --grab …`
and opened for inspection.

- [Compact](WP-086-volume-compact.png) and
  [comfortable](WP-086-volume-comfortable.png): the three planes show round
  sphere cross-sections (core 220, shell 110) even though coronal and
  sagittal voxels are twice as tall. The cyan crosshair crosses the centre of
  each plane. The editor shows the selected `shell` stop (value 110.0,
  opacity 0.90). The readout is `World point: 0.00, 0.00, 0.00 mm · Cursor
  value 220 a.u.`.
- [Moved cursor](WP-086-volume-cursor.png), produced by
  [WP-086-volume-cursor.qml](WP-086-volume-cursor.qml): the shared cursor at
  world (4.0, −3.2, 3.2) mm moves axial to slice 13/21, coronal to 17/41 and
  sagittal to 26/41. The smaller cross-sections and the crosshairs agree
  across views (voxel 25, 16, 12, shell value 110).

Representative `--dump` lines (compact, then comfortable):

```
ImageSliceView#phantomAxial  0,0 413x595
  Mono#imageSliceIndex  307,5 46x15  "11 / 21"
  ImageSliceItem#imageSliceImage  0,0 405x546
  Caption#imageSliceCoordinate  0,574 405x13  "Voxel 20.0, 20.0, 10.0 · 220 a.u."
TransferFunctionEditor#phantomTransferEditor  12,664 1256x103
  GradientEditor#transferGradient  0,19 1240x40
  NumberField#transferPointValue  28,0 566x24
TransferFunctionEditor#phantomTransferEditor  14,641 1252x122
  GradientEditor#transferGradient  0,20 1232x48
```

As found, the editor dumped as `12,699 1256x68`, with
`GradientEditor#transferGradient 0,12 1240x22` and
`NumberField#transferPointValue 28,-5 566x24` overlapping it.

## Known limits and remaining WP-086 scope

- Views are 2D voxel-index planes. There is no oblique, world-axis or
  patient-axis reslicing, and there are no overlays with geometry checks.
- There is no `VolumeView`: no GPU raycasting, clipping, camera, picking or
  GPU capability fallback. The scientific 3D scatter/surface/volume examples
  are still missing.
- Provider reads are synchronous during repaint, with no asynchronous
  prefetch or cancellation. Slices rasterize on the CPU; `Linear` costs up to
  eight voxel reads per view pixel, with the image capped at 2048 × 2048.
- Transfer edits are direct model calls. They are not undoable typed property
  transactions. There is no histogram, preset library or opacity curve
  display, and swatches are theme colors; hosts supply a full picker through
  `customColorRequested`.
- Decision number D-086 is provisional. It is unused on the integration
  branch at `898da32`. Expect merge conflicts in `docs/catalog.json`,
  `docs/controls.md`, `docs/index.md` and `examples/gallery/Main.qml`.
