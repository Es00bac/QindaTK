<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local property synchronization (WP-082 first slice)

`LocalPropertySyncService` and `LocalPropertySyncClient` synchronize scalar
`PropertyDocument` values between independent document instances on one Qt GUI
thread. The transport is process-local: the service routes operation packets
between clients and stores accepted packets through `QSaveFile` when
`storagePath` is set. This is a functional two-client backend, not a network
protocol or a general document merge engine.

## Set up

Create one baseline `PropertyDocument` with typed descriptors and stable
entity IDs. Give each client its own copy of that **same** baseline, for
example by `ReferenceDocumentCodec::capture` and `decode`, or by opening the
same reference document in separate `DocumentManager` instances. The copies
must have the same document UUID, schema, entities, descriptors and values.
Assign that UUID to `service.documentId` and a path to `service.storagePath`,
then call `service.open()`. For each client, set `document` (or a
`DocumentSession.document`), `service`, and a unique canonical UUID `actorId`;
call `connectToService()`. Use `client.setValue(entityId, propertyId, value)`
for collaborative edits, including while disconnected. Values keep their
exact Qt type. Supported kinds are bool, int, uint, int64, uint64, finite
double, string and bytes. A property descriptor must accept that type.

`disconnectFromService()` keeps a client's unsent queue and local document.
On reconnect, the client replays the service log, then submits each queued
operation with its original identity and logical time. Repeated delivery is
idempotent. A fresh client can replay the durable log against the same
baseline after the service restarts. The service stores at most 4,096
operations in a 4 MiB file; each encoded value is at most 64 KiB, property
names at most 256 characters, and up to eight clients may connect.

## Conflict and history rule

An operation identifies a document, entity, property, actor and operation UUID
and carries a Lamport logical time. For each `(entity, property)`, the winner
is the maximum `(logicalTime, actorId, operationId)` tuple in lexicographic
order. This works even when packets are duplicated or arrive in a different
order. Separate properties merge independently. Every accepted operation is
retained in the service log, including a losing value; a client emits
`operationLost(losingId, winningId)` when one of its visible edits loses.

The `PropertyDocument` revision counts **applied value changes**, not service
operations. It is local to each replica and is not a shared revision ID.
Synchronized values bypass `UndoService` and invalidate its previous local
history, because replaying a stale inverse value over a remote edit would
erase committed work. An application must issue a new `setValue` operation to
reverse a collaborative change. Direct edits, structural changes, descriptor
changes and preview transactions while a client is attached are unsupported;
the client detects an unexpected revision/reset and fails closed. `DocumentSession`
still handles ordinary save and recovery of the current local snapshot.

Presence uses `setPresence({ ... })` and the `presenceChanged`/`presenceLeft`
signals. It is ephemeral, limited to a 4 KiB JSON-compatible map, broadcast
only while connected, and never enters the operation log, document revision or
undo history. The map can carry a name and stable selection ID; views must
decide whether those coordinates remain valid after an edit.

## Remaining WP-082 scope

- A network or interprocess transport, authenticated actors, authorization and
  permission changes, and a durable offline queue across client process exit.
- Generic domain operations, concurrent object deletion and rich text merge.
  Structural changes to the reference document currently stop synchronization.
- A version browser with immutable previews, compare, branch and restore as a
  new collaborative operation. The log is evidence for this future service,
  not a version browser.
- Domain-aware undo/redo, conflict resolution UI, bounded log compaction and
  migration, presence expiry and selection rebasing.

The package remains **in progress** against the full WP-082 acceptance gate.
