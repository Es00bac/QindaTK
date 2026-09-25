<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Service decisions

Continuation of [decisions.md](decisions.md).

## D-030 — Independent settings and workspace files

**Context.** A creative application must restart with preferences, windows,
views and panels without treating those records as document edits. **Decision.**
Typed preferences and workspace topology use separate versioned JSON schemas
and atomic files. Document bytes remain under `DocumentSession`; panel
placement remains under `DockModel`. Unknown setting keys and workspace
record fields are preserved. Restored windows are constrained to available
screens, and missing document URIs remain visible as unavailable references.
Floating shade is transient host presentation state and resets on workspace
switch. **Consequences.** Apps can replace storage independently, retain
plugin settings and unresolved panel references, and restore multi-view
sessions without mutating documents. Native window creation is an application
or later `WindowHost` task.

## D-031 — Stable subscriptions across asynchronous assets

**Context.** Media tiles and large documents issue duplicate requests while
views scroll, zoom, recycle and close. **Decision.** `JobModel` bounds worker
concurrency and retains GUI-thread status, with document revision and view
generation checks before delivering results. `AssetProvider` returns stable
subscription IDs, coalesces requests by content/region/time/size key, caches
bounded results and cancels a flight when its last subscriber releases it.
`ResourceResolver` owns logical URI identity; backend work receives immutable
request values. **Consequences.** A late result cannot repaint a recycled
delegate or mutate a closed document, and applications can replace the file
backend without changing UI subscription semantics. Video/remote providers
remain explicit domain adapters.

## D-032 — Document URI history and runtime resource projection

**Context.** A media relink changes one logical resource location used by
several clips, and undo may be requested after the old file disappears.
**Decision.** `PropertyDocument` owns the manifest URI and `UndoService`
records its edit. `DocumentResourceLinks` validates a user relink against an
existing local destination, commits one URI change, and projects committed
changes to `ResourceResolver`. Its projection accepts a missing URI during
undo/redo and preserves the expected content hash. Clip references store
the logical ID and never receive copied replacement paths. **Consequences.**
Relink, undo and redo update every reference without changing clip timing;
missing media remains an explicit document state instead of blocking undo.

## D-033 — Transfer intent and authoritative destination edits

**Context.** Copy, paste and drag/drop cross document boundaries and may
carry stale IDs, rich content or missing resources. **Decision.** A bounded,
versioned payload carries source identity and requested operation but never
performs edits. Clipboard and drag/drop services validate the destination
against its current revision and capability; the domain adapter commits one
authoritative transaction and records its inverse. The reference collection
adapter remaps copied object IDs. A cross-document Move is unsupported unless
an adapter proves an atomic source-and-destination commit. **Consequences.**
Malformed or unsupported content cannot partially mutate a document, and an
accepted reference paste/drop is undoable as one step.

## D-034 — Semantic identity independent of visual delegates

**Context.** Virtual grids, canvases and timelines recycle visual delegates,
while assistive tools need stable objects and the same actions as keyboard and
pointer users. **Decision.** `AccessibilityAdapter` exposes model-owned
semantic IDs as virtual Qt accessible children. Each node provides a role,
name, text alternative, state, value and command mapping; selection and
focus read authoritative models. Removed IDs retire their Qt interface, and
action handlers use the existing command/transaction path. **Consequences.**
Offscreen content remains navigable without allocating one QML delegate per
model item. Platform screen-reader behavior is a separate qualification gate.

## D-035 — Locale at the presentation boundary, gesture at the edit boundary

**Context.** Localized strings and mixed input devices can otherwise alter
stored values or create duplicate edits. **Decision.** `LocaleService`
formats/parses per instance while documents retain canonical typed values;
chrome direction remains separate from bidi content direction.
`GestureRouter` grants one semantic gesture per view and owns the associated
`EditTransaction` lifetime after a claim, including cancellation on lost
grabs and view/target invalidation. **Consequences.** Locale changes do not
rewrite a dirty document, and competing pen/touch/mouse paths cannot commit
the same view edit twice.

## D-036 — Exact time storage with decimal QML transfer

**Context.** Audio samples, video frames and musical ticks can exceed the
exact integer range of QML `Number`; tempo and rational frame rates need
repeatable rounding. **Decision.** `TimeMap` stores signed 64-bit positions,
uses exact bounded 128-bit intermediates, and transfers positions as decimal
strings through QML. Piecewise constant tempo uses integer microseconds per
quarter. Floating point is confined to relative visible pixel transforms,
which reject spans outside the exact display range. **Consequences.** Long
sessions and cross-domain conversions do not silently round identifiers;
unsupported drop-frame rates, overflow and malformed positions report errors.

## D-037 — Keep native dialogs optional to the core package

**Context.** Native file picking benefits from platform dialogs, while
QindaTK's reusable controls and QML module must work in a `QGuiApplication`
without Qt Widgets. **Decision.** `FileDialogAdapter` is a backend-injected
core service with explicit unavailable and canceled states. A separate
`QindaTKWidgets` package supplies the Qt Widgets dialog backend for hosts
using `QApplication`. `WindowHost` binds native `QQuickWindow` lifetimes to
`DocumentManager` view IDs, with dirty final-view close guarded by document
state. **Consequences.** Core consumers need no Widgets link dependency;
desktop-specific operations are capability-gated and require target-platform
qualification.

## D-038 — Approve each format operation against a frozen input

**Context.** A format name or extension says little about editable content,
losses or whether a source or destination changed while a user reviewed a
preview. **Decision.** Providers advertise import, export and roundtrip
separately. `FormatRegistry` preflights an immutable byte source or document
snapshot, reports bounded previews and losses, and returns source, revision or
destination-hash receipts in its options. Starting a job with those options
rechecks the approved input; lossy output requires explicit acknowledgement.
Workers never mutate live documents. Import adoption is a separate GUI-thread
step after success, and export writes through `QSaveFile`. **Consequences.**
The built-in native reference and strict UTF-8 text paths can make precise
fidelity claims without implying support for unrelated vendor formats. A
late cancellation after commit is reported as committed output, not an erased
file.

## D-039 — Keep civil dates separate from ambiguous instants

**Context.** An all-day date has no time zone, while a local clock reading can
occur twice or not at all at a daylight-saving transition. **Decision.**
`LocaleService` stores all-day values as ISO dates and formats them through
the configured calendar. It parses local date/time fields without the host
zone normalizing them first, then classifies the result in the configured
zone as valid, ambiguous or nonexistent. An ambiguous value exposes both UTC
instants and requires the user to choose; a nonexistent value cannot commit.
`CalendarPicker` uses calendar-native month fields but emits stable ISO date
identities. **Consequences.** Locale and display-calendar changes never
silently rewrite document values, and date-only values never acquire a time
or offset by accident.

## D-288 — Render output is a verified directory receipt

**Context.** `JobModel` schedules transient functions and cannot recover a
render after process exit. **Decision.** `RenderQueueModel` journals immutable
snapshots and attempts, stages provider files beside an absent target, then
publishes one manifest-bearing directory. The manifest binds file hashes to
the snapshot and provider version. Interrupted attempts require explicit
retry; an already published and verified output is recovered as success.
**Consequences.** A host can add engines through a provider callback while
retaining its document and media ownership. Output paths are unique per job;
a new revision uses a new snapshot and target.
