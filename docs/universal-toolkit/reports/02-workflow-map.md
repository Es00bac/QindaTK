<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Application families and missing workflows

**Status: gap analysis.** Current status is based on repository source;
requirements below are the author's proposed targets, informed where indicated
by official product documentation. Names identify familiar application styles,
including historical suite members, rather than a current product lineup.

All families can reuse the existing theme, layout, menus, ordinary controls and
docking. “Reuse” in this table means specific extra primitives. It never implies
the domain workflow or file compatibility already exists.

## Creative and publishing applications

| Application family | Specific current reuse | Missing reusable interface/editor | Required engine or application model |
| --- | --- | --- | --- |
| Photoshop-style image editing | Viewport, StylusHandler, TreeRow, ColorField, sliders | [Canvas/color pack](../proposed/04-canvas-color.md): tool modes, layer stack, masks/channels, transforms, selections, brush controls, histogram/curves | Tiled raster document, brush/composite pipeline, adjustment graph, color management, image formats |
| Illustrator-style vector authoring | Viewport, Ruler, NumberField, property rows | [Canvas/color pack](../proposed/04-canvas-color.md): path/node editing, handles, snapping, artboards, stroke/fill, gradients, typography controls | Vector object model, geometry/booleans, tessellation, text-on-path, vector import/export |
| Lightroom/Bridge-style photo library | Thumbnail, Filmstrip, DataTable, SearchField | [Data views](../proposed/03-data-views.md): virtual asset grid, ratings/tags, metadata filters, compare view; nondestructive adjustments | Catalog/index, RAW decode, metadata, preview cache, develop pipeline, sidecars |
| InDesign/Publisher-style page layout | AppWindow, Viewport, Ruler, ColorButton, PropertyGroup | [Office pack](../proposed/05-documents-office.md): page/spread views, linked text frames, styles, master pages, preflight, print/export UI | Pagination/typesetting, document objects, linked assets, output/color pipeline |
| Acrobat-style PDF workflow | Viewport, Thumbnail, forms/dialog primitives | [Office pack](../proposed/05-documents-office.md): page navigation, text selection/search, annotations, form overlays, redaction review | PDF parser/renderer/writer, text geometry, OCR, signature and redaction operations |
| Premiere/Media Composer-style NLE | DockHost, TimeRuler, Filmstrip, Thumbnail, Waveform | [Video pack](../proposed/07-video-motion.md): timeline editing, source/program monitors, trim, multicam, bins, captions, scopes | Frame clock, edit sequence, codecs, proxies, media relinking, compositor and render queue |
| After Effects-style motion/compositing | Viewport, property rows, basic numeric controls | [Video/motion pack](../proposed/07-video-motion.md): keyframes, curve editor, effect stack, masks, tracking, composition graph | Evaluation graph, expressions, compositor, image cache, tracking/roto algorithms |
| Audition-style waveform and spectral editing | Waveform, Graph, Meter, RangeSlider | [Audio pack](../proposed/06-audio-music.md): multichannel edit view, spectrogram selection, effect rack, loudness display | Sample editing, FFT/spectral processing, DSP, recording/playback and analysis |
| Animate/Character Animator-style animation | StylusHandler, Viewport, layers as composed rows | [Motion pack](../proposed/07-video-motion.md): dope sheet, onion skin, exposure/pose tools, rig handles and lip-sync UI | Animation/rig model, deformation, tracking, audio alignment and animation export |
| Substance-style material and texture tools | Docking, property controls, colors, viewport shell | [Motion/3D pack](../proposed/07-video-motion.md): node graph, texture layer stack, UV view, material/lighting previews | GPU material graph, texture baking/painting, mesh/scene formats and 3D renderer |
| Dreamweaver-style code/web authoring | Tabs, panels, search and text inputs | [Specialist pack](../proposed/09-specialized-domains.md): CodeEditor, project tree, diagnostics, preview inspector | Language services, text buffer, browser/preview adapter, source control and build runner |
| Media Encoder-style batch conversion | DataTable, ProgressBar, dialogs | [Integration pack](../proposed/08-platform-integrations.md): job queue, preset editor, dependency/cancel/retry UI | Codec/export workers, resource scheduling, output validation and restartable jobs |
| XD/Figma-style interaction design | Canvas shell, dock panels, ordinary controls | [Canvas pack](../proposed/04-canvas-color.md): editable component/artboard tree, constraints, symbol instances; prototype links | Design document model, reusable component inheritance, constraints and prototype runtime |

The need for structured layer operations is illustrated by Adobe's
[Smart Object documentation](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/smart-objects/create-embedded-smart-objects.html)
and [mask editing](https://helpx.adobe.com/photoshop/using/editing-layer-masks.html).
Publishing adds linked text and production checks, as documented for
[threaded frames](https://helpx.adobe.com/indesign/desktop/add-and-manage-text/add-and-import-text/thread-text-frames.html)
and [preflight](https://helpx.adobe.com/indesign/desktop/print/preflight/configure-and-use-the-preflight-panel.html).
These motivate the proposed model contracts; reproducing a panel's appearance
alone would not implement those operations.

## Office and productivity applications

| Application family | Specific current reuse | Missing reusable interface/editor | Required engine or application model |
| --- | --- | --- | --- |
| Word-style word processor | AppWindow, Viewport, Ruler, TextArea, color controls | Rich document view, style/format controls, page navigation, comments/revisions, tables, equations, find/replace | Document tree, shaping/pagination, anchored objects, fields/references, file conversion and print |
| Excel-style spreadsheet | DataTable, TableColumn, NumberField, Graph | SheetView, range selection, FormulaBar, fill handle, freeze panes, headers, charts, pivots, validation rules | Workbook/cell storage, formula parser/dependency graph, recalculation, formats and data connectors |
| PowerPoint-style presentations | Viewport, Thumbnail, TabStrip, property rows | SlideCanvas, SlideSorter, masters, object tools, animation pane, notes/presenter view and independent audience output | Slide object model, theme/layout inheritance, transitions, media playback and file formats |
| OneNote-style notebook | TextArea, StylusHandler, tabs, panels | Notebook navigation, block editor, infinite note canvas, attachments, ink selection, tags/search | Durable block/ink model, attachment store, indexing, synchronization and conflict resolution |
| Outlook-style mail/calendar/tasks | ListRow, DataTable, SearchField, menus/forms | Message/thread view, recipient token entry, rich composer, agenda/week/month calendar, recurrence and task board | Mail/account protocols, attachment store, calendar/time-zone/recurrence model, offline cache and search |
| Access-style database application builder | DataTable, forms and inspector rows | FormDesigner, schema/relationship canvas, QueryDesigner, editable record views and ReportDesigner | Database connections, transactions, query execution, schema metadata and report pagination |
| Visio-style diagramming | Viewport, Ruler, color/number fields | DiagramCanvas, connection ports, orthogonal connectors, stencil library, alignment and routing feedback | Diagram graph, shape geometry, routing/layout algorithms and interchange |
| Project-style planning | DataTable, TimeRuler, progress/status controls | GanttView, resource/time allocation grid, dependency editor, calendar scales, baseline comparison | Schedule/dependency engine, working calendars, constraints and resource leveling |

All Office proposals are in the [document/Office pack](../proposed/05-documents-office.md),
with diagrams/Gantt in [data views](../proposed/03-data-views.md) and
[specialist domains](../proposed/09-specialized-domains.md).

Official examples show why these require document models: Word's
[tracked revisions](https://support.microsoft.com/en-us/word/training/track-changes-in-word)
retain reviewable edits; Excel's
[data model](https://support.microsoft.com/en-us/excel/create-a-data-model-in-excel)
relates tables used by pivots; PowerPoint's
[slide masters](https://support.microsoft.com/en-us/powerpoint/training/what-is-a-slide-master-in-powerpoint)
and [presenter view](https://support.microsoft.com/en-us/powerpoint/training/use-presenter-view-in-powerpoint)
add inheritance and separate display roles.

## Professional audio and music

| Workflow family | Specific current reuse | Missing reusable interface/editor | Required engine or application model |
| --- | --- | --- | --- |
| Pro Tools-style recording/post | Waveform, Meter, TimeRuler, dock panels | EditTimeline, transport, takes/playlists, fades, mixer strips, sends/inserts, routing, automation, timecode and loudness | Realtime processing graph, recording buffers, device/clock management, plugin host, sync and session serialization |
| Logic-style composition/production | The same media primitives plus sliders/NumberField | PianoRoll, controller lanes, step sequencer, drum grid, tempo/signature lanes, arrangement/clip launcher, instrument browser | MIDI/event scheduling, instruments/samplers, tempo map, musical transforms and audio engine |
| Sibelius-style notation | Viewport, text controls, ordinary inspector chrome | ScoreView, note entry, symbol palettes, part/page layout, playback cursor and engraving inspector | Semantic notation, engraving, parts, MusicXML-like interchange and playback mapping |
| Live performance/control surface app | Buttons, Switch, Meter, Graph, panels | Large configurable performance pads, parameter binding, MIDI learn, transport and device/session feedback | Controller I/O, bounded-latency processing, routing, session state and hardware protocols |

Apple documents [audio/MIDI editing, mixing, automation and notation](https://support.apple.com/guide/logicpro/what-is-logic-pro-lgcpe9cc45dd/10.7/mac/11.0).
Avid documents [recording, playlists, clip editing, routing and sound-to-picture workflows](https://www.avid.com/pro-tools).
The [audio/music proposals](../proposed/06-audio-music.md) target reusable UI
contracts for these classes. They do not promise identical feature sets,
hardware support or third-party plugin compatibility.

## Video, film and animation

For “Avid,” this book covers Media Composer in addition to Pro Tools and the
notation application class. Avid's
[Media Composer editing guide](https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf)
documents editing and multicamera workflows. Adobe's
[keyframe tools](https://helpx.adobe.com/after-effects/desktop/animate-in-after-effects/animation-keyframes/setting-selecting-deleting-keyframes.html)
and [Graph Editor](https://www.adobe.com/learn/after-effects/web/adjusting-keyframes-dynamic-movement)
illustrate the separate requirements for animation editing.

The required shared base is exact time representation, selection, transactions,
media providers and viewport synchronization. Video-specific trimming and
musical quantization then remain separate policies. A timeline drawn with
floating-point seconds is insufficient evidence of frame/sample-accurate edits.

## Applications outside the named suites

| Class | Additional surface | Why the extension architecture matters |
| --- | --- | --- |
| IDE and technical editor | CodeEditor, diagnostics, diff/merge, terminal integration | Very large text and language services have different models from a form field |
| CAD and engineering | Constraint geometry, dimension tools, scene/object inspectors | Exact geometry and units must remain independent of screen pixels |
| GIS and mapping | MapView, spatial layer tree, projection-aware picking | Geographic coordinates and tile services need domain adapters |
| Scientific/medical imaging viewer | Image slices, transfer functions, volumetric views | Large typed arrays, calibrated units and provenance need explicit models |
| Business dashboard/admin app | Editable data grids, charts, filters, forms and jobs | Ordinary controls are reusable but data loading/validation must scale |
| Games, simulations and device tools | Custom render surface, controller input, scene state | A custom engine must be embeddable without making QML its execution loop |

These are extension targets, not assertions of regulated suitability or existing
support. The [specialist addendum](../proposed/09-specialized-domains.md) keeps
their algorithms outside the common widget layer.
