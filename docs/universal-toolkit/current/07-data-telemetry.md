<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Tables and telemetry displays

Status: **CURRENT — source review dated 2026-09-22.** These entries describe the
working tree, not promised capabilities or a runtime certification. Use
`import QindaTK as Tk` for main-module types. `T` below means QtQuick.Templates.

Each entry describes purpose, consumer behavior and limits. **Declared members**
list locally declared properties, including computed implementation helpers;
inherited Qt properties/signals are not repeated exhaustively. A `ro` suffix
means read-only at the declaration. Method lists can include internal helpers;
the consumer contract is the prose, the existing reference and the linked source.
Object names list source assignments; expressions ending in an ID/index generate
per-item names. Test links show source coverage, not a test run in this audit.

[Book index](../README.md) · [Type index](00-type-index.md) ·
[Source findings](../reports/04-source-audit.md)

## DataTable

Row-virtualized readout table with sortable headers and custom cells.

**Source:** [src/qml/DataTable.qml](../../../src/qml/DataTable.qml) · **Base:** `Item` · **Kind:** QML component.

columns are TableColumn objects; model must be an array or supply get(index) records with a supported row count/model interface. sortRequested asks the owner to reorder; currentIndex is one selected row. Fixed columns resize; cells can use ramps/delegates. It lacks column virtualization, frozen panes, spreadsheet ranges and standard cell editing.

**Declared members:** `columns: list<Tk.TableColumn>`, `model: var`, `sortKey: string`, `sortOrder: int`, `currentIndex: int`, `showHeader: bool`, `alternatingRows: bool`, `rowHeight: real`, `emptyText: string`, `columnWidths: var`, `count: int` (ro), `visibleColumns: var` (ro).

**Declared signals:** `sortRequested(string key, int order)`, `activated(int index)`, `rowRightClicked(int index, var point)`, `columnResized(string key, real width)`.

**Source methods:** `widthOf()`, `resolvedWidth()`, `toggleSort()`, `cellOf()`, `recordAt()`. See source for parameters and helper visibility.

**Object names:** `"tableHeader"`, `"headerCell_" + headerCell.modelData.key`, `"tableBody"`, `"tableRow"`.

**Coverage pointers:** [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## TableColumn

Declarative column specification consumed by DataTable.

**Source:** [src/qml/TableColumn.qml](../../../src/qml/TableColumn.qml) · **Base:** `QtObject` · **Kind:** QML component.

key/title/width/minWidth/flex, align/mono/muted, visible/sortable/resizable/descendingFirst, formatter/delegate and ramp range describe cells. The default width follows `Theme.size.tableColumn` (80 compact), and `minWidth` follows `Theme.size.control` (24 compact); callers can override them for document data. display(value,row) formats text. Loaded delegates read parent.value/row/column. It is not a spreadsheet column model with formulas or transactions.

**Declared members:** `key: string`, `title: string`, `width: real`, `minWidth: real`, `flex: real`, `align: int`, `mono: bool`, `muted: bool`, `visible: bool`, `sortable: bool`, `resizable: bool`, `descendingFirst: bool`, `tooltip: string`, `formatter: var`, `delegate: Component`, `ramp: var`, `rampFrom: real`, `rampTo: real`.

**Source methods:** `display()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## Graph

C++ bounded history plot with multiple time-ordered numeric series.

**Source:** [src/core/graph.h](../../../src/core/graph.h) · **Base:** `QQuickItem` · **Kind:** C++ type.

capacity, min/max, autoScale/floor/headroom, mirrored and grid styling configure it. append/appendTo/appendRow/clear and seriesAt/seriesCount feed it. GPU scene-graph geometry has a software raster fallback. X is sample order; there are no explicit timestamp axes, annotations, arbitrary XY curves or editing handles.

**Declared members:** `series: QQmlListProperty<QindaTK::GraphSeries>` (ro), `capacity: int`, `minValue: qreal`, `maxValue: qreal`, `autoScale: bool`, `autoScaleFloor: qreal`, `headroom: qreal`, `effectiveMax: qreal` (ro), `mirrored: bool`, `gridRows: int`, `gridColumns: int`, `gridColor: QColor`, `baselineColor: QColor`.

**Declared signals:** `seriesChanged()`, `capacityChanged()`, `scaleChanged()`, `styleChanged()`.

**Source methods:** `seriesCount()`, `appendTo()`, `append()`, `appendRow()`, `clear()`, `seriesAt()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp), [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## GraphSeries

Fixed-capacity ring of numeric samples owned by Graph.

**Source:** [src/core/graph.h](../../../src/core/graph.h) · **Base:** `QObject` · **Kind:** C++ type.

values or append feed data; at(0) is oldest, last/peak/count/capacity are readouts. color/ramp/fill/fillOpacity/lineWidth/visible/label style it. Capacity is set by Graph; ordinary non-finite inputs are sanitized. This object is GUI-facing, not a realtime audio buffer.

**Declared members:** `color: QColor`, `ramp: QindaTK::ThemeRamp *`, `fill: bool`, `fillOpacity: qreal`, `lineWidth: qreal`, `visible: bool`, `label: QString`, `values: QVariantList`, `count: int` (ro), `last: qreal` (ro), `peak: qreal` (ro), `capacity: int` (ro).

**Declared signals:** `styleChanged()`, `samplesChanged()`, `capacityChanged()`.

**Source methods:** `append()`, `clear()`, `at()`. See source for parameters and helper visibility.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp), [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## Sparkline

One-series Graph sized for compact rows or table cells.

**Source:** [src/qml/Sparkline.qml](../../../src/qml/Sparkline.qml) · **Base:** `Tk.Graph` · **Kind:** QML component.

values, color/ramp, fill, fillOpacity and lineWidth proxy the series; last/peak are readouts and append is inherited. Theme defines sparkline extent, with no grid/baseline. It has Graph's sample-order limitations.

**Declared members:** `color: alias`, `ramp: alias`, `fill: alias`, `fillOpacity: alias`, `lineWidth: alias`, `values: alias`, `last: alias` (ro), `peak: alias` (ro), `tooltip: string`.

**Coverage pointers:** [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).

## Meter

Painted magnitude bar for utilization and bounded measurements.

**Source:** [src/core/meter.h](../../../src/core/meter.h) · **Base:** `QQuickPaintedItem` · **Kind:** C++ type.

value/from/to normalize to position; vertical, segments, gap/radius, colors and ramp configure display. ByPosition colors sections, ByValue colors the complete reading; valueColor can tint a label. It has no audio peak hold, clipping latch, loudness integration or decay ballistics.

**Declared members:** `value: qreal`, `from: qreal`, `to: qreal`, `position: qreal` (ro), `color: QColor`, `trackColor: QColor`, `ramp: QindaTK::ThemeRamp *`, `rampMode: RampMode`, `segments: int`, `segmentGap: qreal`, `radius: qreal`, `vertical: bool`, `valueColor: QColor` (ro), `tooltip: QString`, `hovered: bool` (ro).

**Declared signals:** `valueChanged()`, `rangeChanged()`, `styleChanged()`, `tooltipChanged()`, `hoveredChanged()`.

**Coverage pointers:** [tst_telemetry.cpp](../../../tests/cpp/tst_telemetry.cpp), [tst_controls_telemetry.qml](../../../tests/qml/tst_controls_telemetry.qml).
