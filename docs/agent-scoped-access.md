<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Scoped agent read access

The public `QindaTK::agent` SDK includes a synchronous, application-owned
`ScopedAgentHost` for reading one explicitly granted resource. It retains its
own grant records and asks the application's `ScopedAgentResourcePort` for an
atomic bounded snapshot. It never calls the legacy provider's broad
`state()`, `actions()`, `revision()` or `execute()`.

This is a production SDK host seam with injected authority ports. It does
**not yet authenticate an OS transport, admit a desktop session, implement a
lock monitor, expose a scoped socket/CLI/MCP method, or grant any installed
application access**. Those ports are trusted assumptions until desktop and
application composition supplies and tests them. The focused tests exercise
the real host with deterministic port fixtures; they are not live session
qualification.

See [the cooperative v1 interface](agent-interface.md) and
[decision D-308](decisions.md).

## Public boundary and ownership

Include `scoped_agent_host.h`, `scoped_agent_policy.h` or
`scoped_agent_types.h`; link `QindaTK::agent`. Headers install under
`include/qindatk`. This additive static-library API requires consumers to
rebuild with matching headers and C++20 (exported by the target). The existing v1 API and wire protocol retain
their behavior. A scoped grant never changes `AgentHost::Grant`, and an
application must not implement it by calling a global access-level setter.

An application owns its host and registers a borrowed `AgentProvider`
**identity** alongside a borrowed resource adapter. Each is a same-thread
QObject; destroying either withdraws that provider and all its scoped grants.
Moving either to another thread causes subsequent access to refuse. A resource
adapter must validate the exact provider/resource/generation itself and create
only its permitted domain projection. An application with two documents must
not capture all application state and then relabel or filter it as one
document. The host cannot discover accidental secrets inside a trusted
provider's permitted JSON: minimum-disclosure projection is the owner's job.

Task and privacy ports are borrowed non-QObject interfaces and must outlive
the host. Their query methods are side-effect free and must not reenter the
host or change application/authority state. Resource callbacks may synchronously
revoke grants or change document state, which the host checks after return;
they must not destroy the host or enter a nested event loop. All calls run
synchronously on the host thread. Wrong-thread operations refuse without
calling providers or authority ports; void invalidations do nothing there.
Schedule invalidation onto the owner thread. No SDK type owns a worker thread,
socket, model provider or desktop/compositor private interface.

## Identity and authority

| Value | Owner and rule |
| --- | --- |
| Principal | Trusted composition supplies exact session, task incarnation and admitted transport IDs. Request JSON never supplies authority. Task IDs must not be reused after termination, and a reconnect needs its own admitted transport ID. |
| Provider | Existing `AgentProvider::id()` UUID lives for that QObject lifetime. |
| Resource | Adapter supplies a stable opaque ID and nonzero generation. Reopening/replacing the resource advances generation; generations must never repeat in that provider lifetime. It is not a filename, title or window index. |
| Revision | Adapter supplies a nonempty opaque string for the exact permitted projection. Every observable change, including edit/undo, advances it without repetition within the resource generation. A content hash alone is insufficient. |
| Grant | Host stores principal, resource, privacy epoch, issue/deadline and fresh handle. A copied or altered handle cannot create or widen its record. |
| Privacy | Port supplies current session, Unlocked/Blocked/Unknown, nonzero transition epoch and monotonic time. Every lock, unknown state, owner or session transition advances the epoch even if notifications were missed. |

Only trusted application UI/composition calls `grantRead`; no request method
creates grants. It first checks current task, session/privacy and the current
resource, then rechecks after the adapter callback. Grant lifetime is 1 ms
through 8 hours. There is no persistence, renewal in place or shared grant:
renewal creates a fresh handle. A caller cannot modify expiry by editing copied
values. Revoke removes the owner record; revoke-all also fences a grant being
constructed during a resource callback. A new host instance invalidates all
old handles. Handle IDs are opaque; their randomness complements the current
principal checks and does not replace authentication.

Read admission checks the current task, exact stored grant, resource, privacy
session/epoch/state and monotonic deadline **before provider lookup**.
Unavailable/unknown privacy fails closed. `refreshAdmission()` permanently
drops currently invalid records and belongs on task/privacy change callbacks;
every read also checks current facts so a delayed notification is not enough
to bypass lock, expiry or a changed epoch. Composition must preserve incarnation
and transition epochs across intermediate events.

## Snapshot and disclosure

`read(principal, handle, resource, expectedRevision)` obtains the owner's
current revision, checks admission again, and asks the adapter to atomically
capture that exact revision. It checks current authority, lifetime and revision
after capture and after validating payload bounds. An optional expected
revision refuses a stale caller before capture. A changed document or revoked
grant discards the entire copied payload. Failures contain no revision/content.

Results distinguish Ready, Denied, Invalid, Unavailable, StaleRevision,
ResourceLimit and WrongThread. Adapter failures are normalized to Unavailable
except explicit StaleRevision. Denied does not reveal whether a guessed
provider/resource exists. There are no edits, proposals, action receipts,
activity subscription or enumeration in this first scoped host slice.

A future wire adapter must call `disclosureAllowed` with the same principal,
handle, resource and returned revision **immediately before writing queued
bytes**, with no event-loop turn or asynchronous gap between that check and
the write. A queued snapshot is only a copy, not enduring authority. Revocation
cannot recall bytes already released. That writer must also purge queued
context on invalidation and authenticate the peer outside request parameters.
The synchronous API alone cannot enforce those future transport obligations.

## Bounds and failure modes

IDs accept 1–128 ASCII letters, digits, hyphens, underscores, periods or colons.
Revision strings are at most 256 UTF-8 bytes and reject control characters and
surrogates. The host admits at most 128 providers, 256 live grants total and
64 per exact principal. Capacity refusal never evicts a still-valid grant.
Expired/invalid grants are pruned before new grants.

Payload validation uses a conservative 1 MiB serialized upper budget, maximum
depth 16 and 16,384 JSON values. It accounts for escaping before serializing;
therefore some ASCII payloads smaller than 1 MiB are deliberately refused.
It refuses the entire result and does not truncate a document or redact
secrets. Providers must also bound their capture allocation and execution
time: validation cannot recover memory or time already spent inside trusted
application code.

## Verification and next production packets

`tst_scoped_agent_policy` covers shape, identity, time/overflow and JSON bounds.
`tst_scoped_agent_host` invokes the actual SDK host with a two-resource denial
fixture and a broad legacy snapshot containing an unrelated secret. It checks
zero broad-provider calls, altered handles/principals, missed lock transitions,
expiry, replaced documents, callback invalidation, queued disclosure,
destruction, limits, refresh and wrong-thread denial. `tst_agent_host` retains
the independent v1 IPC/CLI compatibility gate. These tests require a source
build and execution; merely adding their sources is not a passing result.

The next separately reviewed packets are:

1. Desktop/session admission supplies authenticated principal and privacy
   ports through a public SDK boundary, including current lock owner and
   restart/incarnation fencing. Window association remains shell-owned.
2. An application implements the resource adapter directly over its real
   document engine and a scoped consent surface. It must demonstrate two
   independent resources and unrelated context absent from output.
3. A separate scoped transport dispatches reads through this host, pins
   admitted peers and gates queued disclosure. Legacy v1 endpoint discovery
   must not imply scoped authority.
4. Proposal/edit support retains exact owner-issued intent, binding, deadline
   and receipt records and has direct forged/replaced-intent regression tests
   before exposing any mutation.

None of these pending packets is satisfied by the pure desktop design models
or by the injected port fixtures in this SDK.
