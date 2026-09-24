<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Local property synchronization (WP-082 first slice)

`LocalPropertySyncService` and `LocalPropertySyncClient` synchronize scalar
`PropertyDocument` values between independent document instances on one Qt GUI
thread. The service routes operation packets and stores accepted packets
through `QSaveFile` when `storagePath` is set. `PropertySyncSocketServer` and
`PropertySyncSocketClient` connect independent processes over an owner-only
`QLocalServer` socket. This is a local collaboration backend, not an Internet
service or a general document merge engine.

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

## Interprocess transport and permissions

The server owns an opened `LocalPropertySyncService`. Set the socket name,
then grant each canonical actor UUID a private credential and an editor or
viewer permission with `PropertySyncSocketServer::grant`, then `listen()`.
Each process owns a separate opened `LocalPropertySyncService` with the same
document UUID **and a writable `storagePath`**, plus a normal
`LocalPropertySyncClient` attached to its replica. Set the socket bridge's
local service, socket name, actor UUID and credential, then call
`connectToServer()`. Its `connected` signal means server replay has arrived
and it has begun sending this actor's outgoing operations. Listen for
`operationRejected` and `errorOccurred` to show permission or transport errors.

The local service store doubles as the durable outgoing queue. Editing while
the socket is disconnected still commits to that store. A new process can
reopen the same store and baseline replica, attach its local client, then
reconnect; the bridge resends the actor's operations with their original IDs.
The server deduplicates exact matches and rejects reused IDs with different
contents. It checks actor credentials at connection and editor permission
on every submitted operation. A viewer receives the log but cannot submit.
There is no automatic credential provisioning: the host must distribute them
privately. A socket name is local to the same machine and the endpoint grants
access only to the current OS user. Credentials do not enter the operation
store. Frames are bounded to 96 KiB and queued output to 8 MiB per peer.

Bind `PropertySyncSocketClient::setLocalClient` to the matching local client
to relay its `setPresence` updates across processes and deliver remote
`presenceChanged`/`presenceLeft` signals back to it. The socket bridge also
offers `setPresence` and those two signals directly. A newly connected peer
receives current presence; a peer or socket disconnect emits one leave for
each actor visible through that connection. Reconnect sends the local current
presence again. Presence is capped at 4 KiB, lives only in socket peer memory,
and never enters either operation store. Empty presence means leave.
If the same authenticated actor reconnects before its old socket has fully
closed, the new connection replaces the old one and the old presence leaves.

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

`Tk.PresenceLayer` is a passive overlay for that stream. Bind `client` to a
`LocalPropertySyncClient` or `PropertySyncSocketClient`; each remote packet
may contain `label`, `activity`, and `cursor`/`selection` anchors, for example
`{objectId: "scene/title"}` or `{position: 42}`. Supply `mapper(anchor, kind,
packet)`, returning `{x,y,width,height}` in overlay coordinates, and increment
`viewRevision` when a scroll, zoom or layout change alters that mapping. The
control shows readable names and activity alongside its coloured marks; it
removes actors on leave/disconnect, client replacement or `staleAfterMs` idle
timeout. Unknown anchors are omitted. It never writes the document or local
selection. The host still owns selection rebasing and anchor validity.

## Remaining WP-082 scope

- Remote-network transport, credential provisioning/rotation, permission
  change notifications, and idle presence expiry beyond socket disconnect.
- Generic domain operations, concurrent object deletion and rich text merge.
  Structural changes to the reference document currently stop synchronization.
- Shared checkpoint metadata across clients, branch requests, rich previews,
  checkpoint migration/compaction and domain-specific restore policies. The
  first version history store is local to its owner; restore itself travels as
  a synchronized operation to all property replicas.
- Domain-aware undo/redo, conflict resolution UI, bounded log compaction and
  migration, transport-level idle expiry and selection rebasing.

The package remains **in progress** against the full WP-082 acceptance gate.

## Named versions and restore

`PropertyVersionHistory` binds a `LocalPropertySyncClient` and a writable
checkpoint path. Call `open()`, then `checkpoint(name)` to save the committed
document as an immutable snapshot with actor UUID, local revision, UTC time
and operation-count provenance. `versions` lists newest first; `inspect(id)`
returns typed values; `diff(a,b)` and `diffCurrent(a)` compare immutable
snapshots with the current committed document. `Tk.VersionBrowser` displays
these records and lets a user create, inspect and restore a selected version.

`restore(id)` first saves the current committed state as a named safety
checkpoint. It then submits **one** bounded restore operation containing the
selected snapshot; the existing service persists that packet and broadcasts
it. Each replica validates the same entity/property shape and publishes all
winning values as one document revision. The operation participates in the
same Lamport ordering per property as scalar edits, so a newer concurrent edit
can still win for its property. The old state and the restore packet remain
available; restore does not erase previous history. Checkpoint snapshots are
limited to 40 KiB and the store to 64 checkpoints/4 MiB. Structural schema
changes and larger documents require a domain-specific version backend.
