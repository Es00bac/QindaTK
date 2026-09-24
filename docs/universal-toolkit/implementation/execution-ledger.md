<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Execution ledger

**Execution state: 33 packages accepted, five active, two in review, and 44 planned.**
Sol updates this ledger during execution. Use `planned`, `ready`, `active`,
`review`, `accepted`, or `blocked`.
A blocked row names the missing prerequisite and unblock action in its evidence
record. Keep unrelated ready work moving. A row reaches `accepted` only through
the [completion gate](acceptance.md).

## Review batch closed

WP-020 and WP-022 were accepted earlier. The remaining 12 review packages
passed the consolidated Qt 6.11/6.8 repository, QML lint, staged-consumer and
visual gates recorded in the [2026-09-23 acceptance record](evidence/review-acceptance-2026-09-23.md).
WP-031 subsequently passed its own installed-consumer gate and is accepted.
WP-040 is in review: its WP-033 dependency remains open. The Qt 6.8
offscreen RHI readback repair passed the full integrated suite. WP-048 is accepted
after its time-axis and host-media boundaries were reconciled with D-045;
both full integrated suites and its installed consumers pass. WP-050 and WP-055
remain active in separate Opus worktrees; their draft code is not integrated
or accepted. WP-033 has a dual-Qt editable, sortable and filterable two-document
workbench plus installed lifecycle consumers; its remaining end-to-end gates
are recorded in its evidence. WP-041 now has a document-space snap query,
guide layer, lasso and reference-canvas group move/resize/rotation controller, while
WP-042 has layer, navigator, alignment and artboard presentation.
Both spatial packages still have model, command and acceptance gates open.
WP-043 has CurveEditor, GradientEditor and ColorPicker presentation plus
numerical RGB ICC and typed curve evaluators. Tangent UI, managed-display and
typed transaction gates remain open.

Record details in one package evidence file per WP using the
[evidence template](task-templates.md#evidence-record). Create those files when
work begins, split long records, and link them in the last column. Sol updates
manifest status consistently with this table. Never rewrite historical results
as though a later fix had already passed the earlier run.

| Package | Phase | Lead | State | Accepted commit / evidence |
| --- | --- | --- | --- | --- |
| [WP-001 — Capture the execution baseline and prepare isolated work](phase-00-baseline.md#wp-001) | 00 | Sol | accepted | [Checkpoint `924750b4`; build 0; ctest 15/15; visual baseline](evidence/WP-001.md) |
| [WP-002 — Resolve the three source behavior findings](phase-00-baseline.md#wp-002) | 00 | Luna | accepted | [F01–F03 integrated; 15/15 tests](evidence/WP-002.md) |
| [WP-003 — Repair layout, token and documentation drift](phase-00-baseline.md#wp-003) | 00 | Luna | accepted | [F05/F06/DOC-01 integrated; 15/15 tests](evidence/WP-003.md) |
| [WP-004 — Make the catalog a reliable API inventory](phase-00-baseline.md#wp-004) | 00 | Luna | accepted | [101-entry catalog and generator evidence](evidence/WP-004.md) |
| [WP-005 — Isolate private Qt dependencies and establish support targets](phase-00-baseline.md#wp-005) | 00 | Sol | accepted | [Qt 6.8.3/6.11.1 implementation and staged-consumer evidence](evidence/WP-005.md); [D-015](../../decisions.md#d-015--keep-private-qt-behind-a-public-stylus-facade) |
| [WP-006 — Complete layout and scrolling contracts](phase-00-baseline.md#wp-006) | 00 | Sol | accepted | [Integrated guarantees, tests, pixels and support limits](evidence/WP-006.md) |
| [WP-007 — Harden theme, popup and control interaction behavior](phase-00-baseline.md#wp-007) | 00 | Sol | accepted | [Integrated acceptance, scope and support limits](evidence/WP-007.md); [detailed progress](evidence/WP-007-progress.md) |
| [WP-010 — Define typed cross-layer contracts and capability discovery](phase-01-services.md#wp-010) | 01 | Sol | accepted | [Typed contracts, dual-Qt tests and installed consumer](evidence/WP-010.md) |
| [WP-011 — Unify commands, contexts, shortcuts and focus](phase-01-services.md#wp-011) | 01 | Sol | accepted | [Dual-Qt command workflow, IME/focus tests and installed consumer](evidence/WP-011.md) |
| [WP-012 — Implement reversible editing, stable selection and properties](phase-01-services.md#wp-012) | 01 | Sol | accepted | [Dual-Qt transactional editing, selection, validation and installed consumer](evidence/WP-012.md) |
| [WP-013 — Implement document sessions, persistence and crash recovery](phase-01-services.md#wp-013) | 01 | Sol | accepted | [Dual-Qt persistence/recovery, fault matrix, visual fixture and installed consumer](evidence/WP-013.md) |
| [WP-014 — Persist settings and workspace state](phase-01-services.md#wp-014) | 01 | Sol | accepted | [Dual-Qt settings/workspace, monitor recovery, dock restore and installed consumer](evidence/WP-014.md) |
| [WP-015 — Implement asynchronous jobs and asset services](phase-01-services.md#wp-015) | 01 | Sol | accepted | [Dual-Qt jobs/assets, undoable relinks, installed consumer and preview](evidence/WP-015.md) |
| [WP-016 — Implement clipboard and drag/drop protocols](phase-01-services.md#wp-016) | 01 | Sol | accepted | [Dual-Qt transfer workflow, installed consumer and limits](evidence/WP-016.md) |
| [WP-017 — Provide accessibility, locale and gesture services](phase-01-services.md#wp-017) | 01 | Sol | accepted | [Qt 6.11/6.8, external AT-SPI action, RTL preview and installed consumer](evidence/WP-017.md) |
| [WP-018 — Add native window, file and desktop integration](phase-01-services.md#wp-018) | 01 | Sol | accepted | [Dual-Qt native-window tests and installed base/optional consumers](evidence/WP-018.md) |
| [WP-019 — Establish format registry and import/export jobs](phase-01-services.md#wp-019) | 01 | Sol | accepted | [Implementation and integrated checks](evidence/WP-019.md) |
| [WP-020 — Build command controls and adaptive toolbars](phase-02-controls-data.md#wp-020) | 02 | Luna | accepted | [Dual-Qt command controls and overflow acceptance](evidence/WP-020.md) |
| [WP-021 — Build document and application navigation](phase-02-controls-data.md#wp-021) | 02 | Luna | accepted | [Focused navigation checks](evidence/WP-021.md) |
| [WP-022 — Build reusable typed inspectors](phase-02-controls-data.md#wp-022) | 02 | Luna | accepted | [Dual-Qt inspector transactions and mixed-state acceptance](evidence/WP-022.md) |
| [WP-023 — Add expression, path and token editors](phase-02-controls-data.md#wp-023) | 02 | Sol | accepted | [Dual-Qt editor/provider behavior and installed consumers; dependency gate passed](evidence/WP-023.md) |
| [WP-024 — Add typography and calendar value pickers](phase-02-controls-data.md#wp-024) | 02 | Luna | accepted | [Provider and editor checks; mixed/reset follow-up passed](evidence/WP-024.md) |
| [WP-025 — Add preferences, shortcut editing and find/replace](phase-02-controls-data.md#wp-025) | 02 | Luna | accepted | [Focused persistence, search and undo checks](evidence/WP-025.md) |
| [WP-026 — Add history, notifications and task feedback](phase-02-controls-data.md#wp-026) | 02 | Luna | accepted | [Focused feedback checks](evidence/WP-026.md) |
| [WP-027 — Implement virtual lists, trees and asset grids](phase-02-controls-data.md#wp-027) | 02 | Luna | accepted | [100k-model, lazy tree and bounded-delegate checks](evidence/WP-027.md) |
| [WP-028 — Implement editable two-dimensional grids](phase-02-controls-data.md#wp-028) | 02 | Sol | accepted | [Sparse core, bounded visual grid and interaction checks](evidence/WP-028.md) |
| [WP-029 — Implement filters, query building and pivots](phase-02-controls-data.md#wp-029) | 02 | Sol | accepted | [Typed core](evidence/WP-029.md); [visual editor and pivot evidence](evidence/WP-029-visual.md) |
| [WP-030 — Build the general chart framework](phase-02-controls-data.md#wp-030) | 02 | Sol | accepted | [Typed core](evidence/WP-030.md); [visual, export and dual-render evidence](evidence/WP-030-visual.md) |
| [WP-031 — Complete analytical chart and scheduling families](phase-02-controls-data.md#wp-031) | 02 | Sol | accepted | [Seven families, dual-Qt suite and installed consumer](evidence/WP-031.md) |
| [WP-032 — Build event, diff and resource views](phase-02-controls-data.md#wp-032) | 02 | Luna | accepted | [Event, diff and resource integration status](evidence/WP-032.md) |
| [WP-033 — Prove the shared application framework](phase-02-controls-data.md#wp-033) | 02 | Sol | active | [Editable sort/filter workbench and dual-Qt consumers; lifecycle gaps open](evidence/WP-033.md) |
| [WP-040 — Build the spatial canvas and rendering boundary](phase-03-authoring.md#wp-040) | 03 | Sol | review | [Integrated renderer and installed consumer; dual-Qt 102/102; WP-033 dependency open](evidence/WP-040.md) |
| [WP-041 — Implement spatial selection, snapping and transforms](phase-03-authoring.md#wp-041) | 03 | Sol | active | [Lasso, snap, guides and reference group move/resize/rotation; shared selection open](evidence/WP-041.md) |
| [WP-042 — Add artboards, layers and document navigation](phase-03-authoring.md#wp-042) | 03 | Luna | active | [Layer tree, navigator, alignment and artboard views; model integration open](evidence/WP-042.md) |
| [WP-043 — Implement document color and curve editing](phase-03-authoring.md#wp-043) | 03 | Sol | active | [Curve, tangent, gradient and picker presentation; ICC, curve and gradient backends; bounded canvas display; integration gates open](evidence/WP-043.md) |
| [WP-044 — Implement vector authoring and appearance](phase-03-authoring.md#wp-044) | 03 | Sol | planned | — |
| [WP-045 — Implement raster, masks, brushes and comparison](phase-03-authoring.md#wp-045) | 03 | Sol | planned | — |
| [WP-046 — Implement exact time and tempo conversions](phase-03-authoring.md#wp-046) | 03 | Sol | accepted | [Dual-Qt exact arithmetic and timecode evidence](evidence/WP-046.md) |
| [WP-047 — Connect transport state to an authoritative clock](phase-03-authoring.md#wp-047) | 03 | Sol | accepted | [Clock and transport-bar checks](evidence/WP-047.md) |
| [WP-048 — Implement the shared edit timeline](phase-03-authoring.md#wp-048) | 03 | Sol | accepted | [Exact edits, dual-Qt 101/101 suites, visuals and installed consumer](evidence/WP-048.md) |
| [WP-049 — Implement tempo and automation editing](phase-03-authoring.md#wp-049) | 03 | Sol | planned | — |
| [WP-050 — Implement rich text and paginated document editing](phase-04-office.md#wp-050) | 04 | Sol | review | [Rich-text model, layout, controls and codec integrated; focused dual-Qt checks passed](evidence/WP-050.md) |
| [WP-051 — Add publishing structure, styles and tables](phase-04-office.md#wp-051) | 04 | Sol | planned | — |
| [WP-052 — Add equations, references and document review](phase-04-office.md#wp-052) | 04 | Sol | planned | — |
| [WP-053 — Implement printing and publishing preflight](phase-04-office.md#wp-053) | 04 | Sol | planned | — |
| [WP-054 — Implement PDF viewing and annotation](phase-04-office.md#wp-054) | 04 | Sol | planned | — |
| [WP-055 — Implement a workbook and spreadsheet editing stack](phase-04-office.md#wp-055) | 04 | Sol | active | Isolated Opus lane; G-1–G-8 and full package scope remain under review |
| [WP-056 — Implement slide authoring and presentation](phase-04-office.md#wp-056) | 04 | Sol | planned | — |
| [WP-057 — Implement notebooks and structured blocks](phase-04-office.md#wp-057) | 04 | Sol | planned | — |
| [WP-058 — Implement mail, calendars and task workflows](phase-04-office.md#wp-058) | 04 | Sol | planned | — |
| [WP-059 — Implement forms, schemas and report design](phase-04-office.md#wp-059) | 04 | Sol | planned | — |
| [WP-060 — Integrate a real audio engine and device settings](phase-05-audio.md#wp-060) | 05 | Sol | planned | — |
| [WP-061 — Implement calibrated audio controls and meters](phase-05-audio.md#wp-061) | 05 | Sol | planned | — |
| [WP-062 — Implement mixing, panning and routing](phase-05-audio.md#wp-062) | 05 | Sol | planned | — |
| [WP-063 — Implement waveform and spectral editing](phase-05-audio.md#wp-063) | 05 | Sol | planned | — |
| [WP-064 — Implement MIDI, patterns and clip launching](phase-05-audio.md#wp-064) | 05 | Sol | planned | — |
| [WP-065 — Implement take comping](phase-05-audio.md#wp-065) | 05 | Sol | planned | — |
| [WP-066 — Integrate plugin hosting and plugin controls](phase-05-audio.md#wp-066) | 05 | Sol | planned | — |
| [WP-067 — Implement score and notation editing](phase-05-audio.md#wp-067) | 05 | Sol | planned | — |
| [WP-068 — Implement pitch analysis and correction editing](phase-05-audio.md#wp-068) | 05 | Sol | planned | — |
| [WP-070 — Integrate media decoding and source/program monitors](phase-06-video.md#wp-070) | 06 | Sol | planned | — |
| [WP-071 — Implement professional timeline video edits](phase-06-video.md#wp-071) | 06 | Sol | planned | — |
| [WP-072 — Implement captions, scopes and grading controls](phase-06-video.md#wp-072) | 06 | Sol | planned | — |
| [WP-073 — Implement motion composition and keyframe editing](phase-06-video.md#wp-073) | 06 | Sol | planned | — |
| [WP-074 — Implement node-based compositing](phase-06-video.md#wp-074) | 06 | Sol | planned | — |
| [WP-075 — Implement tracking and rotoscoping](phase-06-video.md#wp-075) | 06 | Sol | planned | — |
| [WP-076 — Implement 3D scene rendering and manipulation](phase-06-video.md#wp-076) | 06 | Sol | planned | — |
| [WP-077 — Implement rig, UV and texture authoring](phase-06-video.md#wp-077) | 06 | Sol | planned | — |
| [WP-078 — Implement durable render queues](phase-06-video.md#wp-078) | 06 | Sol | planned | — |
| [WP-080 — Implement detached native panels](phase-07-integrations.md#wp-080) | 07 | Sol | planned | — |
| [WP-081 — Implement extensions, scripting and developer surfaces](phase-07-integrations.md#wp-081) | 07 | Sol | planned | — |
| [WP-082 — Implement collaboration, presence and version history](phase-07-integrations.md#wp-082) | 07 | Sol | planned | — |
| [WP-083 — Implement diagramming and CAD surfaces](phase-07-integrations.md#wp-083) | 07 | Sol | planned | — |
| [WP-084 — Implement geographic mapping](phase-07-integrations.md#wp-084) | 07 | Sol | planned | — |
| [WP-085 — Implement scientific plotting](phase-07-integrations.md#wp-085) | 07 | Sol | planned | — |
| [WP-086 — Implement image slices, volumes and transfer functions](phase-07-integrations.md#wp-086) | 07 | Sol | planned | — |
| [WP-087 — Implement dependency and trace exploration](phase-07-integrations.md#wp-087) | 07 | Sol | planned | — |
| [WP-088 — Qualify platform and backend integration boundaries](phase-07-integrations.md#wp-088) | 07 | Sol | planned | — |
| [WP-089 — Qualify format, device and output fidelity](phase-07-integrations.md#wp-089) | 07 | Sol | planned | — |
| [WP-090 — Finish representative applications and domain acceptance](phase-08-qualification.md#wp-090) | 08 | Sol | planned | — |
| [WP-091 — Complete performance and fault qualification](phase-08-qualification.md#wp-091) | 08 | Sol | planned | — |
| [WP-092 — Complete accessibility, localization and input qualification](phase-08-qualification.md#wp-092) | 08 | Sol | planned | — |
| [WP-093 — Complete SDK, documentation and distributable artifacts](phase-08-qualification.md#wp-093) | 08 | Sol | planned | — |
| [WP-094 — Audit complete scope and hand off the finished implementation](phase-08-qualification.md#wp-094) | 08 | Sol | planned | — |
