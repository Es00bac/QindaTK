<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-030 chart visual lane evidence

Status: visual implementation checkpoint; WP-030 is not accepted yet.

## Delivered in this lane

- `Tk.ChartDataset` is a bounded `QAbstractListModel` ingestion adapter with
  stable series and point IDs, exact decimal UTC millisecond input, gap rows,
  bubble size and vector displacement fields, and row/error notifications.
- `Tk.ChartView` and `Tk.ChartLegend` render every `ChartSeries::Kind` through
  the typed axes and core projection. Paint decimation is separate from the
  full-resolution table and CSV records. Theme roles supply default series,
  fill, axis, and chart colors.
- Selection and point inspection use stable IDs. Wheel zoom, middle-button
  pan, normalized viewport control, a virtual accessible data table, and CSV
  output are present. The table includes bubble size and vector dx/dy.
- `renderImage`, `exportPng(path,size)`, and `exportSvg(path,size)` share the
  same chart drawing routine. PNG and SVG file writes are atomic and limited
  to 1–8192 pixels per side. `exportImageReady` and the QML
  `exportRequested("image", image)` signal remain host integration seams.

## Focused verification

Isolated build directory: `build/wp030-visual` (Qt 6.11.1, configured system
parallelism unchanged).

- `cmake --build build/wp030-visual --target qtk-preview tst_chart_dataset tst_qml`
  completed successfully.
- `tst_chart_dataset`: 7 passed, including exact UTC input, rejection atomicity,
  a known-coordinate line that ends in a gap, PNG pixel output, SVG vector
  markup, and safe axis destruction.
- `tst_chart_data`: 9 passed after rebuilding its executable for the expanded
  table row schema.
- `tst_qml -input tests/qml/tst_chart_view.qml`: 4 passed, covering exact time,
  CSV and selection/zoom, scatter/table output, and histogram rules.
- `qtk-preview examples/chart-view/Main.qml --check --size 960x560` passed.
- Software preview geometry at compact density: `ChartView#chartView 12,68
  936x429`, `ChartRenderer#chartPlot 0,0 936x429`.
- `--grab` output was inspected at compact and comfortable density:
  `build/wp030-visual/chart-compact.png` and
  `build/wp030-visual/chart-comfortable.png` (960x560). Both show the expected
  gap-separated line, step series, scatter points, axes, legend, and table
  toggle without clipping.
- `QSG_INFO=1 ... qtk-preview ... --grab build/wp030-visual/chart-rhi.png
  --size 960x560 --density compact --gpu` selected QRhi OpenGL. Qt reported
  AMD Radeon RX 5700 XT (Mesa 26.1.8, OpenGL 4.6); the capture was visually
  inspected and matches the software result. Ordinary `--grab` reported
  `qt.scenegraph.general: Loading backend software`.
- Earlier in this lane, `all_qmllint` completed successfully with existing
  repository warnings. `git diff --check` is clean.

## Remaining acceptance gates

- Installed-consumer integration and a consolidated supported-build matrix
  still need verification before acceptance. The renderer's direct C++ raster
  and file export smoke checks use offscreen software; both the software and
  actual RHI OpenGL preview captures were inspected.
- Renderer file export works, but application-wide export dialogs/jobs,
  format policy, and destination selection belong to the host integration
  seam and are not implemented by this lane.
