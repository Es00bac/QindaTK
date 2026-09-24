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
limited to `[-8,8]` EV. Signed power preserves negative lifted components
until output encoding; the deterministic 8-bit output quantizer clamps each
encoded channel to `[0,1]` and rounds to nearest byte. Alpha is copied without
grading. Processing refuses untagged input, unresolved profiles, invalid
parameters, unsupported working profiles/intents, and images above 16 MP.

The [color-grade fixture](../examples/color-grade/Main.qml) binds the wheel
requests to this processor and shows before/after images and bounded scope
snapshots. Its synthetic pixels demonstrate the transform only; this is not a
video decoder, broadcast/legal-range path, codec, calibrated display pipeline,
or undo/export integration. `ColorGradePreviewModel` is a fixture adapter;
production hosts should call the reusable processor with decoded tagged frames.

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
CMYK needs a channel-aware value contract. Managed canvas rendering
needs tagged object/image inputs and a declared composition space; converting
vertex colors before blending is not equivalent to converting the composite.
GPU/software agreement, display/compositor interpretation, gamut evaluation,
and transaction/undo integration remain unimplemented. The separate
[numerical gradient evaluator](gradient.md) supplies bounded linear-light
interpolation; it does not connect that pipeline to rendered previews.
