<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Execution ledger

**Execution state: 49 packages accepted, 28 active, none in review, 1 planned and 6 deferred.**
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
WP-040 is accepted: its WP-033 dependency and live-window render lifecycle
qualification passed. The Qt 6.8 offscreen RHI readback repair passed the full
integrated suite. WP-048 is accepted
after its time-axis and host-media boundaries were reconciled with D-045;
both full integrated suites and its installed consumers pass. WP-050 is
accepted after focused merged editing, view and session checks; WP-055 is
accepted after the merged workbook and sheet checks.
WP-033 is accepted after its dual-Qt two-document workbench, installed
lifecycle consumers, domain sessions, recovery, shared undo and external
AT-SPI command action passed. WP-041 now has a document-space snap query,
guide layer, lasso and reference-canvas group move/resize/rotation controller, while
WP-042 has layer, navigator, alignment and artboard presentation.
Both spatial packages still have model, command and acceptance gates open.
WP-043 has CurveEditor, GradientEditor and ColorPicker presentation plus
numerical RGB ICC, curve and gradient evaluators, typed transaction models,
tangent UI, managed canvas display and soft proofing. Typed canvas rendering
and package acceptance remain open. WP-080 has a native detached-panel
implementation accepted; mixed-scale display qualification remains in WP-088.
WP-081 is accepted after its merged developer-surface checks; other terminal
platforms remain in WP-088. WP-041, WP-076 and WP-078 are accepted after
merged Qt 6.11 and Qt 6.8 checks; WP-045, WP-073 and WP-074 after the full
suite passed on both (261/261). Scope decision by the user (2026-09-24):
qualification covers Linux only, each package's live examples stand in for
separate demo applications, and WP-089 to WP-094 are deferred.
WP-049 now has beat-locked clip timing, tempo and automation editing, and
engine-facing immutable schedule snapshots; its focused Qt 6.11 and Qt 6.8
build, test and preview checks passed. WP-061 has calibrated control visuals,
a peak/RMS meter model and parameter taper; real-engine integration remains.

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
| [WP-033 — Prove the shared application framework](phase-02-controls-data.md#wp-033) | 02 | Sol | accepted | [Native collection transfer, dual-Qt workbench lifecycle and external AT-SPI action passed](evidence/WP-033.md) |
| [WP-040 — Build the spatial canvas and rendering boundary](phase-03-authoring.md#wp-040) | 03 | Sol | accepted | [Integrated renderer, installed consumer, dual-Qt 102/102 and focused resource lifecycle checks](evidence/WP-040.md) |
| [WP-041 — Implement spatial selection, snapping and transforms](phase-03-authoring.md#wp-041) | 03 | Sol | accepted | [Lasso, snapping, persistent guides, shared selection, group and numeric transforms passed merged checks](evidence/WP-041.md) |
| [WP-042 — Add artboards, layers and document navigation](phase-03-authoring.md#wp-042) | 03 | Luna | active | [Layer tree, navigator, alignment and artboard views; model integration open](evidence/WP-042.md) |
| [WP-043 — Implement document color and curve editing](phase-03-authoring.md#wp-043) | 03 | Sol | accepted | [Typed curve/gradient/picker editing, managed ICC canvas output and matched software/RHI pixels](evidence/WP-043.md) |
| [WP-044 — Implement vector authoring and appearance](phase-03-authoring.md#wp-044) | 03 | Sol | active | [Editable multi-contour paths, holes, native persistence, SVG output, undoable clipboard and parametric polygon, star and rounded-rectangle paths merged; booleans, stroke alignment, patterns and appearance effects open](evidence/WP-044.md) |
| [WP-045 — Implement raster, masks, brushes and comparison](phase-03-authoring.md#wp-045) | 03 | Sol | accepted | [Raster on the canvas, bounded history, brush previews, masks, tablet input and tracked comparison passed merged checks](evidence/WP-045.md) |
| [WP-046 — Implement exact time and tempo conversions](phase-03-authoring.md#wp-046) | 03 | Sol | accepted | [Dual-Qt exact arithmetic and timecode evidence](evidence/WP-046.md) |
| [WP-047 — Connect transport state to an authoritative clock](phase-03-authoring.md#wp-047) | 03 | Sol | accepted | [Clock and transport-bar checks](evidence/WP-047.md) |
| [WP-048 — Implement the shared edit timeline](phase-03-authoring.md#wp-048) | 03 | Sol | accepted | [Exact edits, dual-Qt 101/101 suites, visuals and installed consumer](evidence/WP-048.md) |
| [WP-049 — Implement tempo and automation editing](phase-03-authoring.md#wp-049) | 03 | Sol | accepted | [Tempo/automation models, clip anchoring, schedule snapshots and lanes accepted](evidence/WP-049.md) |
| [WP-050 — Implement rich text and paginated document editing](phase-04-office.md#wp-050) | 04 | Sol | accepted | [Styled multi-page editing, cursor mapping and persistence; focused merged and dual-Qt checks passed](evidence/WP-050.md) |
| [WP-051 — Add publishing structure, styles and tables](phase-04-office.md#wp-051) | 04 | Sol | active | [Four authoring controls and undoable table row/column editing integrated; merged cells, styles and frame flow pending](evidence/WP-051.md) |
| [WP-052 — Add equations, references and document review](phase-04-office.md#wp-052) | 04 | Sol | active | [Equation, reference and review request controls integrated; semantic document backend pending](evidence/WP-052.md) |
| [WP-053 — Implement printing and publishing preflight](phase-04-office.md#wp-053) | 04 | Sol | active | [Three-page PDF export and preview/preflight controls integrated; native print jobs and backend checks pending](evidence/WP-053.md) |
| [WP-054 — Implement PDF viewing and annotation](phase-04-office.md#wp-054) | 04 | Sol | accepted | [Real page/text backend and native note/highlight save/reopen passed focused merged checks; unsupported PDF capabilities are explicit](evidence/WP-054.md) |
| [WP-055 — Implement a workbook and spreadsheet editing stack](phase-04-office.md#wp-055) | 04 | Sol | accepted | [Formula engine, sheet UI, domain-session persistence, formatting/validation and declared CSV/TSV interchange passed merged checks](evidence/WP-055.md) |
| [WP-056 — Implement slide authoring and presentation](phase-04-office.md#wp-056) | 04 | Sol | active | [Five slide/presenter controls integrated; persistent model and playback pending](evidence/WP-056.md) |
| [WP-057 — Implement notebooks and structured blocks](phase-04-office.md#wp-057) | 04 | Sol | active | [NotebookView and BlockEditor integrated; persistent block model and content providers pending](evidence/WP-057.md) |
| [WP-058 — Implement mail, calendars and task workflows](phase-04-office.md#wp-058) | 04 | Sol | active | [Local messages, rich drafts, attachments, recurring events and tasks persist with recovery and stale-writer rejection; delivery and sync providers remain](evidence/WP-058.md) |
| [WP-059 — Implement forms, schemas and report design](phase-04-office.md#wp-059) | 04 | Sol | active | [SQLite provider and three controls integrated; design persistence and paginated export pending](evidence/WP-059.md) |
| [WP-060 — Integrate a real audio engine and device settings](phase-05-audio.md#wp-060) | 05 | Sol | active | [Host-acknowledged device panel integrated; real audio engine pending](evidence/WP-060.md) |
| [WP-061 — Implement calibrated audio controls and meters](phase-05-audio.md#wp-061) | 05 | Sol | active | [Control visuals, peak/RMS model and parameter taper integrated; engine binding pending](evidence/WP-061.md) |
| [WP-062 — Implement mixing, panning and routing](phase-05-audio.md#wp-062) | 05 | Sol | active | [Four mixer/routing controls integrated; real DSP and graph operations pending](evidence/WP-062.md) |
| [WP-063 — Implement waveform and spectral editing](phase-05-audio.md#wp-063) | 05 | Sol | active | [Waveform and spectrogram editor controls integrated; DSP and tile backend pending](evidence/WP-063.md) |
| [WP-064 — Implement MIDI, patterns and clip launching](phase-05-audio.md#wp-064) | 05 | Sol | active | [Four MIDI editing/launch controls integrated; scheduling, persistence and interchange pending](evidence/WP-064.md) |
| [WP-065 — Implement take comping](phase-05-audio.md#wp-065) | 05 | Sol | active | [TakeCompView integrated; source interval, rendering and persistence pending](evidence/WP-065.md) |
| [WP-066 — Integrate plugin hosting and plugin controls](phase-05-audio.md#wp-066) | 05 | Sol | active | [Plugin browser/rack integrated; ABI processing and editor host pending](evidence/WP-066.md) |
| [WP-067 — Implement score and notation editing](phase-05-audio.md#wp-067) | 05 | Sol | active | [ScoreView integrated; notation layout, persistence and playback pending](evidence/WP-067.md) |
| [WP-068 — Implement pitch analysis and correction editing](phase-05-audio.md#wp-068) | 05 | Sol | active | [PitchEditor integrated; analysis and correction DSP pending](evidence/WP-068.md) |
| [WP-070 — Integrate media decoding and source/program monitors](phase-06-video.md#wp-070) | 06 | Sol | active | [Monitor/bin controls and local frame decode integrated; playback and frame lifecycle pending](evidence/WP-070.md) |
| [WP-071 — Implement professional timeline video edits](phase-06-video.md#wp-071) | 06 | Sol | active | [Trim, multicam and transition controls integrated; sequence edit backend pending](evidence/WP-071.md) |
| [WP-072 — Implement captions, scopes and grading controls](phase-06-video.md#wp-072) | 06 | Sol | active | [Caption model, burned PNG sequence, scopes and grade edit history integrated; video encoding and fidelity work remain](evidence/WP-072.md) |
| [WP-073 — Implement motion composition and keyframe editing](phase-06-video.md#wp-073) | 06 | Sol | accepted | [Layer timing, one-transaction path edits, bounded frame caches and exact-tick export passed merged checks](evidence/WP-073.md) |
| [WP-074 — Implement node-based compositing](phase-06-video.md#wp-074) | 06 | Sol | accepted | [Typed ports, masks, blend modes, groups, keyboard editing and file format 2 passed merged checks](evidence/WP-074.md) |
| [WP-075 — Implement tracking and rotoscoping](phase-06-video.md#wp-075) | 06 | Sol | active | [Live model-to-control tracking and mask preview integrated; broader motion and qualification remain](evidence/WP-075.md) |
| [WP-076 — Implement 3D scene rendering and manipulation](phase-06-video.md#wp-076) | 06 | Sol | accepted | [Quick3D viewport, materials, transform gizmo, graphics-loss recovery and shared scene save/undo/recovery passed merged checks](evidence/WP-076-completion.md) |
| [WP-077 — Implement rig, UV and texture authoring](phase-06-video.md#wp-077) | 06 | Sol | active | [Core, controls and live request wiring merged; raw GPU texture and merged UI gates pending](evidence/WP-077.md) |
| [WP-078 — Implement durable render queues](phase-06-video.md#wp-078) | 06 | Sol | accepted | [Durable queue, view and real PNG/PDF/WAV providers passed merged Qt 6.11 and Qt 6.8 checks](evidence/WP-078.md) |
| [WP-080 — Implement detached native panels](phase-07-integrations.md#wp-080) | 07 | Sol | accepted | [Real native windows, state, focus, popup routing and screen fallback passed focused checks](evidence/WP-080.md) |
| [WP-081 — Implement extensions, scripting and developer surfaces](phase-07-integrations.md#wp-081) | 07 | Sol | accepted | [Real PTY terminal, code editor with clangd LSP, bounded script runtime with macro persistence, and explicit extension discovery and load passed merged checks](evidence/WP-081.md) |
| [WP-082 — Implement collaboration, presence and version history](phase-07-integrations.md#wp-082) | 07 | Sol | active | [Two-client sync, authenticated IPC, mapped remote presence and synchronized version restore merged; remote transport, domain operations and shared checkpoints open](evidence/WP-082-presence-layer.md) |
| [WP-083 — Implement diagramming and CAD surfaces](phase-07-integrations.md#wp-083) | 07 | Sol | active | [Diagram graph/flow and constrained CAD sketch merged; additional CAD output in progress](evidence/WP-083-cad.md) |
| [WP-084 — Implement geographic mapping](phase-07-integrations.md#wp-084) | 07 | Sol | accepted | [GeoJSON editing, raster providers, antimeridian, composed export and indexed projection switching passed merged checks](evidence/WP-084.md) |
| [WP-085 — Implement scientific plotting](phase-07-integrations.md#wp-085) | 07 | Sol | active | [Numeric, uncertainty and export implementation lane underway](evidence/WP-085.md) |
| [WP-086 — Implement image slices, volumes and transfer functions](phase-07-integrations.md#wp-086) | 07 | Sol | active | [Typed voxel volumes, linked slice views and transfer editing merged; GPU volume rendering follows](evidence/WP-086-volume-slices.md) |
| [WP-087 — Implement dependency and trace exploration](phase-07-integrations.md#wp-087) | 07 | Sol | active | [QML asset extraction, bounded dependency index and view plus clock-aligned trace index and explorer merged](evidence/WP-087-trace.md) |
| [WP-088 — Qualify platform and backend integration boundaries](phase-07-integrations.md#wp-088) | 07 | Sol | planned | Linux only, by the user (2026-09-24) |
| [WP-089 — Qualify format, device and output fidelity](phase-07-integrations.md#wp-089) | 07 | Sol | deferred | Deferred by the user (2026-09-24) |
| [WP-090 — Finish representative applications and domain acceptance](phase-08-qualification.md#wp-090) | 08 | Sol | deferred | Deferred by the user (2026-09-24) |
| [WP-091 — Complete performance and fault qualification](phase-08-qualification.md#wp-091) | 08 | Sol | deferred | Deferred by the user (2026-09-24) |
| [WP-092 — Complete accessibility, localization and input qualification](phase-08-qualification.md#wp-092) | 08 | Sol | deferred | Deferred by the user (2026-09-24) |
| [WP-093 — Complete SDK, documentation and distributable artifacts](phase-08-qualification.md#wp-093) | 08 | Sol | deferred | Deferred by the user (2026-09-24) |
| [WP-094 — Audit complete scope and hand off the finished implementation](phase-08-qualification.md#wp-094) | 08 | Sol | deferred | Deferred by the user (2026-09-24) |
