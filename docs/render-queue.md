<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Durable render queue

`RenderQueueModel` owns a JSON journal and a `JobModel` worker pool. Open one
absolute journal path with `open(path)` before submission. `enqueueJob(snapshot)`
returns a stable ID and stores the entire immutable snapshot before dispatch.
The model exposes list roles (`jobId`, `label`, `jobState`, `progress`, `error`,
`result`, `outputPath`, `providerId`, `format`, `sourceId`, `dependencies`,
`attempt`) and a `jobs` list compatible with `RenderQueueView`'s presentation
contract. Connect the view's `cancelRequested(id)` and `retryRequested(id)`
to the matching model methods. The host validates its draft and calls
`enqueueJob` for `submitRequested`; `openOutputRequested` remains a host file
open action. The queue is the source of truth once a job is submitted.

A snapshot contains `id` (optional; UUID assigned when absent), `label`,
`providerId`, `providerVersion`, `format`, `capabilityId`, `sourceId`,
`sourceRevision`, absolute `outputPath`, `settings` object, prior `dependencies`
IDs and `resourceSlots` (default 1). `sourceRange` is optional UI metadata.
The provider ID, version and format must match a registered provider. IDs and
snapshot fields stay fixed across retry. `maxResourceSlots` defaults to 4;
a job consumes its declared slots while running. Dependencies run only after
successful predecessors and fail with a diagnostic if a predecessor fails.
An unavailable provider leaves a restored job queued until the host registers
it. There is no implicit conversion between provider versions.

## Built-in caption sequence

`qindatk.caption-png-sequence` version `1` accepts format `png-sequence` and
capability `caption-burn-in`. `settings.captionTrackJson` is a committed
`CaptionTrackModel.toJson()` string. `settings.frames` is an ordered array
of 1–10000 `{path, sha256, timeMs}` objects. Each `path` names an existing
PNG; `sha256` is its lowercase hex SHA-256; `timeMs` is a non-negative decimal
millisecond string. The queue rehashes every source file before reading it,
so changed, missing or undecodable sources fail without publishing output.
Each source is limited to 256 MiB and 100 million decoded pixels. The caption frame renderer burns cues at each timestamp and writes
`frame-000000.png`, etc. The host captures source identity and all rendering
inputs before enqueue; no live document or QML object crosses to a worker.

A provider registered by C++ with `registerProvider(id, version, formats,
RenderProvider)` receives the immutable JSON snapshot, a private staging
directory, cancellation token and progress reporter. It writes files only
inside that directory and returns relative leaf names plus optional metadata.
The queue hashes every returned file, writes `render-manifest.json` containing
provider/version/capability, source identity, settings and snapshot SHA-256, file sizes and
SHA-256 values, then renames the complete directory into the output path.
The target must not already exist. The manifest and every file are verified
after publish and by `verifyOutput(id)`. The built-in provider also records source frame hashes and the caption track SHA-256. The manifest is an exact receipt for
this job snapshot; successful output cannot be overwritten on retry.

## Restart and cancellation

The journal is committed with `QSaveFile` at each state transition. On reopen,
a recorded `running` or `cancelling` job is `interrupted`; its private staging
directory is deleted and it requires explicit `retryJob(id)`. If the final
output exists and verifies against the snapshot, reopen recovers `succeeded`
instead. A previously succeeded output that fails verification becomes
`failed`. Queued work can dispatch after reopen. Cancellation is cooperative:
the worker checks between frames and before publish. If cancellation races
with a verified atomic publish, the result is success. Retry reuses the same
snapshot and output path and never replaces an existing target. A changed
source continues to fail until the host restores the same bytes or submits a
new job with a new snapshot and target. Journal failures are reported through
`lastError`; hosts should surface that diagnostic.

The built-in adapter produces PNG image sequences, not a muxed video, audio,
or PDF. It does not checkpoint individual frames; interrupted work starts a
fresh private stage. The host owns decoding or rendering any other media
engine and registers its own provider with explicit format and version.
Providers must check cancellation and must not touch live QObjects. The
current journal is local single-writer storage; concurrent processes must use
separate journal and output paths. Disk durability follows `QSaveFile` and
same-directory rename semantics of the local filesystem; the queue does not
claim remote/object-store transactions.
