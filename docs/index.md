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
| [collaboration.md](collaboration.md) | Property synchronization: operation identity, deterministic conflicts, local IPC, actor permissions, durable offline reconnect, presence and scope limits |
| [gantt.md](gantt.md) | Task schema, working-calendar projection, schedule transactions, dependency and grid seams |
| [productivity.md](productivity.md) | Local calendar recurrence expansion, event identity, DST policy and host persistence boundary |
| [control-interaction.md](control-interaction.md) | Owner state, user signals, editing states, focus and assistive action baseline |
| [application-contracts.md](application-contracts.md) | Strong IDs, revisions, exact QML values, snapshots, cancellation and capability discovery |
| [extension-registry.md](extension-registry.md) | Explicit local extension discovery, descriptor and dependency diagnostics, trusted native load/unload, capability publication and preserved state |
| [volume-imaging.md](volume-imaging.md) | Typed voxel chunks, calibrated principal slices, linked cursor and editable transfer functions |
| [commands.md](commands.md) | Scoped command contexts, registry and bindings, shortcut routing, focus restoration, and menu/toolbar/palette adapters |
| [editing.md](editing.md) | Reference document, reversible transactions, per-document undo, stable selection, property projection and validation |
| [editable-grid.md](editable-grid.md) | Sparse 2D model, stable IDs, bounded visible ranges, frozen geometry and range transactions |
| [workbook.md](workbook.md) | Workbook model, formula grammar and functions, reference rules and names, incremental recalculation, transactions, cell and number formats, conditional formats, data validation, merged cells, sheet properties, clipboard ranges, CSV/TSV interchange, sessions and the native workbook format |
| [query-pivot.md](query-pivot.md) | Typed query AST, null semantics, bounded grouping and pivots, provenance and grid/provider adapters |
| [diff-view.md](diff-view.md) | Bounded local diff, lazy large-provider seam, stable hunk IDs and host transaction requests |
| [rich-text.md](rich-text.md) | Structured rich-text model, `qindatk.richtext/1` persistence, per-view shaping and incremental pagination, commands, clipboard and object seams |
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
| [node-graph.md](node-graph.md) | Typed source/transform/blend/output graph, stable IDs, local history, incremental image evaluation and native topology storage |
| [controls-media.md](controls-media.md) | Media controls: thumbnails, waveforms, filmstrips, time rulers and shortcut caps |
| [caption-rendering.md](caption-rendering.md) | Burned-caption frame rendering, exact interval semantics, style limits, atomic PNG sequences and timing manifests |
| [render-queue.md](render-queue.md) | Durable render snapshots, provider registration, ordered PNG output, manifests and restart policy |
| [tracking-roto.md](tracking-roto.md) | Accepted feature tracks, loss confidence, cancellable analysis, keyframed normalized masks and composition pixel application |
| [edit-timeline.md](edit-timeline.md) | Multitrack edit timeline: exact ticks, typed edit transactions, snapping, ripple, history, virtualization, media and transfer seams |
| [tempo-automation.md](tempo-automation.md) | Tempo/meter events, beat-locked clips, automation curves and modes, engine schedule snapshots |
| [audio-measurement.md](audio-measurement.md) | Channel peak/RMS measurement, dBFS display, peak hold, fall and clip latch |
| [vector-authoring.md](vector-authoring.md) | Vector appearance panels and undoable solid canvas style edits |
| [chart-data.md](chart-data.md) | Typed axes/series, linked viewports, annotations, projection, hit identity, decimation, tabular export and histogram bin ranges |
| [distribution-plot.md](distribution-plot.md) | Source-backed quantiles and density, declared intervals, selection and accessible tables |
| [financial-displays.md](financial-displays.md) | Source-backed OHLC sessions, candlesticks, bullet and gauge targets, missing values and export seam |
| [polar.md](polar.md) | Numeric polar, categorical radar and radial-bar coordinates, validation, selection and export |
| [uncertainty.md](uncertainty.md) | Declared interval meanings, error bars, continuous bands, provenance and metadata-preserving export |
| [geographic-map.md](geographic-map.md) | Local GeoJSON vectors, WGS84/Web Mercator semantics, antimeridian wrap, selection, attribution and unsupported tile state |
| [scientific-plot.md](scientific-plot.md) | Float64 scientific samples, units, missing values, host-supplied uncertainty, calibrated 2D cursor and reproducible figure export |
| [event-log.md](event-log.md) | Bounded event ingestion, UTC identity, filtering, pause/follow, selection and CSV data seam |
| [docking.md](docking.md) | DockModel operations, zones/lanes/groups, the JSON schema, DockHost gestures |
| [qindastudio-migration.md](qindastudio-migration.md) | Moving QindaStudio onto QindaTK: type map (StudioPanel, DockHost, StudioPill, StudioIcon, StudioTheme → QindaTK), build integration, migration order |
| [css-cheatsheet.md](css-cheatsheet.md) | Tailwind/CSS idiom → QindaTK equivalent, one line each |
| [agent-playbook.md](agent-playbook.md) | How an agent builds and verifies a screen with QindaTK, step by step |
| [decisions.md](decisions.md), [decisions-application.md](decisions-application.md), [decisions-services.md](decisions-services.md) | ADR-lite logs of durable choices |
| [verification.md](verification.md) | Build/test gates, headless verification, manual checklist |
| [platform-support.md](platform-support.md) | Tested Qt/platform/backend matrix, open qualification gates and upgrade procedure |
| [overflow-diagnostics-20260919.md](overflow-diagnostics-20260919.md) | Source diagnosis and scope of the menu/dialog overflow repair |
| [Universal toolkit audit and proposal](universal-toolkit/README.md) | Current component reference, creative/Office/audio/video gap reports, hypothetical extension documentation and implementation roadmap |
| [Complete implementation plan for Sol](universal-toolkit/implementation/README.md) | All 212 proposals in 84 work packages; Luna delegation, Astra escalation, engine integration and full acceptance gates |
| [catalog.json](catalog.json) | Machine-readable control catalog (generated) |

Screenshots live in `docs/screenshots/` and are regenerated by
`tools/scripts/screenshots.sh`.

- [3D scene model and viewport](scene-3d.md): persistent primitives, hierarchy outliner, world/local transforms, real Qt Quick 3D rendering and transactional manipulation.
- [Mesh, rig, UV and texture document](mesh-rig.md): stable triangulated geometry, weighted bone poses, UV seams/islands, projected texture pixels, undo and OBJ/PNG output.
