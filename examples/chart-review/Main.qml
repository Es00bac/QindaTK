// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1120
    height: 620
    color: Tk.Theme.color.bg

    Tk.ChartAxis { id: sharedTime; minimum: 0; maximum: 4; unit: "s" }
    Tk.ChartAnnotationModel {
        id: notes
        objectName: "reviewAnnotations"
        Component.onCompleted: replaceAnnotations([
            {id: "threshold", label: "Target reached", kind: Tk.ChartAnnotationModel.Point,
             x: 2, y: 5, xUnit: "s", yUnit: "count"},
            {id: "deadline", label: "Deadline", kind: Tk.ChartAnnotationModel.VerticalLine,
             x: 3, xUnit: "s"}
        ])
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.lg
        rowGap: Tk.Theme.space.md
        Tk.Heading { text: qsTr("Linked axes, annotations and histogram ranges"); level: 2 }
        Tk.Flex {
            direction: Tk.Flex.Row
            columnGap: Tk.Theme.space.md
            Tk.Flex.grow: 1
            Tk.ChartView {
                id: trend
                objectName: "reviewTrend"
                Tk.Flex.flex: 1
                xAxis: sharedTime
                yAxis.minimum: 0; yAxis.maximum: 10; yAxis.unit: "count"
                tooltip: qsTr("Observed count over time")
                annotationModel: notes
                Tk.ChartDataset {
                    seriesId: "observed"; name: qsTr("Observed")
                    xUnit: "s"; yUnit: "count"
                    Component.onCompleted: replacePoints([
                        {pointId:"a",x:0,y:2}, {pointId:"b",x:1,y:4},
                        {pointId:"c",x:2,y:5}, {pointId:"d",x:3,y:8},
                        {pointId:"e",x:4,y:7}
                    ])
                }
            }
            Tk.ChartView {
                id: distribution
                objectName: "reviewHistogram"
                Tk.Flex.flex: 1
                xAxis: sharedTime
                yAxis.minimum: 0; yAxis.maximum: 10; yAxis.unit: "count"
                tooltip: qsTr("Observed count histogram")
                Component.onCompleted: {
                    setHistogram([0, 1, 2, 3, 4], [2, 5, 8, 3], 0, "observed", "s")
                    selectHistogramBins(1, 2)
                }
            }
        }
        Tk.Flex {
            direction: Tk.Flex.Row
            columnGap: Tk.Theme.space.md
            Tk.Button {
                text: qsTr("Zoom both X axes")
                tooltip: qsTr("Zoom the shared time axis in both charts")
                onClicked: trend.zoom(2)
            }
            Tk.CheckBox {
                text: qsTr("Annotation table")
                tooltip: qsTr("Inspect annotation IDs and exact source coordinates")
                onToggled: trend.showAnnotationTable = checked
            }
            Tk.Label {
                text: distribution.histogramSelection.valid
                    ? qsTr("Selected %1–%2 %3").arg(distribution.histogramSelection.lower)
                        .arg(distribution.histogramSelection.upper).arg(distribution.histogramSelection.unit)
                    : qsTr("Drag across histogram bins to select a domain range")
                color: Tk.Theme.color.textMuted
            }
        }
    }
}
