// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: root
    width: 360
    height: 240
    readonly property alias chartView: chart
    readonly property alias chartDataset: series
    readonly property bool datasetReady: series.count === 3 && series.lastError === ""
    readonly property string selectedPointId: chart.selectedPointId

    function selectFirstPoint() { return chart.navigateSelection(Qt.Key_Home) }
    function selectedPoint() { return chart.selectedPoint() }
    function exportPng(path, size) { return chart.exportPng(path, size) }
    function exportSvg(path, size) { return chart.exportSvg(path, size) }
    function csvData() { return chart.csvData() }

    Tk.ChartView {
        id: chart
        objectName: "installedChartView"
        anchors.fill: parent
        tooltip: qsTr("Installed chart consumer")
        xAxis.minimum: 0
        xAxis.maximum: 2
        xAxis.unit: "frame"
        yAxis.minimum: 0
        yAxis.maximum: 10
        yAxis.unit: "dB"

        Tk.ChartDataset {
            id: series
            objectName: "installedChartDataset"
            seriesId: "consumer-signal"
            name: qsTr("Signal")
            xUnit: "frame"
            yUnit: "dB"
            Component.onCompleted: {
                appendPoint({pointId: "point-a", x: 0, y: 2})
                appendPoint({pointId: "point-b", x: 1, y: 7})
                appendPoint({pointId: "point-c", x: 2, y: 4})
            }
        }
    }
}
