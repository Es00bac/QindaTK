<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Collaboration: synchronization, presence and version history (WP-082)

`LocalPropertySyncService` and `LocalPropertySyncClient` synchronize a
`PropertyDocument` between independent replicas: typed values, object
creation and deletion, and plain-text properties merged character by
character. The service routes operation packets and stores accepted packets
through `QSaveFile` when `storagePath` is set. `PropertySyncSocketServer` and
`PropertySyncSocketClient` connect replicas in other processes over an
owner-only `QLocalServer` socket, or on other Linux machines over a TCP port on
the local network. This is a peer-hosted collaboration backend: there is no
hosted service, and anything that needs a third-party server is a host adapter
(see [Host adapters](#host-adapters)).

## Set up

Create one baseline `PropertyDocument` with typed descriptors and stable
entity IDs. Give each client its own copy of that **same** baseline, for
example by `ReferenceDocumentCodec::capture` and `decode`, or by opening the
same reference document in separate `DocumentManager` instances. The copies
must have the same document UUID, schema, entities, descriptors and values.
Assign that UUID to `service.documentId` and a path to `service.storagePath`,
then call `service.open()`. For each client, set `document` (or a
`DocumentSession.document`), `service`, a unique canonical UUID `actorId` and,
if any string properties should merge as text, `textProperties` (identical on
every replica); then call `connectToService()`. The document assigned to
`document` is captured as the baseline at that moment.

Edit only through the client, including while disconnected:

| Call | Operation | Merge rule |
| --- | --- | --- |
| `setValue(entity, property, value)` | `Set` | last writer wins per property |
| `createEntity(values)` | `Create` (+ `Set` per initial value) | returns the new entity UUID |
| `deleteEntity(entity)` | `Delete` | observed-remove: see [Objects and deletes](#objects-and-deletes) |
| `replaceText(entity, property, start, removeCount, text)` | `TextDelete` + `TextInsert` (chained inserts past 4,096 units) | character sequence: see [Text merge](#text-merge) |
| `restoreVersion(snapshot)` | `Restore` (+ `Delete`, text edits) | see [Named versions and restore](#named-versions-and-restore) |
| `shareCheckpoint(name, snapshot)` | `Checkpoint` | never changes values |
| `undo()` / `redo()` | new operations | see [Undo](#undo) |

Values keep their exact Qt type. Supported kinds are bool, int, uint, int64,
uint64, finite double, string and bytes. A property descriptor must accept that
type. Text positions are UTF-16 code units; a host that edits grapheme
clusters passes cluster-aligned positions.

`disconnectFromService()` keeps a client's unsent queue and local document.
On reconnect, the client replays the service log, then submits each queued
operation with its original identity and logical time. Repeated delivery is
idempotent. A fresh client can replay the durable log against the same
baseline after the service restarts. A service retains at most 4,096
operations in an 8 MiB file; each encoded value is at most 64 KiB, property
names at most 256 characters, and up to eight clients may connect. The
service that holds the whole session's log sets `authoritative: true` (a
socket server sets it on its service): it then compacts, so a document never
runs out of operations (see [Compaction](#compaction)).

While a client is connected, an edit the service cannot store (a full or
unwritable store) is refused **before** it shows, or taken back if the store
fails while writing, and `lastError` says why; a connected replica never shows
an edit that exists only in memory (D-295). Multi-operation edits
(`replaceText`, `createEntity` with values, restore) are checked as a whole
before their first operation applies, so a refused edit leaves nothing
behind.

## Compaction

An authoritative service compacts once `compactAt` operations (default
2,048, clamped to 16–4,096) are retained, and whenever an edit would not fit.
It folds every operation into a **base**: the state a replica derives from
them (per-property winners, entity supporters and removed sets, and each text
sequence with its element IDs and tombstones). The log then keeps only newer
operations and the shared checkpoints (D-295). The base is written next to
the store (`<storagePath>.base.<id>`) before the store names it, so a crash
leaves either the old or the new pair. Folding uses a replica attached to the
service that holds exactly base + log; a headless hub (a server with no
replica in its process) calls `setCompactionBaseline(snapshot,
textProperties)` with the shared baseline and folds through a scratch replica.

A replica that joins or reconnects after a compaction rebuilds from the base,
then re-applies its own queued edits: a `rebuilt` signal and one document
reset. Every merge rule is idempotent, so an operation delivered again after
it was folded changes nothing. The hub sends a new base to live socket peers,
which adopt it into their own store (`baseId`); their unacknowledged edits
survive adoption and are resent. Logical clocks are never reset, so an edit
made after a rebuild still outranks the actor's earlier ones, and undo keeps
reversing this actor's own steps across a fold.

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
erase committed work; use the client's own [undo](#undo) instead. Direct
edits, descriptor changes and preview transactions while a client is attached
are unsupported; the client detects an unexpected revision/reset and fails
closed. `DocumentSession` still handles ordinary save and recovery of the
current local snapshot.

## Objects and deletes

`createEntity` issues a `Create` for a fresh entity UUID. `deleteEntity`
issues a `Delete` that names the *supporters* this replica has observed: the
baseline token (the entity UUID, for baseline objects), its `Create`
operations and the current winning operation of each of its properties. An
entity is present while any supporter is not named by some `Delete` (D-166).

So a delete removes exactly what its author saw. If another actor, unaware of
the delete, made an edit that wins its property, that edit is a new supporter
and the object returns on every replica with its values. An edit that loses to
a value the deleter had seen does not revive it. A revived or restored object
takes each property's winning value, else its baseline value, else the
descriptor default. Values and text of a deleted object are retained, so a
revival or a restore brings them back. A `Set` or text operation that arrives
before its `Create` (reordered or dropped delivery) creates the object with
defaults; the result is the same once all packets arrive.

## Text merge

A property listed in `textProperties` is a sequence of UTF-16 code units with
stable element IDs (D-167). Baseline characters are `base:<n>`; an insert of
`k` characters by operation `U` creates `U:0` … `U:k-1`. `TextInsert` carries
the text and the ID of the element it follows (none for the start);
`TextDelete` carries ranges of element IDs and leaves tombstones. Elements
after the same anchor order by descending `(logicalTime, actor, operation,
index)`, so concurrent inserts at one position interleave identically on every
replica, and a later edit never splits an earlier word. An insert whose anchor
has not arrived waits for it; a delete of an unknown element is remembered.
`setValue` on a text property is refused. One `TextInsert` carries at most
4,096 units; a longer `replaceText` sends chained inserts, each anchored on
the previous chunk's last element. A sequence holds at most 65,536 elements
including tombstones; an edit that would pass it is refused whole, before its
delete applies.

**Anchors (D-206).** `textAnchor(entity, property, position)` returns the ID
of the element just before `position` (empty for the start), and
`textPosition(entity, property, anchor)` returns where that element is now
(`-1` for an unknown anchor). A caret or selection shared through presence as
anchors follows concurrent inserts and deletes on every replica; an anchor
whose character was deleted collapses to the gap it left. A compaction keeps
element IDs, so anchors stay valid across it.

## Undo

`undo()` reverses this replica's own last step by issuing **new** operations
(D-169): the previous value for a `Set`, a delete for a created object, a
revival (`Create` of the same UUID) for a deleted one, and a delete plus
re-insert for a text edit. It never touches other actors' edits. If a newer
edit has replaced the value, or the object or text is gone, the step is
dropped, `undoConflict(operationId, reason)` is emitted and nothing is sent.
`redo()` is symmetric; a fresh edit clears the redo stack. At most 256 steps
are kept. Restore and checkpoints are not undo steps.

Undoing a `createEntity` deletes the object only while no other actor has
touched it since: a newer winning value, text edit or revival by another
actor makes the step conflict (`undoConflict`), because the delete would name
that edit as observed and hide it on every replica.

## Interprocess and network transport

The server owns an opened `LocalPropertySyncService`. Grant each canonical
actor UUID a private credential (16–256 characters) and an editor or viewer
permission with `PropertySyncSocketServer::grant`, then `listen()` on a local
socket name and/or `listenNetwork(address, port)` on a TCP address (port 0
picks a free port, read back with `networkPort()`). Each client process owns a
separate opened `LocalPropertySyncService` with the same document UUID **and
a writable `storagePath`**, plus a normal `LocalPropertySyncClient` attached to
its replica. Set the socket bridge's local service, actor UUID and credential,
then either `setSocketName(name)` or `setHost(host, port)`, and call
`connectToServer()`. Its `connected` signal means server replay has arrived
and it has begun sending this actor's outgoing operations. Listen for
`operationRejected`, `permissionChanged`, `revoked` and `errorOccurred`.

The local service store doubles as the durable outgoing queue: its
**outbox** lists the local operations the hub has not acknowledged
(`outboxCount`). Editing while the socket is disconnected still commits to
that store. A new process can reopen the same store and baseline replica,
attach its local client, then reconnect; the bridge resends the outbox with
the original IDs, and each `ack` settles one entry.
The server deduplicates exact matches and rejects reused IDs with different
contents. Frames are bounded to 96 KiB of body and queued output to 8 MiB per
peer; at most eight sessions connect.

### Wire protocol

Both transports speak the same protocol (version 2, `PropertySyncChannel`,
D-168). The server opens with a plain challenge nonce. The client answers with
its actor, document and own nonce and an HMAC-SHA256 proof keyed by its
credential; the credential itself never crosses the wire. Both sides then
derive a session key from the two nonces and exchange only sealed frames: a
per-direction sequence number, the compact JSON body and an HMAC over both. A
frame that was altered, replayed, reordered or dropped fails verification and
closes the connection; reconnect replay then restores the lost operations, so
network faults never corrupt state. Frames are **authenticated, not
encrypted**: document values are readable on the network path. Use a trusted
LAN or a private tunnel (WireGuard, Tailscale), or wrap the TCP stream in TLS
through a host adapter when that is not acceptable.

### Permissions

The server checks editor permission on **every** submitted operation, so a
change takes effect on the next edit. `grant` while listening pushes the new
permission to the actor's live sessions; the bridge then sets its local
client's `readOnly`, so a viewer's replica refuses new local edits instead of
queueing edits the server will reject. `grant` with a different credential
(rotation) and `revoke(actor)` close the actor's sessions with `revoked`; the
old credential no longer authenticates. There is no automatic credential
provisioning: the host must distribute credentials privately. A viewer receives
the log but cannot submit.

A reject says whether it is **permanent** (no permission, actor or document
mismatch, invalid packet, reused ID) or transient (a full or unwritable hub
store). A permanently refused operation, such as an edit queued offline
before a permission loss, is removed from the replica's store and every local
replica is rebuilt without it; the bridge emits `operationDiscarded(id,
reason)` so the host can tell the user which edit was dropped (D-296). The
replica then matches the hub again and nothing is resent. A transient
refusal stays queued and is retried on the next reconnect.

### Heartbeat and idle expiry

A connected bridge pings every `setHeartbeatInterval` ms (default 5,000) and
drops the connection when nothing arrives for `setIdleTimeout` ms (default
15,000). The server closes sessions silent for its `setIdleTimeout` (default
15,000 ms; 0 disables), including half-open handshakes, so an unplugged host's
presence leaves on every other replica.

## Presence

Presence uses `setPresence({ ... })` and the `presenceChanged`/`presenceLeft`
signals. It is ephemeral, limited to a 4 KiB JSON-compatible map, broadcast
only while connected, and never enters the operation log, document revision or
undo history. Bind `PropertySyncSocketClient::setLocalClient` to the matching
local client to relay its `setPresence` updates across processes and deliver
remote `presenceChanged`/`presenceLeft` signals back to it. A newly connected
peer receives current presence; a peer or socket disconnect, idle expiry or a
replacing session emits one leave for each actor visible through that
connection. Reconnect sends the local current presence again. Empty presence
means leave. The map can carry a name and stable selection ID; for text,
share [anchors](#text-merge) rather than positions so the selection follows
edits. Replicas attached to the hub's own service (the hosting user) and
socket peers see each other's presence both ways, including the snapshot on
connect and leaves.

`Tk.PresenceLayer` is a passive overlay for that stream. Bind `client` to a
`LocalPropertySyncClient` or `PropertySyncSocketClient`; each remote packet
may contain `label`, `activity`, and `cursor`/`selection` anchors, for example
`{objectId: "scene/title"}` or `{position: 42}`. Supply `mapper(anchor, kind,
packet)`, returning `{x,y,width,height}` in overlay coordinates, and increment
`viewRevision` when a scroll, zoom or layout change alters that mapping. The
control shows readable names and activity alongside its coloured marks; it
removes actors on leave/disconnect, client replacement or `staleAfterMs` idle
timeout. Unknown anchors are omitted. It never writes the document or local
selection. For text, a `mapper` resolves a shared anchor with
`client.textPosition(entity, property, anchor)`, so the mark follows edits.

## Named versions and restore

`PropertyVersionHistory` binds a `LocalPropertySyncClient`. Call `open()`, then
`checkpoint(name)` to save the committed document as an immutable snapshot
with actor UUID, UTC time and source revision provenance. With `shared:
false` (default) checkpoints live in a writable `storagePath` owned by this
replica (with its local operation count). With `shared: true` each checkpoint
is a `Checkpoint` operation in the log: every replica, including one started
later from the durable log, lists the same versions in the same order (by
operation order), with the author's actor UUID and logical time (D-169).
`versions` lists newest first; `inspect(id)` returns typed values; `diff(a,b)`
and `diffCurrent(a)` compare snapshots by entity and property, with `change`
`changed`, `added` or `removed`. `Tk.VersionBrowser` displays these records
and lets a user create, inspect and restore a selected version.

`restore(id)` first saves the current committed state as a named safety
checkpoint (shared when the history is shared). It then submits one bounded
`Restore` operation containing the selected snapshot, followed by a `Delete`
for each object created after the version and, for text properties, a text
edit that replaces the current text. The `Restore` competes per property in
the same Lamport order as scalar edits, so a newer concurrent edit can still
win for its property, and it revives objects the version contains. The old
state and the restore packets remain in the log; restore never erases history.
A version larger than one 40 KiB packet is restored as several `Restore`
operations (whole objects each; one object must fit a packet), and text is
restored by replacing only the part that differs, in chained inserts. Both
histories keep the **newest 64 versions**: a new checkpoint, including
restore's safety checkpoint, evicts the oldest instead of failing, so
restore keeps working for the life of the document. A local history stores
snapshots up to 1 MiB each in a 16 MiB file and `remove(id)` deletes one; a
shared checkpoint is one log packet (40 KiB). Schema (descriptor) changes
require a domain-specific version backend.

## Host adapters

AGENT-CONTRACT (this section; `PropertySyncSocketServer` and
`PropertySyncSocketClient`): a host that needs a relay, directory, identity
provider or TLS terminator implements it outside the toolkit and keeps these
invariants: it forwards whole protocol-2 frames unchanged in both directions,
it never needs or stores the actor credential, and it maps its own identity to
a `grant`/`revoke` call on the server. Operation packets, their order rule and
the durable logs stay identical, so a relay changes reachability, not merge
results.

## Scope notes

Owned elsewhere (user decision 2026-09-24: Linux only; qualification is the
deferred WP-088 to WP-094):

- Performance and scale limits of the logs and text sequences under load:
  WP-091. Other platforms and Qt versions: WP-088. Screen-reader runs and
  translation of `VersionBrowser`/`PresenceLayer`: WP-092.
- Adopting these operations in each domain editor (rich-text formatting spans,
  canvas and timeline objects) and demo applications: WP-090. Rich-text
  formatting is not merged by this backend; a domain maps it to properties.
- SDK packaging and API versioning of the transport: WP-093. Log compaction
  is part of this package ([Compaction](#compaction)); the store format moved
  to version 2 and still opens version-1 stores.
