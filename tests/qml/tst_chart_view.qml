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
}
