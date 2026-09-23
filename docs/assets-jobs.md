<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Jobs, assets and resource links

WP-015 adds a bounded background-work path for reference applications. The
`JobModel` is a GUI-thread `QAbstractListModel`: each row exposes a stable ID,
label, state, priority, progress, document/view identity, error and result.
The C++ caller supplies a `JobSpec` and a worker-only `JobWork` function. A
`JobWork` receives a cancellation token and progress reporter; it must never
read a live QObject or QML item. The default queue permits four concurrent
jobs and 64 waiting jobs; both limits are configurable. Waiting jobs dispatch
by priority, preserving FIFO order at equal priority. Completed rows are
retained up to 256; `clearFinished()` removes them explicitly.

A job can be queued, running, cancelling, succeeded, failed, cancelled or
stale. `cancelJob` requests cooperative cancellation. A running worker must
return before its slot is released; a backend that ignores the token cannot
be stopped safely. Document jobs capture a `SnapshotHeader` and a guarded
`PropertyDocument` pointer on the GUI thread. Completion is stale if that
document is destroyed or its revision/reset generation changes. Call
`invalidateView(viewId)` when a virtualized delegate or tab is recycled and
`invalidateDocument(id)` on close; both cancel queued/running work and mark
its result stale. Only a current successful job exposes its result. A progress
reporter supplies values from 0 to 1; intermediate Qt watcher notifications
can be coalesced, while terminal progress records the last value supplied.

`ResourceResolver` maps a stable logical resource ID to an external or
embedded URI, observed revision, SHA-256 and availability (`missing`, `ready`
or `modified`). `lookup(id)` reads the cached record without I/O, so a rapid
asset request does not hash a large file on the UI thread. `refresh(id)` and
`resolve(id)` inspect an external file synchronously; use
`refreshAsync(jobs,id)` for interactive large files. Its worker streams SHA-256
in 1 MiB chunks and applies the result only if URI and revision still match
the captured record. `relink(id,newUri)` validates the destination before
replacing the mapping; every reference to that logical ID then resolves the
new URI. `proposeRelinksAsync(jobs,ids,candidates)` hashes candidates on a
worker and returns exact-match proposals through the job result without
changing any mapping. The synchronous proposal method remains for small
reference fixtures. Both forms bound inputs to 256 resource IDs and 2048
candidate URIs. `restoreMapping(id,uri)` projects an already committed
document URI during undo/redo, including a now-missing local file; it keeps
the expected hash and reports the missing status. It is not the user-facing
relink validation path.

`DocumentResourceLinks` joins a `PropertyDocument` resource manifest to the
resolver and `UndoService`. Each manifest row names a stable `resourceId`, a
resource entity, its string URI property, and optional expected SHA-256.
Clip or object entities store only the logical ID, so several references
follow one mapping without modifying their timing or geometry. Call
`relink(id,newUri,expectedDocumentRevision)` to validate an existing local
destination and commit one undoable URI edit. Undo and redo synchronize the
resolver from committed document values; if the old file is gone, undo
restores its missing URI and retains the baseline hash. Callers should
replace the manifest when switching documents so retired logical IDs do not
leak into the next session.

`AssetProvider` accepts `AssetRequest` records by C++ or
`requestAsset(resourceId,kind,options,document,viewId)` from QML. Kind,
region, exact decimal-string time/rate, size, quality and revision form a
cache identity. It uses `ResourceResolver.lookup`, coalesces duplicate
in-flight requests, runs a replaceable `AssetBackend` through its `JobModel`,
and has a 32 MiB LRU cache by default. `cacheBudgetBytes` is configurable;
results above 64 MiB are not cached. The provider caps live subscriptions
at 4096. A request returns a subscription ID; `asset(id)` reports `loading`,
`ready`, `missing`, `failed`, `cancelled` or `stale` and the returned data.
`release(id)` removes a subscriber and cancels work if nobody remains.
Relinking, backend replacement, document change/close and view recycling
invalidate relevant subscriptions before their result can be delivered.
Delegates must release their IDs when pooled or destroyed. Result data can
include metadata, encoded preview bytes, a `previewUrl` and reduced waveform
peaks. The `AssetThumbnail`, `AssetWaveform` and `AssetFilmstrip` adapters
manage those subscriptions for QML callers.

The default `FileAssetBackend` handles local paths and `file:` URIs. It reads
ordinary metadata, decodes/crops/scales images to PNG thumbnails, and reduces
integer PCM RIFF/WAV samples (8/16/24/32-bit) to bounded peak columns on a
worker. The reference adapter limits files to 256 MiB, source images to 100
million pixels, and waveform output to 8192 columns. It reports video frame
and filmstrip requests as unsupported; a domain backend supplies video
decoding, remote files, larger media and domain-specific revision rules. No
partial or fabricated preview is shown for an unsupported operation.

`JobModel` exposes inspectable failures and cancellation; applications can
show these in their own task view or command feedback. A producer must take
an immutable snapshot on the GUI thread before enqueueing any work that
depends on a document. A consumer must use the stable subscription or job ID
and release it on navigation, pooling, close and error paths. The reference
tests use held, failing backends to prove concurrency bounds, priority,
coalescing, cache eviction, cancellation, stale rejection and relink behavior.
