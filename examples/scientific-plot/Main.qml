// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

// Two scientific figures: a measured decay with asymmetric error bars, a
// model trace and annotations; and a sampled Gaussian surface (colour map)
// with scatter probes carrying X and Y uncertainty. Demo data only.
Rectangle {
    width: 1280
    height: 680
    color: Tk.Theme.color.bg

    Tk.ScientificDataset {
        id: measurements
        sourceId: "demo:exponential-decay-run-01"
        label: "Measured"
        xUnit: "s"
        yUnit: "mV"
        parameters: ({ model: "25 exp(-t/1.8) + 0.2", sampleCount: 18,
                       uncertainty: "host supplied one standard deviation" })
    }
    Tk.ScientificDataset {
        id: modelCurve
        sourceId: "demo:exponential-decay-model"
        label: "Model 25 exp(-t/1.8) + 0.2"
        xUnit: "s"
        yUnit: "mV"
    }
    Tk.ChartAnnotationModel {
        id: notes
        Component.onCompleted: replaceAnnotations([
            { id: "tau", label: "τ = 1.8 s", kind: Tk.ChartAnnotationModel.VerticalLine, x: 1.8, xUnit: "s" },
            { id: "floor", label: "Offset 0.2 mV", kind: Tk.ChartAnnotationModel.HorizontalLine, y: 0.2, yUnit: "mV" }])
    }
    Tk.ScientificGrid {
        id: field
        sourceId: "demo:gaussian-field"
        label: "Gaussian field"
        xUnit: "mm"
        yUnit: "mm"
        zUnit: "K"
        parameters: ({ model: "300 + 40 exp(-(x² + y²) / 8)", lattice: "25 x 25" })
    }
    Tk.ScientificDataset {
        id: probes
        sourceId: "demo:probe-positions"
        label: "Probes"
        kind: Tk.ScientificDataset.Scatter
        xUnit: "mm"
        yUnit: "mm"
    }

    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Row
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.md
        Tk.ScientificPlot {
            id: figure
            objectName: "scientificExampleFigure"
            Tk.Flex.flex: 1
            dataset: measurements
            series: [modelCurve]
            annotations: notes
            title: "Exponential decay · measured response"
            xLabel: "Time"
            yLabel: "Response"
            tooltip: "Measured response with asymmetric one standard deviation error bars"
        }
        Tk.ScientificPlot {
            id: surfaceFigure
            objectName: "scientificExampleSurface"
            Tk.Flex.flex: 1
            surface: field
            series: [probes]
            title: "Gaussian field with probe positions"
            xLabel: "x"
            yLabel: "y"
            tooltip: "Sampled temperature field, bilinear interpolation"
        }
    }

    Component.onCompleted: {
        const rows = [], curve = []
        for (let i = 0; i < 18; ++i) {
            const t = 0.25 + i * 0.35
            const y = 25 * Math.exp(-t / 1.8) + 0.2
            rows.push(i === 9 ? { id: "sample-" + i, x: t, missingY: true }
                      : { id: "sample-" + i, x: t, y: y,
                          sigmaYLow: 0.15 + 0.01 * y, sigmaYHigh: 0.25 + 0.02 * y })
        }
        for (let k = 0; k <= 140; ++k) {
            const t = k * 0.05
            curve.push({ id: "model-" + k, x: t, y: 25 * Math.exp(-t / 1.8) + 0.2 })
        }
        measurements.replaceSamples(rows)
        modelCurve.replaceSamples(curve)
        figure.xAxis.minimum = 0
        figure.xAxis.maximum = 7
        figure.yAxis.minimum = 0
        figure.yAxis.maximum = 26

        const axis = [], values = []
        for (let i = 0; i < 25; ++i) axis.push(-6 + i * 0.5)
        for (let r = 0; r < 25; ++r)
            for (let c = 0; c < 25; ++c)
                values.push(r === 3 && c === 20 ? null
                            : 300 + 40 * Math.exp(-(axis[c] * axis[c] + axis[r] * axis[r]) / 8))
        field.replaceGrid(axis, axis, values)
        probes.replaceSamples([
            { id: "p1", x: -3.2, y: 1.1, sigmaX: 0.3, sigmaY: 0.2 },
            { id: "p2", x: 0.4, y: -0.6, sigmaX: 0.2, sigmaYLow: 0.1, sigmaYHigh: 0.4 },
            { id: "p3", x: 2.8, y: 3.5, sigmaX: 0.4, sigmaY: 0.3 }])
        surfaceFigure.xAxis.minimum = -6
        surfaceFigure.xAxis.maximum = 6
        surfaceFigure.yAxis.minimum = -6
        surfaceFigure.yAxis.maximum = 6
    }
}
