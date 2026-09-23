<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# QindaTK as a universal application toolkit

**Audit and hypothetical documentation set — 2026-09-22.**

QindaTK can already supply the shell and many inspectors of a professional
desktop application. Its biggest missing pieces are reusable editing surfaces,
shared application services, scalable data models/views, and contracts for
specialized document and media engines. Adding those would let applications
share substantially more than their appearance.

This book describes **101 current types and helpers**, maps the requested
application families to gaps, and provides a proposed extension reference.
Product names identify workflow families; this is not a claim of file-format
compatibility or feature parity with any commercial product.

## Read by purpose

| Document | Purpose |
| --- | --- |
| [Executive assessment](reports/01-assessment.md) | What is missing, what is already useful, and the highest-value work |
| [Application/workflow coverage](reports/02-workflow-map.md) | Adobe, Office, Pro Tools, Logic, Media Composer and adjacent application classes |
| [Toolkit and engine boundaries](reports/03-engine-boundaries.md) | What belongs in QindaTK, what needs a backend, and integration candidates |
| [Source audit](reports/04-source-audit.md) | Concrete implementation limits, documentation drift and evidence quality |
| [Current type index](current/00-type-index.md) | Alphabetically discoverable entries for every current component/type in scope |
| [Hypothetical architecture](proposed/00-architecture.md) | Ownership, models, commands, gestures, threading and extension contracts |
| [Proposal index](proposed/10-proposal-index.md) | Categorized list of proposed additions and priorities |
| [Application recipes](recipes.md) | How the current and proposed pieces compose into representative apps |
| [Implementation roadmap](roadmap.md) | Dependency order, deliverables and measurable acceptance gates |
| [Complete Sol implementation plan](implementation/README.md) | 84 executable work packages covering all 212 proposals and supporting engine/foundation work |
| [Sol handoff prompt](implementation/sol-handoff.md) | Sol orchestration, bounded Luna tasks, selective Astra consultation and preserved build policy |
| [Sources and method](sources.md) | Repository baseline, official external references and limitations |
| [Verification record](verification.md) | Coverage, link and source consistency checks for this documentation set |

## Current reference

| Category | Contents |
| --- | --- |
| [Foundations](current/01-foundations.md) | Theme, density, tokens, ramps, icons, desktop bridge |
| [Layout](current/02-layout.md) | Flex, Grid, Stack, Box, Scroll, attached properties, splitters |
| [Text and actions](current/03-text-actions.md) | Labels, buttons, chips, selectors, status indicators, shortcut caps |
| [Inputs](current/04-inputs.md) | Text, numbers, sliders, choices, color fields/swatches |
| [Shell and navigation](current/05-shell-navigation.md) | Windows, toolbars, menus, tabs, status bars, palette |
| [Structure and feedback](current/06-structure-feedback.md) | Panels, inspector rows, list/tree delegates, dialogs, notices |
| [Data and telemetry](current/07-data-telemetry.md) | DataTable, columns, Graph, series, Sparkline, Meter |
| [Media and canvas](current/08-media-canvas.md) | Thumbnails, waveforms, filmstrips, rulers, viewport, stylus |
| [Docking](current/09-docking.md) | DockModel, DockHost, declarations and all internal renderers |
| [Tools and verification infrastructure](current/10-tools-tests.md) | Preview, examples, tests, packaging and non-widget helpers |

## Addendum: hypothetical extension reference

Every file below is marked **PROPOSED**. The proposed names are design sketches,
not types available in the current import. Each entry supplies a purpose,
input/output contract, dependency boundary and a completion check.

| Category | Contents |
| --- | --- |
| [Application services](proposed/01-application-services.md) | Commands, undo, documents, selection, settings, jobs, recovery |
| [Controls and navigation](proposed/02-controls-navigation.md) | Menus, ribbons, inspectors, forms, pickers, advanced navigation |
| [Data views and visualization](proposed/03-data-views.md) | Trees, editable grids, charts, pivots, filters, large collections |
| [Canvas, vector and color](proposed/04-canvas-color.md) | Object selection, transforms, guides, paths, layers, color and brushes |
| [Documents and Office](proposed/05-documents-office.md) | Rich documents, sheets, slides, PDF, notes, mail, calendar and databases |
| [Audio and music](proposed/06-audio-music.md) | Timeline, transport, mixer, automation, wave editing, MIDI and notation |
| [Video, motion and 3D](proposed/07-video-motion.md) | Monitors, trim, multicam, scopes, keyframes, nodes, tracking and 3D |
| [Platform and integrations](proposed/08-platform-integrations.md) | Accessibility, localization, OS services, plugins, collaboration, export |
| [Additional application classes](proposed/09-specialized-domains.md) | Code, diagrams, CAD, maps, science and other extension surfaces |

## Status vocabulary

| Label | Meaning |
| --- | --- |
| CURRENT | Source implementation exists in this working tree; exact limits are stated |
| PARTIAL | A related primitive exists, but the complete workflow is not implemented |
| COMPOSE | Existing pieces can form the UI; application behavior still has to be written |
| PROPOSED | New toolkit component/service or an extension to a current one |
| ENGINE | Specialized algorithm, file format, runtime, device or remote service supplied through an adapter |
| UNVERIFIED | Source suggests behavior or a risk; this audit did not measure it at runtime |

Priorities: **P0** establishes shared contracts/correctness, **P1** unlocks many
applications, **P2** completes a domain, and **P3** extends specialist workflows.
They indicate dependency/value order, not duration or promised delivery dates.

## How to use this book

For the requested complete implementation, start with the
[Sol execution plan](implementation/README.md). It includes the full advanced
scope, a dependency graph, coverage ledger, worker instructions and acceptance
gates. This documentation has not launched implementation or agents.

Start with the assessment and the workflow row for the app you want to build.
Follow its links to proposed components, then use the roadmap's vertical slice
to validate them in a real application. Existing operational references remain
[controls](../controls.md), [layout](../layout.md), [theming](../theming.md),
[docking](../docking.md), and [media controls](../controls-media.md).

The audited baseline is HEAD `89d5ca39a13c974d2877597a39d0fe6db88fbaf0` plus local
changes present during inspection. This is a source and documentation audit;
runtime tests, rendered geometry, screenshots and performance measurements were
not performed. Proposed contracts have not been adopted as repository decisions.
