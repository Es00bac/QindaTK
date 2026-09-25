<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Numerical document color

`color_profile.h`, `color_context.h` and `color_engine.h` provide a C++ CPU
color conversion service. It is independent of Theme. `ColorPickerModel`
converts picker previews into typed document values; `GradientEditorModel`
uses the numerical gradient evaluator for its preview. Neither adapter
changes arbitrary QColor controls or canvas object painting by itself.
The canvas has a separate bounded Qt `QColorSpace` transform after sRGB tile
compositing; it does not yet consume `ColorEngine` or typed document values.

## Profiles and values

`ColorProfile::fromIcc(bytes, &error)` returns an immutable shared value that
owns a copy of its bytes, including when the input uses QByteArray::fromRawData.
Its ID is `icc-sha256:` plus the SHA-256 of those exact bytes. Descriptions
are labels, never identities. Persist the ICC bytes alongside document values;
resolving a hash is the application's responsibility.

Import accepts ordinary RGB input, display, output and color-space profiles
with XYZ or Lab PCS. It checks the ICC size, signature and tag bounds/alignment,
parses the container and requires a relative-colorimetric input or output
direction. Executable tags are validated when the requested transform is
constructed; successful import alone does not guarantee every direction works.
Profiles smaller than 132 bytes, larger than 16 MiB, truncated profiles and
profiles with trailing data are rejected. Gray, CMYK, device links, abstract
and named-color profiles are unsupported.

`sRgb()` and `linearSRgb()` obtain ICC bytes from Qt's named spaces and import
them through the same path. Their hashes identify the bytes produced by that
Qt version; applications must not assume a stable builtin hash across Qt
versions. Checked-in test profiles are independent, fixed ICC fixtures.

`ColorValue` stores straight-alpha RGB doubles. Alpha is linear coverage in
`[0,1]`; finite RGB outside `[0,1]` is valid. For this engine, `profileId` must
equal the source profile's `id()` and `transferId` must equal its `transferId()`
(`icc`). The ICC profile defines its full encoding, including transfer curves.
Mismatched identities fail validation; there is no additional transfer pass.
This encoding convention is specific to ColorEngine; ColorValue remains the
shared value record for other explicitly declared domain engines.

## Snapshot and conversion

Publish `ColorContextSnapshot` by value or `shared_ptr<const ...>` and increment
its generation when settings change. It carries document, working, display,
output and proof profile values. A request owns a copy of that snapshot and
an explicit resolved source profile matching its value. The document profile
records the host's default document encoding; it never overrides a value's
source tag.

| Operation | Destination | Implemented policy |
| --- | --- | --- |
| `Working` | snapshot working profile | Relative colorimetric, BPC off |
| `Display` | snapshot display profile | Same; numerical display encoding only |
| `Output` | snapshot output profile | Same; numerical output encoding only |
| `ProofPreview` | snapshot display profile, simulating the snapshot proof profile | Relative colorimetric soft proof, BPC off |

Other rendering intents and black-point compensation return `Unsupported` in
this slice. Proof settings never modify ordinary display/output operations.
Proof preview requires resolved proof and display RGB profiles, uses Little
CMS's soft-proof transform, and tags the result with the display profile. The
proof profile and proof intent are part of the compiled-transform cache key.
Missing selected profiles return `Unavailable`; no destination is guessed.
Malformed values return `InvalidArgument`, encoding mismatches `Validation`,
unsupported model/direction/policy `Unsupported`, and transform failures
`BackendFailure`, using `OperationError` with field and `lcms2` backend tags.

The result contains an optional ColorValue, error, operation, context generation
and `Float32` precision. No value is returned on failure. The host must compare
the returned generation against its live context before applying asynchronous
work; the synchronous engine does not read live document state.

RGB is narrowed to Float32 and transformed with Little CMS using no optimization
and no one-pixel cache. Components exceeding Float32's finite range are rejected.
Storage doubles do not imply double-precision transforms. Very small values may
underflow on narrowing. Alpha is copied exactly outside LCMS, including at zero;
hidden RGB is retained. No toolkit clamping or premultiplication occurs. ICC
LUTs may constrain their own domain, so extended-range preservation is only
verified for the supplied matrix/TRC fixtures. Nonfinite results fail.

`outsideNominalRange` reports whether returned RGB contains a component outside
`[0,1]`. `gamut` is always `Unknown`: neither nominal range nor successful
conversion establishes profile gamut, and this slice makes no clipping claim.

## CPU image grade

`ColorGradeProcessor::process(QImage, ColorGradeParameters, ColorContextSnapshot)`
is a synchronous, bounded CPU image transform for the host-defined wheel
coordinates supplied by `ColorWheels`. Input must carry a valid RGB ICC tag;
the processor converts it to `context.working`, applies the grade in linear
sRGB, then converts to and tags `context.output`. The working profile must be
the resolved `ColorProfile::linearSRgb()`. Output uses Qt's ICC conversion;
only relative-colorimetric conversion without BPC is currently accepted.

`ColorGradeParameters` provides `exposureEv` and lift/gamma/gain wheel points.
Each wheel point has finite x/y in `[-1,1]`. x moves red against green and y
moves blue against yellow in a zero-sum opponent basis:
`(x/2-y/3, -x/2-y/3, 2y/3)`. Processing order is lift (add 0.25 times that
vector), signed power gamma (exponent `2^(-wheelVector)` per channel), gain
(multiply by `max(0, 1+wheelVector)`), then exposure (`2^EV`). Exposure is
limited to `[-8,8]` EV. The grade runs on 32-bit float linear pixels
(`Format_RGBA32FPx4`): an 8-bit linear buffer maps sRGB codes 0–12 onto three
values and banded shadows even under a neutral grade. Graded linear values
clamp to `[0,1]` (SDR output) and the conversion to the output profile rounds
to the nearest byte. Alpha is copied without
grading. Processing refuses untagged input, unresolved profiles, invalid
parameters, unsupported working profiles/intents, and images above 16 MP.

The [color-grade fixture](../examples/color-grade/Main.qml) binds wheel gestures to `ColorGradeEditSession` and shows before/after images and bounded scope snapshots. The session accepts a host supplied tagged `QImage` and immutable context through `setFrame(clipId, frameId, image, context)`; QML can also bind `sourceImage` when frame ownership is elsewhere. It routes transient parameter maps through `EditTransaction` and `UndoService`, so finish records one complete grade state, cancel restores the committed state, and undo/redo rerender the same frame. A frame or context change cancels an open gesture. `scope` says what the grade belongs to: `FrameGrade` (the default, a still grade), `ClipGrade` or `ProgramGrade`. Leaving the scope (a new frame or clip for a frame grade, a new clip for a clip grade) starts a neutral grade with a fresh history so undo cannot affect a different target; a new target within the scope only replaces the preview source and keeps the grade and its history. Context generations must change whenever a profile or transform policy changes. `saveJson()`/`loadJson()` persist the committed grade as `qindatk.color-grade/1` (`format`, `scope`, `clipId`, `grade`); a load replaces the grade as a new document without an undo step and refuses malformed or out-of-range grades without changing anything. The fixture uses `ColorGradePreviewModel` only for deterministic source pixels.

## Grade gestures

`ColorGradeEditSession` publishes `wheels`, the `ColorWheels` descriptors for
lift, gamma and gain (current x/y, ranges `[-1,1]`, step 0.01, enabled once a
frame is set), `exposure` and `gradeSpace`, the declared space: linear sRGB
working space, opponent wheel offsets that sum to zero over R, G and B (so a
wheel changes chroma and never adds luma; exposure is the luma control). A
pointer drag calls `previewWheel(gestureId, wheel, x, y)` for each move and
`finishWheel(gestureId, wheel, x, y)` on release: one gesture, one undo step.
Numeric entry calls `setWheelValue(wheel, x, y)`, `setExposure(ev)` or
`resetWheel(wheel)`, each one undo step. Both paths build the parameter map
with the same `withWheel` step, so equal x/y give equal parameters and equal
pixels (`tst_color_grade_edit_session`). `ColorWheels.requestWheelValue`
emits the same preview/finish signals as a drag, so a host cannot tell
keyboard, numeric and pointer input apart. `committedParameters()` excludes an
open gesture's preview.

## Output grade (D-126)

The grade is part of the video output pipeline, not a still-image preview.
`MediaFinishing` is the last stage of a `MediaProgram`: `render()` applies it
to every frame it returns, so the program monitor, the render queue and
`MediaExport` get the same pixels. It grades with `ColorGradeProcessor` in
`MediaFinishing::gradeContext()` (the frame's own tag into linear sRGB, output
encoded sRGB, the SDR program policy of D-096) and then burns in captions. A
neutral grade is skipped, so it is an exact identity. `VideoFinishing` builds
the stage from a `ColorGradeEditSession` (and a `CaptionTrackModel`) and
`MediaPlayer.finishing` attaches it: the monitor shows `value(true)`, which
includes a drag in progress, while `exportJob` uses the committed grade.
Attaching a session switches it to `ProgramGrade`: a host feeding it each
presented frame through `setFrame` only changes the preview source, and the
grade, its undo history and the next export stay. A `ProgramGrade` session
previews in `ColorGradeProcessor::programContext()` (the same context
`MediaFinishing` grades with; the host's generation is kept) whatever output
profile the host's context names, because program frames and the lossless
export are encoded sRGB (D-096); a `FrameGrade` still keeps the host's output
profile. `tst_video_finishing_paths` checks the grade surviving new frames,
save/load, a Display P3 host context previewing the program's pixels, and
`exportJob` → program JSON → `MediaExport::renderProvider` writing the
committed grade and the embedded captions while a drag is open. The
program's cache key includes the finishing digest, so no ungraded cached frame
is shown after a change. `tst_video_finishing` checks that a decoded frame
graded by the program equals the session's preview pixel for pixel, and that
the lossless export decodes to the same pixels.

## Picker model and edits

`ColorPickerModel` is the QML-facing adapter for `Tk.ColorPicker`. It owns
typed `ColorValue` properties in a `PropertyDocument`, a selection of target
IDs and an `UndoService` history. Picker `QColor` previews are interpreted as
sRGB, converted by `ColorEngine` into the snapshot document profile, then
shown as transient `EditTransaction` previews. Finish commits the gesture as
one undo entry; cancel drops it. Multi-target previews apply the same
document-profile value to every selected target. Hosts can replace the default
sRGB snapshot with `setColorContext`; selection IDs must identify targets
owned by this model.

Every preview records the context generation. A changed generation cancels
the open gesture, and results whose generation no longer matches are refused.
`saveJson()` / `loadJson()` use the small `qindatk.color-picker/1` codec and
preserve typed channel values, alpha and profile/transfer IDs. ICC profile
bytes remain host-owned and must be resolved before loading a different
profile; this codec does not embed ICC resources. Color values remain straight
alpha, and conversion errors are exposed as `errorMessage` rather than
clamping or guessing.

`CanvasStyleModel.applyPicker(selectedCanvasIds, picker)` copies the first
selected picker's committed typed value and its document ICC into one atomic
canvas edit for the selected closed vector objects. `applyGradient(ids, model)`
similarly copies the committed `GradientEditorModel` definition, including
each stop's ICC resource. These fills are owned by the native canvas document,
reopen without an external profile registry, and participate in object undo.
A plain QColor fill replaces a managed fill. The renderer samples gradients
across the document-space object bounds and composites managed fills in sRGB
before converting the tile to the document output RGB ICC. The native output
ICC is persisted and becomes the default render display profile; a host can
set an explicit display context. The output path supports relative colorimetric
intent with BPC off and rejects other intents and soft proof requests through
`colorError`.

## Ownership, cache and build

`ColorEngine` is synchronous, noncopyable and safe for serialized concurrent
calls through its mutex. Call it from the GUI or worker domain, never a render
or realtime callback. Complete all calls before destroying the engine.
Its private LCMS context has a local diagnostic callback; it does not install a
global handler. RAII releases transforms before their context. Public headers
contain no LCMS handles or headers.

The LRU holds at most the constructor's requested transform count, clamped to
`[1,256]` (default 32). Keys contain both profile hashes, intent and BPC policy;
sample format and transform flags are fixed by this backend version. Generation
and operation do not alter the compiled transform when the destination is the
same. Entries retain profile values; eviction releases compiled transforms.
The bound is on entry count, not total LCMS allocation size. Import size is
separately bounded and callers should bound the number of loaded profiles.

CMake requires pkg-config and `lcms2 >= 2.14`, privately linked by the shared
qindatk library. This slice was qualified with LCMS 2.19; older supported API
versions still need platform qualification. Qt 6.8 remains the project minimum.
See [Gentoo packaging requirements](../packaging/gentoo/README.md).

## Verification and remaining boundaries

`tst_color_engine` compares the fixed sRGB/linear ICC fixtures against independent
piecewise sRGB equations, including both knees, neutral/colored values,
extended channels, exact alpha preservation and zero-alpha hidden RGB.
The declared absolute tolerance is `2e-6` against the fixtures’ encoded
16.16 transfer coefficients. The ideal sRGB 0.5 reference checks use `1e-5`
to account for ICC coefficient quantization (stored-profile decoding gives
`0.2140451968`, while ideal sRGB gives `0.2140411405`). It also checks
owned profile bytes, malformed containers, unsupported Gray/proof operations,
invalid values/encoding tags, destination tagging and conversion after cache
eviction. Fixture provenance and hashes are in
[tests/fixtures/color](../tests/fixtures/color/README.md).

Additional proofing intents and gamut warnings need reference fixtures.
CMYK needs a channel-aware value contract. Managed canvas solids and gradients
are rendered and saved; tagged image-object inputs, calibrated display/
compositor interpretation and gamut evaluation remain open. The reference
curve adapter's host constraint callback remains the boundary for
domain-specific transfer-curve policies (see [curve.md](curve.md)).
