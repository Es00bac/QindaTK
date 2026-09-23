// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QtTest
import QindaTK as Tk

TestCase {
    name: "ChartView"
    when: windowShown
    visible: true
    width: 560
    height: 420

    Component {
        id: timeChartComponent
        Tk.ChartView {
            width: 480
            height: 300
            xAxis.kind: Tk.ChartAxis.DateTime
            xAxis.minimumUtc: "9007199254740993"
            xAxis.maximumUtc: "9007199254741993"
            xAxis.unit: "UTC ms"
            yAxis.minimum: 0
            yAxis.maximum: 10
            yAxis.unit: "m"
            Tk.ChartDataset {
                seriesId: "survey"
                name: "Survey"
                xUnit: "UTC ms"
                yUnit: "m"
                Component.onCompleted: {
                    appendPoint({ pointId: "p-a", utcMs: "9007199254740993", y: 2 })
                    appendPoint({ pointId: "p-b", utcMs: "9007199254741493", y: 8 })
                }
            }
        }
    }
    Component {
        id: numericChartComponent
        Tk.ChartView {
            width: 320
            height: 200
            xAxis.minimum: 0
            xAxis.maximum: 4
            xAxis.unit: "luma"
            yAxis.minimum: 0
            yAxis.maximum: 10
            yAxis.unit: "count"
            Tk.ChartDataset {
                seriesId: "scatter-fixture"
                name: "Samples"
                kind: Tk.ChartDataset.Scatter
                xUnit: "luma"
                yUnit: "count"
                color: Tk.Theme.color.warning
                fillColor: Tk.Theme.color.warning
                Component.onCompleted: {
                    appendPoint({ pointId: "a", x: 1, y: 2 })
                    appendPoint({ pointId: "b", x: 3, y: 8 })
                }
            }
        }
    }
    Component { id: exportSpyComponent; SignalSpy { signalName: "exportRequested" } }
    Component {
        id: linkedChartComponent
        Item {
            width: 640; height: 220
            property alias leftChart: left
            property alias rightChart: right
            Tk.ChartAxis { id: sharedX; minimum: 0; maximum: 4; unit: "s" }
            Tk.ChartAnnotationModel {
                id: notes
                Component.onCompleted: replaceAnnotations([
                    { id: "threshold", label: "Threshold", kind: Tk.ChartAnnotationModel.Point,
                      x: 2, y: 5, xUnit: "s", yUnit: "count" }
                ])
            }
            Tk.ChartView {
                id: left
                width: 300; height: 200
                xAxis: sharedX
                yAxis.minimum: 0; yAxis.maximum: 10; yAxis.unit: "count"
                annotationModel: notes
            }
            Tk.ChartView {
                id: right
                x: 320; width: 300; height: 200
                xAxis: sharedX
                yAxis.minimum: 0; yAxis.maximum: 20
            }
        }
    }

    function test_exact_time_table_csv_and_zoom() {
        const chart = createTemporaryObject(timeChartComponent, this)
        verify(chart !== null)
        compare(chart.tableModel.count, 2)
        compare(chart.tableModel.get(0).x, "9007199254740993")
        verify(chart.csvData().includes("9007199254740993"))
        verify(chart.navigateSelection(Qt.Key_Right))
        compare(chart.selectedPointId, "p-a")
        verify(chart.navigateSelection(Qt.Key_Right))
        compare(chart.selectedPointId, "p-b")
        compare(chart.horizontalViewFraction, 1)
        chart.zoom(2, 0.5, 0.5)
        verify(chart.horizontalViewFraction < 1)
        const spy = createTemporaryObject(exportSpyComponent, this, { target: chart })
        chart.renderImage(Qt.size(240, 140))
        compare(spy.count, 1)
    }

    function test_histogram_contract_and_scatter_display() {
        const chart = createTemporaryObject(numericChartComponent, this)
        verify(chart !== null)
        compare(chart.tableModel.count, 2)
        verify(chart.setHistogram([0, 1, 3, 4], [2, 5, 3], 0, "luma", "luma"))
        compare(chart.tableModel.count, 5)
        verify(chart.csvData().includes("histogram"))
        chart.renderImage(Qt.size(160, 100))
        chart.setViewport(0.2, 0.8, 0.1, 0.9)
        chart.pan(0.1, 0)
        chart.showDataTable = true
        verify(chart.showDataTable)
        verify(!chart.setHistogram([0, 1, 1], [2, 5], 0, "bad", "luma"))
    }

    function test_linked_axes_annotation_and_bin_range() {
        const pair = createTemporaryObject(linkedChartComponent, this)
        verify(pair !== null)
        const left = pair.leftChart
        const right = pair.rightChart
        compare(left.annotationModel.count, 1)
        const plot = findChild(left, "chartPlot")
        const m = plot.metrics
        const px = m.leftMargin + (plot.width - m.leftMargin - m.rightMargin) / 2
        const py = m.topMargin + (plot.height - m.topMargin - m.bottomMargin) / 2
        compare(left.annotationAt(px, py).id, "threshold")
        verify(left.selectAnnotationAt(px, py))
        compare(left.selectedAnnotationId, "threshold")
        left.showAnnotationTable = true
        compare(findChild(left, "chartAnnotationTable").count, 1)
        verify(left.annotationCsvData().includes("\"threshold\""))
        left.showAnnotationTable = false
        verify(left.setHistogram([0, 1, 3, 4], [2, 5, 3], 0, "signal", "s"))
        const rightPlot = findChild(right, "chartPlot")
        verify(right.setHistogram([0, 1, 3, 4], [2, 5, 3], 0, "signal", "s"))
        const rm = rightPlot.metrics
        const rw = rightPlot.width - rm.leftMargin - rm.rightMargin
        const rh = rightPlot.height - rm.topMargin - rm.bottomMargin
        mouseClick(rightPlot, rm.leftMargin + rw * 0.5,
                   rm.topMargin + rh * 0.75, Qt.LeftButton)
        verify(right.histogramSelection.valid)
        compare(right.histogramSelection.firstBin, 1)
        compare(right.histogramSelection.upper, 3)
        verify(!right.histogramSelection.upperInclusive)
        verify(left.selectHistogramBins(1, 2))
        compare(left.histogramSelection.lower, 1)
        compare(left.histogramSelection.upper, 4)
        verify(left.histogramSelection.upperInclusive)
        left.setViewport(0.2, 0.8, 0, 1)
        compare(right.horizontalViewFraction, left.horizontalViewFraction)
        compare(right.verticalViewFraction, 1)
        right.zoom(2)
        compare(left.horizontalViewFraction, right.horizontalViewFraction)
    }
}
