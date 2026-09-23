// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Section {
    title: "Uncertainty intervals"
    note: "UncertaintyView · typed confidence endpoints · missing intervals"
    Tk.UncertaintyModel {
        id: readings
        sourceDatasetId: "gallery-readings"
        xUnit: "hour"
        yUnit: "°C"
        meaning: Tk.UncertaintyModel.Confidence
        coverage: 0.95
        meaningDetail: "mean temperature"
    }
    Component.onCompleted: readings.replaceIntervals([
        {id: "h0", sourceId: "sensor-0", x: 0, estimate: 4, low: 3, high: 5},
        {id: "h1", sourceId: "sensor-1", x: 1, estimate: 5, low: 3.5, high: 6.5},
        {id: "h2", sourceId: "sensor-2", x: 2, estimate: 7, low: 5, high: 9},
        {id: "h3", sourceId: "sensor-3", x: 3, estimate: 8},
        {id: "h4", sourceId: "sensor-4", x: 4, estimate: 7, low: 5, high: 9}
    ])
    Tk.UncertaintyView {
        objectName: "galleryUncertainty"
        width: 940
        height: 240
        Tk.Flex.shrink: 0
        model: readings
        tooltip: qsTr("Gallery temperature confidence intervals")
        xAxis.minimum: -0.5
        xAxis.maximum: 4.5
        yAxis.minimum: 0
        yAxis.maximum: 12
    }
}
