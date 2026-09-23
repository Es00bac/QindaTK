// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Linked analytical charts"
    note: "ChartView · shared axis · stable annotation · histogram range"
    Tk.ChartAxis { id: sharedX; minimum: 0; maximum: 4; unit: "s" }
    Tk.ChartAnnotationModel {
        id: marks
        Component.onCompleted: replaceAnnotations([
            {id:"midpoint", label:"Midpoint", x:2, y:5, xUnit:"s", yUnit:"count"}
        ])
    }
    Tk.Flex {
        direction: Tk.Flex.Row
        columnGap: Tk.Theme.space.md
        width: 940
        height: 230
        Tk.Flex.shrink: 0
        Tk.ChartView {
            objectName: "galleryLinkedChart"
            Tk.Flex.flex: 1
            xAxis: sharedX
            yAxis.minimum: 0; yAxis.maximum: 10; yAxis.unit: "count"
            annotationModel: marks
            tooltip: qsTr("Linked trend with midpoint annotation")
            Tk.ChartDataset {
                seriesId: "trend"; name: "Trend"; xUnit: "s"; yUnit: "count"
                Component.onCompleted: replacePoints([
                    {pointId:"a",x:0,y:2}, {pointId:"b",x:1,y:4},
                    {pointId:"c",x:2,y:5}, {pointId:"d",x:3,y:8}
                ])
            }
        }
        Tk.ChartView {
            objectName: "galleryLinkedHistogram"
            Tk.Flex.flex: 1
            xAxis: sharedX
            yAxis.minimum: 0; yAxis.maximum: 10; yAxis.unit: "count"
            tooltip: qsTr("Linked histogram; select its bin range")
            Component.onCompleted: {
                setHistogram([0,1,2,3,4], [2,5,8,3], 0, "trend", "s")
                selectHistogramBins(1, 2)
            }
        }
    }
}
