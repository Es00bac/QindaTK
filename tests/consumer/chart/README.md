<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Installed chart consumer fixture

This standalone CMake project finds the staged `QindaTK` package, links only
`QindaTK::qindatk`, resolves the installed `QindaTK` QML import directory, and
loads `Tk.ChartDataset` and `Tk.ChartView`. Its tiny three-point signal checks
QML model ingestion, keyboard selection, full-resolution point inspection,
CSV, and PNG/SVG file export through the public `ChartView` API.

Configure against an installed staging prefix with
`-DCMAKE_PREFIX_PATH=<staged-prefix>`, build with the machine's configured job
and load limits, then run with `QT_QPA_PLATFORM=offscreen` and
`QT_QUICK_BACKEND=software`. No source-tree QML import path is added, so a
successful run verifies the installed module registration and import layout.
