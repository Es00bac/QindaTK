// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    id: page
    width: 960
    height: 560
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        rowGap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("Chart view"); level: 2 }
        Tk.ChartLegend {
            id: legend
            datasets: [signalData, targetData, scatterData]
            width: parent.width
        }
        Tk.ChartView {
            id: chart
            width: parent.width
            Tk.Flex.grow: 1
            xAxis.minimum: 0
            xAxis.maximum: 6
            xAxis.unit: "s"
            yAxis.minimum: 0
            yAxis.maximum: 100
            yAxis.unit: "%"
            tooltip: qsTr("Signal inspection chart")
            Tk.ChartDataset {
                id: signalData
                objectName: "lineDataset"
                seriesId: "signal"
                name: qsTr("Signal")
                kind: Tk.ChartDataset.Line
                xUnit: "s"
                yUnit: "%"
                color: Tk.Theme.color.accent
                fillColor: Tk.Theme.color.accentSubtle
                Component.onCompleted: {
                    appendPoint({ pointId: "p0", x: 0, y: 32 })
                    appendPoint({ pointId: "p1", x: 1, y: 48 })
                    appendPoint({ pointId: "p2", x: 2, y: 41, gap: true })
                    appendPoint({ pointId: "p3", x: 3, y: 68 })
                    appendPoint({ pointId: "p4", x: 4, y: 61 })
                    appendPoint({ pointId: "p5", x: 5, y: 83 })
                }
            }
            Tk.ChartDataset {
                id: targetData
                seriesId: "target"
                name: qsTr("Target")
                kind: Tk.ChartDataset.Step
                xUnit: "s"
                yUnit: "%"
                color: Tk.Theme.color.warning
                fillColor: Tk.Theme.color.warning
                Component.onCompleted: {
                    appendPoint({ pointId: "t0", x: 0, y: 55 })
                    appendPoint({ pointId: "t1", x: 2, y: 55 })
                    appendPoint({ pointId: "t2", x: 4, y: 75 })
                    appendPoint({ pointId: "t3", x: 5, y: 75 })
                }
            }
            Tk.ChartDataset {
                id: scatterData
                seriesId: "sample-points"
                name: qsTr("Samples")
                kind: Tk.ChartDataset.Scatter
                xUnit: "s"
                yUnit: "%"
                color: Tk.Theme.color.success
                fillColor: Tk.Theme.color.success
                Component.onCompleted: {
                    appendPoint({ pointId: "s0", x: 0.7, y: 61 })
                    appendPoint({ pointId: "s1", x: 2.3, y: 35 })
                    appendPoint({ pointId: "s2", x: 4.6, y: 91 })
                }
            }
        }
        Tk.CheckBox {
            text: qsTr("Show accessible data table")
            tooltip: qsTr("Switch from chart marks to full resolution tabular data")
            onToggled: chart.showDataTable = checked
        }
        Tk.Label {
            text: chart.selectedPoint().pointId !== undefined
                ? qsTr("Selected %1 / %2").arg(chart.selectedSeriesId).arg(chart.selectedPointId)
                : qsTr("Wheel to zoom · middle drag to pan · select a mark to inspect")
            color: Tk.Theme.color.textMuted
        }
    }
}
