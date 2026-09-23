<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Hypothetical application composition recipes

These are architectural recipes, **not runnable QML examples**. Current building
blocks are identified separately from proposed types. Each recipe defines an
end-to-end edit so a future implementation can be judged by behavior.

All recipes start with existing AppWindow, Theme/Density, Flex/Grid/Stack,
menus/toolbars/status, dialogs and appropriate DockHost panels. The shared
proposed spine is DocumentSession → CommandContext/Registry → EditTransaction →
domain model → UndoService, with stable selection and versioned async results.

## Image or vector editor

**Use now:** Viewport, StylusHandler, Ruler, NumberField, ColorField/Swatches,
PropertyRow/Group, Thumbnail and dock panels. **Add:** CanvasView, ToolController,
SelectionOverlay, TransformHandles, SnapModel, LayerTree, ColorContext and the
chosen PathEditor or brush controls. See [canvas proposals](proposed/04-canvas-color.md).

The domain model holds objects/layers or raster tiles, plus assets and color
metadata. Pointer hits resolve to IDs, selection updates the inspector, and a
handle/number edit previews one transaction. Commit changes the model and adds
history; renderer invalidation produces new pixels. Escape restores the original.

**Proof:** create three objects/layers, select several, transform, mask or paint,
undo/redo, save/reopen and export. Verify document colors remain unchanged when
the interface switches between light and dark. A stroked overlay by itself is
not a completed paint engine.

## Word processor or publishing application

**Use now:** AppWindow, Viewport, Ruler, toolbar/color controls and inspector
groups. **Add:** RichDocumentView, FormatBar, StyleManager, PageNavigator,
TextFrameEditor for publishing, ReviewPane and PrintPreview. See
[Office proposals](proposed/05-documents-office.md).

The document owns logical text, styles, objects and anchored references. The
layout engine maps them to pages; selection/cursor use logical positions. Views
virtualize pages, and edits invalidate affected layout. Formatting is an
operation on the document selection rather than a font change on a QML Label.

**Proof:** insert multilingual styled text, a table and an anchored image;
edit across a page boundary, review a tracked change, undo and reopen. Compare
preview and exported pagination. Ruler markers must feed canonical paragraph
properties, not become the only storage of indentation.

## Spreadsheet or business-data application

**Use now:** DataTable for read-only summaries, search/number/text inputs,
Graph/Sparkline for telemetry and the ordinary shell. **Add:** EditableGrid,
SelectionModel, HeaderView, SheetView, FormulaBar, WorkbookTabs and ChartView.

For a business data editor, a database/model adapter supplies rows and validated
edits. For a spreadsheet, a workbook engine supplies cells, formulas, references
and recalculation. Both share view virtualization and range-selection mechanics,
but they do not share every edit rule.

**Proof:** paste a rectangular range, reject invalid values with an explicit
policy, sort a view, undo and reopen. In the workbook variant, copy a formula
with relative/absolute references and compare calculated values against a known
fixture. Existing DataTable sorting signals do not implement this workflow.

## Presentation application

**Use now:** Viewport, Thumbnail, TabStrip, colors, numeric fields and docking.
**Add:** SlideCanvas, SlideSorter, SlideMasterEditor, AnimationPane, PresenterView
and WindowHost, using the shared canvas selection/transform tools.

The slide model owns objects and master/layout inheritance. Editor views share
document data; presenter and audience windows have distinct roles and view
state. Playback evaluation consumes the same saved animation data that the
editor exposes.

**Proof:** create and reorder slides, apply a master, transform text/shapes,
animate an object, save/reopen and present. Disconnect the audience screen and
verify notes do not appear on the replacement audience output.

## DAW: recording, composition and mixing

**Use now:** Waveform/TimeRuler/Filmstrip for previews, numeric controls and panels.
**Add:** TimeMap, TransportModel/Bar, EditTimeline, track/clip views, AudioMeter,
Fader/Knob, MixerView, AutomationLane and PianoRoll or ScoreView as needed. See
[audio proposals](proposed/06-audio-music.md).

The engine owns the clock, routing graph and sample/event processing. Its GUI
facade publishes bounded state snapshots. Timeline gestures commit exact
sample/beat positions; parameter gestures preserve automation mode and pass
through an adapter with defined scheduling. Peak/frame requests are asynchronous.

**Proof:** import or record takes, comp a region, trim/fade, move a MIDI note and
write automation. Undo, reopen the session and render a known result. Verify
timing with the engine, not with the QML playhead's screen position. No spoken
status updates or sound playback are needed to author or test the UI contract.

## NLE: editorial video and sound-to-picture

**Use now:** studio-shell composition, docking, supplied thumbnails/filmstrips
and waveform/ruler displays. **Add:** shared timeline/time services,
SourceProgramMonitor, MediaBin, TrimView, MulticamView, CaptionEditor,
VideoScopes and RenderQueueView. See [video proposals](proposed/07-video-motion.md).

The sequence model owns edit decisions and source references. Decode providers
return frame IDs/timestamps, a compositor assembles output, and the UI exposes
their revisions. Ripple/slip/roll policies validate linked-track effects before
one transaction commits. Missing media has a stable identity that can be relinked.

**Proof:** make a sequence from mixed sources, trim at known frames, switch a
multicam angle, relink a missing source, add a caption and export. Compare exact
boundary frames and durations before making frame-accuracy claims.

## Motion graphics, animation or material editor

**Use now:** property controls, Viewport/Ruler and dock panels. **Add:**
KeyframeTimeline, CurveEditor, CompositionView, NodeGraph, effect/appearance
controls and, where required, SceneView3D/MaterialPreview.

The domain graph owns evaluation, parameter dependencies and cycles. A keyframe
editor changes timed parameter data; a graph editor changes topology; both use
the common command/transaction interface. Preview requests carry graph and time
revisions so old work cannot replace a newer result.

**Proof:** connect a graph, animate a parameter, edit a tangent and render sampled
times. Save/reopen, undo and verify the same evaluated values and output frames.
Reject incompatible ports/cycles according to the graph's declared policy.

## Mail, calendar, notebook or database utility

**Use now:** list rows, tables, search, text inputs, tabs and forms.
**Add as needed:** MailView/RichMailComposer/TokenField, CalendarView/RecurrenceEditor,
NotebookView/BlockEditor, or FormDesigner/DatabaseSchemaView/ReportDesigner.
All reuse the data and application packs with distinct domain adapters.

**Proof:** perform the app's main offline edit, save a draft or transaction,
reload, reconcile an external update and retain recoverable local state. Model
time zones, account identity and database transaction boundaries explicitly;
they cannot be inferred from general-purpose form controls.
