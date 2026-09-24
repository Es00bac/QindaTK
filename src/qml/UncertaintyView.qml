// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: view
    property Tk.UncertaintyModel model: null
    property alias xAxis: horizontalAxis
    property alias yAxis: verticalAxis
    property int mode: Tk.UncertaintyRenderer.ErrorBars
    property string tooltip: qsTr("Uncertainty intervals")
    property string title: ""
    property bool showDataTable: false
    readonly property string selectedId: renderer.selectedId
    readonly property var selectedInterval: renderer.selectedInterval
    readonly property real horizontalViewFraction: renderer.viewX
    readonly property real verticalViewFraction: renderer.viewY
    signal intervalSelected(string id)
    function selectId(id) { return renderer.selectId(id) }
    function navigateSelection(key) { return renderer.navigateSelection(key) }
    function setViewport(xs, xe, ys, ye) { renderer.setViewport(xs, xe, ys, ye) }
    function pan(dx, dy) { renderer.pan(dx, dy) }
    function zoom(factor, xAnchor, yAnchor) { renderer.zoom(factor, xAnchor, yAnchor) }
    function hitAt(x, y) { return renderer.hitAt(x, y) }
    function projected(id) { return renderer.projected(id) }
    function dataAt(x, y) { return renderer.dataAt(x, y) }
    function csvData() { return renderer.csvData() }
    function renderImage(size) { return renderer.renderImage(size) }
    function exportPng(path, size) { return renderer.exportPng(path, size) }
    function exportSvg(path, size) { return renderer.exportSvg(path, size) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    objectName: "uncertaintyView"
    activeFocusOnTab: true
    Accessible.role: Accessible.Pane
    Accessible.name: tooltip
    Accessible.description: model ? qsTr("%1 in %2 from %3. %4 missing intervals. Selected %5. Toggle the data table for exact endpoints and source IDs.")
        .arg(model.meaningLabel()).arg(model.yUnit).arg(model.sourceDatasetId)
        .arg(model.missingCount).arg(selectedId || qsTr("none")) : qsTr("No data")
    Accessible.focusable: true
    Accessible.focused: activeFocus || renderer.activeFocus
    Keys.onPressed: function(event) {
        if (renderer.navigateSelection(event.key)) event.accepted = true
    }

    Tk.ChartAxis { id: horizontalAxis; unit: view.model ? view.model.xUnit : "" }
    Tk.ChartAxis { id: verticalAxis; unit: view.model ? view.model.yUnit : "" }
    Tk.UncertaintyRenderer {
        id: renderer
        objectName: "uncertaintyPlot"
        anchors.fill: parent
        visible: !view.showDataTable
        model: view.model
        xAxis: horizontalAxis
        yAxis: verticalAxis
        mode: view.mode
        title: view.title
        backgroundColor: Tk.Theme.color.canvas
        gridColor: Tk.Theme.color.border
        lineColor: Tk.Theme.color.accent
        bandColor: Tk.Theme.color.accentSubtle
        textColor: Tk.Theme.color.textMuted
        missingColor: Tk.Theme.color.warning
        selectionColor: Tk.Theme.color.focus
        metrics: ({ leftMargin: Tk.Theme.size.labelWidth / 2,
                    rightMargin: Tk.Theme.space.sm,
                    topMargin: view.title ? Tk.Theme.font.body + Tk.Theme.space.md : Tk.Theme.space.sm,
                    bottomMargin: Tk.Theme.font.body * 2 + Tk.Theme.space.md,
                    fontFamily: Tk.Theme.font.family,
                    fontSize: Tk.Theme.font.caption,
                    labelGap: Tk.Theme.space.xs,
                    tickLabelWidth: Tk.Theme.size.fieldWidth / 2,
                    lineWidth: Tk.Theme.size.border,
                    pointRadius: Tk.Theme.size.iconSm / 3,
                    capHalfWidth: Tk.Theme.size.iconSm / 2,
                    hitRadius: Tk.Theme.size.controlSm / 2 })
        onIntervalSelected: function(id) { view.intervalSelected(id) }
    }
    Rectangle {
        objectName: "uncertaintyFocusRing"
        anchors.fill: renderer
        radius: Tk.Theme.radius.sm
        color: Tk.Theme.alpha(Tk.Theme.color.canvas, Tk.Theme.opacity.hidden)
        border.color: Tk.Theme.color.focus
        border.width: Tk.Theme.size.focusRing
        visible: (view.activeFocus || renderer.activeFocus) && !view.showDataTable
    }
    Tk.DataTable {
        id: table
        objectName: "uncertaintyDataTable"
        anchors.fill: parent
        visible: view.showDataTable
        model: view.model
        tooltip: view.tooltip + qsTr(" data")
        columns: [
            Tk.TableColumn { key: "id"; title: qsTr("Interval ID"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "sourceId"; title: qsTr("Source ID"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "estimate"; title: qsTr("Estimate"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "low"; title: qsTr("Low"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "high"; title: qsTr("High"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "status"; title: qsTr("Status"); width: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "meaning"; title: qsTr("Meaning"); width: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "coverage"; title: qsTr("Coverage"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "meaningDetail"; title: qsTr("Detail"); width: Tk.Theme.size.fieldWidth }
        ]
    }
    HoverHandler { id: hover }
    Tk.ToolTip { text: view.tooltip; visible: hover.hovered && !view.showDataTable }
}
