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
    readonly property bool annotationsReady: notes.count === 1 && notes.lastError === ""
    readonly property string selectedPointId: chart.selectedPointId

    function selectFirstPoint() { return chart.navigateSelection(Qt.Key_Home) }
    function selectedPoint() { return chart.selectedPoint() }
    function exportPng(path, size) { return chart.exportPng(path, size) }
    function exportSvg(path, size) { return chart.exportSvg(path, size) }
    function csvData() { return chart.csvData() }
    function annotationCsvData() { return chart.annotationCsvData() }
    function zoomLinked() {
        chart.zoom(2, 0.5, 0.5)
        return chart.horizontalViewFraction < 1
            && chart.horizontalViewFraction === linked.horizontalViewFraction
    }

    Tk.ChartAxis { id: sharedX; minimum: 0; maximum: 2; unit: "frame" }
    Tk.ChartAnnotationModel {
        id: notes
        Component.onCompleted: replaceAnnotations([
            { id: "review-note", label: "Peak", kind: Tk.ChartAnnotationModel.Point,
              x: 1, y: 7, xUnit: "frame", yUnit: "dB" }
        ])
    }

    Tk.ChartView {
        id: chart
        objectName: "installedChartView"
        anchors.fill: parent
        tooltip: qsTr("Installed chart consumer")
        xAxis: sharedX
        annotationModel: notes
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

    Tk.ChartView {
        id: linked
        visible: false
        xAxis: sharedX
        yAxis.minimum: 0
        yAxis.maximum: 10
    }
}
