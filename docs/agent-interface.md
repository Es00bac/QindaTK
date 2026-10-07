<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Live agent interface (v1)

`QindaTK::agent` is an independent Qt Core/Network C++ library. It does not
depend on QtQuick, a model provider, a screenshot or a browser. Applications
implement `AgentProvider` over their existing document/view and editing code,
then register it with `AgentHost`. `AgentServer` exposes that same contract
on a private local Unix socket. The installed `qinda-agent` Python-stdlib
client provides a CLI and an MCP stdio adapter.

This is the first synchronous Linux contract, not automatic coverage of every
toolkit control. Providers own domain semantics, validation, undo and useful
changed-target/result data. Standard-control adapters, async task cancellation,
remote access and multi-document transactions remain separate work.

The additive [scoped Read SDK](agent-scoped-access.md) keeps resource-specific
grants separate from this cooperative v1 boundary. Its injected task/privacy
ports are not yet authenticated desktop transport or installed app admission.

## Provider ownership and state

All calls run on the provider/host's owning thread (normally GUI). The host
borrows the provider; destruction withdraws its document. A provider gets a
UUID for its lifetime. Expose a title, type, active view, selection, drafts,
modal/validation state and domain values in `state()`. These are JSON copies,
never QObjects or credentials. Do not send document context to a model here.

Connect every human and agent mutation, undo, load or replacement to `touch()`.
The revision is an opaque generation plus a hash of the current state. The
generation prevents edit/undo ABA; the hash also detects a changed selection,
active sheet or draft. Clients must send exactly the returned revision string.
Do not interpret it as a file modification time or an integer.

`actions()` describes only explicitly supported semantic mutations, each with
`id`, `description`, `inputSchema`, `enabled`, and `disabled_reason` when
disabled. `execute()` runs after host checks. Return `status`, `message`,
`changed`, `result`, and an `undo_ref` where supported. A provider that partially
applies work must report `partial` and errors; the host does not pretend to
provide a transaction around an arbitrary engine.

## Grants and local boundary

Providers default to `Off`. Application code or its UI sets `Off`, `Read` or
`Edit` per provider. No remote RPC grants access. Read access allows inspect,
discovery and activity; it cannot mutate. Revocation is checked before a cached
receipt is returned. The socket directory must be owned by the user with 0700
permissions; the socket is user-only. This is a same-OS-user cooperative access
boundary, not isolation between different clients running under that UID.

Create a private directory under `AgentServer::runtimeDirectory()`, then call
`listen()` with an absolute socket path. Existing socket paths are never
silently unlinked. There is no TCP or web endpoint. The server limits concurrent
connections to 32, idle connections to 30 seconds, incoming messages to 256 KiB,
and outgoing messages/backlog to 8 MiB. It ignores JSON-RPC notifications.

## JSON-RPC and outcomes

Requests are UTF-8 JSON-RPC 2.0 objects followed by a newline. Supply a transport
`id`, method and object `params`. Methods:

| Method | Parameters | Result |
| --- | --- | --- |
| `hello` | none | protocol version and methods |
| `documents.list` | none | granted open document IDs/titles/types/access |
| `document.inspect` | document_id | live state, document_id, revision, access |
| `actions.list` | document_id | actions and current revision |
| `action.invoke` | document_id, action, arguments, if_revision, request_id | application receipt |
| `events.poll` | after: decimal sequence string | events, sequence, refresh_required |

Permission, stale revision, unavailable action or invalid argument failures
occur before mutation. Provider receipts include document ID, request ID and
post-operation revision (`null` with `document_closed` if the action closes
its provider). Choose a unique 1–128-character request ID for a new
operation. On uncertain delivery retry **the same ID and identical parameters**.
An executed request is cached once; different inputs with its ID fail. Replays
include `replayed` and `current_revision`, which may differ from the original
receipt revision after subsequent edits. A replay reports the original result,
not the current document. Inspect again for current facts.

Each host retains at most 1,024 execution receipts. It refuses further new
mutations at that limit rather than evict receipts and accidentally reapply an
old request. Restart the application to create a new session. The activity
journal retains 256 metadata events; `refresh_required` signals a gap. Events
for currently ungranted/closed scopes are omitted. Polling is v1's event adapter;
unsolicited subscriptions and asynchronous jobs are not advertised.

## Input schema subset

Supported: type (object/array/string/number/integer/boolean/null), properties,
required, additionalProperties (bool or schema), items, anyOf, enum, pattern,
minimum/maximum, min/maxLength, min/maxItems, min/maxProperties, description.
Unknown keywords and malformed schemas fail validation, including constraints
on optional properties not present in the request.
Schemas nest at most 16 levels. Providers must additionally validate domain
rules (cell bounds, formulas, target identities, current editing/modal state).
This deliberately avoids adding a model or JSON-schema package dependency.

## CLI and MCP

```
qinda-agent sessions
qinda-agent --socket /run/user/1000/qindatk-agent/app.sock documents
qinda-agent --socket /run/user/1000/qindatk-agent/app.sock inspect DOCUMENT_ID
qinda-agent --socket /run/user/1000/qindatk-agent/app.sock actions DOCUMENT_ID
qinda-agent --socket /run/user/1000/qindatk-agent/app.sock invoke DOCUMENT_ID calc.set_cells \
  --args '{"cells":{"B5":"2"}}' --if-revision REVISION --request-id lighting-42
qinda-agent --socket /run/user/1000/qindatk-agent/app.sock mcp
```

CLI exits nonzero for refused or partial requests. Without `--socket`,
document discovery spans all available local endpoints and includes their
paths; reads/writes route the lifetime UUID to its currently granted owner.
Unresponsive endpoints are reported in `unavailable_endpoints`. Discovery
uses a two-second timeout per endpoint; stale socket paths do not block other
applications. Event sequences remain process-local: choose `--socket` for
CLI events, or pass `endpoint` to the MCP event tool.

MCP exposes endpoint discovery, document discovery,
inspection, action discovery, invocation and event polling as tools, and
granted document snapshots as JSON resources. MCP stdout contains only newline
JSON; diagnostics go to stderr. Configure a compatible client to launch
`qinda-agent mcp` (which initializes before applications open), or
`qinda-agent --socket ACTUAL_ENDPOINT mcp`; configuration formats are client
specific. Tool discovery gives the formal action arguments. MCP compatibility
is based on the [2025-11-25 stdio protocol](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports).

Verification: `tst_agent_host` uses a real socket and subprocess CLI/MCP clients,
and checks grants, schema rejection, revisions, retry, ABA, synchronous
provider closure, multi-process routing and MCP initialization before startup.
