// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: chart
    default property alias datasets: renderer.datasets
    property alias xAxis: horizontalAxis
    property alias yAxis: verticalAxis
    property string tooltip: qsTr("Chart")
    property bool showDataTable: false
    readonly property string selectedSeriesId: renderer.selectedSeriesId
    readonly property string selectedPointId: renderer.selectedPointId
    readonly property var tableModel: renderer.tableModel
    readonly property real horizontalViewFraction: renderer.viewX
    readonly property real verticalViewFraction: renderer.viewY
    readonly property string histogramError: renderer.histogramError
    signal pointSelected(string seriesId, string pointId)
    signal exportRequested(string format, var image)
    function csvData() { return renderer.csv() }
    function renderImage(size) { return renderer.renderImage(size) }
    function exportPng(path, size) { return renderer.exportPng(path, size) }
    function exportSvg(path, size) { return renderer.exportSvg(path, size) }
    function setHistogram(edges, counts, normalization, channel, unit) {
        return renderer.setHistogram(edges, counts, normalization, channel, unit)
    }
    function setViewport(xStart, xEnd, yStart, yEnd) {
        renderer.setViewport(xStart, xEnd, yStart, yEnd)
    }
    function zoom(factor, xAnchor, yAnchor) { renderer.zoom(factor, xAnchor, yAnchor) }
    function pan(xFraction, yFraction) { renderer.pan(xFraction, yFraction) }
    function selectedPoint() { return renderer.selectedPoint() }
    function navigateSelection(key) { return renderer.navigateSelection(key) }

    implicitWidth: Tk.Theme.size.panelMinWidth
    implicitHeight: Tk.Theme.size.panelMinHeight
    Accessible.role: Accessible.Pane
    Accessible.name: chart.tooltip
    Accessible.description: chart.showDataTable ? qsTr("Accessible chart data table")
        : qsTr("Use arrow keys to inspect points. Activate the data table from the owner to inspect values.")
    Accessible.focusable: renderer.enabled
    Accessible.focused: chart.activeFocus || renderer.activeFocus
    activeFocusOnTab: true
    objectName: "chartView"
    Keys.onPressed: function(event) {
        if (renderer.navigateSelection(event.key)) event.accepted = true
    }

    Tk.ChartAxis { id: horizontalAxis }
    Tk.ChartAxis { id: verticalAxis; minimum: 0; maximum: 1 }

    Tk.ChartRenderer {
        id: renderer
        objectName: "chartPlot"
        anchors.fill: parent
        visible: !chart.showDataTable
        xAxis: horizontalAxis
        yAxis: verticalAxis
        backgroundColor: Tk.Theme.color.canvas
        gridColor: Tk.Theme.color.border
        axisColor: Tk.Theme.color.textMuted
        defaultSeriesColor: Tk.Theme.color.accent
        histogramColor: Tk.Theme.color.accentSubtle
        metrics: ({
            leftMargin: Tk.Theme.size.labelWidth / 2,
            rightMargin: Tk.Theme.space.sm,
            topMargin: Tk.Theme.space.xs,
            bottomMargin: Tk.Theme.font.body + Tk.Theme.space.sm,
            fontFamily: Tk.Theme.font.family,
            fontPixelSize: Tk.Theme.font.caption,
            axisLabelHeight: Tk.Theme.font.body + Tk.Theme.space.xs,
            tickHalfWidth: Tk.Theme.size.fieldWidth / 2,
            smallGap: Tk.Theme.space.xs,
            gridWidth: Tk.Theme.size.border,
            lineWidth: Tk.Theme.size.border * 1.5,
            pointRadius: Tk.Theme.size.iconSm / 3,
            bubbleMinRadius: Tk.Theme.size.iconSm / 2,
            bubbleMaxRadius: Tk.Theme.size.iconLg,
            arrowSize: Tk.Theme.size.iconSm / 2,
            minimumBarWidth: Tk.Theme.size.border,
            selectionWidth: Tk.Theme.size.border * 2,
            selectionRadius: Tk.Theme.size.grip / 2,
            hitRadius: Tk.Theme.size.controlSm / 2
        })
        onPointSelected: function(seriesId, pointId) { chart.pointSelected(seriesId, pointId) }
        onExportImageReady: function(image) { chart.exportRequested("image", image) }
    }

    Rectangle {
        objectName: "chartFocusRing"
        anchors.fill: renderer
        radius: Tk.Theme.radius.sm
        color: "transparent"
        border.width: Tk.Theme.size.focusRing
        border.color: Tk.Theme.color.focus
        visible: chart.activeFocus || renderer.activeFocus
    }

    Tk.DataTable {
        id: accessibleTable
        objectName: "chartDataTable"
        anchors.fill: parent
        visible: chart.showDataTable
        model: renderer.tableModel
        tooltip: chart.tooltip + qsTr(" data")
        columns: [
            Tk.TableColumn { key: "seriesName"; title: qsTr("Series"); width: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "x"; title: qsTr("X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "y"; title: qsTr("Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "size"; title: qsTr("Size"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "dx"; title: qsTr("Vector X"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "dy"; title: qsTr("Vector Y"); width: Tk.Theme.size.fieldWidth; mono: true },
            Tk.TableColumn { key: "category"; title: qsTr("Category / upper edge"); width: Tk.Theme.size.fieldWidth },
            Tk.TableColumn { key: "gap"; title: qsTr("Gap"); width: Tk.Theme.size.controlLg;
                formatter: function(value) { return value ? qsTr("Missing") : "" } },
            Tk.TableColumn { key: "pointId"; title: qsTr("Point ID"); flex: 1; minWidth: Tk.Theme.size.fieldWidth; mono: true }
        ]
    }
}
