<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Backend and engine execution strategy

[Plan](README.md) · [Engine ownership](supporting-scope.md) · [Acceptance](acceptance.md)

Widgets need functional models and engines to satisfy this program. Core QindaTK
should still be usable without loading a document parser, audio runtime, codec,
collaboration client or 3D engine. Preserve the boundary described in the
[original engine report](../reports/03-engine-boundaries.md).

## Repeat this sequence for each engine obligation

1. **Extract required operations.** Read every consuming proposal's contract and
   completion check. Write the minimal real workflow and failure case; do not
   reduce scope to whatever a conveniently available library happens to expose.
2. **Inspect what exists.** Check live source/dependencies and reusable Qt or
   system facilities. Record supported versions and native/export boundaries.
3. **Compare a small candidate set.** Use current primary documentation to check
   APIs, formats, supported platforms, deployment and license compatibility.
   Inspect actual metadata before adding a dependency. No candidate versions or
   product support claims are frozen by this documentation-only plan.
4. **Prototype the hardest operation.** Use a tiny real dataset, not only a mock
   interface. Examples: table flow across page breaks, formula reference changes,
   frame-exact seek, interrupted plugin loading or two-client concurrent deletes.
5. **Select and record an ADR.** Sol decides from measured evidence, names the
   chosen version and accepted operation set, and explains any compatibility
   constraint. Routine backend choices do not need another user approval gate.
6. **Implement a typed boundary.** Define identity, revision, ownership, precision,
   cancellation, errors, resource lifetime and serialization. Add a deterministic
   test provider alongside the real provider for repeatable contract checks.
7. **Implement actual operations.** Integrate the provider and required missing
   functionality; connect commands/transactions rather than embedding engine
   objects into QML delegates. Keep optional dependencies separately selectable.
8. **Exercise failure and scale.** Reject obsolete results, bound work/cache
   residency, recover from failures, and measure the relevant latency/deadlines.
9. **Qualify output.** Compare semantic data, pixels, samples or coordinates with
   known references and declared tolerances. A rendering check alone does not
   prove editable import or roundtrip compatibility.
10. **Publish capabilities and evidence.** Register exact available operations,
    backend/version provenance and disabled reasons. Add fixtures for each claim;
    update the capability matrix when a provider changes.

If no candidate supports a mandatory operation, Sol either implements the missing
domain logic or records the actual dependency blocker and continues independent
work. It cannot accept the named proposal by hiding the control or substituting
an unconnected visual placeholder.

## Reference provider requirements by domain

| Domain | Required functional reference path | Proof that a facade alone cannot supply |
| --- | --- | --- |
| Application documents | Native versioned document store, atomic save and recovery | Failed/interrupted save plus later edit restores a valid revision |
| Graphics | Object/path and tiled raster model, brush/compositor and document color transforms | Actual edited pixels/geometry survive undo, reopen and supported output |
| Text/publishing | Structured text layout, pagination and serializer | Mixed scripts, tables and anchored content flow across pages with valid cursors |
| Workbook | Formula parser, dependency calculation and reference rewrite | Inserting a row changes references/results correctly and reversibly |
| Presentations | Master inheritance, scene storage and time evaluation | Changed master/animation produces expected saved and presented output |
| PDF | Page/text/annotation provider with real annotation serialization | An annotation edits, exports and reopens at the correct page coordinates |
| Audio | Device/clock/graph engine plus offline renderer | Real samples match edits/automation; callback deadlines are measured |
| Spectral/pitch | Actual analysis/processing providers | Known tones/bins/contours and processed audio have declared accuracy |
| MIDI/notation | Event scheduler, persistent musical model and score layout | Exact event playback plus rhythm/layout fixtures and score persistence |
| Plugins | One or more explicitly supported real plugin hosts | Scan, process, state recall, editor lifecycle, failure and unload work |
| Video/motion | Decode, frame/time mapping, composition and output | Boundary frames and audio sync survive edits and real encoding |
| Tracking/roto | Actual feature tracking and time-varying masks | Known motion/occlusion and rendered masks match declared behavior |
| 3D/rig/texture | Real scene, geometry, rig and rendering provider | Poses, transforms, UVs and projected paint produce persistent scene output |
| Mail/calendar/tasks | Local persistent models and tested provider protocol | Draft/recurrence/offline behavior works against local test endpoints |
| Database/forms/reports | Actual local relational and paginated report providers | Bound data, totals and page output change correctly with the data/schema |
| Collaboration | Actual local multi-client transport/store and merge logic | Clients converge after reordered messages and offline concurrent edits |
| Developer tools | Actual local script/extension/runtime and PTY providers | Commands execute/cancel, diagnostics update and terminal processes resize/exit |
| Diagrams/CAD | Graph layout and declared geometry/constraint provider | Routed edges/flows and constrained geometry satisfy known fixtures |
| GIS | Actual projection, vector/raster data and tile providers | Coordinate conversions and feature picking remain correct across layers |
| Science/volumes | Typed arrays, numerical sampling and rendering providers | Known analytic functions/voxel phantoms preserve units, orientation and values |

The native reference schema is a toolkit test/application format. It is not a
claim of compatibility with a commercial suite's native documents or sessions.
Reference apps prove the reusable abstractions; each published external-format
claim has its own operation-level fixture set.

## Suggested implementation boundaries

These are proposed areas, not pre-existing directories or an instruction to move
the entire codebase before useful work begins:

- `src/core/`: small shared contracts and Qt-facing services independent of
  optional heavy backends.
- `src/qml/`: themed templates, composites, delegates and overlays using accepted
  services/models and the single Tk naming convention.
- Optional domain libraries/adapters: authoritative models, engines and provider
  implementations with explicit build/install dependency boundaries. Sol records
  final locations and package registration in WP-010's ADR.
- `tests/`: focused numerical/model/interaction/adapter conformance fixtures and
  separate integration/qualification scenarios.
- `examples/`: runnable reference applications, small galleries and fixture
  providers clearly labeled by mock versus functional backend.

Avoid introducing separate overlapping command, undo, time, selection or color
frameworks inside each domain. Reuse accepted contracts. Keep domain-specific
operations behind policy/adapters: spreadsheet ranges, text positions, media
intervals and object selections are not interchangeable storage structures.

A work-package dependency specifies implementation readiness, not a mandatory
runtime link dependency. Extract shared graph, page-output or scene contracts
into the appropriate lower layer when a second pack consumes them; a CAD app
must not load a video compositor merely because it reuses graph presentation.
Qualify core-only and independent pack combinations in WP-088/093.

## Platform and format policy

The supplied repository is a Linux development environment. Begin there and
verify the declared Qt 6.8 minimum plus the chosen current supported build.
WP-005 records the intended support matrix; WP-088/092 qualify the actual claimed
OS, rendering, input and assistive combinations. Windows/macOS or hardware paths
are not automatically certified by compiling Linux or by running a mock device.

For each adapter record: format/version, operation, feature subset, backend
version, known limitations, fixture IDs, output comparison and actual platform.
Do not use one “supports PSD/DOCX/AAF/etc.” boolean when rendering, editable
import and lossless roundtrip have different evidence. Add the exact capability
needed by an application rather than assuming suite-wide interoperability.

Unavailable proprietary SDKs, devices or accounts are concrete dependencies.
Sol can implement/test interface contracts and local providers while access is
missing, but any required unverified integration remains open. No real message,
cloud deployment or production mutation is needed to test the local reference
workflows in this plan.

## Mock versus real evidence

| Evidence | What it establishes | What it does not establish |
| --- | --- | --- |
| Deterministic clock/provider | View/model protocol, cancellation, state transitions | Real timing, recording, decoder or device behavior |
| Small native model | Identity, transactions, persistence contract | Third-party format fidelity or advanced domain algorithms |
| Headless software rendering | Instantiation, many geometry/interaction/render checks | GPU resource lifetime or native display/input behavior |
| Known-output numerical fixture | Correctness for declared inputs/tolerances | All input domains or qualitative perceptual quality |
| Actual backend vertical slice | End-to-end behavior for that path/environment | Unrun platforms, formats, devices or operating conditions |

Keep both kinds of providers where they add useful test coverage. Complete the
real path before accepting a proposal whose contract requires it.
