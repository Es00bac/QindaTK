<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-031 DAT-17 Gantt lane

Gantt source was committed as `99d690b` in the isolated `wp031-gantt` worktree
and merged into the integration tree after the Heatmap/Distribution checkpoint.
The isolated Qt 6.11.1 build, `tst_gantt`, focused QML test, standalone example
and gallery smoke test passed. The build used plain `cmake --build` with the
configured system `MAKEOPTS=-j24 -l24` unchanged. The shared Qt 6.11.1
integration build, focused `tst_gantt`, focused QML file, standalone example
and gallery also passed. Dual-Qt gates are pending.

`GanttCalendar` projects exact UTC millisecond string instants onto local
working intervals, compressing nonworking time and counting actual elapsed
minutes across DST transitions. `GanttTaskModel` validates and atomically
replaces stable UUID rows, keeps previews separate from committed rows, and
passes entire schedule edits through a transaction adapter. `GanttView` draws
task intervals, progress, baselines, dependencies and allocation overlays;
selection can share `SelectionModel` with the adjacent grid. See
[the contract](../../../gantt.md) for reference adapter limitations.

The [compact capture](../../../screenshots/gantt-compact.png) and
[comfortable capture](../../../screenshots/gantt-comfortable.png) were opened
and inspected. At 1120×640, the compact dump shows
`GanttView#exampleGantt 8,36 1104x296`, `ganttHeader 0,0 1104x22`, and the
first `ganttTaskBar 0,2 235x18`. Comfortable density shows
`GanttView#exampleGantt 10,44 1100x291`, `ganttHeader 0,0 1100x26`, and the
first `ganttTaskBar 0,2 277x22`. The aligned task grid is visible below.
