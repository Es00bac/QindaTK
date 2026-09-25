<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# QindaTK documentation

QindaTK is a Qt 6 / QML toolkit for dense, visually controlled desktop
interfaces. Start with the README for the pitch and a quick start; the
pages below are the reference.

| Page | What it covers |
| --- | --- |
| [architecture.md](architecture.md) | Module layout, C++/QML split, ownership and threading, how apps link it |
| [layout.md](layout.md) | Flex, Grid, Stack, Box, Scroll: the CSS-equivalent layout model, with the exact algorithm rules |
| [theming.md](theming.md) | Theme roles, ladders, density, presets, JSON themes, the QindaQt QST-1 bridge |
| [controls.md](controls.md) | Every control: properties, signals, sizes, objectNames (the API contract) |
| [code-editor.md](code-editor.md) | Native text buffer, visual folding, multi-caret edits, optional local LSP process, diagnostics and local UTF-8 save/reopen |
| [diagram.md](diagram.md) | Stable diagram nodes/ports/edges, editing, deterministic orthogonal paths and local JSON |
| [cad.md](cad.md) | Typed 2D sketch entities, units, measurements, constraints, viewport, native file and SVG/DXF output |
| [script-runtime.md](script-runtime.md) | Bounded local JavaScript, explicit command allowlist, replay, semantic undo group and console UI |
| [collaboration.md](collaboration.md) | Collaboration: operation identity, deterministic conflicts, object deletes, text merge, own-step undo, local and network transport, live permissions, presence, shared versions and restore |
| [gantt.md](gantt.md) | Task schema, working-calendar projection, schedule transactions, dependency and grid seams |
| [productivity.md](productivity.md) | Local calendar recurrence expansion, event identity, DST policy and host persistence boundary |
| [productivity-providers.md](productivity-providers.md) | SMTP delivery, IMAP mailbox sync, folders and APPEND, POP3 download, CalDAV event/task sync and discovery, iCalendar feeds and files over the local store: result states, credentials, conflicts and host adapters |
| [control-interaction.md](control-interaction.md) | Owner state, user signals, editing states, focus and assistive action baseline |
| [application-contracts.md](application-contracts.md) | Strong IDs, revisions, exact QML values, snapshots, cancellation and capability discovery |
| [extension-registry.md](extension-registry.md) | Explicit local extension discovery, descriptor and dependency diagnostics, trusted native load/unload, capability publication and preserved state |
| [volume-imaging.md](volume-imaging.md) | Typed voxel chunks, calibrated principal and oblique slices, GPU/reference volume rendering, linked cursor and undoable transfer functions |
| [commands.md](commands.md) | Scoped command contexts, registry and bindings, shortcut routing, focus restoration, and menu/toolbar/palette adapters |
| [editing.md](editing.md) | Reference document, reversible transactions, per-document undo, stable selection, property projection and validation |
| [editable-grid.md](editable-grid.md) | Sparse 2D model, stable IDs, bounded visible ranges, frozen geometry and range transactions |
| [workbook.md](workbook.md) | Workbook model, formula grammar and functions, reference rules and names, incremental recalculation, transactions, cell and number formats, conditional formats, data validation, merged cells, sheet properties, clipboard ranges, CSV/TSV interchange, sessions and the native workbook format |
| [slides.md](slides.md) | Slide deck model, master/layout inheritance and overrides, rich text boxes, exact-tick builds and easing, the shared slide painter and PDF export, deterministic playback, audience/presenter windows and display reconnect |
| [query-pivot.md](query-pivot.md) | Typed query AST, null semantics, bounded grouping and pivots, provenance and grid/provider adapters |
| [forms-reports.md](forms-reports.md) | Local SQLite forms and reports: the design document with undo, save and recovery, report definitions (bands, nested groups, totals, typed parameters), evaluation through the query engine, paginated output for preview, PDF and printers, schema qualification |
| [diff-view.md](diff-view.md) | Bounded local diff, lazy large-provider seam, stable hunk IDs and host transaction requests |
| [rich-text.md](rich-text.md) | Structured rich-text model, `qindatk.richtext` persistence, per-view shaping and incremental pagination, commands, clipboard and object seams |
| [publishing.md](publishing.md) | Named styles with inheritance and overrides, merged table cells, table styles, linked text frames, page columns and `qindatk.richtext` version 2 |
| [printing.md](printing.md) | Print tickets and destination capabilities, one sheet geometry for preview, PDF and printers, bounded preview cache, cancellable jobs, publishing preflight with validated repairs, page bleed |
| [references-review.md](references-review.md) | Equations (TeX subset, layout, linear text), reference fields that renumber in one undo step, comments and tracked changes with accept/reject |
| [notebooks.md](notebooks.md) | Persistent notebooks: sections, pages and nested blocks with stable IDs, split/merge/nesting, cross-block selection and paste, search, links, missing media, code evaluation, chart blocks, sessions and recovery |
| [resource-table.md](resource-table.md) | Stable resource and time-bucket identity, allocation units, conflicts and reversible edits |
| [persistence.md](persistence.md) | Document sessions of any registered kind, the reference file format, atomic save, multi-document close and recovery behavior |
| [document-models.md](document-models.md) | Domain model/command/codec contracts, the shared history engine, save clean marks, recovery frames and how a new domain joins |
| [format-registry.md](format-registry.md) | Operation-specific import/export providers, preflight receipts, loss acknowledgement and job lifecycle |
| [settings-workspace.md](settings-workspace.md) | Scoped settings, independent workspace schema, view restore, monitor recovery and shade policy |
| [assets-jobs.md](assets-jobs.md) | Bounded job queue, asset subscriptions/cache, resource relinking and local media backend |
| [asset-dependencies.md](asset-dependencies.md) | Stable asset graph, local QML extraction, missing/cycle diagnostics, stale-scan rejection, impact and view requests |
| [trace-exploration.md](trace-exploration.md) | Bounded trace snapshots, explicit clock domains, nested and cross-thread queries, timeline view and JSON export |
| [history-feedback.md](history-feedback.md) | Stable edit-history navigation, bounded notifications and task status contracts |
| [find-replace.md](find-replace.md) | Revision-tagged text search, cancellation and atomic replace transactions |
| [transfers.md](transfers.md) | Versioned clipboard and drag/drop payloads, destination validation and undoable reference transfers |
| [accessibility-locale-input.md](accessibility-locale-input.md) | Virtual accessibility nodes, per-instance locale policy and gesture arbitration |
| [time-map.md](time-map.md) | Exact sample/frame/tick conversions, tempo segments, timecode and precision-safe QML values |
| [native-integration.md](native-integration.md) | Native window/view lifecycle, file dialogs and desktop capability boundaries |
| [terminal-session.md](terminal-session.md) | Explicit local PTY lifecycle, parsed screen spans, resize, input and bounded scrollback; pair with `Tk.TerminalView` |
| [gradient.md](gradient.md) | Tagged RGB gradient stops, linear-light alpha interpolation, duplicate positions and numerical output |
| [curve.md](curve.md) | Typed numerical curves, normalized knot/handle constraints, interpolation and edge semantics |
| [color.md](color.md) | Numerical RGB ICC profiles, straight alpha, CPU transforms, precision and unsupported proof/display boundaries |
| [canvas.md](canvas.md) | Spatial canvas: document/view/device transforms, object model, picking, tool gestures, synchronized views and the tile rendering boundary |
| [compare.md](compare.md) | Aligned before/after image comparison, split/blink/difference modes and color limits |
| [raster-document.md](raster-document.md) | Deterministic pressure/tilt brush samples, pen/mouse canvas input, 256 px raster tiles, sparse 8-bit masks, bounded undo, versioned document save/reopen and atomic PNG export |
| [node-graph.md](node-graph.md) | Typed source/solid/mask/transform/blend/output graph: type table and parameter identity, groups, edit transactions and history, incremental image evaluation and versioned topology storage |
| [controls-media.md](controls-media.md) | Media controls: thumbnails, waveforms, filmstrips, time rulers and shortcut caps |
| [caption-rendering.md](caption-rendering.md) | Caption formats (JSON, WebVTT with languages, SRT, embedded Matroska SubRip, burned-in), split/merge/shift edits, reading warnings, burned-caption rendering with exact intervals, atomic PNG sequences |
| [media-playback.md](media-playback.md) | Media decode with exact frame identity and rate policy, audio sync, source/timeline programs, playback engine and frame transfer, program finishing (grade and captions), virtual media bins with proxies and relink, frame feeds, verified export and the test signal |
| [video-scopes.md](video-scopes.md) | Waveform, RGB parade, histogram and vectorscope snapshots of decoded frames under stated matrix, range and space conventions, graticule targets, demand-driven analysis bound to the player's presented frame |
| [render-queue.md](render-queue.md) | Durable render snapshots, provider registration, ordered PNG output, manifests and restart policy |
| [tracking-roto.md](tracking-roto.md) | Feature tracks on a cancellable worker (subpixel, occlusion, decoded footage), loss confidence, keyframed normalized masks and layer-time composition masking |
| [edit-timeline.md](edit-timeline.md) | Multitrack edit timeline: exact ticks, typed edit transactions, snapping, ripple, history, virtualization, media and transfer seams |
| [sequence-edits.md](sequence-edits.md) | NLE sequence edits: roll, ripple, slip and slide with linked sound, frame-exact bounds, transitions and their evaluation, multicam angles and switching, and the SequenceEditor/MulticamModel backends of the trim, transition and multicam views |
| [tempo-automation.md](tempo-automation.md) | Tempo/meter events, beat-locked clips, automation curves and modes, engine schedule snapshots |
| [audio-engine.md](audio-engine.md) | Realtime audio engine and session: threads/queues, graph lifetime, devices and reconfiguration, recording latency, persistence, relink, offline render, callback measurements |
| [audio-mixer.md](audio-mixer.md) | Mixer and routing: channel/bus/port identities, pan laws and layouts, sends, solo in place and groups, feedback refusals, device ports that never fall back, channel meters, MixerView/ChannelStrip/PanningControl/RoutingMatrix |
| [audio-editing.md](audio-editing.md) | Waveform and spectral editing: sample/channel coordinates, exact peak pyramid, FFT window/hop/scaling conventions, bounded spectrum tiles, source edits with undo and persistence, cancellable spectral previews, WaveformEditor/SpectrogramView providers |
| [audio-plugins.md](audio-plugins.md) | Plugin hosting: CLAP capabilities per format, out-of-process scan/cache and instances, realtime block exchange with crash/hang containment, parameters as automation, latency compensation, offline renders, editor window ownership, session recall of unavailable plugins, PluginBrowser/PluginRack wiring |
| [audio-comping.md](audio-comping.md) | Takes and comping: loop passes cut from record jumps, non-destructive punch, comp coordinates and edits, take-junction crossfades, previews, audition/compare, persistence and relink, TakeCompView wiring |
| [audio-pitch.md](audio-pitch.md) | Pitch analysis and correction: MIDI-cent units, YIN frames/notes/phrases with confidence and unvoiced states, TD-PSOLA pitch/formant/timing edits with declared limits, fixture results and limitations, holding analysis through previews, AudioPitchSource for PitchEditor |
| [audio-midi.md](audio-midi.md) | MIDI, patterns and clip launching: tick/velocity/controller resolutions, per-note expression, exact reversible edits and gestures, stateless sample-accurate scheduling and test instrument, polymetric steps and seeded probability, engine-quantized launching, audition lifecycle, MIDI file interchange |
| [notation.md](notation.md) | Score model and rhythm rules, notation edits, engraving layout and pagination, PDF output, MusicXML interchange with IDs, MIDI playback and audition (WP-067) |
| [audio-measurement.md](audio-measurement.md) | Realtime meter tap, peak/RMS meter and BS.1770-4 loudness models, hold/fall/clip on audio time, engine parameter transactions for Knob and Fader, silence floor |
| [vector-authoring.md](vector-authoring.md) | Vector appearance panels and undoable solid canvas style edits |
| [chart-data.md](chart-data.md) | Typed axes/series, linked viewports, annotations, projection, hit identity, decimation, tabular export and histogram bin ranges |
| [distribution-plot.md](distribution-plot.md) | Source-backed quantiles and density, declared intervals, selection and accessible tables |
| [financial-displays.md](financial-displays.md) | Source-backed OHLC sessions, candlesticks, bullet and gauge targets, missing values and export seam |
| [polar.md](polar.md) | Numeric polar, categorical radar and radial-bar coordinates, validation, selection and export |
| [uncertainty.md](uncertainty.md) | Declared interval meanings, error bars, continuous bands, provenance and metadata-preserving export |
| [geographic-map.md](geographic-map.md) | Local GeoJSON vectors, WGS84/Web Mercator semantics, antimeridian wrap, selection, attribution and unsupported tile state |
| [scientific-plot.md](scientific-plot.md) | Float64 scientific series and sampled surfaces: units, missing values, asymmetric/X uncertainty, multi-series 2D figures, downsampling, 3D surface/scatter with explicit interpolation and camera, reproducible figure and panel export |
| [event-log.md](event-log.md) | Bounded event ingestion, UTC identity, filtering, pause/follow, selection and CSV data seam |
| [docking.md](docking.md) | DockModel operations, zones/lanes/groups, the JSON schema, DockHost gestures |
| [qindastudio-migration.md](qindastudio-migration.md) | Moving QindaStudio onto QindaTK: type map (StudioPanel, DockHost, StudioPill, StudioIcon, StudioTheme → QindaTK), build integration, migration order |
| [css-cheatsheet.md](css-cheatsheet.md) | Tailwind/CSS idiom → QindaTK equivalent, one line each |
| [agent-playbook.md](agent-playbook.md) | How an agent builds and verifies a screen with QindaTK, step by step |
| [decisions.md](decisions.md), [decisions-application.md](decisions-application.md), [decisions-services.md](decisions-services.md), [decisions-domains.md](decisions-domains.md) | ADR-lite logs of durable choices |
| [verification.md](verification.md) | Build/test gates, headless verification, manual checklist |
| [platform-support.md](platform-support.md) | Tested Qt/platform/backend matrix, open qualification gates and upgrade procedure |
| [qualification.md](qualification.md) | WP-088 Linux qualification harness (`tools/qualify`): configuration × operation matrix, optional-engine variants, install relocation, result table |
| [overflow-diagnostics-20260919.md](overflow-diagnostics-20260919.md) | Source diagnosis and scope of the menu/dialog overflow repair |
| [Universal toolkit audit and proposal](universal-toolkit/README.md) | Current component reference, creative/Office/audio/video gap reports, hypothetical extension documentation and implementation roadmap |
| [Complete implementation plan for Sol](universal-toolkit/implementation/README.md) | All 212 proposals in 84 work packages; Luna delegation, Astra escalation, engine integration and full acceptance gates |
| [catalog.json](catalog.json) | Machine-readable control catalog (generated) |

Screenshots live in `docs/screenshots/` and are regenerated by
`tools/scripts/screenshots.sh`.

- [3D scene model and viewport](scene-3d.md): persistent primitives, hierarchy outliner, world/local transforms, real Qt Quick 3D rendering and transactional manipulation.
- [Mesh, rig, UV and texture document](mesh-rig.md): stable triangulated geometry, weighted bone poses, UV seams/islands, projected texture pixels, undo and OBJ/PNG output.
