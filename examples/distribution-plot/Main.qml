// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1180
    height: 840
    color: Tk.Theme.color.bg
    Tk.DistributionModel { id: timings }
    Component.onCompleted: timings.replaceGroups([
        {id: "baseline", label: "Baseline", unit: "ms", values: [18, 20, 21, 22, 24, 26, 29],
         interval: {low: 20, high: 25, estimate: 22.9, meaning: "95% confidence interval of mean"}},
        {id: "cached", label: "Cached", unit: "ms", values: [10, 11, 12, 13, 13, 15, 17],
         interval: {low: 11, high: 14, estimate: 13, meaning: "95% confidence interval of mean"}},
        {id: "batched", label: "Batched", unit: "ms", values: [13, 14, 16, 18, 19, 20, 24],
         interval: {low: 15, high: 20, estimate: 17.7, meaning: "95% confidence interval of mean"}}
    ])
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.sm
        Tk.Label { text: qsTr("Request latency distributions"); font.pixelSize: Tk.Theme.font.title }
        Tk.DistributionPlot {
            distributionModel: timings
            tooltip: qsTr("Latency box plot")
            mode: "box"
            Tk.Flex.flex: 1
        }
        Tk.DistributionPlot {
            distributionModel: timings
            tooltip: qsTr("Latency violin plot")
            mode: "violin"
            Tk.Flex.flex: 1
        }
        Tk.DistributionPlot {
            distributionModel: timings
            tooltip: qsTr("Latency confidence intervals")
            mode: "interval"
            Tk.Flex.flex: 1
        }
    }
}
