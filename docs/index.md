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
| [gantt.md](gantt.md) | Task schema, working-calendar projection, schedule transactions, dependency and grid seams |
| [control-interaction.md](control-interaction.md) | Owner state, user signals, editing states, focus and assistive action baseline |
| [application-contracts.md](application-contracts.md) | Strong IDs, revisions, exact QML values, snapshots, cancellation and capability discovery |
| [commands.md](commands.md) | Scoped command contexts, registry and bindings, shortcut routing, focus restoration, and menu/toolbar/palette adapters |
| [editing.md](editing.md) | Reference document, reversible transactions, per-document undo, stable selection, property projection and validation |
| [editable-grid.md](editable-grid.md) | Sparse 2D model, stable IDs, bounded visible ranges, frozen geometry and range transactions |
| [query-pivot.md](query-pivot.md) | Typed query AST, null semantics, bounded grouping and pivots, provenance and grid/provider adapters |
| [diff-view.md](diff-view.md) | Bounded local diff, lazy large-provider seam, stable hunk IDs and host transaction requests |
| [resource-table.md](resource-table.md) | Stable resource and time-bucket identity, allocation units, conflicts and reversible edits |
| [persistence.md](persistence.md) | Reference document sessions, versioned file format, atomic save, multi-document close and recovery behavior |
| [format-registry.md](format-registry.md) | Operation-specific import/export providers, preflight receipts, loss acknowledgement and job lifecycle |
| [settings-workspace.md](settings-workspace.md) | Scoped settings, independent workspace schema, view restore, monitor recovery and shade policy |
| [assets-jobs.md](assets-jobs.md) | Bounded job queue, asset subscriptions/cache, resource relinking and local media backend |
| [history-feedback.md](history-feedback.md) | Stable edit-history navigation, bounded notifications and task status contracts |
| [find-replace.md](find-replace.md) | Revision-tagged text search, cancellation and atomic replace transactions |
| [transfers.md](transfers.md) | Versioned clipboard and drag/drop payloads, destination validation and undoable reference transfers |
| [accessibility-locale-input.md](accessibility-locale-input.md) | Virtual accessibility nodes, per-instance locale policy and gesture arbitration |
| [time-map.md](time-map.md) | Exact sample/frame/tick conversions, tempo segments, timecode and precision-safe QML values |
| [native-integration.md](native-integration.md) | Native window/view lifecycle, file dialogs and desktop capability boundaries |
| [canvas.md](canvas.md) | Spatial canvas: document/view/device transforms, object model, picking, tool gestures, synchronized views and the tile rendering boundary |
| [controls-media.md](controls-media.md) | Media controls: thumbnails, waveforms, filmstrips, time rulers and shortcut caps |
| [edit-timeline.md](edit-timeline.md) | Multitrack edit timeline: exact ticks, typed edit transactions, snapping, ripple, history, virtualization, media and transfer seams |
| [chart-data.md](chart-data.md) | Typed axes/series, linked viewports, annotations, projection, hit identity, decimation, tabular export and histogram bin ranges |
| [distribution-plot.md](distribution-plot.md) | Source-backed quantiles and density, declared intervals, selection and accessible tables |
| [financial-displays.md](financial-displays.md) | Source-backed OHLC sessions, candlesticks, bullet and gauge targets, missing values and export seam |
| [polar.md](polar.md) | Numeric polar, categorical radar and radial-bar coordinates, validation, selection and export |
| [uncertainty.md](uncertainty.md) | Declared interval meanings, error bars, continuous bands, provenance and metadata-preserving export |
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
