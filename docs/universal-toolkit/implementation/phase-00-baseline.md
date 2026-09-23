<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Phase 00 — Baseline and existing foundations

**Execution in progress.** The live status and evidence are in the
[execution ledger](execution-ledger.md); the remaining package instructions
on this page describe required future work.

[Plan](README.md) · [Ledger](execution-ledger.md) · [Delegation rules](orchestration.md) · [Acceptance](acceptance.md)

Execute only packages whose listed dependencies are accepted. Phase numbers
organize the work; the dependency graph controls readiness. Sol remains the
orchestrator and integration owner even where Luna is the implementation lead.
Every package also inherits the common completion gate and the existing
proposal contract/completion check. Paths below identify intended change areas;
new filenames and interfaces are finalized in the owning package.

<a id="wp-001"></a>
## WP-001 — Capture the execution baseline and prepare isolated work

**Implementation lead:** Sol. **Status:** accepted; see
[baseline evidence](evidence/WP-001.md). **Depends on:** None; first execution package.

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Repository, build presets, existing tests and execution records.

1. Read the live AGENTS.md, user build policy and this plan. Record HEAD, tracked/untracked changes and active builds; preserve the existing source, gallery, icon and documentation edits.
2. Record installed Qt/compiler/CMake/backend versions and the actual Portage MAKEOPTS. Identify which Qt versions, displays, GPU paths and assistive tools can actually be tested.
3. Create an isolated integration worktree from a faithful snapshot of the intended working tree, including necessary uncommitted files. Verify its diff against the source snapshot; never silently start workers from HEAD alone.
4. Run the existing build/test baseline in the integration build directory under the required job/load policy. Classify existing failures separately from later regressions; save exact commands, exit codes and logs.
5. Initialize the execution ledger, assign the first ready packages and capture baseline geometry/images for controls affected by the audit. Register single-writer ownership of shared files.

**Accept when:** A reproducible snapshot, environment record, baseline result and conflict-free worker allocation exist; no user edits are lost.

**Delegate:** Luna may inventory tests, catalog entries and documentation against the captured snapshot. Sol creates worktrees and controls integration.

<a id="wp-002"></a>
## WP-002 — Resolve the three source behavior findings

**Implementation lead:** Luna. **Status:** accepted; see [F01–F03 evidence](evidence/WP-002.md). **Depends on:** [WP-001](phase-00-baseline.md#wp-001)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** src/qml/Label.qml, Ruler.qml, Waveform.qml; focused QML/media tests.

**Additional obligations:** [FIX-F01](supporting-scope.md#fix-f01), [FIX-F02](supporting-scope.md#fix-f02), [FIX-F03](supporting-scope.md#fix-f03).

1. Reproduce Label.selectable, Ruler pixelsPerUnit changes at constant size/origin, and zero/one/many waveform peaks. Preserve the distinction between confirmed behavior and source risk.
2. Implement a selectable text path with explicit selection/copy semantics; retain ordinary Label sizing and default behavior. Sol resolves any public API compatibility choice.
3. Correct ruler invalidation only where the reproduction requires it, and define finite one-column/empty waveform geometry without changing normal envelope mapping.
4. Exercise keyboard selection/copy, runtime ruler updates and finite waveform paths; compare compact/comfortable geometry and inspect captured pixels.
5. Document the actual contracts and close each finding with its reproduction, fix and result. A finding disproved by runtime evidence is resolved with that evidence, without an unnecessary patch.

**Accept when:** F01–F03 each have a recorded disposition and meaningful regression evidence; existing label/media behavior remains covered.

**Delegate:** One bounded Luna task per finding after Sol provides the baseline and owned test files.

<a id="wp-003"></a>
## WP-003 — Repair layout, token and documentation drift

**Implementation lead:** Luna. **Status:** accepted; see [F05/F06/DOC-01 evidence](evidence/WP-003.md). **Depends on:** [WP-001](phase-00-baseline.md#wp-001)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** KeyCap.qml, Filmstrip.qml, TableColumn.qml, Ruler.qml, Dialog.qml; theme tables and existing docs.

**Additional obligations:** [FIX-F05](supporting-scope.md#fix-f05), [FIX-F06](supporting-scope.md#fix-f06), [DOC-01](supporting-scope.md#doc-01).

1. Record current KeyCap/Filmstrip geometry and classify every cited literal as document/demo data or control chrome before editing.
2. Replace the two Row positioners with the repository layout model while preserving spacing, implicit sizing and filmstrip scrolling.
3. Add derived theme roles for missing chrome metrics with theme.h properties, theme.cpp role entries and theme_presets.cpp derivations; retain caller-provided data dimensions.
4. Repair the audited private-API, tooltip, media-index, size-role, verification-summary and graph software-renderer documentation drift against live source.
5. Check density, text growth, overflow, theme role tests and visual evidence. Submit shared theme/doc edits to Sol for integration rather than competing with other workers.

**Accept when:** F05/F06 and the documented drift list are resolved with consistent source, roles, examples and documentation.

**Delegate:** Split the layout conversion and documentation inventory between Luna tasks; Sol serializes shared theme changes.

<a id="wp-004"></a>
## WP-004 — Make the catalog a reliable API inventory

**Implementation lead:** Luna. **Status:** accepted; see [catalog evidence](evidence/WP-004.md). **Depends on:** [WP-001](phase-00-baseline.md#wp-001)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** tools/scripts/gen_catalog.py and catalog fixtures; docs/catalog.json and reference indexes.

**Additional obligations:** [FIX-F07](supporting-scope.md#fix-f07).

1. Create small representative declaration fixtures for QML_VALUE_TYPE, anonymous helpers, bridge QML, pointer spacing and multiline invokables.
2. Choose structured metadata or a documented parser strategy that covers those declarations; keep public, internal, value and bridge entries distinguishable.
3. Add fields needed to represent properties, invokables, signals and source provenance without inventing runtime-export status from a regex match.
4. Compare generated output with source registration and the 101-entry audit; explain intentional differences and verify deterministic ordering and preservation.
5. Regenerate the catalog once after integration and update the audit notes. The prior six-entry catalog refresh is already present and is not this parser fix.

**Accept when:** All cited omission patterns have fixtures; generated output is reproducible and no existing API is silently lost.

**Delegate:** Luna implements parser fixtures and the bounded parser change; Sol reviews C++ registration and compatibility semantics.

<a id="wp-005"></a>
## WP-005 — Isolate private Qt dependencies and establish support targets

**Implementation lead:** Sol. **Status:** accepted for the [verified Linux headless paths](evidence/WP-005.md); D-015 records the boundary and migration. **Depends on:** [WP-001](phase-00-baseline.md#wp-001)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** src/core/stylus_handler.*, layout_info.*, CMake/export configuration and consumer fixtures.

**Additional obligations:** [FIX-F08](supporting-scope.md#fix-f08).

1. Trace public and private Qt includes through installed headers, QML registration and PUBLIC link dependencies; reproduce a clean consumer build.
2. Record an ADR for supported Qt versions, public/private boundary and input-handler strategy. Retain tablet semantics while reducing exposed private dependencies where feasible.
3. Implement the chosen boundary with explicit lifetime and event-delivery contracts; avoid promising ABI stability for a private Qt base.
4. Build/run the relevant input/layout and clean-consumer fixtures on Qt 6.8 and the selected newer supported version, obtaining environments if needed. Record actual unavailable targets separately.
5. Publish the initial platform/backend matrix and upgrade procedure; feed packaging constraints into WP-093.

**Accept when:** F08 has an implemented mitigation or explicit supported-version boundary with consumer evidence; the declared minimum is tested.

**Delegate:** Luna can inventory includes, prepare consumer fixtures and update documentation after the interface is settled.

**Astra trigger:** Ask Astra only if a public Qt replacement cannot preserve required stylus/grab behavior, or private-base ABI isolation remains unresolved after a concrete prototype.

<a id="wp-006"></a>
## WP-006 — Complete layout and scrolling contracts

**Implementation lead:** Sol. **Status:** accepted; [integrated evidence and support limits](evidence/WP-006.md). **Depends on:** [WP-002](phase-00-baseline.md#wp-002), [WP-003](phase-00-baseline.md#wp-003), [WP-005](phase-00-baseline.md#wp-005)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Flex/Grid/Box/Scroll, layout metadata and layout tests; docs/layout.md.

**Additional obligations:** [FND-01](supporting-scope.md#fnd-01), [FND-02](supporting-scope.md#fnd-02), [FND-03](supporting-scope.md#fnd-03), [FND-04](supporting-scope.md#fnd-04), [FND-05](supporting-scope.md#fnd-05).

1. Write small geometry fixtures for mixed-font baselines, intrinsic wrapping, invalid tracks, RTL start/end, rounded per-side borders and nested scrollers.
2. Implement true Flex baseline alignment. Define and implement the accepted Grid grammar, including explicit decisions and fixtures for auto-fill/auto-fit and responsive/intrinsic tracks; preserve existing 1fr semantics.
3. Add logical direction and narrow-window behavior consistently across containers. Implement the agreed rounded-border/clipping contract or record the conditional feature decision without advertising support.
4. Implement nested wheel/keyboard ownership, focus reveal and scroll chaining with a documented overscroll policy; preserve Scroll horizontal-overflow defaults.
5. Verify geometry at both densities, RTL and enlarged text on GPU/software paths. Write an ADR before compatibility-sensitive semantic changes and migrate affected examples.

**Accept when:** All five foundation rows have implemented guarantees and tested support limits; baseline/RTL/scroll claims have geometry and inspected pixel evidence.

**Delegate:** Luna owns independent fixture scenes, documentation and localized QML adaptations; Sol owns layout algorithms and event routing.

**Astra trigger:** Use Astra for an unresolved baseline/intrinsic-sizing algorithm or nested-input ownership conflict, with failing fixtures and candidate algorithms attached.

<a id="wp-007"></a>
## WP-007 — Harden theme, popup and control interaction behavior

**Implementation lead:** Sol. **Status:** accepted at the documented support level; [integrated acceptance evidence](evidence/WP-007.md). **Depends on:** [WP-002](phase-00-baseline.md#wp-002), [WP-003](phase-00-baseline.md#wp-003)

**Proposal scope:** Supporting implementation/qualification work; no new proposal ID.

**Change area:** Theme/Density, custom controls, SectionHeader/TimeRuler and popup infrastructure.

**Additional obligations:** [FIX-F04](supporting-scope.md#fix-f04), [FND-07](supporting-scope.md#fnd-07), [FND-08](supporting-scope.md#fnd-08), [FND-09](supporting-scope.md#fnd-09), [FND-10](supporting-scope.md#fnd-10).

1. Inventory current controls against reset/default, mixed, invalid, read-only, disabled, user/programmatic notification, focus and accessible-action semantics.
2. Add missing current-control keyboard and assistive actions, beginning with SectionHeader and TimeRuler; test behavior through the same semantic operation as pointer input.
3. Complete high-contrast, text scaling and hit-target roles. Decide scoped-theme ownership in an ADR; retain process-wide Tk.Theme compatibility and add scoped behavior only through the agreed contract.
4. Formalize popup bounds, overflow, focus return, dynamic-anchor updates and monitor/scale changes. Exercise nested menus, dialogs and popovers in small windows.
5. Capture current-control behavior fixtures and visual evidence, document derived roles, and provide the baseline policy consumed by PLT-06/08 and every new control.

**Accept when:** F04 and five foundation rows have evidence-backed behavior/decisions; current controls remain usable with keyboard, large text and high contrast.

**Delegate:** Luna can repair audited controls and build focused scenes after Sol fixes the shared policy. Sol owns theme scope and popup focus architecture.
