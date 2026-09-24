// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Item {
    width: 1080
    height: 680
    Tk.ScientificDataset {
        id: measurements
        sourceId: "demo:exponential-decay-run-01"
        xUnit: "s"
        yUnit: "mV"
        parameters: ({ model: "25 exp(-t/1.8) + 0.2", sampleCount: 18,
                       uncertainty: "host supplied one standard deviation" })
    }
    Tk.ScientificPlot {
        id: figure
        objectName: "scientificExampleFigure"
        anchors.fill: parent
        anchors.margins: Tk.Theme.space.md
        dataset: measurements
        title: "Exponential decay · measured response"
        tooltip: "Measured response with one standard deviation error bars"
    }
    Component.onCompleted: {
        const rows = []
        for (let i = 0; i < 18; ++i) {
            const t = 0.25 + i * 0.35
            const y = 25 * Math.exp(-t / 1.8) + 0.2
            rows.push({ id: "sample-" + i, x: t, y: y,
                        sigmaY: 0.20 + 0.015 * y })
        }
        measurements.replaceSamples(rows)
        figure.xAxis.minimum = 0
        figure.xAxis.maximum = 7
        figure.yAxis.minimum = 0
        figure.yAxis.maximum = 26
    }
}
