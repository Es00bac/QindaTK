// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Statistical distributions"
    note: "DistributionPlot · box, violin, interval · source inspection"
    Tk.DistributionModel { id: samples }
    Component.onCompleted: samples.replaceGroups([
        {id: "one", label: "One", unit: "ms", values: [1, 2, 3, 4, 5],
         interval: {low: 2, high: 4, estimate: 3, meaning: "95% confidence interval of mean"}},
        {id: "two", label: "Two", unit: "ms", values: [2, 4, 4, 6, 8],
         interval: {low: 3, high: 6, estimate: 4.8, meaning: "95% confidence interval of mean"}}
    ])
    Tk.DistributionPlot {
        width: 940
        height: 240
        distributionModel: samples
        tooltip: qsTr("Gallery distributions")
        Tk.Flex.shrink: 0
    }
}
