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
