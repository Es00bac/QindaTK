<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Office toolkit follow-up qualification — 2026-10-10

Branch `finish/office-toolkit-20261010` completes the preserved M98a/M98b work
based on M85 (`8e402c7`). Original WIP branches remain intact. No installed
library or user desktop session was modified.

## Scope and consumer contract

The control batch includes bounded Toast sizing, NumberField and design-history
binding fixes, ten report groups with intervals/running sums/paging, value-list
grid editors, shrinking dock lanes, BorderEditor, task metadata, plain-model
LayerTree, SearchChecklist, FieldWells and SectionedGallery. Follow-up integration
fixes retain chart-only Gantt disclosure, stop StylusHandler stealing popup
clicks, and defer PrintPreview column changes beyond synchronous layout.

The protocol batch includes IMAP keywords/PERMANENTFLAGS/header fetch/COPY and
MOVE handling, prepared SMTP bytes and metadata, ManageSieve, calendar categories,
iMIP, shared POP3/IMAP HTML metadata and optional desktop notifications. Existing
VEVENT recurrence overrides/EXDATE and projection remain the common implementation.
[Mail protocol documentation](mail-protocols.md) specifies compatibility defaults,
limits, TLS and the legacy non-UIDPLUS EXPUNGE limitation.

C++ additions require the coordinated r18 subslot and consumer rebuild. The
[build-tree package](build-tree-consumers.md) was independently configured,
linked and run using QindaTK::qindatk, QindaTK::agent and prefixed public headers.
This offers a matching ABI/QML runtime without a home-prefix installation.

## Reproducible environment

All compilation ran on qinda, through `~/scratch/qqo/qq-build`, inheriting its
shared slots, nice 19, idle I/O and OOM preference. No job/load limits or MAKEOPTS
were changed. Tests used `nice -n 19 ionice -c3`, offscreen and software Quick;
the full gate unset display sockets and used a private runtime directory.
Notifications used a private dbus-run-session bus; protocol tests used local
fake servers, including a real STARTTLS handshake.

The source/build/evidence tree is stored at
`/mnt/archive/cabewse/qindatk/office-toolkit-20261010`, reached by the preserved
`~/scratch/qtk-m98b` symlink. Consumers use
`-DQindaTK_DIR=/home/cabewse/scratch/qtk-m98b/build/dev/cmake-build`.
Do not remove the source/build while consumers reference this export.

## Gates

- Full build: passed. Initial full CTest: 378/384 passed in 537.03 seconds.
  That run exposed one ReportDesigner regression, subsequently corrected.
- The complete QML rerun then passed 1121 cases with one PrintPreview warning
  failure. The final deferred-layout correction is covered by a Scroll fixture
  which failed before the change and passes after it. Final affected QML gates:
  OfficeExtensions 12/12, GanttPlanning 6/6, StylusPopups 4/4. The final complete
  QML CTest passes all 1123 Qt cases in 108.48 seconds. The combined full-run plus correction gates
  qualify 379/384 CTest entries; the five environment exceptions below remain.
  This is an aggregate result, not a claim of a second complete CTest run.
- New protocol/report/history CTest batch: 5/5 passed. Existing stylus and raster
  input CTest batch: 2/2 passed. Every other C++/example test was covered by the
  full run; unchanged tests were not redundantly rebuilt.
- `all_qmllint`: exit 0. New controls and changed expressions have no new
  diagnostics; existing unqualified-access/property-override warnings in older
  DockZone, NumberField, TaskBoard and ReportDesigner remain.
- Header/library/QML consumer probe: configure, build and execution passed.

Five known headless/environment exceptions remain, not product passes:
`tst_canvas_display_rhi`, `tst_canvas_typed_output_rhi`, `tst_canvas_render_rhi`
and `tst_canvas_two_windows_rhi` abort because offscreen cannot initialize their
OpenGL RHI; `example_raster-editor` cannot import the separately packaged
QindaTK.Raster companion. No live GPU/session qualification is claimed.

## Pixels and geometry

Reviewed offscreen PNGs and dumps at 900 and 520 px: Office controls, list/field
controls, FormDesigner, ReportDesigner, LayerTree and chart-only Gantt. Also
reviewed light/comfortable controls and the existing forms/schema/report example.
Evidence is in the build tree's `evidence/` directory; standalone designer
fixture sources are kept alongside the grabs for repeatability.

The 520 px dump shows `DockHost#dockDemo 0,526 520x240`, both side panes inside
the viewport, `ComboBox#gridValueListEditor 0,0 80x22`, and task category swatches
at 12x12. The chart toggle retains `Icon#ganttGroupChevron 2,5 12x12` with a
contrasting backing. Toast remains within both windows. Field wells wrap from
four columns to two; section headers preserve collapsed/selected state.
