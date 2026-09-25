<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Domain decisions

Continuation of [decisions.md](decisions.md) for specialized domain packages
(scientific imaging, audio and similar) and for later application decisions,
such as the rich-text publishing structure (D-089 to D-091, D-104) and its
equations, references and review (D-114 to D-117), since
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

## D-114 — Equations are inline objects with a TeX-subset source

**Context.** Documents need editable equations that lay out, print, persist
and undo with the text. A full TeX or MathML engine is far larger than the
toolkit's scope, and inferring meaning from styled plain text would not
round-trip.

**Decision.** An equation is an inline `RichObject` of kind `equation`. Its
`source` is the canonical source of a documented TeX subset and its
`altText` is the generated linear text. The toolkit parses the subset into
an AST, writes a canonical source (`parse(canonicalSource(n)) == n`), lays it
out with a small TeX-like box model, and paints it in the page painter
before any host object renderer is asked. Anything outside the subset is
refused as `Unsupported`, and syntax errors as `InvalidArgument`, each with
the source offset. The object's size comes from the layout at the document's
default size, and painting scales the layout into the object's box.

**Consequences.** Equations reuse object anchoring, pagination, selection,
copy, PDF output and `StepObject` undo, and need no new schema version: an
older reader shows a labelled object with its linear text. Matrices,
environments, accents and font commands are not supported. Equation size
does not follow later changes to the default size until the source is set
again. See [references-review.md](references-review.md#equations).

## D-115 — Field results are rewritten inside the committing transaction

**Context.** Captions, cross-references, citations and bibliographies show
numbers that depend on the whole document's order. If numbering were a view
effect, the saved and printed text would disagree with the screen. If it
were a separate update step, undo would leave numbers out of date.

**Decision.** A field is a run of characters whose format names a
`ReferenceField` record. `commitTransaction` calls `reconcileFields`, which
recomputes every result in one pass over the document. It appends a
`StepText` for each result that changed, and strips the field mark from any
second run of a field, before the transaction is recorded. The
after-selection is mapped through those steps. A reference whose target is
gone reads `[?]` and is reported broken. Results are atomic under typing,
deletion and paste.

**Consequences.** A renumbering is part of the edit that caused it: it
undoes, redoes, persists and repaginates with that edit, and a reference
never silently points at another caption. Each commit in a document with
fields costs one scan of its spans. Footnotes are refused because pagination
has no note area.

## D-116 — Review ranges are character-format marks

**Context.** Comment ranges and tracked changes must follow edits before and
inside them, overlap freely, survive undo exactly, and keep other reviewers'
changes when one change is accepted or rejected. Position anchors that edits
map forward cannot be restored exactly by undo once a range has collapsed.

**Decision.** `CharFormat` carries sorted `commentIds`, an `insertionId` and a
`deletionId`, which name `CommentThread` and `TrackedChange` records in the
document's `Annotations`. Records change through `StepAnnotation`. With
tracking on, insertions mark new text and deletions mark text instead of
removing it, except one's own insertions. Accept and reject are ordinary
`StepSpans`/`StepText` steps plus the record step. A change left without
characters after another change's text was removed goes with it.

**Consequences.** Ranges move, resize, overlap and undo exactly with no
anchor bookkeeping, and a cut and paste carries a comment along. Formats
multiply by the combinations of marks in use. Paragraph marks and formatting
changes are not tracked. See [references-review.md](references-review.md#review).

## D-117 — Annotations are richtext version-2 members

**Context.** Fields, sources, comments and changes are new saved structure.
D-104 writes version 2 only for documents that use publishing structure, so
version-1 files stay byte-identical.

**Decision.** The records are the version-2 members `fields`, `sources`,
`comments` and `changes`, sorted by ID and written only when non-empty. The
marks are the format members `field`, `comments`, `insertion` and
`deletion`. `requiredVersion` returns 2 when a document has any record. The
decoder drops a mark naming no record of its kind with a warning, as it does
for character styles, and fails on invalid records with their path. The
session adapter writes the same members, and fragments carry marks for the
pasting document to check.

**Consequences.** Documents without annotations keep their version-1 bytes.
Orphaned comments and cut fields keep their identity across a save. A
WP-051 version-2 reader reports the new members as ignored and loses them;
both readers ship in the same round. See
[references-review.md](references-review.md#persistence).

## D-130 — One output description for preview, files and printers

**Context.** WP-053 needs preview, PDF export and printer output to agree
page for page. The first PDF export painted layout pages straight into a
writer, and a preview drawn some other way could drift from it.

**Decision.** `Print::sheetFor` is the only source of output geometry: the
sheet size, the page's trim box on it, the scale, bleed and crop marks. It is
a pure function of the page setup, the ticket and the paper.
`Print::paintSheet` is the only painting path; every device (the preview
image, `QPdfWriter`, the system print engine) gives it a painter in 1/96-inch
units, the unit `RichTextLayout` already paints in. `exportPdf` is the same
path with the default file ticket.

**Consequences.** A test device that rasterises job output matches the
preview pixel for pixel, and PDF media boxes match the plan. Devices never
scale or place pages themselves. A new device only opens, advances and
finishes sheets. See [printing.md](printing.md#one-output-description).

## D-131 — Print jobs print a snapshot, one sheet per turn

**Context.** The document is GUI-thread only, and a user may keep editing
while a long job prints. Cancel must work mid-job, and a failure must not
lose the settings or pretend success.

**Decision.** `print()` copies the document through its own codec and lays
out the copy; the job paints that copy one sheet per event-loop turn.
`cancel()` is a request that the next turn honours by aborting the device.
Devices commit only in `finish()` (QSaveFile for PDF, a `.part` file for the
system engine, the CUPS spool), so an aborted job leaves no output; a device
that streams reports `deliveredSheets()`. A failed or cancelled job names the
pages that did not reach paper or disk, and the ticket stays unchanged.

**Consequences.** Editing during a job is safe, and the GUI stays responsive
between sheets. Copying costs one encode and decode (about 50 ms for 500
pages). "Completed" for a printer means handed to CUPS; tracking the job on
the printer is not part of this package.

## D-132 — Preflight issues have derived IDs and validated repairs

**Context.** `PreflightPanel` keys its selection on issue IDs, and OFF-13
asks that issues clear after a validated repair.

**Decision.** An issue ID is the check plus its target
(`font-missing:<family>`, `image-missing:<objectId>`, `output-<field>`), so a
re-run finds the same ID. `PrintAdapter.repair` applies one action, as one
document transaction or one ticket change, then runs preflight again. It
succeeds only if the ID is gone; otherwise it undoes the transaction,
restores the ticket and reports failure. Checks that need the author's
judgement (overset text, low resolution, open review marks) navigate instead
of repairing.

**Consequences.** A repair can never leave a half-fix behind, and panels can
keep their selection across runs. A host with external undo
(`RichTextDocument.externalUndo`) must accept that a failed repair's undo
goes through its own history. See [printing.md](printing.md#preflight).

## D-133 — Bleed is page setup; page geometry stays document-wide

**Context.** WP-051 refused bleed and section/page scopes in
`applyPageSetupMm` and passed both to WP-053.

**Decision.** Bleed is a page property: `PageSetup.bleedPt` (0–72 pt),
saved in `qindatk.richtext` version 2 and set through `applyPageSetupMm`. A
ticket chooses whether sheets include it and crop marks. Section and page
scopes stay refused (`unsupported`): `RichTextLayout`, `RichTextArea`, the
page model and the tiles all assume one page size, and changing that is a
layout project of its own, not output work.

**Consequences.** Documents without bleed keep their version-1 bytes.
`PageSetupDialog` can offer bleed now; its scope choice still gets
`unsupported` for anything but `document`. Per-section geometry has no owning
package yet; it needs a plan decision before any work starts.

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

## D-146 — One raycasting algorithm, specified by a CPU reference

**Context.** `VolumeView` must draw real volumes on the GPU, yet every
automated check runs on Qt's software scene graph, which has no shaders and
no 3D textures. A GPU-only renderer would be untested; two independent
renderers would drift apart.

**Decision.** `VolumeRaycaster` (C++) defines the algorithm and the GPU
fragment shader re-implements it line for line: an orthographic orbit camera
(yaw about world Y, pitch about the camera's right axis; yaw 0, pitch 0 looks
down world −Z with +X right and +Y up), zoom 1 fitting the volume diagonal to
the shorter view side, rays through pixel centres clipped to the (clip) box
of whole cells in continuous voxel indices, samples at
`t = tNear + (k + ½)·step` with `step = smallest spacing / samplingRate`,
values interpolated before classification (D-086), a 256-entry RGBA8
transfer table looked up linearly, opacity correction
`1 − (1 − α)^(1 / samplingRate)`, front-to-back compositing with early
termination at 0.99, and a maximum-intensity mode that classifies the
largest sample. Missing samples contribute nothing. Picking marches the same
ray to 0.5 accumulated opacity (or the maximum sample).

**Consequences.** Tests pin exact pixel values of known phantoms on the
reference path, and a GPU run compares against the same image within
filtering precision. A change to how a volume looks must land in
`volume_raycast.cpp` and `volume_raycast.frag`; AGENT-CONTRACT markers in both
say so. The transfer table quantizes colours to 8 bits, so volume colours can
differ from `colorAt` by one level. Perspective projection, shading and
gradient lighting are not part of this contract.

## D-147 — The GPU volume path falls back to the reference image, and says why

**Context.** The GPU path needs an RHI scene graph, the compiled shaders
(Qt Shader Tools is optional), 3D textures, a sampleable voxel format and a
texture within the device's size and a memory budget. Provider volumes may
also have missing chunks, which a texture cannot represent.

**Decision.** Each frame `VolumeViewItem` probes these in a fixed order and
draws the GPU node only when all hold; otherwise it draws the CPU reference
image, capped at 192² rays and scaled up, and publishes `renderPath`
(`gpu` or `reference`) and a one-sentence `fallbackReason`. `renderer:
Reference` forces the reference path. The upload packs one channel and frame
chunk by chunk through the volume's bounded read cache into R8, R16 or R32F
texels (value = texel × 255, 65535 or 1) and drops its staging copy after the
upload; `maximumTextureBytes` (256 MiB) bounds that copy and the texture.
Every QRhi resource lives in `VolumeRenderNode` on the render thread.

**Consequences.** Headless tests always see the reference path, with its
reason, and the item stays usable on any backend. The 3D texture side is
capped at 2048 because QRhi reports only the 2D limit. Qualifying the GPU
path across drivers and its performance are WP-088 to WP-094 work.

## D-148 — Transfer edits are typed history steps; gestures are transactions

**Context.** Transfer functions are view settings, but users expect a stop
drag or an opacity slide to undo as one action, Escape to cancel a drag, and
a document session to own the history like any other model.

**Decision.** `TransferFunctionModel` is a `DocumentModel`. Every committed
edit records one immutable step holding the stop lists before and after (at
most 256 stops), keyed by the model's document ID and reset generation, in
its own `UndoService` until a session adopts it. `beginEdit(label)` opens a
gesture: later edits publish `changed()` for live views but record nothing,
`commitEdit()` records one step from the gesture's start and `cancelEdit()`
restores that start without a step. `configure()` and `restoreSettings()` are
loads and reset the history. The editor maps drags, keyboard nudges and
slider presses onto gestures.

**Consequences.** Views repaint during a drag at no history cost, undo never
lands mid-gesture, and a stale step cannot apply after a reload. `committed()`
tells persistence when a real change happened.

## D-149 — Oblique reslices are square world frames through the cursor

**Context.** Principal planes show voxel-index order, so a rotated or
reflected volume, or an arbitrary cut, needs a world-space reslice with its
own geometry for clicks, the crosshair and navigation.

**Decision.** `ImageSliceItem.plane: Oblique` samples the plane through the
shared cursor whose right, down and normal directions are the columns of
`resliceOrientation` (an orthonormal world matrix; identity shows world X
right and world Y down). The image is a square one volume diagonal wide,
centred on the volume centre projected onto the plane, fitted into the item
and sampled once per item pixel with the view's interpolation. Slices step
by the smallest voxel spacing along the normal; arrow keys move the cursor
by that step within the plane.

**Consequences.** In-plane cursor moves never pan the image, so the image
depends only on the plane's offset and is cached on it. The frame covers the
whole volume at any angle, leaving blank corners for thin volumes. Principal
planes keep their whole-cell geometry (D-086).

## D-158 — Slide placeholders inherit per property with explicit override flags

**Context.** WP-056 needs master and layout edits to reach dependent slides
while a deliberate per-slide change survives them (OFF-21). Copying master
values into slides loses the link; a single "overridden" flag per object
throws away inheritance of the parts nobody touched.

**Decision.** A slide object bound to a placeholder role stores its own
bounds, text and fill plus three flags (`ownBounds`, `ownText`, `ownFill`).
Resolution takes bounds from the slide's layout, else the master
placeholder, and text, fill, kind and font size from the master, unless the
matching flag is set. Moving, editing text and setting a fill set the flag;
`resetOverride` clears all three; applying a master keeps them.

**Consequences.** Master and layout edits are one small change that every
inheriting slide reflects immediately, and the editor can count overrides
per placeholder. Other properties (lock, visibility) are always per object.

## D-159 — Slide timing is exact ticks; only eased progress is a double

**Context.** Presentation playback must be deterministic (WP-056 item 3):
the same position has to give the same frame in the audience window, in a
test sample and in an export. Seconds as doubles accumulate error and differ
between a live clock and a sampled one.

**Decision.** Delays, durations, transitions and positions are signed 64-bit
ticks in the shared 705,600,000-per-second timebase. QML seconds round to the
millisecond once on entry. A position is (slide, build step, ticks into the
step); the evaluator is a pure function of it. The only double is progress
`(t − start) / duration`, eased linearly or through `CurveEvaluator` cubic
Béziers. The live clock converts monotonic nanoseconds to ticks with one
rounding per run.

**Consequences.** Frames sampled at exact ticks are reproducible bit for bit
and tests compare exact values. Sub-millisecond seconds entered from QML are
rounded; exact tick strings remain available.

## D-160 — One slide painter for every output

**Context.** Slides appear in the presenter preview, the audience window,
thumbnails, sampled test images and PDF export. Separate QML and C++
renderers would drift.

**Decision.** `Slides::paintFrame` (C++, `QPainter`) is the only slide
renderer. `SlideFrameView` paints through it, and `renderFrame`,
`previewDataUrl` and `exportPdf` call it on images and PDF pages. Text goes
through `QTextDocument` from the rich-text fragment's HTML.

**Consequences.** The audience output equals the export for the same
position, which the tests check pixel for pixel. The slide canvas used for
editing is still the QML `SlideCanvas`; it shows plain text and geometry,
not the evaluated animation frame.

## D-161 — Audience and presenter are separate native windows with a keep-alive audience

**Context.** OFF-23 requires disconnecting and reconnecting a display
without exposing notes on the audience output, while timing and navigation
stay controllable.

**Decision.** `PresentationWindows` creates the two windows through
`WindowHost`. The audience window contains only a `SlideFrameView`, which has
no notes input; notes are bound only in the presenter delegate. The audience
display is chosen by screen name. When it disappears, the audience window
is not closed: it becomes a normal window on the presenter's display with
status `waiting`, and it returns full screen when a display with that name
is added. Closing the presenter ends the presentation.

**Consequences.** Unplugging a projector never ends the show or reveals the
notes, and plugging it back in needs no action. The status is visible to the
presenter (`audienceStatus`, `displays`). Behaviour on real window managers
and monitors is left to platform qualification.

## D-182 — Reports evaluate in the query engine over a bounded snapshot

**Context.** The first report cut built SQL for one group and a SUM. That SQL
followed SQLite's own rules for nulls and mixed types, while FilterBar, pivots
and the grid follow the WP-029 rules. Nested groups, averages, typed
parameters and data errors would each have needed more hand-built SQL.

**Decision.** `SqliteTableSource` reads a table's reportable columns in one
bound read, at most 4,000 rows. It maps each declared type to a `QueryType`
by SQLite affinity and turns a stored value of the wrong kind into an error
value. Reports run through `QueryEngine`: a detail run for order and cells,
and a grouped run for every total. Filters and parameters are WP-029 query
JSON.

**Consequences.** A report total equals a pivot over the same data, nulls
included. Text stored in a numeric column stops the report, and the error
names the column. A table larger than the bound cannot be reported yet, even
with a narrow filter: pushing filters down into SQL is later provider work
(docs/query-pivot.md#bounds-refresh-and-providers).

## D-183 — A report is a rich-text document with band paragraphs and tables

**Context.** Reports need preview, PDF and printers to agree page for page.
WP-053 already gives rich-text documents one page geometry and one painter.

**Decision.** A report run writes a `qindatk.richtext` document. Headers and
totals are paragraphs, and detail rows are tables whose first row is the
column labels, which the paginator repeats on every continuation page.
The page header and page footer bands are the document's running page
regions: `PageSetup::headerText` and `footerText`, one line each, centred in
the top and bottom margins, with `{page}` and `{pages}` expanded when the
page is painted. A report without `pageFooterText` prints
"Page {page} of {pages}". Group page breaks are `pageBreakBefore` on the
outermost group header that changed. The model decodes each run into one
stable document, and only after the whole run succeeded.

**Consequences.** `PrintAdapter`, `PrintPreview`, `RichDocumentView` and
`exportPdf` need no report-specific code: `RichTextLayout::paintPage` paints
the regions for tiles, sheets and PDF pages alike. The regions sit outside
the content box, so they never change pagination; they are plain text, not
bands with fields or totals. A document that sets them is written as
rich-text version 2. When a completed page count changes, every page's
revision is stamped again so tiles never keep a stale "of N". A failed run
leaves the last good pages on screen.

## D-184 — A design is a session document of whole-design steps

**Context.** Form and report designs were loose JSON files. Edits had no undo,
and the files had no autosave or recovery.

**Decision.** `LocalDataDesignDocument` implements `DocumentModel`. Each edit
records the whole design before and after as one step (`QJsonObject` is
implicitly shared, and designs are small), and `LocalDataDesignStore::checkStructure`
checks it first. The built-in codec `qindatk.local-data-design.v1` writes the
design map plus a `session` header, so the store's own reader still accepts
the file. Version 1 files get an identity derived from their `designId`.

**Consequences.** `DocumentManager` sessions, the shared `UndoService`,
macros, autosave and recovery apply to designs unchanged. A refused edit
changes nothing. The designers must go through the document to be undoable;
without one they keep their host request signals.

## D-185 — Saved designs are qualified against the live schema

**Context.** A design outlives schema changes. A column can be renamed,
dropped or retyped by a table rebuild, and a saved report must not print
wrong totals or fail silently.

**Decision.** A design records the bound column types. `validate(provider)`
reports each bound field as `ok`, `missing`, `typeChanged` or `unsupported`,
and runs the report definition through the report parser against the live
schema. A missing field or a report that no longer parses is fatal. A type
change on its own is a warning. Every report run parses again and keeps the
previous document when parsing fails.

**Consequences.** Reverting a schema change makes the same design valid again
and gives an identical report, which the tests check for a rename and for a
retype. Hosts show the non-fatal issues to the user; nothing rewrites a
design automatically.
## D-162 — Project dependency scans read off-thread and yield to newer answers

**Context.** WP-087 needs real dependency extraction for a whole project and
must reject obsolete analyses. The first slice scanned one QML file at a
time on the caller's thread.

**Decision.** `AssetDependencyIndex.scanProjectAsync()` walks the project
root for QML files (no symlinks or hidden entries, at most 4,096 files) and
extracts edges on a worker. The worker never touches the index. When it
finishes, the GUI thread applies each source's edges only if the source's
scan token is unchanged since the scan started, so a file rescanned in the
meantime keeps its newer edges; those sources are reported as skipped. A
newer scan or `cancelProjectScan()` discards the older result. An indexed
QML source whose file is gone is marked unavailable and loses its outgoing
edges, so its dependents report a missing input.

**Consequences.** Hosts get whole-project extraction without blocking the
GUI. A project over the file cap is refused with an error, not partially
indexed. Other source formats still commit edges through
`beginScan`/`commitScanEdges` with the same token rule.

## D-163 — Edges record where they were written; relinks are plans

**Context.** A "missing file" diagnostic is only actionable if it says which
line to fix, and relinking must not rewrite user files behind the host's
undo history.

**Decision.** Every edge stores `kind` (`import`, `reference`, `host`), a
1-based `line` and the reference text as written. Diagnostics name the
line. `relinkCandidates(id)` lists project files with the same file name
(a bounded walk of 20,000 entries, at most 32 results).
`relinkPlan(missingId, replacementId)` returns one edit per dependent with
the replacement text relative to that source. The index never writes files;
the host applies the plan in its own transaction and rescans.

**Consequences.** Relink is reviewable and undoable by the host. Directory
imports cannot be relinked file by file; the plan says so in `note`.

## D-164 — Chrome Trace Event JSON is the one foreign trace format

**Context.** Real execution traces come from profilers. Chrome, Perfetto and
Qt Creator's trace tools read and write Chrome Trace Event JSON.

**Decision.** `TraceIndex.ingestJsonAsync`/`ingestFileAsync` read either the
index's own export or Chrome trace events, off the GUI thread, up to 64 MiB.
Chrome `ts`/`dur` microseconds become a `chrome` clock domain of 1 ns per
tick, rounded to the nearest nanosecond. `X`, matched `B`/`E` and instant
events are kept; counters, async and flow phases and unmatched begins count
as rejected. Parents come from time containment on the same thread. Record
IDs are `e<n>`, the event's 1-based position in the file.

**Consequences.** Real profiler output opens without a host adapter.
Cross-thread parents and clock-domain metadata need the index's own format,
which round-trips exactly through `exportJson()`.

## D-165 — Trace cancellation is cooperative and keeps the current snapshot

**Context.** Large traces must stay off the GUI thread and be cancellable
without losing what the user is looking at.

**Decision.** Each analysis gets a cancel flag that the worker polls every
1,024 rows. `cancel()`, a newer ingest, `setClockDomains` and `clear()` set
it and bump the generation, so a finished stale result is discarded with
`analysisDiscarded`. `cancel()` leaves the published snapshot in place.

**Consequences.** A cancelled row loop stops within 1,024 rows. JSON parsing
and the final sort cannot be interrupted, so their result is simply
discarded. The view never shows a half-applied trace.

## D-166 — Collaborative deletes remove only what their author observed

**Context.** WP-082 needs object creation and deletion across replicas that
edit offline. A plain "delete wins" rule erases a concurrent committed edit;
"edit wins" by Lamport time alone cannot tell an unseen edit from an old one.

**Decision.** A `Delete` operation names the supporters its author observed:
the baseline token, the object's `Create` operations and the winning operation
of each property. An object is present while any supporter is not named by a
delete (observed-remove). Values and text of a deleted object are retained.
A `Create` of an existing UUID revives it; a `Restore` that wins a property
supports the object too. See [collaboration.md](collaboration.md#objects-and-deletes).

**Consequences.** Replicas converge for any delivery order, and a concurrent
edit that wins its property brings the object back rather than vanishing. A
concurrent edit that loses to a value the deleter saw does not. A delete is
bounded by the object's property count, not by its edit history.

## D-167 — Plain text merges as a replicated sequence of code units

**Context.** Two people typing in one string property under last-writer-wins
lose one person's words.

**Decision.** Properties a client lists in `textProperties` merge as an RGA
sequence: each UTF-16 unit has a stable ID (`base:<n>` or `<operation>:<i>`),
inserts anchor after an element and order after it by descending
`(logicalTime, actor, operation, index)`, and deletes leave tombstones. A
version restore replaces text through ordinary text operations rather than
the `Restore` packet. Formatting (rich text) is not part of this sequence.

**Consequences.** Concurrent inserts and deletes keep every character on
every replica, with an identical interleaving. Tombstones grow the sequence;
it is capped at 65,536 elements per property and fails closed beyond that.
Positions are code units, so hosts must keep grapheme clusters together.

## D-168 — One authenticated frame protocol for local and network peers

**Context.** The first transport sent the actor credential in its hello over
an owner-only local socket. A local-network transport must not expose the
credential and must resist injected, replayed or reordered frames.

**Decision.** Protocol 2 is shared by the local socket and TCP: server
challenge nonce, client nonce and HMAC-SHA256 proof keyed by the credential,
a per-connection session key, and sealed frames with a strict per-direction
sequence number and MAC. Any failed frame closes the connection; operation
replay on reconnect recovers state. Permissions are read per operation and
pushed live; rotation and revocation close sessions. Frames are not
encrypted; confidentiality comes from the network (LAN, WireGuard/Tailscale)
or a host TLS adapter. No hosted service is part of the toolkit.

**Consequences.** Peers on other Linux machines collaborate without a third
party, and a network fault becomes a reconnect rather than corrupt state.
Protocol-1 peers cannot connect to protocol-2 endpoints. Document values are
visible to anyone who can read the network path.

## D-169 — Shared versions and undo are new operations in the log

**Context.** Local checkpoint files differ per replica, and a linear undo
chain replays stale inverse values over other people's edits.

**Decision.** A shared checkpoint is a `Checkpoint` operation carrying the
snapshot and its name/time; every replica lists them sorted by operation
order. Collaborative undo reverses only this actor's own steps by issuing new
operations, and refuses a step a newer edit has superseded (`undoConflict`).

**Consequences.** Version lists and restores agree on all replicas and
survive restart through the durable log. Undo never erases a collaborator's
work; a superseded step is dropped rather than applied. Checkpoints consume
log space (40 KiB snapshots within the 4 MiB log).

## D-170 — Notebook blocks are a flat depth-first list with parent IDs

**Context.** Notebooks nest blocks (list items, toggles, sub-points) but are
read, virtualized, searched, selected and pasted in reading order. A tree of
child arrays makes every flat operation (cross-block selection, the ListView
model, caret movement) walk the tree, and makes a single-block edit copy a
branch.

**Decision.** `NotebookDocument` stores each page's blocks as one flat list in
reading order. A block's `parentId` names an earlier block of the same page,
and every block between a parent and its child is also that parent's
descendant. The native decoder rejects any other order; every structural edit
ends with `NotebookTree::repair`, which lifts a block whose parent is gone to
its nearest remaining ancestor. Moves, deletes and copies of a block include
its contiguous subtree. Split hands the children to the tail; merge hands the
second block's children to the first.

**Consequences.** Views, selection and search use flat indices directly, and
depth is derived. Operations that need siblings (indent, `moveBlockBy`)
compute them per call, which is linear in the page (at most 10,000 blocks).
Timing for very large pages belongs to WP-091. Docs: [notebooks](notebooks.md#model).

## D-171 — Notebook history steps share unchanged pages

**Context.** WP-057 needs undo for every text and structural edit, including
cross-page moves and multi-block pastes, on notebooks of 10,000 or more
blocks, through the shared `UndoService` and `DocumentSession`.

**Decision.** A notebook step stores the whole state before and after. Qt's
implicit sharing keeps unchanged pages and block lists shared by the live
state and every step, so `retainedBytes` counts only the pages a step
changed. Steps are validated by a stamp issued once per document lifetime,
not by comparing content, so undo and redo are constant-time. Typing in one
block merges under `text:<blockId>`.

**Consequences.** One command type covers every operation, and composing
macros is exact. A step touching many pages (a paste into a huge page) is
charged for all of them, which the 256 MiB default budget bounds.

## D-172 — Cross-block selections and pastes follow rich-text fragment rules

**Context.** Copying from the middle of one block to the middle of another,
then pasting into the middle of a third, must give the same result every
time, keep formatting, and never reuse IDs.

**Decision.** A selection is ordered by flat index, then UTF-16 offset
snapped to grapheme boundaries. Text endpoints contribute their selected
part; other endpoints and all blocks between are whole. The copied range has
`openStart`/`openEnd` exactly like a rich-text `Fragment`: pasting into a
text block splits it at the caret, joins an open first fragment to the head
and an open last fragment to the tail. Every pasted block gets a fresh UUID,
and references within the range (parents, a chart's source table) are
rewritten. Formatting travels as runs over `RichText::Spans`, and HTML goes
through `RichTextHtml`, so notebooks exchange formatted text with the rich
text editor and other applications.

**Consequences.** Paste and delete-selection are single undo steps with
exact, testable results. Pasting a range whose endpoints are not text (a
table) never merges.

## D-173 — Block views are virtualized and keep drafts by block ID

**Context.** The first `BlockEditor` built every block card with a Repeater,
and a text field's unsaved text lived only in that field. Long pages were
slow, and a rebuilt field lost its text.

**Decision.** `BlockEditor` lists blocks in a `ListView` with `reuseItems`
whose model is the ordered block IDs. The ID list is replaced only when the
order changes, so a text commit updates cards in place. Uncommitted text is
kept in the editor's `drafts`, keyed by block ID, and restored when a card is
created or reused. Structural edits commit the affected draft first. Focus
after an edit is requested by block ID and applied after the deferred model
refresh.

**Consequences.** Only the visible cards and the cache buffer exist.
Scrolling away and back, or a structural refresh, never loses typing. A host
that binds its own blocks sees the same behaviour.
