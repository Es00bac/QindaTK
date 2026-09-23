<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 08 — Qualification and complete handoff

**PLANNED — no implementation or execution is claimed by this plan.**

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-090"></a>
## WP-090 — Finish representative applications and domain acceptance

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-021](phase-02-controls-data.md#wp-021), [WP-033](phase-02-controls-data.md#wp-033), [WP-045](phase-03-authoring.md#wp-045), [WP-052](phase-04-office.md#wp-052), [WP-053](phase-04-office.md#wp-053), [WP-054](phase-04-office.md#wp-054), [WP-055](phase-04-office.md#wp-055), [WP-056](phase-04-office.md#wp-056), [WP-057](phase-04-office.md#wp-057), [WP-058](phase-04-office.md#wp-058), [WP-059](phase-04-office.md#wp-059), [WP-062](phase-05-audio.md#wp-062), [WP-063](phase-05-audio.md#wp-063), [WP-064](phase-05-audio.md#wp-064), [WP-065](phase-05-audio.md#wp-065), [WP-066](phase-05-audio.md#wp-066), [WP-067](phase-05-audio.md#wp-067), [WP-068](phase-05-audio.md#wp-068), [WP-071](phase-06-video.md#wp-071), [WP-072](phase-06-video.md#wp-072), [WP-073](phase-06-video.md#wp-073), [WP-074](phase-06-video.md#wp-074), [WP-075](phase-06-video.md#wp-075), [WP-077](phase-06-video.md#wp-077), [WP-078](phase-06-video.md#wp-078), [WP-081](phase-07-integrations.md#wp-081), [WP-082](phase-07-integrations.md#wp-082), [WP-083](phase-07-integrations.md#wp-083), [WP-084](phase-07-integrations.md#wp-084), [WP-085](phase-07-integrations.md#wp-085), [WP-086](phase-07-integrations.md#wp-086), [WP-087](phase-07-integrations.md#wp-087), [WP-088](phase-07-integrations.md#wp-088), [WP-089](phase-07-integrations.md#wp-089)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Runnable reference applications, recipes and end-to-end scenario evidence.

**Additional obligations:** [QUAL-03](supporting-scope.md#qual-03), [VIS-13](supporting-scope.md#vis-13).

1. Assemble the graphics, documents, sheets, slides, DAW, NLE and motion reference applications from the implemented packs.
2. Add compact productivity, developer, diagram/CAD, GIS/science/volume and collaboration demonstrations so every named proposal is exercised.
3. Run each scenario against a real functional backend: create/import, edit, cancel, undo/redo, save/reopen and declared output where meaningful.
4. Include Gantt/calendar/resource scheduling as a joined example, and verify shared selections/properties/commands across multiple views.
5. Trace every proposal to at least one meaningful scenario and repair missing integration; attractive isolated gallery components cannot satisfy this gate.

**Accept when:** All 212 proposals and every domain pack appear in runnable, behaviorally verified examples with real providers.

**Delegate:** Luna implements example composition and scenario scripts against stable APIs; Sol integrates engines and resolves cross-pack defects.

<a id="wp-091"></a>
## WP-091 — Complete performance and fault qualification

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-090](phase-08-qualification.md#wp-090)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Benchmark fixtures, stress/failure harnesses and regression budgets.

**Additional obligations:** [QUAL-01](supporting-scope.md#qual-01).

1. Freeze reproducible dataset generators, build type, hardware/backend descriptions and operation-specific budgets from early measurements.
2. Run large grids/trees/canvases/timelines/documents/telemetry and multi-window fixtures, measuring latency distributions, memory, delegates and work queues.
3. Exercise repeated open/close, model reset, provider delay/failure, canceled jobs, recovery, large undo and render/device context loss.
4. Measure realtime audio callback behavior separately from GUI frame rate, and inspect resource ownership with suitable native diagnostics.
5. Repair measured bottlenecks/leaks/correctness failures and rerun only affected qualification plus required integration gates; record results and residual supported limits.

**Accept when:** Declared scale, latency, memory and recovery behavior has repeatable evidence, with no unexplained regression against accepted budgets.

**Delegate:** Luna can generate fixtures and aggregate measurements; Sol profiles, prioritizes fixes and owns resource/realtime correctness.

<a id="wp-092"></a>
## WP-092 — Complete accessibility, localization and input qualification

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-090](phase-08-qualification.md#wp-090)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Assistive/keyboard/IME scenarios, translation fixtures and display/input matrix.

**Additional obligations:** [QUAL-02](supporting-scope.md#qual-02).

1. Run complete keyboard and real assistive workflows through each representative application, including custom grids, canvases and timelines.
2. Exercise IME composition, bidi/RTL, long translations, locale parsing, high contrast and independent large text.
3. Exercise mouse, touch, pen and trackpad paths where claimed, including capture loss, monitor changes and mixed display scales.
4. Inspect geometry and screenshots for dense/default/adapted layouts; record assistive tool, OS and device evidence explicitly.
5. Fix failures in shared services first, then domain adapters; unavailable hardware/assistive paths remain open evidence requirements when support is claimed.

**Accept when:** Every claimed interaction/platform capability is usable in end-to-end workflows and documented with actual verification.

**Delegate:** Luna repairs bounded presentation/label/focus issues and prepares scenarios; Sol owns cross-control and native accessibility/input fixes.

<a id="wp-093"></a>
## WP-093 — Complete SDK, documentation and distributable artifacts

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-090](phase-08-qualification.md#wp-090), [WP-091](phase-08-qualification.md#wp-091), [WP-092](phase-08-qualification.md#wp-092)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Public API/versioning, catalog, existing documentation, examples, CMake/install and Gentoo packaging.

**Additional obligations:** [QUAL-04](supporting-scope.md#qual-04).

1. Reconcile implemented contracts with proposal/current references, controls/layout/theme/media docs, stable objectNames and ADRs; label remaining unsupported operations accurately.
2. Regenerate catalog and API indexes from source; verify examples use Tk-qualified imports, Theme roles and supported layout primitives.
3. Finalize public/internal/optional boundaries, version/migration policy and header/QML exports. Split hand-written files exceeding 500 non-blank lines.
4. Build/install packages and run clean external C++/QML consumers with core-only and selected domain packs, preserving configured build limits.
5. Produce local release artifacts, dependency/license notices, capability/result matrices and migration examples; do not imply remote publication has occurred.

**Accept when:** A clean consumer can install/import/link the documented SDK; shipped docs and catalog match real functionality and support evidence.

**Delegate:** Luna handles documentation, inventory and consumer fixtures; Sol owns exports, versioning and packaging integration.

<a id="wp-094"></a>
## WP-094 — Audit complete scope and hand off the finished implementation

**Implementation lead:** Sol. **Status:** planned. **Depends on:** [WP-093](phase-08-qualification.md#wp-093)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Execution ledger, manifest coverage, evidence index and final report.

**Additional obligations:** [QUAL-05](supporting-scope.md#qual-05).

1. Check all 84 packages, 212 proposal IDs, nine findings, ten foundation rows, engine obligations and visualization families against evidence.
2. Resolve any missing implementation, test, runtime proof, docs or integration links; no package is complete because only a class name or placeholder exists.
3. Confirm conditional foundation decisions are explicit and that no mandatory proposal or P3 package was silently deferred.
4. Verify accepted commits/artifacts are present in the integration result, user changes remain preserved and the final supported capability matrix is truthful.
5. Report delivered applications/packs, measured qualification, exact supported boundaries and local artifact paths. Any unmet required item keeps the overall program incomplete.

**Accept when:** All mandatory scope is accepted with traceable evidence; the final report is self-contained and no required work is hidden behind a completion claim.

**Delegate:** Luna may independently check links, coverage and artifact inventory; Sol alone accepts the final integrated result.
