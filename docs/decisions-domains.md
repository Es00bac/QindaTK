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
