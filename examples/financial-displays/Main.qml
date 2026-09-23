// SPDX-License-Identifier: LGPL-3.0-or-later
import QtQuick
import QindaTK as Tk

Rectangle {
    width: 1180
    height: 840
    color: Tk.Theme.color.bg
    Tk.OhlcModel { id: prices }
    Tk.TargetModel { id: measures }
    Component.onCompleted: {
        prices.replaceCandles([
            {id: "mon", utcMs: "1704067200000", open: 100, high: 110, low: 95, close: 108},
            {id: "tue", utcMs: "1704153600000", open: 108, high: 112, low: 104, close: 106},
            {id: "holiday", utcMs: "1704240000000", missing: true},
            {id: "thu", utcMs: "1704326400000", open: 106, high: 109, low: 97, close: 99},
            {id: "fri", utcMs: "1704412800000", open: 99, high: 105, low: 96, close: 103}
        ], "USD/share", "Host-defined daily exchange session", "UTC", 100)
        measures.replaceTargets([
            {id: "availability", label: "Availability", unit: "%", minimum: 0, maximum: 100,
             value: 82, target: 95, direction: "higher",
             thresholdMeaning: "Service availability", bands: [
                 {end: 40, meaning: "Low"}, {end: 70, meaning: "Watch"},
                 {end: 100, meaning: "Healthy"}]},
            {id: "latency", label: "Latency", unit: "ms", minimum: 0, maximum: 100,
             value: 32, target: 25, direction: "lower",
             thresholdMeaning: "Response-time budget", bands: [
                 {end: 25, meaning: "Within budget"}, {end: 60, meaning: "Watch"},
                 {end: 100, meaning: "Over budget"}]},
            {id: "planned", label: "Planned capacity", unit: "%", minimum: 0, maximum: 100,
             missing: true, target: 80, direction: "higher",
             thresholdMeaning: "Provisioning target", bands: [
                 {end: 50, meaning: "Low"}, {end: 80, meaning: "Planned"},
                 {end: 100, meaning: "Reserve"}]}
        ])
    }
    Tk.Flex {
        anchors.fill: parent
        direction: Tk.Flex.Column
        padding: Tk.Theme.space.md
        gap: Tk.Theme.space.sm
        Tk.Label { text: qsTr("Market sessions and performance targets")
            font.pixelSize: Tk.Theme.font.title }
        Tk.CandlestickView { candleModel: prices; Tk.Flex.flex: 1 }
        Tk.TargetDisplay { targetModel: measures; mode: "bullet"; Tk.Flex.flex: 1 }
        Tk.TargetDisplay { targetModel: measures; mode: "gauge"; Tk.Flex.flex: 1 }
    }
}
