<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application contracts and capabilities

These are the version-one boundaries for services and optional adapters built
above QindaTK controls. The public C++ headers are
`<qindatk/application_contracts.h>` and `<qindatk/capability_registry.h>`.
Applications can continue using the existing QML controls with only
`import QindaTK as Tk`.

## Identity, revisions and exact values

| Record | Meaning | QML/wire representation |
| --- | --- | --- |
| `EntityId`, `DocumentId` | Distinct UUID-backed types; neither a row number nor a QObject pointer | Canonical UUID string |
| `RevisionId` | Monotonic document revision chosen by the owning model | Unsigned decimal string |
| `SnapshotHeader` | Schema ID, document ID, revision and reset generation | `ContractCodec::toQml()` map; both integers are decimal strings |
| `ExactTime` | Signed ticks and a positive rational rate; domain engines choose the tick convention | All three integers are decimal strings; no QML `Number` roundtrip |
| `UnitQuantity` | Finite numeric magnitude, dimension and explicit unit ID | Domain adapters convert units; the toolkit never guesses from a bare number |
| `ColorValue` | Finite components, alpha, profile ID and transfer ID | A color engine converts profiles before combining values |

`ContractCodec::snapshotHeaderFromQml()` and `exactTimeFromQml()` reject
numeric QVariant values, invalid decimal strings, overflows, invalid UUIDs
and nonpositive rates. This is intentional: JavaScript numbers lose integer
precision above 2^53. A QML control may display an approximate position, but
it must pass its exact edit request through the decimal-string contract.
Domain packages will add checked arithmetic and serialization for their own
unit/time systems; these records do not assert that every timebase converts
without rounding.

## Ownership and lifecycle

`QObject` services, models and QML items belong to their GUI thread.
`ImmutableSnapshot` is held as `SnapshotPtr` (`shared_ptr<const ...>`); workers
receive a copied pointer, `CancellationToken` and `RequestContext`. A worker
never dereferences a GUI `QObject`. Render and realtime callbacks likewise use
their own immutable or lock-free domain data and never call QML. An owner
publishes a new snapshot for an edit, rather than mutating a published payload.

`CommandInvocationContext` snapshots the request, focused view, selection,
focus scope and tool as IDs/values when a command is dispatched. The live
context stack and precedence belong to WP-011. `EditRequestContext` adds a
transaction UUID and merge key; preview/commit/rollback belong to WP-012.
Neither record carries a `QObject` pointer into a worker.

`RequestContext::accepts()` requires the same document, schema, revision and reset
generation and a token that has not been cancelled. A reset increments
`resetGeneration`, invalidating old delegate indexes and requests even when a
revision number happens to be unchanged. An entity ID must be resolved against
the current model after reset; callers must not keep a stale row pointer.
Closing a document cancels outstanding requests and drops their results.
Errors use `OperationError` with a code, user-readable message, field path and
optional backend ID; validation issues keep per-field paths. The owning service
decides whether a result can be rebased or must return `StaleRevision` or
`StaleGeneration`; this header only defines the shared vocabulary.

`CancellationSource::cancel()` is thread safe. Cancellation is cooperative:
it does not forcibly stop an I/O operation or guarantee an immediate callback.
The consumer checks the token before applying a completion and records its
own resource-release behavior.

## Capability discovery

`CapabilityRegistry` is a GUI-thread service, created per application or
workspace and exposed to QML by that owner. A C++ adapter publishes one
`CapabilityDescriptor` per feature/operation/backend triple. The entry has
availability, a required disabled reason when unavailable, backend ID/version
and priority. `resolve(feature, operation)` chooses an available backend
first, then higher priority, then lexically smaller backend ID. If none is
available it reports the highest-ranked unavailable provider and its reason;
if no provider exists it reports “No provider registered for this operation.”
`removeBackend()` removes all of a backend's entries and notifies affected
bindings. Replacing an unchanged entry is silent.

`query()` returns a QML map with feature ID, operation ID, availability,
reason, backend ID and backend version. `operations(feature)` enumerates
published operation IDs. `generation` is a decimal string and changes after
mutations. QML code that needs live enablement uses `CapabilityBinding`:

```qml
import QindaTK as Tk

Tk.CapabilityBinding {
    id: pdfExport
    registry: appCapabilities // supplied by the application
    featureId: "document.pdf"
    operationId: "export"
}
Tk.Button {
    text: qsTr("Export PDF")
    enabled: pdfExport.available
    tooltip: pdfExport.available ? text : pdfExport.reason
}
```

Publishing/querying a manifest never loads an optional engine. An application
opts into an adapter, reads its lightweight metadata and publishes the
operation entries; backend startup happens only when an accepted operation is
invoked. Availability changes are re-published. License/account UI policy is
owned by the application and is not folded into toolkit capability truth.
Unsupported document content stays in the document model for a capable
adapter or later roundtrip; an unavailable action may not discard it.

## Optional build and registration boundary

`QindaTK::qindatk` and `import QindaTK as Tk` contain the contracts and
existing controls but link no optional creative, Office or media engine.
Future feature packs use separately selected CMake targets with their own
dependencies and explicit application registration. A core-only install and
consumer must build and query missing operations without those targets.
Pack-specific QML registration, plugin packaging, ABI versioning and
discovery/loading policy are settled by the first pack that needs them;
no global scan or implicit `dlopen` is part of this version-one registry.
