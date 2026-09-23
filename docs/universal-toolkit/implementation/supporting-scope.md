<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Supporting scope beyond the 212 named proposals

**64 obligations, each with a primary owner.** These prevent the work from
ending at a list of new widget names. See [proposal coverage](proposal-coverage.md)
for the 212 additions and [the ledger](execution-ledger.md) for package state.

The original [source audit](../reports/04-source-audit.md) distinguishes verified
source findings from runtime risks. The [foundation table](../proposed/00-architecture.md)
includes conditional design investigations; those require an explicit supported
behavior/ADR decision. Such a decision cannot defer a mandatory named proposal.

Engine and visualization rows inherit the complete consuming proposal contracts.
Their owner is accountable for the combined proof where several packages supply
parts. Cross-domain combined evidence is completed at WP-090 or the named later
owner, after all relevant dependencies are accepted.

## Audit findings — 9

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="fix-f01"></a>FIX-F01 | Label.selectable no-op | Implement actual selectable/copy behavior with a compatibility-preserving default and focused evidence. | [WP-002](phase-00-baseline.md#wp-002) |
| <a id="fix-f02"></a>FIX-F02 | Ruler invalidation risk | Reproduce pixelsPerUnit changes at fixed geometry; fix invalidation if confirmed and record the rendered result. | [WP-002](phase-00-baseline.md#wp-002) |
| <a id="fix-f03"></a>FIX-F03 | One-column waveform risk | Verify zero/one/many peaks and finite coordinates; define and test the edge cases. | [WP-002](phase-00-baseline.md#wp-002) |
| <a id="fix-f04"></a>FIX-F04 | Custom-control keyboard/accessibility | Repair current SectionHeader/TimeRuler semantics and carry the audit into all custom composites. | [WP-007](phase-00-baseline.md#wp-007) |
| <a id="fix-f05"></a>FIX-F05 | Forbidden Row positioners | Migrate KeyCap/Filmstrip to toolkit layout with before/after geometry and inspected captures. | [WP-003](phase-00-baseline.md#wp-003) |
| <a id="fix-f06"></a>FIX-F06 | Missing theme metrics | Classify data versus chrome literals, derive missing roles and verify density/text growth. | [WP-003](phase-00-baseline.md#wp-003) |
| <a id="fix-f07"></a>FIX-F07 | Catalog parser omissions | Cover value types, bridge, pointer properties and multiline invokables with representative declaration fixtures. | [WP-004](phase-00-baseline.md#wp-004) |
| <a id="fix-f08"></a>FIX-F08 | Private Qt exposure | Implement/test an explicit public/private boundary and actual supported-version/consumer policy. | [WP-005](phase-00-baseline.md#wp-005) |
| <a id="fix-f09"></a>FIX-F09 | Floating shade persistence | Declare transient versus persisted behavior; persistent arrangement remains in DockModel. | [WP-014](phase-01-services.md#wp-014) |

## Documentation drift — 1

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="doc-01"></a>DOC-01 | Existing documentation drift | Repair architecture/private-API, tooltip, media index, theme size, test inventory and graph renderer comments against source. | [WP-003](phase-00-baseline.md#wp-003) |

## Existing foundations — 10

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="fnd-01"></a>FND-01 | Flex baseline and intrinsic behavior | True baseline alignment plus documented wrapping/intrinsic rules; mixed-font geometry and pixel fixtures. | [WP-006](phase-00-baseline.md#wp-006) |
| <a id="fnd-02"></a>FND-02 | Grid grammar and responsive tracks | Implement the accepted grammar, document auto-fill/fit/intrinsic decisions and verify invalid input, placement and overflow. | [WP-006](phase-00-baseline.md#wp-006) |
| <a id="fnd-03"></a>FND-03 | Logical direction and adaptive layout | RTL start/end and narrow-window/long-text behavior across containers, preserving document geometry. | [WP-006](phase-00-baseline.md#wp-006) |
| <a id="fnd-04"></a>FND-04 | Rounded per-side Box borders | Record the original conditional support decision; if added, verify corners/clipping on GPU and software paths. | [WP-006](phase-00-baseline.md#wp-006) |
| <a id="fnd-05"></a>FND-05 | Nested scrolling and focus reveal | Wheel/keyboard routing, chaining and overscroll policy with nested inspectors, grids and canvases. | [WP-006](phase-00-baseline.md#wp-006) |
| <a id="fnd-06"></a>FND-06 | Viewport evolution | CanvasView supplies rotation, touch/trackpad policy, synchronized transforms and rendering hooks while preserving existing use. | [WP-040](phase-03-authoring.md#wp-040) |
| <a id="fnd-07"></a>FND-07 | Theme and density completeness | Derived roles, high contrast, independent text scale and sufficient hit targets with usable workflows. | [WP-007](phase-00-baseline.md#wp-007) |
| <a id="fnd-08"></a>FND-08 | Optional scoped theme decision | ADR defines ownership and compatibility with process-wide Tk.Theme; implement only the agreed scoped contract, never imply a singleton became per-window automatically. | [WP-007](phase-00-baseline.md#wp-007) |
| <a id="fnd-09"></a>FND-09 | Popup behavior | Bounds, overflow, dynamic anchors, focus return and monitor changes keep menus/dialogs/popovers reachable. | [WP-007](phase-00-baseline.md#wp-007) |
| <a id="fnd-10"></a>FND-10 | Uniform control editing semantics | Reset/default, mixed/invalid/read-only/disabled states and user versus programmatic notification contracts. | [WP-007](phase-00-baseline.md#wp-007) |

## Engine and integration obligations — 25

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="eng-01"></a>ENG-01 | Shared typed model and provider contracts | Stable identity/revisions, typed precision, capability discovery and GUI/worker/render/realtime ownership. | [WP-010](phase-01-services.md#wp-010) |
| <a id="eng-02"></a>ENG-02 | Native persistence and recovery | Actual versioned serialization, atomic save and crash/failure recovery with newer-edit protection. | [WP-013](phase-01-services.md#wp-013) |
| <a id="eng-03"></a>ENG-03 | Graphics and color engine | Combined WP-040/043/044/045 vector, raster, brush, tile, compositing and color output proof. | [WP-045](phase-03-authoring.md#wp-045) |
| <a id="eng-04"></a>ENG-04 | Rich text and pagination | Actual structured text layout/shaping/pagination with persistent content and valid selection/cursor mapping. | [WP-050](phase-04-office.md#wp-050) |
| <a id="eng-05"></a>ENG-05 | Workbook calculation | Actual parsing, dependencies, references and recalculation plus reversible structural sheet operations. | [WP-055](phase-04-office.md#wp-055) |
| <a id="eng-06"></a>ENG-06 | Slide presentation engine | Persistent masters/scenes/notes and deterministic time evaluation with audience/presenter output. | [WP-056](phase-04-office.md#wp-056) |
| <a id="eng-07"></a>ENG-07 | PDF backend | Actual page/text/annotation behavior and operation-specific serialization/output evidence. | [WP-054](phase-04-office.md#wp-054) |
| <a id="eng-08"></a>ENG-08 | Realtime audio and devices | Actual recording/playback, graph/clock scheduling, offline output and callback deadline/device evidence. | [WP-060](phase-05-audio.md#wp-060) |
| <a id="eng-09"></a>ENG-09 | Waveform and spectral DSP | Peak reduction, FFT conventions, real sample/spectral edits and verified rendered output. | [WP-063](phase-05-audio.md#wp-063) |
| <a id="eng-10"></a>ENG-10 | MIDI and sequencing | Exact real event scheduling, audition shutdown, controller/expression precision and persistent patterns. | [WP-064](phase-05-audio.md#wp-064) |
| <a id="eng-11"></a>ENG-11 | Notation engine | Structured score layout, rhythm semantics, musical playback mapping and supported notation output. | [WP-067](phase-05-audio.md#wp-067) |
| <a id="eng-12"></a>ENG-12 | Pitch analysis/correction | Actual detection and correction with known signals, confidence/failure states and output checks. | [WP-068](phase-05-audio.md#wp-068) |
| <a id="eng-13"></a>ENG-13 | Media decode and evaluation | Real exact frame/stream providers, playback sync, basic composition and encode, expanded by later motion packages. | [WP-070](phase-06-video.md#wp-070) |
| <a id="eng-14"></a>ENG-14 | Tracking and rotoscoping | Actual feature tracking and time-varying mask evaluation with known motion/occlusion fixtures. | [WP-075](phase-06-video.md#wp-075) |
| <a id="eng-15"></a>ENG-15 | 3D, rig, UV and texture engines | WP-076/077 real scene rendering and persistent geometry/rig/texture operations, including projection/constraint fixtures. | [WP-077](phase-06-video.md#wp-077) |
| <a id="eng-16"></a>ENG-16 | Plugin host | Real declared plugin ABI lifecycle, processing, state recall, failure containment and editor integration. | [WP-066](phase-05-audio.md#wp-066) |
| <a id="eng-17"></a>ENG-17 | Collaboration backend | Actual local clients, transport, store, merge/convergence and offline recovery, separate from presence. | [WP-082](phase-07-integrations.md#wp-082) |
| <a id="eng-18"></a>ENG-18 | Diagram/CAD engines | Real graph layout, routed flow and declared geometry/constraint operations with persistent data. | [WP-083](phase-07-integrations.md#wp-083) |
| <a id="eng-19"></a>ENG-19 | Geographic providers | Real projections, vector/raster/tiles and tested coordinate/source capability semantics. | [WP-084](phase-07-integrations.md#wp-084) |
| <a id="eng-20"></a>ENG-20 | Scientific volume providers | Typed arrays, spacing/orientation, resampling and bounded slice/volume rendering with known phantoms. | [WP-086](phase-07-integrations.md#wp-086) |
| <a id="eng-21"></a>ENG-21 | Mail/calendar/task providers | Persistent local drafts/messages/events/tasks and tested recurrence/provider acknowledgement/failure semantics. | [WP-058](phase-04-office.md#wp-058) |
| <a id="eng-22"></a>ENG-22 | Database/form/report providers | Actual relational data, schema introspection, form binding, report evaluation and paginated output. | [WP-059](phase-04-office.md#wp-059) |
| <a id="eng-23"></a>ENG-23 | Statistical and chart layout providers | Computed statistics/hierarchy/coordinate semantics, real chart families and scheduling operations. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="eng-24"></a>ENG-24 | Extension/code/terminal providers | Actual script/extension runtime, diagnostics/language-service boundary and PTY/process lifecycle. | [WP-081](phase-07-integrations.md#wp-081) |
| <a id="eng-25"></a>ENG-25 | Format/device/output qualification | Each advertised format operation and relevant device/print/media output has explicit capability and fixture evidence. | [WP-089](phase-07-integrations.md#wp-089) |

## Visualization families — 14

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="vis-01"></a>VIS-01 | Line, step, area, stacked area | Explicit x/y, gap/interpolation/stack semantics, selection and export through actual series providers. | [WP-030](phase-02-controls-data.md#wp-030) |
| <a id="vis-02"></a>VIS-02 | Bar, grouped/stacked, waterfall | Categorical axes, signed contributions, totals and accessible data alternatives. | [WP-030](phase-02-controls-data.md#wp-030) |
| <a id="vis-03"></a>VIS-03 | Scatter, bubble, vector fields | Point identity, size/direction/units and correct hit/selection coordinates. | [WP-030](phase-02-controls-data.md#wp-030) |
| <a id="vis-04"></a>VIS-04 | Pie/donut, treemap, sunburst | Actual part-to-whole and hierarchy layouts with explicit totals and nonvisual data access. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-05"></a>VIS-05 | Heatmap and matrix | Typed cells/coordinates, color mapping, missing values and bounded tiled data work. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-06"></a>VIS-06 | Histogram, density, box, violin | WP-030/031 declared bins/statistical summaries/estimators and known-answer numerical fixtures. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-07"></a>VIS-07 | Error bars and uncertainty bands | Typed interval endpoints and their stated statistical/domain meaning, preserved in display/export. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-08"></a>VIS-08 | Radar, polar and radial plots | Declared angular/radial units and ordering with transformed hit/axis behavior. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-09"></a>VIS-09 | OHLC/candlestick | Actual OHLC values and time/calendar-gap adapter semantics; no financial data service is implied. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-10"></a>VIS-10 | Bullet, gauge and KPI displays | Defined current/target/range/threshold semantics with labels and accessible values. | [WP-031](phase-02-controls-data.md#wp-031) |
| <a id="vis-11"></a>VIS-11 | Networks, flows and Sankey | Node/edge identity, actual layout, direction and quantity/conservation fixtures. | [WP-083](phase-07-integrations.md#wp-083) |
| <a id="vis-12"></a>VIS-12 | Geographic/spatial plots | Geometry plus CRS/attributes via the real MapView provider. | [WP-084](phase-07-integrations.md#wp-084) |
| <a id="vis-13"></a>VIS-13 | Gantt, calendars and resources | Joined WP-031/032/058 workflow with intervals, recurrence/working calendars, dependencies and allocation. | [WP-090](phase-08-qualification.md#wp-090) |
| <a id="vis-14"></a>VIS-14 | 3D scatter, surface and volume | Joined WP-076/085/086 providers with typed coordinates, units and declared sampling/interpolation. | [WP-086](phase-07-integrations.md#wp-086) |

## Final qualification — 5

| ID | Obligation | Required evidence / disposition | Primary owner |
| --- | --- | --- | --- |
| <a id="qual-01"></a>QUAL-01 | Performance and failure qualification | Reproducible large datasets, measured latency/memory/work bounds and recovery/failure stress evidence. | [WP-091](phase-08-qualification.md#wp-091) |
| <a id="qual-02"></a>QUAL-02 | Accessibility, locale and input qualification | Complete real assistive/keyboard/IME/RTL/scale/input workflows for claimed targets. | [WP-092](phase-08-qualification.md#wp-092) |
| <a id="qual-03"></a>QUAL-03 | Representative applications | Every proposal appears in meaningful integrated scenarios against real functional providers. | [WP-090](phase-08-qualification.md#wp-090) |
| <a id="qual-04"></a>QUAL-04 | Complete SDK and documentation | Clean installed consumers, exports/packages, accurate reference/catalog/examples and migration guidance. | [WP-093](phase-08-qualification.md#wp-093) |
| <a id="qual-05"></a>QUAL-05 | Final complete-scope audit | All required items accepted with source/evidence links; no placeholder or unrun capability is called complete. | [WP-094](phase-08-qualification.md#wp-094) |

## Count reconciliation

9 findings + 1 documentation obligation + 10 foundation rows + 25 engine
obligations + 14 visualization families + 5 qualification obligations = **64**.

These counts describe planning coverage, not completed implementation or a
claim that all engine capabilities have equal size. Every row is initially planned.
