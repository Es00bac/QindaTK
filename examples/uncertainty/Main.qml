// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 960
    height: 640
    color: Tk.Theme.color.bg
    Tk.UncertaintyModel {
        id: readings
        sourceDatasetId: "station-24-hour"
        xUnit: "hour"
        yUnit: "°C"
        meaning: Tk.UncertaintyModel.Confidence
        coverage: 0.95
        meaningDetail: "mean temperature"
        Component.onCompleted: replaceIntervals([
            {id:"h0",sourceId:"sensor-0",x:0,estimate:4,low:3,high:5},
            {id:"h1",sourceId:"sensor-1",x:1,estimate:5,low:3.5,high:6.5},
            {id:"h2",sourceId:"sensor-2",x:2,estimate:7,low:5,high:9},
            {id:"h3",sourceId:"sensor-3",x:3,estimate:8,low:6,high:10},
            {id:"h4",sourceId:"sensor-4",x:4,estimate:7},
            {id:"h5",sourceId:"sensor-5",x:5,estimate:9,low:7,high:11},
            {id:"h6",sourceId:"sensor-6",x:6,estimate:8,low:6.5,high:9.5},
            {id:"h7",sourceId:"sensor-7",x:7,estimate:6,low:4,high:8},
            {id:"h8",sourceId:"sensor-8",x:8,estimate:5,low:3.5,high:6.5}
        ])
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.sm
        Tk.Label { text: qsTr("Temperature uncertainty"); font.pixelSize: Tk.Theme.font.title }
        Tk.UncertaintyView {
            model: readings
            tooltip: qsTr("Hourly error bars")
            mode: Tk.UncertaintyRenderer.ErrorBars
            xAxis.minimum: -0.5
            xAxis.maximum: 8.5
            yAxis.minimum: 0
            yAxis.maximum: 12
            Tk.Flex.flex: 1
        }
        Tk.UncertaintyView {
            model: readings
            tooltip: qsTr("Hourly confidence band")
            mode: Tk.UncertaintyRenderer.Band
            xAxis.minimum: -0.5
            xAxis.maximum: 8.5
            yAxis.minimum: 0
            yAxis.maximum: 12
            Tk.Flex.flex: 1
        }
    }
}
