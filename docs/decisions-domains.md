<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Domain decisions

Continuation of [decisions.md](decisions.md) for specialized domain packages
(scientific imaging, audio and similar) and for later application decisions,
such as the rich-text publishing structure (D-089 to D-091, D-104), since
[application decisions](decisions-application.md) is full;
[service decisions](decisions-services.md) hold the others.

## D-086 — Voxel indices are cell centres; slices show whole cells

**Context.** The first slice view fitted its image to size × spacing but
mapped pixels between the outer voxel centres, so edge voxels drew at half
width and the in-plane aspect drifted by up to one voxel per axis (2.5% for a
40-slice, 3 mm coronal plane). Sampling, clicks and the crosshair need one
convention shared with future volume rendering.

**Decision.** A voxel index is the centre of a cell one spacing wide, as in
ITK and NIfTI. A volume samples continuous indices within −0.5 … size − 0.5;
`Nearest` picks the containing cell and `Linear` clamps to the outer centres
inside the edge half cell. A slice rectangle spans whole cells, and
`sliceImage`, `voxelAt`, `positionForVoxel` and the crosshair share that one
mapping. The cursor stays within the outer centres.

**Consequences.** Every voxel draws at its physical size and a click inside
an edge cell selects that voxel. World positions are unchanged (`world =
origin + orientation × index × spacing`). Views stay in voxel-index planes;
oblique or patient-axis reslicing needs its own resampling contract.

## D-088 — A queue-driven realtime engine with built-in silent devices and optional ALSA

**Context.** WP-060 needs a functional audio engine: real device I/O,
recording, offline render and measured callbacks, without making core QindaTK
depend on an audio runtime, and with automated checks that never play sound.
The machine offers ALSA 1.2, PipeWire 1.6, PulseAudio and Qt Multimedia.
PipeWire needs a running server and silent tests would add nodes to the
user's live graph; Qt Multimedia hides the callback and has no null device;
ALSA runs without a server, reaches PipeWire through its `default` PCM, and
its null/file plugins give real alsa-lib I/O into files.

**Decision.** The engine core (`AudioEngine`, `AudioEngineProcessor`,
`AudioRenderGraph`, `AudioSession`) is always built with two software
reference devices, `builtin:null` and `builtin:loopback`, paced by the
monotonic clock. ALSA is an optional backend (`QINDATK_ENABLE_ALSA`, used
only when `libasound` links), linked privately like Poppler. The device
thread only runs `process()`; it talks to the GUI through bounded lock-free
SPSC rings and atomics, adopts immutable graphs by pointer and never frees
them; devices open and close on a worker thread. Media decode is RIFF/WAVE in
core; the session reuses `TimelineModel`, `AutomationModel`/`EngineSchedule`
and `ResourceResolver` rather than defining its own edit, time or media
identity model.

**Consequences.** Core consumers build and test without any audio library;
with ALSA, `libasound` becomes a runtime dependency of `libqindatk`. The same
render function feeds devices and files, so offline output is the reference
for device output. Other platform backends (PipeWire native, JACK, WASAPI,
CoreAudio) implement `AudioDeviceBackend` and are qualified in WP-088; they
change no engine, session or QML contract.

## D-089 — Materialize style values and record direct overrides

**Context.** WP-051 needs named paragraph, character and table styles with
inheritance, where a parent change reaches every dependent paragraph while
direct formatting survives. `ParagraphFormat` members are concrete values
read by pagination, lists, HTML, the format state and the codec; changing
every reader to resolve styles at read time would touch the whole rich-text
stack and make an older file unreadable in meaning.

**Decision.** A styled paragraph stores the resolved paragraph values plus
`overrides`, the style-controlled members set directly. A style change is
one transaction: `StepStyle` and a `StepParagraphFormat` for each paragraph
of the style or a style inheriting from it; overridden members keep their
values. Character members are not materialized: `CharFormat` already
inherits through empty values, so `effectiveCharFormat` fills a run from its
character style, then its paragraph's style, before heading and document
defaults. Table styles resolve at layout time. Applying a paragraph style
clears overrides; removing one keeps the values as direct formatting.

**Consequences.** Every existing reader keeps working unchanged, style
changes re-shape only the affected paragraphs, and undo is exact. A direct
value equal to the style's is still an override, reported by
`directOverrides` and cleared by `resetDirectFormatting`. Files repeat the
resolved paragraph values, so an older reader shows the look without the
style. See [publishing.md](publishing.md#styles).

## D-090 — Merge cells over a fixed slot grid

**Context.** Merged cells must survive row and column edits, undo and
persistence with stable cell identities, and must paginate when a merged
cell is taller than a page.

**Decision.** Tables keep one slot per row and column. A merged cell is its
top-left slot with spans; covered slots keep their IDs and hold no
paragraphs. `mapTableGrid` validates spans on every commit and load. Merge,
split and grid edits are `StepTable` replacements that share unchanged
paragraph objects. Layout groups rows joined by merged cells, keeps a group
on one page when it fits, and otherwise breaks between rows, continuing a
crossing merged cell at its first unshown line; a row grows only for cells
ending in it.

**Consequences.** Split restores the covered slots' IDs, and deleting a
merged cell's first row or column moves its ID and content on. Merged cells
cannot cross the header boundary. A cell continued into a region of another
width keeps the table's column widths there until the next row break, since
its continuation line indexes belong to one width. See
[publishing.md](publishing.md#merged-cells).

## D-091 — Flow one story through page regions

**Context.** Linked text frames and page columns place the same flow in
several rectangles per page, often of different widths, while pagination,
page identity, mapping and incremental invalidation are built on
`FlowBuilder` filling one content box.

**Decision.** A page is a list of regions: page columns, or the columns of
the story's frames in thread order (`RichTextLayout::regions`). One
`FlowBuilder` fills each region and page items carry the region's origin and
width. The document is one story that starts in `FrameSet::storyStart`; text
after the last frame is overset. A paragraph continued into a region of
another width keeps its earlier line breaks through `WidthBreak` (one
`QTextLayout` whose line widths change at the break). Frames are one
`StepFrames` per change.

**Consequences.** Frame and column changes are undoable and persist in
version 2, and text is never duplicated or lost across regions. A frame
layout has exactly the frames' pages, reuses a page only at its own index,
and puts page objects in page coordinates. Multiple stories, inline frames
and text wrap around frames are not modelled. See
[publishing.md](publishing.md#text-frames).

## D-104 — Write richtext version 2 only for publishing structure

**Context.** Merged cells need cells without paragraphs, which a version-1
decoder rejects, and styles and frames change the meaning of a file, so
these members cannot stay additive members of version 1 like `columnIds`.

**Decision.** `RichTextCodec::CurrentVersion` is 2 and decoding accepts
versions 1 and 2 for documents and fragments. Writers (the canonical codec
and the session adapter) emit version 2 only when a document has styles,
text frames, page columns, merged cells or a table style
(`requiredVersion`); otherwise they emit version 1, byte for byte as before.

**Consequences.** Documents without publishing structure still open in
version-1 readers, and every existing canonical encoding is unchanged. A
version-2 file tells an older reader that it cannot be read faithfully
instead of being misread. See [publishing.md](publishing.md#persistence).

## D-138 — Scientific data is float64 with explicit gaps and exact units

**Context.** WP-085 plots measurements whose precision, gaps and units must
survive display and export. Chart models accept loose numbers, treat NaN
variously and sometimes rescale; an exported figure that silently converted
millivolts to volts, or drew a missing reading as zero, would be wrong in a
way no reader can see.

**Decision.** `ScientificDataset` and `ScientificGrid` store IEEE float64,
reject every non-finite input, and represent a gap only by an explicit
`missingY` flag or a `null` node. Uncertainty is host-supplied nonnegative
offsets, symmetric or asymmetric, in X and Y; the toolkit estimates nothing.
Trace series have strictly increasing X; scatter series accept any X. Units
are exact strings: a figure whose series, surface or annotations disagree
with the axis units refuses to draw and names the mismatch, instead of
converting. Replacement is atomic and bounded (100,000 samples; 1,048,576
grid nodes).

**Consequences.** A figure shows exactly the numbers given and states its
gaps. Hosts convert units and window very large arrays before handing them
over; out-of-core storage is not part of the models.

## D-139 — Figures downsample by envelope and record how

**Context.** A 100,000-sample trace cannot be painted point by point at
interactive rates, and stride sampling hides the spikes and gaps that
scientific readers look for.

**Decision.** A trace over `paintPointLimit` keeps, per equal index bucket,
its first, minimum, maximum and last present sample and its first gap (an M4
envelope, at most five per bucket). Scatter keeps every k-th sample plus the
extreme-X/Y samples. Surfaces are sampled once per raster pixel centre in a
raster of at most 1,024². The method and painted counts are part of the
export metadata, and `paintedIndices()` exposes the exact selection.

**Consequences.** Extremes and gaps are always visible; line shape between
them is approximate at high compression, which the metadata declares.
Performance figures for large data belong to WP-088.

## D-140 — One scene object feeds GPU 3D, software 3D and export

**Context.** Qt Quick 3D gives real shaded surfaces, but it needs an RHI
backend: headless tests and the software adaptation cannot render it, and a
figure exported from GPU pixels would differ by driver.

**Decision.** `ScientificScene3D` (C++) owns the data-to-scene transform
(data Z up, a ±100 cube), the explicit interpolation lattice with holes at
missing nodes, and an orbit camera expressed as position, quaternion,
vertical field of view and clip planes. `ScientificGeometry3D` uploads its
snapshot to Qt Quick 3D with the camera bound to those properties, and the
software view and PNG/SVG export project the same vertices with the same
matrices (depth-sorted faces, headlight shading). Coordinates are float32
only inside the normalized cube; data stays float64.

**Consequences.** Projection, picking, interpolation and export are testable
headless with exact numbers, and 3D figures export reproducibly on any
machine. The GPU image adds lighting and antialiasing differences only;
comparing it with the export belongs to WP-092's output-fidelity work.

## D-141 — Exports embed complete provenance and are byte-reproducible

**Context.** EXT-06 requires reproducible figures with data and parameter
provenance; the first slice embedded one series' metadata in PNG/SVG.

**Decision.** 2D figures, composed panels and 3D scenes embed one JSON
document (PNG text chunk, SVG `<metadata>`): every source's identity, units,
precision, shape, host parameters and full samples or a SHA-256 of the
lattice, plus all render parameters (axes, viewport, sampling, interpolation,
colour domain, camera, size). Painting is deterministic for identical inputs,
size and fonts, so repeated exports are byte-identical. A composite embeds
each panel's full metadata under its (a), (b) label.

**Consequences.** A figure file alone identifies and verifies its data.
Large series make large files, since every sample is embedded; hosts that
need small files export `csvData()`/`metadataJson()` sidecars instead.
