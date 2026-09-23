<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Execution ledger

**Initial state: all packages planned. No agents, builds or implementation
have been launched for this planning handoff.** Sol updates this ledger during
execution. Use `planned`, `ready`, `active`, `review`, `accepted`, or `blocked`.
A blocked row names the missing prerequisite and unblock action in its evidence
record. Keep unrelated ready work moving. A row reaches `accepted` only through
the [completion gate](acceptance.md).

Record details in one package evidence file per WP using the
[evidence template](task-templates.md#evidence-record). Create those files when
work begins, split long records, and link them in the last column. Sol updates
manifest status consistently with this table. Never rewrite historical results
as though a later fix had already passed the earlier run.

| Package | Phase | Lead | State | Accepted commit / evidence |
| --- | --- | --- | --- | --- |
| [WP-001 — Capture the execution baseline and prepare isolated work](phase-00-baseline.md#wp-001) | 00 | Sol | planned | — |
| [WP-002 — Resolve the three source behavior findings](phase-00-baseline.md#wp-002) | 00 | Luna | planned | — |
| [WP-003 — Repair layout, token and documentation drift](phase-00-baseline.md#wp-003) | 00 | Luna | planned | — |
| [WP-004 — Make the catalog a reliable API inventory](phase-00-baseline.md#wp-004) | 00 | Luna | planned | — |
| [WP-005 — Isolate private Qt dependencies and establish support targets](phase-00-baseline.md#wp-005) | 00 | Sol | planned | — |
| [WP-006 — Complete layout and scrolling contracts](phase-00-baseline.md#wp-006) | 00 | Sol | planned | — |
| [WP-007 — Harden theme, popup and control interaction behavior](phase-00-baseline.md#wp-007) | 00 | Sol | planned | — |
| [WP-010 — Define typed cross-layer contracts and capability discovery](phase-01-services.md#wp-010) | 01 | Sol | planned | — |
| [WP-011 — Unify commands, contexts, shortcuts and focus](phase-01-services.md#wp-011) | 01 | Sol | planned | — |
| [WP-012 — Implement reversible editing, stable selection and properties](phase-01-services.md#wp-012) | 01 | Sol | planned | — |
| [WP-013 — Implement document sessions, persistence and crash recovery](phase-01-services.md#wp-013) | 01 | Sol | planned | — |
| [WP-014 — Persist settings and workspace state](phase-01-services.md#wp-014) | 01 | Sol | planned | — |
| [WP-015 — Implement asynchronous jobs and asset services](phase-01-services.md#wp-015) | 01 | Sol | planned | — |
| [WP-016 — Implement clipboard and drag/drop protocols](phase-01-services.md#wp-016) | 01 | Sol | planned | — |
| [WP-017 — Provide accessibility, locale and gesture services](phase-01-services.md#wp-017) | 01 | Sol | planned | — |
| [WP-018 — Add native window, file and desktop integration](phase-01-services.md#wp-018) | 01 | Sol | planned | — |
| [WP-019 — Establish format registry and import/export jobs](phase-01-services.md#wp-019) | 01 | Sol | planned | — |
| [WP-020 — Build command controls and adaptive toolbars](phase-02-controls-data.md#wp-020) | 02 | Luna | planned | — |
| [WP-021 — Build document and application navigation](phase-02-controls-data.md#wp-021) | 02 | Luna | planned | — |
| [WP-022 — Build reusable typed inspectors](phase-02-controls-data.md#wp-022) | 02 | Luna | planned | — |
| [WP-023 — Add expression, path and token editors](phase-02-controls-data.md#wp-023) | 02 | Sol | planned | — |
| [WP-024 — Add typography and calendar value pickers](phase-02-controls-data.md#wp-024) | 02 | Luna | planned | — |
| [WP-025 — Add preferences, shortcut editing and find/replace](phase-02-controls-data.md#wp-025) | 02 | Luna | planned | — |
| [WP-026 — Add history, notifications and task feedback](phase-02-controls-data.md#wp-026) | 02 | Luna | planned | — |
| [WP-027 — Implement virtual lists, trees and asset grids](phase-02-controls-data.md#wp-027) | 02 | Sol | planned | — |
| [WP-028 — Implement editable two-dimensional grids](phase-02-controls-data.md#wp-028) | 02 | Sol | planned | — |
| [WP-029 — Implement filters, query building and pivots](phase-02-controls-data.md#wp-029) | 02 | Sol | planned | — |
| [WP-030 — Build the general chart framework](phase-02-controls-data.md#wp-030) | 02 | Sol | planned | — |
| [WP-031 — Complete analytical chart and scheduling families](phase-02-controls-data.md#wp-031) | 02 | Sol | planned | — |
| [WP-032 — Build event, diff and resource views](phase-02-controls-data.md#wp-032) | 02 | Luna | planned | — |
| [WP-033 — Prove the shared application framework](phase-02-controls-data.md#wp-033) | 02 | Sol | planned | — |
| [WP-040 — Build the spatial canvas and rendering boundary](phase-03-authoring.md#wp-040) | 03 | Sol | planned | — |
| [WP-041 — Implement spatial selection, snapping and transforms](phase-03-authoring.md#wp-041) | 03 | Sol | planned | — |
| [WP-042 — Add artboards, layers and document navigation](phase-03-authoring.md#wp-042) | 03 | Luna | planned | — |
| [WP-043 — Implement document color and curve editing](phase-03-authoring.md#wp-043) | 03 | Sol | planned | — |
| [WP-044 — Implement vector authoring and appearance](phase-03-authoring.md#wp-044) | 03 | Sol | planned | — |
| [WP-045 — Implement raster, masks, brushes and comparison](phase-03-authoring.md#wp-045) | 03 | Sol | planned | — |
| [WP-046 — Implement exact time and tempo conversions](phase-03-authoring.md#wp-046) | 03 | Sol | planned | — |
| [WP-047 — Connect transport state to an authoritative clock](phase-03-authoring.md#wp-047) | 03 | Sol | planned | — |
| [WP-048 — Implement the shared edit timeline](phase-03-authoring.md#wp-048) | 03 | Sol | planned | — |
| [WP-049 — Implement tempo and automation editing](phase-03-authoring.md#wp-049) | 03 | Sol | planned | — |
| [WP-050 — Implement rich text and paginated document editing](phase-04-office.md#wp-050) | 04 | Sol | planned | — |
| [WP-051 — Add publishing structure, styles and tables](phase-04-office.md#wp-051) | 04 | Sol | planned | — |
| [WP-052 — Add equations, references and document review](phase-04-office.md#wp-052) | 04 | Sol | planned | — |
| [WP-053 — Implement printing and publishing preflight](phase-04-office.md#wp-053) | 04 | Sol | planned | — |
| [WP-054 — Implement PDF viewing and annotation](phase-04-office.md#wp-054) | 04 | Sol | planned | — |
| [WP-055 — Implement a workbook and spreadsheet editing stack](phase-04-office.md#wp-055) | 04 | Sol | planned | — |
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
